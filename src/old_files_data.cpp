
#include "old_files_data.h"
#include <boost/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/unordered_map.hpp>
#include <list>
extern "C" {
#include <sqlite3.h>
}
#include "fstream_utf8.h"
#include "log_sched.h"
#include "props_sched.h"

// opens and closes connection each time
static int ofd_exec_db(const char *fname, const char *sql)
{
    sqlite3 *db = 0;
    char *err_msg = 0;
    TLOG << "ofd_exec_db(" << fname << ", " << sql << ") INIT";
    int rc = sqlite3_open(fname, &db);
    if (rc != SQLITE_OK) {
        ELOG << "ofd_exec_db(" << fname << ")->Cannot open database: " << sqlite3_errmsg(db);
        if (db) sqlite3_close(db);
        return 1;
    }
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        DLOG << "ofd_exec_db(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);
    TLOG << "ofd_exec_db(" << fname << ", " << sql << ") END";
    return 0;
}
// open if closed - needs locked access
static int ofd_exec_db_unique(sqlite3 **db, const char *fname, const char *sql)
{
    int rc; char *err_msg = 0;
    TLOG << "ofd_exec_db_unique(" << fname << ", " << sql << ") INIT";
    if (!*db) {
        rc = sqlite3_open(fname, db);
        if (rc != SQLITE_OK) {
            ELOG << "ofd_exec_db_unique(" << fname << ")->Cannot open database: " << sqlite3_errmsg(*db);
            if (*db) sqlite3_close(*db); *db = 0;
            return 1;
        }
    }
    rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        ELOG << "ofd_exec_db_unique(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(*db); *db = 0;
        return 1;
    }
    TLOG << "ofd_exec_db_unique(" << fname << ", " << sql << ") END";
    return 0;
}
// error if closed
static int ofd_exec_db_shared(sqlite3 **db, const char *fname, const char *sql)
{
    int rc; char *err_msg = 0;
    TLOG << "ofd_exec_db_shared(" << fname << ", " << sql << ") INIT";
    if (!*db) {
        ELOG << "ofd_exec_db_shared(" << fname << ")->db closed";
        return 1;
    }
    rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        ELOG << "ofd_exec_db_shared(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        return 1;
    }
    TLOG << "ofd_exec_db_shared(" << fname << ", " << sql << ") END";
    return 0;
}
static int ofd_prep_sql_unique(sqlite3 **db, sqlite3_stmt **stmt, const char *fname, const char *sql)
{
    int rc; char *err_msg = 0;
    if (!*db) {
        rc = sqlite3_open(fname, db);
        if (rc != SQLITE_OK) {
            ELOG << "ofd_prep_sql_unique(" << fname << ")->Cannot open database: " << sqlite3_errmsg(*db);
            if (*db) sqlite3_close(*db); *db = 0; *stmt= 0;
            return 1;
        }
    }
    if (!*stmt) {
        rc = sqlite3_prepare_v2 (*db, sql, -1, stmt, NULL);
        if (rc != SQLITE_OK) {
            ELOG << "ofd_prep_sql_unique(" << fname << ")->Cannot prepare_v2: " << sqlite3_errmsg(*db);
            sqlite3_close(*db); *db = 0; *stmt= 0;
            return 1;
        }
    }
    sqlite3_reset( *stmt);
    return SQLITE_OK;
}
static int ofd_prep_sql_shared(sqlite3 **db, sqlite3_stmt **stmt, const char *fname, const char *sql)
{
    int rc; char *err_msg = 0;
    if (!*db) {
        ELOG << "ofd_prep_sql_shared(" << fname << ")->db closed";
        return 1;
    }
    if (!*stmt) {
        rc = sqlite3_prepare_v2 (*db, sql, -1, stmt, NULL);
        if (rc != SQLITE_OK) {
            ELOG << "ofd_prep_sql_shared(" << fname << ")->Cannot prepare_v2: " << sqlite3_errmsg(*db);
            sqlite3_close(*db); *db = 0; *stmt= 0;
            return 1;
        }
    }
    sqlite3_reset( *stmt);
    return SQLITE_OK;
}
//#define ofd_prep_sql ofd_prep_sql_unique

#define OFD_FILE_DB_FNAME "ofd_file.db"
#define OFD_INFO_DB_FNAME "ofd_info.db"
#define OFD_SIGN_DB_FNAME "ofd_sign.db"

static std::string ofd_file_fname;
static std::string ofd_info_fname;
static std::string ofd_sign_fname;

static boost::shared_mutex mtx_ofd_file;
static boost::shared_mutex mtx_ofd_info;
static boost::shared_mutex mtx_ofd_sign;

static sqlite3 *ofd_file_db;
static sqlite3 *ofd_info_db;
static sqlite3 *ofd_sign_db;

static sqlite3_stmt *ofd_id_create_stmt = 0;
static sqlite3_stmt *ofd_set_sign_stmt = 0;
static sqlite3_stmt *ofd_set_info_stmt = 0;

static std::string magic_number;
const std::string &ofd_get_magic() { return magic_number; };
int ofd_get_entry_(int fid, std::string &name);

static std::list<int> hollows;


static int ofd_get_hollows_()
{
    sqlite3_stmt *stmt = 0;
    TLOG << "ofd_get_hollows_() INIT";
    int rc = ofd_prep_sql_unique(&ofd_file_db, &stmt, ofd_file_fname.c_str(),
                                 "SELECT rowid FROM entry WHERE parent=1 and rowid>1 LIMIT 100000");
    if (rc == SQLITE_OK) {
        for (;;) {
            if (sqlite3_step (stmt) == SQLITE_ROW) { try {
                    hollows.push_back(boost::lexical_cast<int>(sqlite3_column_text(stmt, 0)));
                } catch (std::exception &e) { ELOG << "ofd_get_entry()->BAD ID: " << e.what(); }
            } else { break; }
        }
    } else { ELOG << "ofd_get_hollows_()->ofd_prep_sql_unique"; }
    if (stmt) sqlite3_finalize(stmt);
    ILOG << "ofd_get_hollows_() END hollows.size(): " << hollows.size();
    return rc;
}

int ofd_init()
{
    TLOG << "ofd_init() INIT";

    ofd_file_fname = props::get().confd_path + OFD_FILE_DB_FNAME;
    ofd_info_fname = props::get().confd_path + OFD_INFO_DB_FNAME;
    ofd_sign_fname = props::get().confd_path + OFD_SIGN_DB_FNAME;

    ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(),
                "CREATE TABLE IF NOT EXISTS entry(parent INTEGER, name TEXT,"
                "PRIMARY KEY(parent, name), FOREIGN KEY(parent) REFERENCES entry(rowid))");
	ofd_exec_db_unique(&ofd_info_db, ofd_info_fname.c_str(),
                "CREATE TABLE IF NOT EXISTS info(id INTEGER PRIMARY KEY, inf TEXT)");
	ofd_exec_db_unique(&ofd_sign_db, ofd_sign_fname.c_str(),
                "CREATE TABLE IF NOT EXISTS sign(id INTEGER PRIMARY KEY, sig BLOB)");

    ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), "PRAGMA synchronous = OFF");
	ofd_exec_db_unique(&ofd_info_db, ofd_info_fname.c_str(), "PRAGMA synchronous = OFF");
	ofd_exec_db_unique(&ofd_sign_db, ofd_sign_fname.c_str(), "PRAGMA synchronous = OFF");

	int pid = -1;
    if (magic_number.empty()) {
        pid = ofd_get_entry_(1, magic_number);
        if (pid != -1 && (pid != 1 || magic_number.size() != 6)) {
            ELOG << "ofd_init()-> Error getting magic number ("<<pid<<", " << magic_number << ")";
            magic_number.clear();
        }
    }
    if (pid == -1) {
        if (magic_number.empty()) {
			std::stringstream ss;
			ss << std::setw(6) << std::setfill('0') << std::hex << std::uppercase << (time(0)&0x0ffffff);
			magic_number = ss.str();
        }
        std::string sql = "INSERT INTO entry(parent,name) VALUES (1,'" + magic_number + "')";
        ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), sql.c_str());
    }
    if (!magic_number.empty()) {
        ofd_get_hollows_();
    }
	ILOG << "ofd_init()-> magic_number: " << magic_number;
	return 0;
}
int ofd_end()
{
    TLOG << "ofd_end() INIT";
    if (ofd_id_create_stmt) { sqlite3_finalize(ofd_id_create_stmt); ofd_id_create_stmt = 0; }
    if (ofd_set_sign_stmt) { sqlite3_finalize(ofd_set_sign_stmt); ofd_set_sign_stmt = 0; }
    if (ofd_set_info_stmt) { sqlite3_finalize(ofd_set_info_stmt); ofd_set_info_stmt = 0; }
    if (ofd_file_db) { sqlite3_close(ofd_file_db); ofd_file_db = 0; }
    if (ofd_info_db) { sqlite3_close(ofd_info_db); ofd_info_db = 0; }
    if (ofd_sign_db) { sqlite3_close(ofd_sign_db); ofd_sign_db = 0; }
    hollows.clear();
    magic_number.clear();
    TLOG << "ofd_end() END";
    return 0;
}
static int ofd_get_id(int parent, const std::string &name)
{
    int fid = -1;
    sqlite3_stmt *stmt = 0;
    TLOG << "ofd_get_id("<<parent<<", "<<name<<") INIT";
    boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_file);
    int rc = ofd_prep_sql_shared(&ofd_file_db, &stmt, ofd_file_fname.c_str(),
                    "SELECT rowid FROM entry WHERE parent=? AND name=?");
    if (rc == SQLITE_OK) {
        if (sqlite3_bind_int(stmt, 1, parent) == SQLITE_OK) {
            if (sqlite3_bind_text(stmt, 2, name.c_str(), name.size(), 0) == SQLITE_OK) {
                if (sqlite3_step (stmt) == SQLITE_ROW) {
                    try { fid = boost::lexical_cast<int>(sqlite3_column_text(stmt, 0)); }
                    catch (std::exception &e) { ELOG << "ofd_get_id(" << parent << "," << name << ")->BAD ID: " << e.what(); }
                } else { TLOG << "ofd_get_id(" << parent << "," << name << ")->NOT FOUND"; }
            } else { ELOG << "ofd_get_id(" << parent << "," << name << ")->sqlite3_bind_text: " << sqlite3_errmsg(ofd_file_db); }
        } else { ELOG << "ofd_get_id(" << parent << "," << name << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_file_db); }
    } else { ELOG << "ofd_get_id(" << parent << "," << name << ")->ofd_prep_sql_shared"; }
    if (stmt) sqlite3_finalize(stmt);
    TLOG << "ofd_get_id("<<parent<<", "<<name<<") END";
    return fid;
}

static int ofd_get_id_create(int parent, const std::string &name, bool is_dir)
{
    TLOG << "ofd_get_id_create("<<parent<<", "<<name<<") INIT";
    int fid = ofd_get_id(parent, name);
    if (fid > 0) return fid;
    {boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_file);
        if (is_dir && !hollows.empty()) {
            int hollow = hollows.back(); hollows.pop_back();
            std::string sql = "UPDATE entry SET parent=" + boost::lexical_cast<std::string>(parent)
                + ",name='" + name + "' WHERE rowid=" + boost::lexical_cast<std::string>(hollow);
            if (ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), sql.c_str()) == 0) {
                return hollow;
            }
        }
        if (ofd_prep_sql_unique(&ofd_file_db, &ofd_id_create_stmt, ofd_file_fname.c_str(),
            "INSERT OR IGNORE INTO entry(parent,name) VALUES (?,?)") == SQLITE_OK) {
            if (sqlite3_bind_int(ofd_id_create_stmt, 1, parent) == SQLITE_OK) {
                if (sqlite3_bind_text(ofd_id_create_stmt, 2, name.c_str(), name.size(), 0) == SQLITE_OK) {
                    if (sqlite3_step (ofd_id_create_stmt) != SQLITE_DONE) {
                        sqlite3_finalize(ofd_id_create_stmt); ofd_id_create_stmt = 0;
                        WLOG << "ofd_get_id_create(" << parent << "," << name << ")->sqlite3_step: " << sqlite3_errmsg(ofd_file_db);
                    }
                } else { ELOG << "ofd_get_id_create(" << parent << "," << name << ")->sqlite3_bind_text: " << sqlite3_errmsg(ofd_file_db); }
            } else { ELOG << "ofd_get_id_create(" << parent << "," << name << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_file_db); }
        } else { ELOG << "ofd_get_id_create(" << parent << "," << name << ")->ofd_prep_sql_unique"; }
    }
    fid = ofd_get_id(parent, name);
    TLOG << "ofd_get_id_create("<<parent<<", "<<name<<") END";
    return fid;
}

static boost::shared_mutex mtx_cache;
typedef std::pair<int, std::string> entry_key_t;
boost::unordered_map<entry_key_t, int> fid_cache;
static int ofd_get_id_cache(int parent, const std::string &name, bool is_dir)
{
    TLOG << "ofd_get_id_cache("<<parent<<", "<<name<<") INIT";
    entry_key_t ek(parent,name);
    { boost::shared_lock< boost::shared_mutex > lock(mtx_cache);
    boost::unordered_map<entry_key_t, int>::iterator i = fid_cache.find(ek);
    if (i != fid_cache.end()) return i->second; }
    int fid = ofd_get_id_create(parent, name, is_dir);
    if (fid > 0) {
        boost::unique_lock< boost::shared_mutex > lock(mtx_cache);
        if (fid_cache.size() < 100000) fid_cache[ek] = fid;
        else fid_cache.clear();
    }
    TLOG << "ofd_get_id_cache("<<parent<<", "<<name<<") END";
    return fid;
}
static int ofd_get_id_cache_no_create(int parent, const std::string &name)
{
    TLOG << "ofd_get_id_cache_no_create("<<parent<<", "<<name<<") INIT";
    entry_key_t ek(parent,name);
    { boost::shared_lock< boost::shared_mutex > lock(mtx_cache);
    boost::unordered_map<entry_key_t, int>::iterator i = fid_cache.find(ek);
    if (i != fid_cache.end()) return i->second; }
    int fid = ofd_get_id(parent, name);
    if (fid > 0) {
        boost::unique_lock< boost::shared_mutex > lock(mtx_cache);
        if (fid_cache.size() < 100000) fid_cache[ek] = fid;
        else fid_cache.clear();
    }
    TLOG << "ofd_get_id_cache_no_create("<<parent<<", "<<name<<") END";
    return fid;
}
int ofd_find(const std::string &fname)
{
    int parent = 0;
    TLOG << "ofd_find("<<fname<<") INIT";
    boost::char_separator<char> sep("/");
    boost::tokenizer<boost::char_separator<char> > tok(fname, sep);
    BOOST_FOREACH(const std::string &_s, tok) {
        parent = ofd_get_id_cache_no_create(parent, _s);
        if (parent < 0) return parent;
    }
    TLOG << "ofd_find("<<fname<<") END";
    return parent;
}

int ofd_get(const std::string &fname)
{
    int parent = 0;
    TLOG << "ofd_get("<<fname<<") INIT";
    boost::char_separator<char> sep("/");
    boost::tokenizer<boost::char_separator<char> > tok(fname, sep);
    //BOOST_FOREACH(const std::string &_s, tok) {
    for (boost::tokenizer<boost::char_separator<char> >::iterator i = tok.begin(), n = tok.end(); i != n; ++i) {
        boost::tokenizer<boost::char_separator<char> >::iterator j = i; ++j;
        parent = ofd_get_id_cache(parent, *i, (j != n));
        if (parent < 0) return parent;
    }
    TLOG << "ofd_get("<<fname<<") END";
    return parent;
}
std::string ofd_get_info(int fid)
{
    TLOG << "ofd_get_info("<<fid<<") INIT";
    std::string info;
    if (fid > 0) {
        sqlite3_stmt *stmt = 0;
        bool is_file = false;
        boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_info);
        int rc = ofd_prep_sql_shared(&ofd_info_db, &stmt, ofd_info_fname.c_str(), "SELECT inf FROM info WHERE id=?");
        if (rc == SQLITE_OK) {
            if (sqlite3_bind_int(stmt, 1, fid) == SQLITE_OK) {
                if (sqlite3_step (stmt) == SQLITE_ROW) {
                    info.assign((char*)sqlite3_column_text(stmt, 0),sqlite3_column_bytes(stmt, 0));
                } else { TLOG << "ofd_get_info(" << fid << ")-> INFO NOT FOUND"; }
            } else { ELOG << "ofd_get_info(" << fid << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_info_db); }
        } else { ELOG << "ofd_get_info(" << fid << ")->ofd_prep_sql_shared"; }
        if (stmt) sqlite3_finalize(stmt);
    }
    TLOG << "ofd_get_info("<<fid<<") END";
    return info;
}
int ofd_set_info(int fid, const std::string &info)
{
    TLOG << "ofd_set_info("<<fid<<") INIT";
    if (fid > 0) {
        boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_info);
        int rc = ofd_prep_sql_unique(&ofd_info_db, &ofd_set_info_stmt, ofd_info_fname.c_str(), "INSERT OR REPLACE INTO info VALUES (?,?)");
        if (rc == SQLITE_OK) {
            if ((rc=sqlite3_bind_int(ofd_set_info_stmt, 1, fid)) == SQLITE_OK) {
                if ((rc=sqlite3_bind_text(ofd_set_info_stmt, 2, info.c_str(), info.size(), 0)) == SQLITE_OK) {
                    if ((rc=sqlite3_step (ofd_set_info_stmt)) != SQLITE_DONE) {
                        sqlite3_finalize(ofd_set_info_stmt); ofd_set_info_stmt = 0;
                        ELOG << "ofd_set_info(" << fid << ")->sqlite3_step: " << sqlite3_errmsg(ofd_info_db);
                    }
                } else { ELOG << "ofd_set_info(" << fid << ")->sqlite3_bind_text: " << sqlite3_errmsg(ofd_info_db); }
            } else { ELOG << "ofd_set_info(" << fid << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_info_db); }
        } else { ELOG << "ofd_set_info(" << fid << ")->ofd_prep_sql"; }
        fid = (rc!=SQLITE_DONE) ? rc : fid;
    }
    TLOG << "ofd_set_info("<<fid<<") END";
    return fid;
}
// get/set signatures
int ofd_set_sign(int fid, const std::vector<unsigned char> &sign)
{
    TLOG << "ofd_set_sign("<<fid<<") INIT";
    if (fid > 0) {
        boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_sign);
        int rc = ofd_prep_sql_unique(&ofd_sign_db, &ofd_set_sign_stmt,
                    ofd_sign_fname.c_str(), "INSERT OR REPLACE INTO sign VALUES (?,?)");
        if (rc == SQLITE_OK) {
            if ((rc=sqlite3_bind_int(ofd_set_sign_stmt, 1, fid)) == SQLITE_OK) {
                if ((rc=sqlite3_bind_blob(ofd_set_sign_stmt, 2, &sign.front(), sign.size(), 0)) == SQLITE_OK) {
                    if ((rc=sqlite3_step (ofd_set_sign_stmt)) != SQLITE_DONE) {
                        sqlite3_finalize(ofd_set_sign_stmt); ofd_set_sign_stmt = 0;
                        ELOG << "ofd_set_sign(" << fid << ")->sqlite3_step: " << sqlite3_errmsg(ofd_sign_db);
                    }
                } else { ELOG << "ofd_set_sign(" << fid << ")->sqlite3_bind_blob: " << sqlite3_errmsg(ofd_sign_db); }
            } else { ELOG << "ofd_set_sign(" << fid << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_sign_db); }
        } else { ELOG << "ofd_set_sign(" << fid << ")->ofd_prep_sql"; }
        fid = (rc!=SQLITE_DONE) ? rc : fid;
    }
    TLOG << "ofd_set_sign("<<fid<<") END";
    return fid;
}
int ofd_get_sign(int fid, std::vector<unsigned char> &sign)
{
    TLOG << "ofd_get_sign("<<fid<<") INIT";
    if (fid > 0) {
        sqlite3_stmt *stmt = 0;
        bool is_file = false;
        boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_sign);
        int rc = ofd_prep_sql_shared(&ofd_sign_db, &stmt, ofd_sign_fname.c_str(), "SELECT sig FROM sign WHERE id=?");
        if (rc == SQLITE_OK) {
            if ((rc=sqlite3_bind_int(stmt, 1, fid)) == SQLITE_OK) {
                if ((rc=sqlite3_step (stmt)) == SQLITE_ROW) {
                    sign.resize(sqlite3_column_bytes(stmt, 0));
                    memcpy(&sign.front(), sqlite3_column_blob(stmt, 0), sign.size());
                } else { TLOG << "ofd_get_sign(" << fid << ")-> INFO NOT FOUND"; }
            } else { ELOG << "ofd_get_sign(" << fid << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_sign_db); }
        } else { ELOG << "ofd_get_sign(" << fid << ")->ofd_prep_sql"; }
        if (stmt) sqlite3_finalize(stmt);
        fid = (rc!=SQLITE_ROW) ? rc : fid;
    }
    TLOG << "ofd_get_sign("<<fid<<") END";
    return fid;
}

static bool ofd_is_file(int child)
{
    TLOG << "ofd_is_file("<<child<<") INIT";
    sqlite3_stmt *stmt = 0;
    bool is_file = false;
    boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_info);
    int rc = ofd_prep_sql_shared(&ofd_info_db, &stmt, ofd_info_fname.c_str(), "SELECT id FROM info WHERE id=?");
    if (rc == SQLITE_OK) {
        if (sqlite3_bind_int(stmt, 1, child) == SQLITE_OK) {
            is_file = (sqlite3_step (stmt) == SQLITE_ROW);
        } else { ELOG << "ofd_is_file(" << child << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_info_db); }
    } else { ELOG << "ofd_is_file(" << child << ")->ofd_prep_sql"; }
    if (stmt) sqlite3_finalize(stmt);
    TLOG << "ofd_is_file("<<child<<") END";
    return is_file;
}
int ofd_get_childs(const std::string &fname,
        std::vector<std::string> *folders, std::vector<std::string> *files)
{
    TLOG << "ofd_get_childs("<<fname<<") INIT";
    int fid = fname.empty() ? 0 : ofd_find(fname);
    if (fid >= 0) {
        sqlite3_stmt *stmt = 0;
        boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_file);
        int rc = ofd_prep_sql_shared(&ofd_file_db, &stmt, ofd_file_fname.c_str(),
                "SELECT rowid,name FROM entry WHERE parent=?");
        if (rc == SQLITE_OK) {
            if (sqlite3_bind_int(stmt, 1, fid) == SQLITE_OK) {
                while (sqlite3_step (stmt) == SQLITE_ROW) {
                    int id = -1;
                    try { id = boost::lexical_cast<int>(sqlite3_column_text(stmt, 0)); }
                    catch (std::exception &e) { ELOG << "ofd_get_childs(" << fname << ")->BAD CHILD ID: " << e.what(); }
                    if (id > 0) {
                        std::string name((char*)sqlite3_column_text(stmt, 1),sqlite3_column_bytes(stmt, 1));
                        if (ofd_is_file(id)) { if (files) files->push_back(name); }
                        else { if (folders) folders->push_back(name); }
                    }
                }
            } else { ELOG << "ofd_get_childs(" << fname << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_file_db); }
        } else { ELOG << "ofd_get_childs(" << fname << ")->ofd_prep_sql"; }
        if (stmt) sqlite3_finalize(stmt);
    } else {
        WLOG << "ofd_get_childs(" << fname << ")-> fid NOT found";
    }
    TLOG << "ofd_get_childs("<<fname<<") END";
    return fid;
}
int ofd_move(const std::string &fname_old, const std::string &fname_new)
{
    TLOG << "ofd_move("<<fname_old<<") INIT";
    int fid = ofd_find(fname_old);
    if (fid > 0) {
        std::string fname = fname_new;
        std::string ofname = fname_old;
        size_t n = fname.find_last_of('/');
        if (n < std::string::npos) { fname = fname.substr(n+1); ofname = fname_old.substr(n+1);
            int parent = ofd_find(fname_old.substr(0,n));
            if (parent >= 0) { boost::unique_lock< boost::shared_mutex > lock(mtx_cache); fid_cache.erase(entry_key_t(parent, ofname));}
        } else { boost::unique_lock< boost::shared_mutex > lock(mtx_cache); fid_cache.erase(entry_key_t(0, ofname));}
        std::string s = "UPDATE entry SET name='" + fname + "' WHERE rowid=" +
            boost::lexical_cast<std::string>(fid);
        boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_file);
        ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), s.c_str());
    } else {
        WLOG << "ofd_move(" << fname_old << ")-> fid NOT found";
    }
    TLOG << "ofd_move("<<fname_old<<") END";
    return fid;
}
int ofd_move2(const std::string &name, const std::string &path_old, const std::string &path_new)
{
    TLOG << "ofd_move2("<<name<<", "<<path_old<<", "<<path_new<<") INIT";
    int pid_old = ofd_find(path_old);
    int pid_new = ofd_find(path_new);
    if (pid_old < 0 || pid_new < 0) {
        WLOG << "ofd_move2("<<name<<", "<<path_old<<", "<<path_new<<")-> Bad id " << pid_old << "/" << pid_new;
        return -1;
    }
    int fid = ofd_get_id_cache_no_create(pid_old, name);
    if (fid > 0) {
        { std::string s = "UPDATE entry SET parent=" + boost::lexical_cast<std::string>(pid_new) +
            " WHERE rowid=" + boost::lexical_cast<std::string>(fid);
        boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_file);
        ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), s.c_str());}
        { boost::unique_lock< boost::shared_mutex > lock(mtx_cache); fid_cache.erase(entry_key_t(pid_old, name));}
        std::vector<std::string> folders,files;
        ofd_get_childs(path_old, &folders, &files);
        if (ofd_get_childs(path_old, &folders, &files) > 0 && folders.empty() && files.empty()) ofd_rm(path_old);
    } else {
        WLOG << "ofd_move2("<<name<<", "<<path_old<<", "<<path_new<<")-> fid NOT found";
    }
    TLOG << "ofd_move2("<<name<<", "<<path_old<<", "<<path_new<<") END";
    return fid;
}
int ofd_rm(const std::string &fname)
{
    TLOG << "ofd_rm("<<fname<<") INIT";
    std::vector<std::string> folders,files;
    int fid = ofd_get_childs(fname, &folders, &files);
    if (!folders.empty() || !files.empty()) {
        WLOG << "ofd_rm(" << fname << ")-> Trying to erase a non empty entry";
        return fid;
    }
    if (fid > 0) {
        std::string sfid = boost::lexical_cast<std::string>(fid);
        std::string d_sign = "DELETE FROM sign WHERE id=" + sfid;
        std::string d_info = "DELETE FROM info WHERE id=" + sfid;
        std::string d_entry = "UPDATE entry SET parent=1,name='" + sfid + "' WHERE rowid=" + sfid;
        if (magic_number.empty()) d_entry = "DELETE FROM entry WHERE rowid=" + sfid;
        { boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_sign);
            ofd_exec_db_unique(&ofd_sign_db, ofd_sign_fname.c_str(), d_sign.c_str());}
        { boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_info);
            ofd_exec_db_unique(&ofd_info_db, ofd_info_fname.c_str(), d_info.c_str());}
        { boost::unique_lock< boost::shared_mutex > lock(mtx_ofd_file);
            ofd_exec_db_unique(&ofd_file_db, ofd_file_fname.c_str(), d_entry.c_str());
            if (!magic_number.empty()) hollows.push_back(fid); }
        size_t n = fname.find_last_of('/');
        if (n < std::string::npos) {
            std::string pname = fname.substr(0,n);
            int parent = ofd_find(pname);
            if (parent >= 0) {
                { boost::unique_lock< boost::shared_mutex > lock(mtx_cache);
                fid_cache.erase(entry_key_t(parent, fname.substr(n+1))); }
                std::vector<std::string> folders,files;
                ofd_get_childs(pname, &folders, &files);
                if (folders.empty() && files.empty()) ofd_rm(pname);
            }
        } else {
            boost::unique_lock< boost::shared_mutex > lock(mtx_cache);
            fid_cache.erase(entry_key_t(0, fname));
        }
    } else {
        WLOG << "ofd_rm(" << fname << ")-> fid NOT found";
    }
    TLOG << "ofd_rm("<<fname<<") END";
    return fid;
}

int ofd_get_entry_(int fid, std::string &name)
{
    sqlite3_stmt *stmt = 0;
    int parent = -1;
    TLOG << "ofd_get_entry("<<parent<<", "<<name<<") INIT";
    //boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_file);
    int rc = ofd_prep_sql_shared(&ofd_file_db, &stmt, ofd_file_fname.c_str(),
                    "SELECT parent,name FROM entry WHERE rowid=?");
    if (rc == SQLITE_OK) {
        if (sqlite3_bind_int(stmt, 1, fid) == SQLITE_OK) {
            if (sqlite3_step (stmt) == SQLITE_ROW) { try {
                    parent = boost::lexical_cast<int>(sqlite3_column_text(stmt, 0));
                    name.assign((char*)sqlite3_column_text(stmt, 1),sqlite3_column_bytes(stmt, 1));
                } catch (std::exception &e) { ELOG << "ofd_get_entry(" << fid << ")->BAD ID: " << e.what(); }
            } else { TLOG << "ofd_get_entry(" << fid << ")->NOT FOUND"; }
        } else { ELOG << "ofd_get_entry(" << fid << ")->sqlite3_bind_int: " << sqlite3_errmsg(ofd_file_db); }
    } else { ELOG << "ofd_get_entry(" << fid << ")->ofd_prep_sql_shared"; }
    if (stmt) sqlite3_finalize(stmt);
    TLOG << "ofd_get_entry("<<fid<<"): ["<<parent<<"] - '"<<name<<"' END";
    return parent;
}

int ofd_get_entry(int fid, std::string &name)
{
    boost::shared_lock< boost::shared_mutex > lock(mtx_ofd_file);
    return ofd_get_entry_(fid, name);
}

int ofd_reset(const std::string &magic)
{
    boost::unique_lock< boost::shared_mutex > lock1(mtx_ofd_file);
    boost::unique_lock< boost::shared_mutex > lock2(mtx_ofd_info);
    boost::unique_lock< boost::shared_mutex > lock3(mtx_ofd_sign);
    boost::unique_lock< boost::shared_mutex > lock(mtx_cache);
    fid_cache.clear();
    ofd_end();

    for (int i = 0; i < 30; i++) try {
            boost::this_thread::sleep(boost::posix_time::seconds(2));
            boost::filesystem::remove(ofd_file_fname);
            break;
        } catch (std::exception &e) { DLOG << "ofd_reset(" << magic << ")->remove(" << ofd_file_fname << ")->error("<<i<<"): " << e.what(); }
    boost::system::error_code ec;
	boost::filesystem::remove(ofd_info_fname, ec);
	boost::filesystem::remove(ofd_sign_fname, ec);

    magic_number = magic;
    return ofd_init();
}

void ofd_debug_str(std::stringstream &ss)
{
    ss << "<li><b>ofd_debug_str()</b></li>";
	ss << "<li>magic_number: "<<magic_number<<"</li>\n";
	ss << "<li>fid_cache.size(): "<<fid_cache.size()<<"</li>\n";
	ss << "<li>hollows.size(): "<<hollows.size()<<"</li>\n";
}

//////////////////////////////////////////////////////
//////////////////////////////////////////////////////

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>

static std::string ofd_dump_cache()
{
    std::stringstream ss;
    boost::shared_lock< boost::shared_mutex > lock(mtx_cache);
    boost::unordered_map<entry_key_t, int>::iterator i = fid_cache.begin(), n = fid_cache.end();
    for (; i != n; ++i) {
        ss << i->first.first << "-" << i->first.second << " --> " << i->second << std::endl;
    }
    return ss.str();
}

BOOST_AUTO_TEST_SUITE (main_test_suite_old_files)

BOOST_AUTO_TEST_CASE (old_files_tests_magic_number)
{
    BOOST_CHECK( ofd_init() == 0 );
    std::string mn = ofd_get_magic();
    BOOST_CHECK( mn.size() == 6 );
    BOOST_CHECK( ofd_end() == 0 );
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( mn == ofd_get_magic() );
    mn = "FA8643";
    BOOST_CHECK( ofd_reset(mn) == 0 );
    BOOST_CHECK( mn == ofd_get_magic() );
    BOOST_CHECK( ofd_end() == 0 );
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( mn == ofd_get_magic() );
    BOOST_CHECK( ofd_reset("") == 0 );
    BOOST_CHECK( ofd_get_magic().size() == 6 );
    BOOST_CHECK( mn != ofd_get_magic() );
    mn = ofd_get_magic();
    BOOST_CHECK( ofd_end() == 0 );
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( mn == ofd_get_magic() );
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_magic_rebuild)
{
    int fid[16];
    BOOST_CHECK( ofd_reset("") == 0 );
    BOOST_CHECK( (fid[0] = ofd_get("J:/uno/dos/tres/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get("K:/usno/dos/tress/cinco/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_find("J:")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_find("K:")) > 0 );
    BOOST_CHECK( (fid[4] = ofd_find("J:/uno")) > 0 );
    BOOST_CHECK( (fid[5] = ofd_find("J:/uno/dos")) > 0 );
    BOOST_CHECK( (fid[6] = ofd_find("J:/uno/dos/tres")) > 0 );
    BOOST_CHECK( (fid[7] = ofd_find("J:/uno/dos/tres/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[8] = ofd_find("K:/usno")) > 0 );
    BOOST_CHECK( (fid[9] = ofd_find("K:/usno/dos")) > 0 );
    BOOST_CHECK( (fid[10] = ofd_find("K:/usno/dos/tress")) > 0 );
    BOOST_CHECK( (fid[11] = ofd_find("K:/usno/dos/tress/cinco/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[12] = ofd_find("K:/usno/dos/tress/cinco")) > 0 );
    std::string mn = ofd_get_magic();
    BOOST_CHECK( ofd_reset("") == 0 );
    BOOST_CHECK( ofd_reset(mn) == 0 );
    BOOST_CHECK( (fid[0] == ofd_get("J:/uno/dos/tres/cuatro.ext")) );
    BOOST_CHECK( (fid[1] == ofd_get("K:/usno/dos/tress/cinco/cuatro.ext")) );
    BOOST_CHECK( (fid[2] == ofd_find("J:")) );
    BOOST_CHECK( (fid[3] == ofd_find("K:")) );
    BOOST_CHECK( (fid[4] == ofd_find("J:/uno")) );
    BOOST_CHECK( (fid[5] == ofd_find("J:/uno/dos")) );
    BOOST_CHECK( (fid[6] == ofd_find("J:/uno/dos/tres")) );
    BOOST_CHECK( (fid[7] == ofd_find("J:/uno/dos/tres/cuatro.ext")) );
    BOOST_CHECK( (fid[8] == ofd_find("K:/usno")) );
    BOOST_CHECK( (fid[9] == ofd_find("K:/usno/dos")) );
    BOOST_CHECK( (fid[10] == ofd_find("K:/usno/dos/tress")) );
    BOOST_CHECK( (fid[11] == ofd_find("K:/usno/dos/tress/cinco/cuatro.ext")) );
    BOOST_CHECK( (fid[12] == ofd_find("K:/usno/dos/tress/cinco")) );
    BOOST_CHECK( ofd_reset("") == 0 );
    BOOST_CHECK( ofd_end() == 0 );
}
BOOST_AUTO_TEST_CASE (old_files_tests_hollows)
{
    int fid[16];
    BOOST_CHECK( ofd_reset("") == 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( (fid[0] = ofd_get("J:/uno/dos/tres/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get("K:/usno/dos/tress/cinco/cuatro.ext")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_find("K:")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_find("K:/usno")) > 0 );
    BOOST_CHECK( (fid[4] = ofd_find("K:/usno/dos")) > 0 );
    BOOST_CHECK( (fid[5] = ofd_find("K:/usno/dos/tress")) > 0 );
    BOOST_CHECK( (fid[6] = ofd_find("K:/usno/dos/tress/cinco")) > 0 );
    BOOST_CHECK( (fid[7] = ofd_find("K:/usno/dos/tress/cinco/cuatro.ext")) > 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( ofd_rm("K:/usno/dos/tress/cinco/cuatro.ext") == fid[1] );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( hollows.size() == 6 );
    BOOST_CHECK( ofd_find("K:") < 0 );
    BOOST_CHECK( ofd_find("K:/usno") < 0 );
    BOOST_CHECK( ofd_find("K:/usno/dos") < 0 );
    BOOST_CHECK( ofd_find("K:/usno/dos/tress") < 0 );
    BOOST_CHECK( ofd_find("K:/usno/dos/tress/cinco") < 0 );
    BOOST_CHECK( ofd_find("K:/usno/dos/tress/cinco/cuatro.ext") < 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( (fid[7] = ofd_get("H:/usno/dos/trss/ccinco/cuatro.ext")) > 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( hollows.size() == 1 );
    BOOST_CHECK( fid[7] == (fid[1] + 1) );
    BOOST_CHECK( (fid[2] == ofd_find("H:")) );
    BOOST_CHECK( (fid[3] == ofd_find("H:/usno")) );
    BOOST_CHECK( (fid[4] == ofd_find("H:/usno/dos")) );
    BOOST_CHECK( (fid[5] == ofd_find("H:/usno/dos/trss")) );
    BOOST_CHECK( (fid[6] == ofd_find("H:/usno/dos/trss/ccinco")) );
    BOOST_CHECK( (fid[7] == ofd_find("H:/usno/dos/trss/ccinco/cuatro.ext")) );
    BOOST_CHECK( (fid[8] = ofd_get("H:/usno/dos/trss/ccinco/cuatros.ext")) > 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( hollows.size() == 1 );
    BOOST_CHECK( (fid[9] = ofd_get("H:/usno/dos/trss/ccincos/cuatros.ext")) > 0 );
//std::cout << "hollows.size(): " << hollows.size() << std::endl;
    BOOST_CHECK( hollows.size() == 0 );
    BOOST_CHECK( ofd_reset("") == 0 );
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_entries)
{
    int fid[7];
    int lgl = log_trace_level;
    log_trace_level = 2;
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( (fid[0] = ofd_get_id_create(0, "E:", true)) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get_id_create(fid[0], "uno", true)) > 0 );
    BOOST_CHECK( (fid[2] = ofd_get_id_create(fid[1], "dos", true)) > 0 );
    BOOST_CHECK( (fid[3] = ofd_get_id_create(fid[2], "tres", true)) > 0 );
    BOOST_CHECK( (fid[4] = ofd_get_id_create(fid[3], "cuatro.txt", false)) > 0 );

    BOOST_CHECK( ofd_get_id(0, "E:") == fid[0] );
    BOOST_CHECK( ofd_get_id(fid[0], "uno") == fid[1] );
    BOOST_CHECK( ofd_get_id(fid[1], "dos") == fid[2] );
    BOOST_CHECK( ofd_get_id(fid[2], "tres") == fid[3] );
    BOOST_CHECK( ofd_get_id(fid[3], "cuatro.txt") == fid[4] );

    std::string ename;
    BOOST_CHECK( ofd_get_entry(fid[1], ename) == fid[0] ); BOOST_CHECK( ename == "uno" );
    BOOST_CHECK( ofd_get_entry(fid[2], ename) == fid[1] ); BOOST_CHECK( ename == "dos" );
    BOOST_CHECK( ofd_get_entry(fid[3], ename) == fid[2] ); BOOST_CHECK( ename == "tres" );
    BOOST_CHECK( ofd_get_entry(fid[4], ename) == fid[3] ); BOOST_CHECK( ename == "cuatro.txt" );

    BOOST_CHECK( ofd_get_id(fid[1], "cinco.txt") == -1 );
    BOOST_CHECK( ofd_get_id(fid[3], "seis.txt") == -1 );

    BOOST_CHECK( (fid[5] = ofd_get_id_create(fid[1], "cinco.txt", false)) > 0 );
    BOOST_CHECK( (fid[6] = ofd_get_id_create(fid[3], "seis.txt", false)) > 0 );

    BOOST_CHECK( ofd_get_id(fid[1], "cinco.txt") == fid[5] );
    BOOST_CHECK( ofd_get_id(fid[3], "seis.txt") == fid[6] );

    BOOST_CHECK( ofd_get_entry(fid[5], ename) == fid[1] ); BOOST_CHECK( ename == "cinco.txt" );
    BOOST_CHECK( ofd_get_entry(fid[6], ename) == fid[3] ); BOOST_CHECK( ename == "seis.txt" );

    BOOST_CHECK( ofd_rm("E:/uno/dos/tres/cuatro.txt") == fid[4] );
    BOOST_CHECK( ofd_get_id(fid[3], "cuatro.txt") == -1 );

    BOOST_CHECK( ofd_get_id(fid[1], "cinco.txt") == fid[5] );
    BOOST_CHECK( ofd_get_id(fid[3], "seis.txt") == fid[6] );
    BOOST_CHECK( ofd_get_id(0, "E:") == fid[0] );
    BOOST_CHECK( ofd_get_id(fid[0], "uno") == fid[1] );
    BOOST_CHECK( ofd_get_id(fid[1], "dos") == fid[2] );
    BOOST_CHECK( ofd_get_id(fid[0], "dos") == -1 );
    BOOST_CHECK( ofd_get_id(fid[2], "tres") == fid[3] );
    BOOST_CHECK( ofd_get_id(fid[1], "tres") == -1 );

    BOOST_CHECK( ofd_rm("E:/uno/dos/seis.txt") != fid[6] );
    BOOST_CHECK( ofd_rm("E:/uno/dos/tres/seis.txt") == fid[6] );
    BOOST_CHECK( ofd_get_id(fid[3], "seis.txt") == -1 );

    BOOST_CHECK( ofd_get_id(fid[1], "cinco.txt") == fid[5] );
    BOOST_CHECK( ofd_get_id(0, "E:") == fid[0] );
    BOOST_CHECK( ofd_get_id(fid[0], "uno") == fid[1] );
    BOOST_CHECK( ofd_get_id(fid[1], "dos") == -1 );
    BOOST_CHECK( ofd_get_id(fid[0], "dos") == -1 );
    BOOST_CHECK( ofd_get_id(fid[2], "tres") == -1 );
    BOOST_CHECK( ofd_get_id(fid[1], "tres") == -1 );

    BOOST_CHECK( ofd_rm("E:/uno/cinco.txt") == fid[5] );
    BOOST_CHECK( ofd_get_id(fid[1], "cinco.txt") == -1 );

    BOOST_CHECK( ofd_get_id(fid[0], "uno") == -1 );
    BOOST_CHECK( ofd_get_id(0, "E:") == -1 );
    log_trace_level = lgl;

    BOOST_CHECK( fid_cache.empty() );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}
BOOST_AUTO_TEST_CASE (old_files_tests_cache)
{
    int fid[7];
    fid_cache.clear();
    int lgl = log_trace_level;
    log_trace_level = 2;

    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( (fid[4] = ofd_get("C:/uno/dos/tres/cuatro.txt")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_get("C:/uno/dos/tres/")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_get("C:/uno/dos")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get("C:/uno/")) > 0 );
    BOOST_CHECK( (fid[0] = ofd_get("C:")) > 0 );
    BOOST_CHECK( fid[0] == ofd_get("C:/") );

    BOOST_CHECK( fid_cache.size() == 5 );

    BOOST_CHECK( fid_cache.find(entry_key_t(0, "C:")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[0], "uno")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[1], "dos")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[1], "do")) == fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "tres")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cuatro.txt")) != fid_cache.end() );

    BOOST_CHECK( fid_cache[entry_key_t(0, "C:")] == fid[0] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[0], "uno")] == fid[1] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[1], "dos")] == fid[2] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[2], "tres")] == fid[3] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[3], "cuatro.txt")] == fid[4] );

    BOOST_CHECK( ofd_get_id_cache(0, "C:", true) == fid[0] );
    BOOST_CHECK( ofd_get_id_cache(fid[0], "uno", true) == fid[1] );
    BOOST_CHECK( ofd_get_id_cache(fid[1], "dos", true) == fid[2] );
    BOOST_CHECK( ofd_get_id_cache(fid[2], "tres", true) == fid[3] );
    BOOST_CHECK( ofd_get_id_cache(fid[3], "cuatro.txt", false) == fid[4] );

    BOOST_CHECK( fid_cache.size() == 5 );

    BOOST_CHECK( (fid[5] = ofd_get("C:/uno/dos/tres/cinco.txt")) > 0 );
    BOOST_CHECK( (fid[6] = ofd_get("C:/uno/dos/seis.txt")) > 0 );

    BOOST_CHECK( fid_cache.size() == 7 );

    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "seis.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cinco.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache[entry_key_t(fid[2], "seis.txt")] == fid[6] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[3], "cinco.txt")] == fid[5] );

    BOOST_CHECK( fid_cache.size() == 7 );

    BOOST_CHECK( ofd_rm("C:/uno/dos/tres/cuatro.txt") == fid[4] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cuatro.txt")) == fid_cache.end() );

    BOOST_CHECK( fid_cache.size() == 6 );

    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "seis.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cinco.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache[entry_key_t(fid[2], "seis.txt")] == fid[6] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[3], "cinco.txt")] == fid[5] );

    BOOST_CHECK( ofd_get_id_cache(fid[2], "seis.txt", false) == fid[6] );
    BOOST_CHECK( ofd_get_id_cache(fid[3], "cinco.txt", false) == fid[5] );

    BOOST_CHECK( fid_cache.size() == 6 );

    BOOST_CHECK( ofd_rm("C:/uno/dos/seis.txt") == fid[6] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "seis.txt")) == fid_cache.end() );

    BOOST_CHECK( fid_cache.size() == 5 );

    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cinco.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache[entry_key_t(fid[3], "cinco.txt")] == fid[5] );

    BOOST_CHECK( (fid[4] = ofd_get("C:/uno/dos/tres/cuatro1.txt")) > 0 );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cuatro1.txt")) != fid_cache.end() );
    BOOST_CHECK( fid_cache[entry_key_t(fid[3], "cuatro1.txt")] == fid[4] );
    BOOST_CHECK( fid[4] == ofd_move("C:/uno/dos/tres/cuatro1.txt", "C:/uno/dos/tres/cuatro2.txt") );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cuatro1.txt")) == fid_cache.end() );


    BOOST_CHECK( ofd_rm("C:/uno/dos/tres/cinco.txt") == fid[5] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cinco.txt")) == fid_cache.end() );

    BOOST_CHECK( fid_cache.size() == 4 );

    BOOST_CHECK( fid[1] == ofd_move("C:/uno", "C:/one") );

    BOOST_CHECK( fid_cache[entry_key_t(0, "C:")] == fid[0] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[0], "one")) == fid_cache.end() );
    BOOST_CHECK( fid_cache[entry_key_t(fid[1], "dos")] == fid[2] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[2], "tres")] == fid[3] );

    BOOST_CHECK( ofd_rm("C:/one") == fid[1] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[0], "one")) != fid_cache.end() );
    BOOST_CHECK( ofd_rm("C:/one/dos") == fid[2] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[1], "dos")) != fid_cache.end() );
    BOOST_CHECK( ofd_rm("C:/one/dos/tres") == fid[3] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "tres")) != fid_cache.end() );

    BOOST_CHECK( fid_cache[entry_key_t(0, "C:")] == fid[0] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[0], "one")] == fid[1] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[1], "dos")] == fid[2] );
    BOOST_CHECK( fid_cache[entry_key_t(fid[2], "tres")] == fid[3] );

    BOOST_CHECK( fid_cache.size() == 4 );

    BOOST_CHECK( ofd_rm("C:/one/dos/tres/cuatro2.txt") == fid[4] );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[3], "cuatro2.txt")) == fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[0], "one")) == fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[1], "dos")) == fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(fid[2], "tres")) == fid_cache.end() );
    BOOST_CHECK( fid_cache.find(entry_key_t(0, "C:")) == fid_cache.end() );

    BOOST_CHECK( fid_cache.empty() );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    log_trace_level = lgl;
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_childs)
{
    int lgl = log_trace_level;
    log_trace_level = 2;
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/cinco.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/seis.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/siete.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/ocho/nueve.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/ocho/diez.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/ocho/once.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/doce/trece.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/tres/doce/catorce.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/quince/dieciseis.txt") > 0 );
    BOOST_CHECK( ofd_get("F:/uno/dos/diecisiete.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/dos/tres.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/dos/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/dos/cinco.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/seis/tres.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/seis/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/seis/cinco.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/siete/tres.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/siete/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_get("/uno/siete/cinco.txt") > 0 );

    std::vector<std::string> folders,files;
    BOOST_CHECK( ofd_get_childs("", &folders, &files) == 0);
    BOOST_CHECK( folders.size() >= 2 );
    BOOST_CHECK( files.size() >= 0 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "F:") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "uno") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 0 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "uno") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "dos") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "seis") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "siete") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 0 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "dos") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/dos", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/seis", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/siete", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/siete", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "quince") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "diecisiete.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos/tres", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 6 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "ocho") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "doce") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "seis.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "siete.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos/quince", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "dieciseis.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos/diecisiete.txt", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 0 );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos/tres/ocho", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "nueve.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "diez.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "once.txt") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("F:/uno/dos/tres/doce", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 2 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "trece.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "catorce.txt") != folders.end() );

    BOOST_CHECK( ofd_move("uno/dos", "uno/doss") > 0 );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/dos", &folders, &files) < 0);
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("uno/doss", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 3 );
    BOOST_CHECK( files.size() == 0 ); // FIXME: must be files
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cuatro.txt") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "cinco.txt") != folders.end() );
    folders.clear(); files.clear();

    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/cinco.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/seis.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/siete.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/ocho/nueve.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/ocho/diez.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/ocho/once.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/doce/trece.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/tres/doce/catorce.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/quince/dieciseis.txt") > 0 );
    BOOST_CHECK( ofd_rm("F:/uno/dos/diecisiete.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/doss/tres.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/doss/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/doss/cinco.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/seis/tres.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/seis/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/seis/cinco.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/siete/tres.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/siete/cuatro.txt") > 0 );
    BOOST_CHECK( ofd_rm("/uno/siete/cinco.txt") > 0 );
    log_trace_level = lgl;

    BOOST_CHECK( fid_cache.empty() );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_move)
{
    int fid[7];
    int lgl = log_trace_level;
    log_trace_level = 2;
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( (fid[6] = ofd_get("N:/uno/dos/tres/seis.txt")) > 0 );
    BOOST_CHECK( (fid[5] = ofd_get("N:/uno/dos/tres/cinco.txt")) > 0 );
    BOOST_CHECK( (fid[4] = ofd_get("N:/uno/dos/tres/cuatro.txt")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_get("N:/uno/dos/tres/")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_get("N:/uno/dos")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get("N:/uno/")) > 0 );
    BOOST_CHECK( (fid[0] = ofd_get("N:")) > 0 );

    BOOST_CHECK( fid[6] == ofd_get("N:/uno/dos/tres/seis.txt") );
    BOOST_CHECK( fid[5] == ofd_get("N:/uno/dos/tres/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("N:/uno/dos/tres/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("N:/uno/dos/tres/") );
    BOOST_CHECK( fid[2] == ofd_get("N:/uno/dos") );
    BOOST_CHECK( fid[1] == ofd_get("N:/uno/") );
    BOOST_CHECK( fid[0] == ofd_get("N:") );

    BOOST_CHECK( fid[2] == ofd_move("N:/uno/dos", "N:/uno/doss") );

    BOOST_CHECK( fid[6] == ofd_get("N:/uno/doss/tres/seis.txt") );
    BOOST_CHECK( fid[5] == ofd_get("N:/uno/doss/tres/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("N:/uno/doss/tres/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("N:/uno/doss/tres/") );
    BOOST_CHECK( fid[2] == ofd_get("N:/uno/doss") );
    BOOST_CHECK( fid[1] == ofd_get("N:/uno/") );
    BOOST_CHECK( fid[0] == ofd_get("N:") );

    BOOST_CHECK( fid[1] == ofd_move("N:/uno", "N:/one") );

    BOOST_CHECK( fid[6] == ofd_get("N:/one/doss/tres/seis.txt") );
    BOOST_CHECK( fid[5] == ofd_get("N:/one/doss/tres/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("N:/one/doss/tres/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("N:/one/doss/tres/") );
    BOOST_CHECK( fid[2] == ofd_get("N:/one/doss") );
    BOOST_CHECK( fid[1] == ofd_get("N:/one") );
    BOOST_CHECK( fid[0] == ofd_get("N:") );

    BOOST_CHECK( fid[1] == ofd_rm("N:/one") );
    BOOST_CHECK( ofd_get_id(ofd_get_id(0, "N:"), "one") == fid[1] );

    BOOST_CHECK( fid[6] == ofd_get("N:/one/doss/tres/seis.txt") );
    BOOST_CHECK( fid[5] == ofd_get("N:/one/doss/tres/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("N:/one/doss/tres/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("N:/one/doss/tres/") );
    BOOST_CHECK( fid[2] == ofd_get("N:/one/doss") );
    BOOST_CHECK( fid[1] == ofd_get("N:/one") );
    BOOST_CHECK( fid[0] == ofd_get("N:") );

    BOOST_CHECK( fid[3] == ofd_move("N:/one/doss/tres", "N:/one/doss/iru") );

    BOOST_CHECK( fid[6] == ofd_get("N:/one/doss/iru/seis.txt") );
    BOOST_CHECK( fid[5] == ofd_get("N:/one/doss/iru/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("N:/one/doss/iru/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("N:/one/doss/iru/") );
    BOOST_CHECK( fid[2] == ofd_get("N:/one/doss") );
    BOOST_CHECK( fid[1] == ofd_get("N:/one") );
    BOOST_CHECK( fid[0] == ofd_get("N:") );

    BOOST_CHECK( fid[5] == ofd_rm("N:/one/doss/iru/cinco.txt") );
    BOOST_CHECK( fid[0] == ofd_move("N:", "M:") );

    BOOST_CHECK( fid[6] == ofd_get("M:/one/doss/iru/seis.txt") );
    BOOST_CHECK( fid[5] != ofd_get("M:/one/doss/iru/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_get("M:/one/doss/iru/cuatro.txt") );
    BOOST_CHECK( fid[3] == ofd_get("M:/one/doss/iru/") );
    BOOST_CHECK( fid[2] == ofd_get("M:/one/doss") );
    BOOST_CHECK( fid[1] == ofd_get("M:/one") );
    BOOST_CHECK( fid[0] == ofd_get("M:") );

    BOOST_CHECK( fid[6] == ofd_rm("M:/one/doss/iru/seis.txt") );
    BOOST_CHECK( fid[5] != ofd_rm("M:/one/doss/iru/cinco.txt") );
    BOOST_CHECK( fid[4] == ofd_rm("M:/one/doss/iru/cuatro.txt") );

    BOOST_CHECK( ofd_get_id(0, "N:") == -1 );
    BOOST_CHECK( ofd_get_id(0, "M:") == -1 );
    log_trace_level = lgl;

    BOOST_CHECK( fid_cache.empty() );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_info)
{
    BOOST_CHECK( ofd_init() == 0 );
    int fid1 = ofd_get("U:/one/dos/tres/cuatro.txt");
    int fid2 = ofd_get("U:/one/dos1/tres/cuatro.txt");
    std::string info = "1092870197 50917488901akfjda473%1908----3711028947/3536\\563471043***958431347433";
    int fid = ofd_set_info(fid1, info);
    BOOST_CHECK( ofd_set_info(fid1, info) == fid1 );
    BOOST_CHECK( ofd_set_info(fid2, info) == fid2 );
    BOOST_CHECK( ofd_get_info(fid1) == info );
    BOOST_CHECK( ofd_get_info(fid2) == info );
    info = " 01aka473%191#047/33471@043***958433";
    BOOST_CHECK( ofd_set_info(fid1, info) == fid1 );
    BOOST_CHECK( ofd_get_info(fid1) == info );
    BOOST_CHECK( fid1 == ofd_rm("U:/one/dos/tres/cuatro.txt") );
    BOOST_CHECK( ofd_get_info(fid1).empty() );
    fid1 = ofd_get("/frayperico/one/dos/y su burrico/cuatro.txt");
    info = "ajffn01983rnvzfqçvr394v´´kskjfsa903askjf90394";
    BOOST_CHECK( ofd_set_info(fid1, info) == fid1 );
    BOOST_CHECK( ofd_get_info(fid1) == info );
    info = " 01akn01983rnvzfqçvr394v´´kskjfsa903askjf90a473%191#047/33471@043***9584n01983rnvzfqçvr394v´´kskjfsa903askjf9033";
    BOOST_CHECK( ofd_set_info(fid1, info) == fid1 );
    BOOST_CHECK( ofd_get_info(fid1) == info );
    BOOST_CHECK( fid1 == ofd_rm("/frayperico/one/dos/y su burrico/cuatro.txt") );
    BOOST_CHECK( ofd_get_info(fid1).empty() );
    BOOST_CHECK( ofd_get_info(fid2).size() );
    BOOST_CHECK( ofd_set_info(fid2, info) == fid2 );
    BOOST_CHECK( ofd_get_info(fid2) == info );
    BOOST_CHECK( fid2 == ofd_rm("U:/one/dos1/tres/cuatro.txt") );
    BOOST_CHECK( ofd_get_info(fid2).empty() );

    BOOST_CHECK( fid_cache.empty() );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_sign)
{
    BOOST_CHECK( ofd_init() == 0 );
    int fid = ofd_get("O:/one/dos/tres/cuatro.txt");
    std::vector<unsigned char> sign;
    srand(time(0));
    for (int i = 0; i < 20000; i++) sign.push_back(rand());
    BOOST_CHECK( ofd_set_sign(fid, sign) == fid );
    std::vector<unsigned char> sign2;
    BOOST_CHECK( ofd_get_sign(fid, sign2) == fid );
    BOOST_CHECK( sign == sign2 );
    int fid2 = ofd_get("O:/one2/dos/tres/cuatro.txt");
    BOOST_CHECK( ofd_set_sign(fid2, sign2) == fid2 );
    BOOST_CHECK( sign == sign2 );
    sign.resize(10*1024*1024);
    size_t *data = (size_t*)&sign.front();
    for (int i = 0; i < sign.size()/(sizeof(size_t)); i++) data[i] = rand();
    BOOST_CHECK( ofd_set_sign(fid, sign) == fid );
    BOOST_CHECK( ofd_get_sign(fid, sign2) == fid );
    BOOST_CHECK( sign == sign2 );
    BOOST_CHECK( fid == ofd_rm("O:/one/dos/tres/cuatro.txt") );
    BOOST_CHECK( ofd_get_sign(fid, sign2) != fid );
    BOOST_CHECK( ofd_get_sign(fid2, sign2) == fid2 );
    BOOST_CHECK( sign2.size() == 20000 );
    BOOST_CHECK( fid2 == ofd_rm("O:/one2/dos/tres/cuatro.txt") );
    BOOST_CHECK( ofd_get_sign(fid2, sign2) != fid2 );

    BOOST_CHECK( fid_cache.empty() );
    BOOST_CHECK( ofd_get_id(0, "O:") == -1 );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_foldfile)
{
    int fid[6];
    BOOST_CHECK( ofd_init() == 0 );
    BOOST_CHECK( (fid[0] = ofd_get("G:/uno/dos/tres/cuatro.txt")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_get("G:/uno/dos/cinco.txt")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_get("G:/uno/tres/seis.txt")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_get("G:/uno/dos.txt")) > 0 );
    BOOST_CHECK( (fid[4] = ofd_get("G:/uno.txt")) > 0 );
    BOOST_CHECK( (fid[5] = ofd_get("G:/cero.txt")) > 0 );
    for (int i = 0; i < 6; i++) BOOST_CHECK( ofd_set_info(fid[i], "info") == fid[i] );
    std::vector<std::string> folders,files;
    BOOST_CHECK( ofd_get_childs("", &folders, &files) == 0);
    BOOST_CHECK( folders.size() >= 1 );
    BOOST_CHECK( files.size() >= 0 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "G:") != folders.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 2 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "uno") != folders.end() );
    BOOST_CHECK( std::find(files.begin(), files.end(), "uno.txt") != files.end() );
    BOOST_CHECK( std::find(files.begin(), files.end(), "cero.txt") != files.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:/uno", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 2 );
    BOOST_CHECK( files.size() == 1 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "dos") != folders.end() );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres") != folders.end() );
    BOOST_CHECK( std::find(files.begin(), files.end(), "dos.txt") != files.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:/uno/dos", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 1 );
    BOOST_CHECK( std::find(folders.begin(), folders.end(), "tres") != folders.end() );
    BOOST_CHECK( std::find(files.begin(), files.end(), "cinco.txt") != files.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:/uno/dos/tres", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 1 );
    BOOST_CHECK( std::find(files.begin(), files.end(), "cuatro.txt") != files.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:/uno/tres", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 1 );
    BOOST_CHECK( std::find(files.begin(), files.end(), "seis.txt") != files.end() );
    folders.clear(); files.clear();
    BOOST_CHECK( ofd_get_childs("G:/uno/dos.txt", &folders, &files) > 0);
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 0 );

    BOOST_CHECK( (fid[0] = ofd_rm("G:/uno/dos/tres/cuatro.txt")) > 0 );
    BOOST_CHECK( (fid[1] = ofd_rm("G:/uno/dos/cinco.txt")) > 0 );
    BOOST_CHECK( (fid[2] = ofd_rm("G:/uno/tres/seis.txt")) > 0 );
    BOOST_CHECK( (fid[3] = ofd_rm("G:/uno/dos.txt")) > 0 );
    BOOST_CHECK( (fid[4] = ofd_rm("G:/uno.txt")) > 0 );
    BOOST_CHECK( (fid[5] = ofd_rm("G:/cero.txt")) > 0 );

    BOOST_CHECK( fid_cache.empty() );
    BOOST_CHECK( ofd_get_id(0, "G:") == -1 );
    if (!fid_cache.empty()) std::cout << ofd_dump_cache();
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_CASE (old_files_tests_times)
{
    int lgl = log_trace_level;
//    log_trace_level = 12;
    ILOG << "old_files_tests_times ofd_init()";
    BOOST_CHECK( ofd_init() == 0 );

    std::string fname = "U:/uno/dos/tres/cuatro/cinco/seis/siete/ocho.txt";
    ILOG << "old_files_tests_times ofd_get(" << fname << ")";
    int fid = ofd_get(fname);
    ILOG << "old_files_tests_times ofd_get(" << fname << ") 2";
    BOOST_CHECK( ofd_get(fname) == fid );
    ILOG << "old_files_tests_times ofd_find(" << fname << ")";
    BOOST_CHECK( ofd_find(fname) == fid );
    fid_cache.clear();
    ILOG << "old_files_tests_times ofd_get(" << fname << ") 3";
    BOOST_CHECK( ofd_get(fname) == fid );
    fid_cache.clear();
    ILOG << "old_files_tests_times ofd_find(" << fname << ") 2";
    BOOST_CHECK( ofd_find(fname) == fid );

    ILOG << "old_files_tests_times ofd_set_info(" << fname << ")";
    BOOST_CHECK( ofd_set_info(fid, fname) == fid );
    ILOG << "old_files_tests_times ofd_get_info(" << fname << ")";
    BOOST_CHECK( ofd_get_info(fid) == fname );

    std::vector<unsigned char> v1(1000, 47);
    ILOG << "old_files_tests_times ofd_set_sign(" << fname << ")";
    BOOST_CHECK( ofd_set_sign(fid, v1) == fid );
    ILOG << "old_files_tests_times ofd_get_sign(" << fname << ")";
    BOOST_CHECK( ofd_get_sign(fid, v1) == fid );

    std::vector<std::string> folders,files;
    ILOG << "old_files_tests_times ofd_get_childs(" << fname << ")";
    BOOST_CHECK( ofd_get_childs(fname, &folders, &files) == fid );

    ILOG << "old_files_tests_times ofd_move(" << fname << ")";
    BOOST_CHECK( ofd_move(fname, fname + ".new") == fid );
    ILOG << "old_files_tests_times ofd_rm(" << fname << ")";
    BOOST_CHECK( ofd_rm(fname + ".new") == fid );

    BOOST_CHECK( fid_cache.empty() );
    BOOST_CHECK( ofd_get_id(0, "U:") == -1 );

    ILOG << "old_files_tests_times ofd_end()";
    BOOST_CHECK( ofd_end() == 0 );
    log_trace_level = lgl;
}

BOOST_AUTO_TEST_CASE (old_files_tests_moves)
{
    int lgl = log_trace_level;
    BOOST_CHECK( ofd_init() == 0 );

    std::string fname = "U:/uno/dos/tres/cuatro/cinco/seis/siete/ocho.txt";
    int fid = ofd_get(fname);
    BOOST_CHECK( ofd_get(fname) == fid );
    int fid2 = ofd_find("U:/uno/dos/tres/cuatro/cinco");
    BOOST_CHECK( ofd_move("U:/uno/dos/tres/cuatro/cinco", "U:/uno/dos/tres/cuatro/cincos") == fid2 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/cuatro/cincos/seis/siete/ocho.txt") == fid );
    int fid3 = ofd_find("U:/uno/dos/tres/cuatro/cincos/seis");
    BOOST_CHECK( ofd_move2("seis", "U:/uno/dos/tres/cuatro/cincos", "U:/uno/dos/tres") == fid3 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/seis") == fid3 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/seis/siete/ocho.txt") == fid );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/cuatro/cincos") < 0 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/cuatro") < 0 );
    int fid4 = ofd_get("U:/uno/diez/nueve.txt");
    int fid5 = ofd_find("U:/uno/diez");
    BOOST_CHECK( ofd_move2("diez", "U:/uno", "U:/uno/dos/tres/seis/siete") == fid5 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/seis/siete/ocho.txt") == fid );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/seis/siete/diez/nueve.txt") == fid4 );
    BOOST_CHECK( ofd_find("U:/uno/dos/tres/seis/siete/diez") == fid5 );
    BOOST_CHECK( ofd_rm("U:/uno/dos/tres/seis/siete/ocho.txt") == fid );
    BOOST_CHECK( ofd_rm("U:/uno/dos/tres/seis/siete/diez/nueve.txt") == fid4 );
    BOOST_CHECK( ofd_find("U:") < 0 );

    BOOST_CHECK( fid_cache.empty() );
    BOOST_CHECK( ofd_get_id(0, "U:") == -1 );
    BOOST_CHECK( ofd_end() == 0 );
    log_trace_level = lgl;
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
