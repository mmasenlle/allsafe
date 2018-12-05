
#include <stdio.h>
#include <time.h>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
extern "C" {
#include <sqlite3.h>
}
#include "persistence.h"
#include "log_sched.h"
#include "diff_stats.h"
#include "props_sched.h"

#define DBFNAME "stats.db"

static int persistence_exec(const char *sql)
{
    sqlite3 *db = 0;
    char *err_msg = 0;
    TLOG << "persistence_exec(" << sql << ") INIT";
    int rc = sqlite3_open((props::get().confd_path + DBFNAME).c_str(), &db);
    if (rc != SQLITE_OK) {
        ELOG << "persistence_exec()->Cannot open database: " << sqlite3_errmsg(db);
        if (db) sqlite3_close(db);
        return 1;
    }
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        DLOG << "persistence_exec()->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);
    TLOG << "persistence_exec(" << sql << ") END";
    return 0;
}

static int persistence_exec_db(const char *fname, const char *sql)
{
    sqlite3 *db = 0;
    char *err_msg = 0;
    TLOG << "persistence_exec_db(" << fname << ", " << sql << ") INIT";
    int rc = sqlite3_open(fname, &db);
    if (rc != SQLITE_OK) {
        ELOG << "persistence_exec_db(" << fname << ")->Cannot open database: " << sqlite3_errmsg(db);
        if (db) sqlite3_close(db);
        return 1;
    }
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        DLOG << "persistence_exec_db(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_close(db);
    TLOG << "persistence_exec_db(" << fname << ", " << sql << ") END";
    return 0;
}

static int pers_exec_sql(sqlite3 **db, const char *fname, const char *sql,
        int (*f)(void*,int,char**argv,char **azColName), void *d)
{
    int rc; char *err_msg = 0;
    TLOG << "pers_exec_sql(" << fname << ", " << sql << ") INIT";
    if (!*db) {
        rc = sqlite3_open(fname, db);
        if (rc != SQLITE_OK) {
            ELOG << "pers_exec_sql(" << fname << ")->Cannot open database: " << sqlite3_errmsg(*db);
            if (*db) sqlite3_close(*db); *db = 0;
            return 1;
        }
    }
    rc = sqlite3_exec(*db, sql, f, d, &err_msg);
    if (rc != SQLITE_OK ) {
        ELOG << "pers_exec_sql(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(*db); *db = 0;
        return 1;
    }
    TLOG << "pers_exec_sql(" << fname << ", " << sql << ") END";
    return 0;
}
static int pers_prep_sql(sqlite3 **db, sqlite3_stmt **stmt, const char *fname, const char *sql)
{
    int rc; char *err_msg = 0;
    if (!*db) {
        rc = sqlite3_open(fname, db);
        if (rc != SQLITE_OK) {
            ELOG << "pers_prep_sql(" << fname << ")->Cannot open database: " << sqlite3_errmsg(*db);
            if (*db) sqlite3_close(*db); *db = 0; *stmt= 0;
            return 1;
        }
    }
    if (!*stmt) {
        rc = sqlite3_prepare_v2 (*db, sql, -1, stmt, NULL);
        if (rc != SQLITE_OK) {
            ELOG << "pers_prep_sql(" << fname << ")->Cannot prepare_v2: " << sqlite3_errmsg(*db);
            sqlite3_close(*db); *db = 0; *stmt= 0;
            return 1;
        }
    }
    sqlite3_reset( *stmt);
    return 0;
}
static int pers_prep_sql_str(sqlite3 **db, sqlite3_stmt **stmt, const char *fname,
                             const char *sql, const std::string &str)
{
    char *err_msg = 0;
    if (pers_prep_sql(db, stmt, fname, sql) == SQLITE_OK) {
        if (sqlite3_bind_text(*stmt, 1, str.c_str(), str.size(), 0) != SQLITE_OK) {
            ELOG << "pers_prep_sql_str(" << fname << ")->Cannot sqlite3_bind_text: " << sqlite3_errmsg(*db);
            sqlite3_finalize(*stmt); *stmt= 0;
            sqlite3_close(*db); *db = 0;
            return 1;
        }
        int rc = sqlite3_step (*stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW && rc != SQLITE_OK) {
            ELOG << "pers_prep_sql_str(" << fname << ")->sqlite3_step: " << sqlite3_errmsg(*db);
        }
        return rc;
    } else {
        ELOG << "pers_prep_sql_str(" << fname << ")->pers_prep_sql: " << sqlite3_errmsg(*db);
    }
    return 1;
}

#define DIFF_STATS_FNAME "diff_stats.db"
#define DELETED_FNAME "deleted.db"

static sqlite3 *deleted_db = 0;
static sqlite3 *diff_stats_db = 0;

static sqlite3_stmt *deleted_get_stmt = 0;
static sqlite3_stmt *deleted_del_stmt = 0;

static sqlite3_stmt *diff_stats_get_stmt = 0;
static sqlite3_stmt *diff_stats_del_stmt = 0;

int persistence_end(void)
{
    if (deleted_get_stmt) { sqlite3_finalize(deleted_get_stmt); deleted_get_stmt = 0; }
    if (deleted_del_stmt) { sqlite3_finalize(deleted_del_stmt); deleted_del_stmt = 0; }
    if (diff_stats_get_stmt) { sqlite3_finalize(diff_stats_get_stmt); diff_stats_get_stmt = 0; }
    if (diff_stats_del_stmt) { sqlite3_finalize(diff_stats_del_stmt); diff_stats_del_stmt = 0; }
    if (deleted_db) { sqlite3_close(deleted_db); deleted_db = 0; }
    if (diff_stats_db) { sqlite3_close(diff_stats_db); diff_stats_db = 0; }
    return 0;
}

int persistence_init(void)
{
    pers_exec_sql(&diff_stats_db, (props::get().confd_path + DIFF_STATS_FNAME).c_str(),
            "CREATE TABLE IF NOT EXISTS diff_stats(File TEXT PRIMARY KEY,"
            "first INT, last INT, cnt INT, lsize INT, msize INT, ldur INT, mdur INT);", 0, 0);
    pers_exec_sql(&deleted_db, (props::get().confd_path + DELETED_FNAME).c_str(),
                  "CREATE TABLE IF NOT EXISTS deleted(File TEXT PRIMARY KEY, tstamp INT);"
            "CREATE INDEX IF NOT EXISTS deleted_tstamp ON deleted(tstamp);", 0, 0);

    pers_exec_sql(&diff_stats_db, (props::get().confd_path + DIFF_STATS_FNAME).c_str(), "PRAGMA synchronous = OFF", 0, 0);
    pers_exec_sql(&deleted_db, (props::get().confd_path + DELETED_FNAME).c_str(), "PRAGMA synchronous = OFF", 0, 0);

    const char *sql =
//                "DROP TABLE IF EXISTS Stats0;"
        "CREATE TABLE IF NOT EXISTS Stats0(File TEXT, tstamp INT, modtime INT, cnt INT);"
        "CREATE TABLE IF NOT EXISTS Stats1(File TEXT, tstamp INT, duration INT,"
            " cpu INT, io INT, net INT, psize INT);"
        "CREATE TABLE IF NOT EXISTS Stats2(File TEXT, tstamp INT, duration INT,"
            " cpu INT, io INT, net INT, psize INT);"
        "CREATE TABLE IF NOT EXISTS Stats3(File TEXT, tstamp INT, duration INT,"
            " cpu INT, io INT, net INT, psize INT);"
        ;
    return persistence_exec(sql);
}

static boost::mutex mtx_deleted;
std::vector<std::string> deleted_get(time_t tstamp)
{
    std::vector<std::string> files;
    int rc; char *err_msg = 0;
    boost::mutex::scoped_lock scoped_lock(mtx_deleted);
    rc = pers_prep_sql(&deleted_db, &deleted_get_stmt, (props::get().confd_path + DELETED_FNAME).c_str(),
            "SELECT File FROM deleted INDEXED BY deleted_tstamp WHERE tstamp<? ORDER BY tstamp ASC LIMIT 100");
    if (rc != SQLITE_OK) {
        ELOG << "deleted_get(" << (props::get().confd_path + DELETED_FNAME) << ")->Cannot prepare_v2: " << sqlite3_errmsg(deleted_db);
        if (deleted_get_stmt) sqlite3_finalize(deleted_get_stmt); deleted_get_stmt = 0;
        if (deleted_db) sqlite3_close(deleted_db); deleted_db = 0;
        return files;
    }
    if (sqlite3_bind_int(deleted_get_stmt, 1, tstamp) != SQLITE_OK) {
        ELOG << "deleted_get(" << (props::get().confd_path + DELETED_FNAME) << ")->Cannot sqlite3_bind_int: " << sqlite3_errmsg(deleted_db);
        if (deleted_get_stmt) sqlite3_finalize(deleted_get_stmt); deleted_get_stmt = 0;
        if (deleted_db) sqlite3_close(deleted_db); deleted_db = 0;
        return files;
    }
    do {
        rc = sqlite3_step (deleted_get_stmt);
        if (rc == SQLITE_ROW) {
            files.push_back(std::string((const char *)sqlite3_column_text(deleted_get_stmt, 0),
                                    sqlite3_column_bytes(deleted_get_stmt, 0)));
        } else if (rc != SQLITE_DONE) {
            ELOG << "deleted_get(" << (props::get().confd_path + DELETED_FNAME) << ")->Error in sqlite3_step: " << sqlite3_errmsg(deleted_db);
            if (deleted_get_stmt) sqlite3_finalize(deleted_get_stmt); deleted_get_stmt = 0;
            if (deleted_db) sqlite3_close(deleted_db); deleted_db = 0;
            break;
        }
    } while (rc == SQLITE_ROW);
    return files;
}


int deleted_put(const std::string &fname, time_t tstamp)
{
    std::string sql = "INSERT OR REPLACE INTO deleted VALUES ('" + fname + "',"
            + boost::lexical_cast<std::string>(tstamp) + ")";
    TLOG << "deleted_put("<<fname<<","<<tstamp<<")->(" << sql << ")";
    boost::mutex::scoped_lock scoped_lock(mtx_deleted);
    return pers_exec_sql(&deleted_db, (props::get().confd_path + DELETED_FNAME).c_str(), sql.c_str(), 0, 0);
}
int deleted_del(const std::string &fname)
{
    std::string sql = "DELETE FROM deleted WHERE File=?";
    TLOG << "deleted_del("<<fname<<")->(" << sql << ")";
    boost::mutex::scoped_lock scoped_lock(mtx_deleted);
    return pers_prep_sql_str(&deleted_db, &deleted_del_stmt, (props::get().confd_path + DELETED_FNAME).c_str(), sql.c_str(), fname);
}

////////////////// diff_stats

static boost::mutex mtx_diff_stats;

int diff_stats_load(const std::string &file, diff_stats_t *diff_stats)
{
    std::string sql = "SELECT first,last,cnt,lsize,msize,ldur,mdur "
        "FROM diff_stats WHERE File=?";
    memset(diff_stats, 0, sizeof(diff_stats_t));
    boost::mutex::scoped_lock scoped_lock(mtx_diff_stats);
    if (pers_prep_sql_str(&diff_stats_db, &diff_stats_get_stmt, (props::get().confd_path + DIFF_STATS_FNAME).c_str(),
                    sql.c_str(), file) == SQLITE_ROW) {
        diff_stats->first = boost::lexical_cast<long long>((const char *)sqlite3_column_text(diff_stats_get_stmt, 0));
        diff_stats->last = boost::lexical_cast<long long>((const char *)sqlite3_column_text(diff_stats_get_stmt, 1));
        diff_stats->cnt = boost::lexical_cast<int>((const char *)sqlite3_column_text(diff_stats_get_stmt, 2));
        diff_stats->lsize = boost::lexical_cast<int>((const char *)sqlite3_column_text(diff_stats_get_stmt, 3));
        diff_stats->msize = boost::lexical_cast<int>((const char *)sqlite3_column_text(diff_stats_get_stmt, 4));
        diff_stats->ldur = boost::lexical_cast<int>((const char *)sqlite3_column_text(diff_stats_get_stmt, 5));
        diff_stats->mdur = boost::lexical_cast<int>((const char *)sqlite3_column_text(diff_stats_get_stmt, 6));

        TLOG << "diff_stats_load("<<file<<")->(" << diff_stats->first << ","
             << diff_stats->last << ","  << diff_stats->cnt << ","
              << diff_stats->lsize << ","  << diff_stats->msize << ","
              << diff_stats->ldur << ","  << diff_stats->mdur << ")";
        return 0;
    }
    return 1;
}

int diff_stats_sync(const std::string &file, diff_stats_t *diff_stats)
{
    std::string sql;
    if (diff_stats->first) {
        sql = "UPDATE diff_stats SET "
        "last=" + boost::lexical_cast<std::string>(diff_stats->last) + ","
        "cnt=" + boost::lexical_cast<std::string>(diff_stats->cnt) + ","
        "lsize=" + boost::lexical_cast<std::string>(diff_stats->lsize) + ","
        "msize=" + boost::lexical_cast<std::string>(diff_stats->msize) + ","
        "ldur=" + boost::lexical_cast<std::string>(diff_stats->ldur) + ","
        "mdur=" + boost::lexical_cast<std::string>(diff_stats->mdur) + " "
        "WHERE File='" + std::string(file) + "';";
    } else {
        sql = "INSERT INTO diff_stats VALUES ('" + file + "',"
        + boost::lexical_cast<std::string>(diff_stats->last) + ","
        + boost::lexical_cast<std::string>(diff_stats->last) + ","
        + boost::lexical_cast<std::string>(diff_stats->cnt) + ","
        + boost::lexical_cast<std::string>(diff_stats->lsize) + ","
        + boost::lexical_cast<std::string>(diff_stats->msize) + ","
        + boost::lexical_cast<std::string>(diff_stats->ldur) + ","
        + boost::lexical_cast<std::string>(diff_stats->mdur) + ");";
    }
    TLOG << "diff_stats_sync("<<file<<")->(" << sql << ")";
    boost::mutex::scoped_lock scoped_lock(mtx_diff_stats);
    return pers_exec_sql(&diff_stats_db, (props::get().confd_path + DIFF_STATS_FNAME).c_str(), sql.c_str(), 0, 0);
}
int diff_stats_del(const std::string &file)
{
    std::string sql = "DELETE FROM diff_stats WHERE File=?";
    TLOG << "diff_stats_del("<<file<<")->(" << sql << ")";
    boost::mutex::scoped_lock scoped_lock(mtx_diff_stats);
    return pers_prep_sql_str(&diff_stats_db, &diff_stats_del_stmt, (props::get().confd_path + DIFF_STATS_FNAME).c_str(), sql.c_str(), file);
}



#ifndef NDEBUG
int persistence_insert0(const std::string &fpath, int tt, int cnt)
{
    char sql[1024];
    int tst = time(0);
    sprintf(sql, "INSERT INTO Stats0 VALUES('%s',%d,%d,%d);", fpath.c_str(), tst, tt, cnt);
    return persistence_exec(sql);
}

int persistence_insert(const std::string &fpath, int phase, int tt, int cpu, int io, int net, int ps)
{
    char sql[1024];
    int tst = time(0);
    sprintf(sql, "INSERT INTO Stats%d VALUES('%s',%d,%d,%d,%d,%d,%d);", phase+1, fpath.c_str(),
            tst, tt, cpu, io, net, ps);
    return persistence_exec(sql);
}

static int callback(void *NotUsed, int argc, char **argv,
                    char **azColName)
{
    std::string *sbuf = (std::string *)NotUsed;
    *sbuf += "<tr>";
    for (int i = 0; i < argc; i++) {

 //       printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        *sbuf += "<td>";
        *sbuf += argv[i] ? argv[i] : "NULL";
        *sbuf += "</td>";
    }

    //printf("\n");
    *sbuf += "</tr>\n";
    return 0;
}

static void persistence_dump(const char *sql, std::string *sbuf, const char *fname)
{
    sqlite3 *db;
    char *err_msg = 0;
    TLOG << "persistence_dump("<<fname<<",'"<<sql<<"')";
    int rc = sqlite3_open(fname, &db);
    if (rc != SQLITE_OK) {
        ELOG << "persistence_dump("<<fname<<")->Cannot open database: " << sqlite3_errmsg(db);
        sqlite3_close(db);
        return;
    }
    rc = sqlite3_exec(db, sql, callback, sbuf, &err_msg);
    if (rc != SQLITE_OK ) {
        WLOG << "persistence_dump(" << fname << ")->Error: " << err_msg;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return;
    }
    sqlite3_close(db);
}


std::string persistence_stats()
{
    std::string sbuf = "<br/><table><tr><td><table>";
    sbuf += "<tr><td>Total 1: </td></tr>"
            "<tr><td>Total files 1: </td></tr>"
            "<tr><td>Total tstamps 1: </td></tr>"
            "<tr><td>Total 2: </td></tr>"
            "<tr><td>Total files 2: </td></tr>"
            "<tr><td>Total tstamps 2: </td></tr>"
            "<tr><td>Max duration (s): </td></tr>"
            "<tr><td>Max cpu: </td></tr>"
            "<tr><td>Max io: </td></tr>"
            "<tr><td>Max psize: </td></tr>"
            "<tr><td>Older: </td></tr>"
            "<tr><td>Newer: </td></tr>"
            "<tr><td>Total 0: </td></tr>"
            "<tr><td>Max cnt: </td></tr>"
            "<tr><td>Total 3: </td></tr>"
            "<tr><td>Total files 3: </td></tr>"
            "<tr><td>Total tstamps 3: </td></tr>"
        "</table></td><td><table>";
    persistence_dump("SELECT COUNT(*) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT File) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT tstamp) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(*) FROM Stats2;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT File) FROM Stats2;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT tstamp) FROM Stats2;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(duration) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(cpu) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(io) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(psize) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MIN(tstamp) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(tstamp) FROM Stats1;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(*) FROM Stats0;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT MAX(cnt) FROM Stats0;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(*) FROM Stats3;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT File) FROM Stats3;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    persistence_dump("SELECT COUNT(DISTINCT tstamp) FROM Stats3;", &sbuf, (props::get().confd_path + DBFNAME).c_str());
    sbuf += "</table></td></tr></table><br/>";
static const char *order_item[] = {"file","time","duration","cpu","io","psize"};
    sbuf += "mupatxer by ";
    for (int i = 0; i < 6; i++) {
        sbuf += "<a href=\"stats1?op="; sbuf.push_back(i+'1');
        sbuf += "\">"; sbuf += order_item[i]; sbuf += "</a>&nbsp;&nbsp;";
    }
    sbuf += "<br/><br/>musynchro by ";
    for (int i = 0; i < 6; i++) {
        sbuf += "<a href=\"stats2?op="; sbuf.push_back(i+'1');
        sbuf += "\">"; sbuf += order_item[i]; sbuf += "</a>&nbsp;&nbsp;";
    }
    sbuf += "<br/><br/>winscp by ";
    for (int i = 0; i < 6; i++) {
        sbuf += "<a href=\"stats3?op="; sbuf.push_back(i+'1');
        sbuf += "\">"; sbuf += order_item[i]; sbuf += "</a>&nbsp;&nbsp;";
    }
static const char *order_item0[] = {"file","time","modtime","cnt","time","modtime","cnt"};
    sbuf += "<br/><br/>all files by ";
    for (int i = 0; i < 7; i++) {
        sbuf += "<a href=\"stats0?op="; sbuf.push_back(i+'1');
        sbuf += "\">"; sbuf += order_item0[i]; sbuf += "</a>&nbsp;&nbsp;";
    }
    return sbuf;
}

std::string persistence_dump0(int order)
{
    std::string sbuf = "<h3>Procesed files stats</h3><table><tr>"
            "<th>Path</th><th>Time</th><th>Modification time</th>"
            "<th>Modifications</th>\n";
    std::string sql = "SELECT * FROM Stats0";
    switch (order) {
    case 1: sql += " ORDER BY File ASC LIMIT 80"; break;
    case 2: sql += " ORDER BY tstamp DESC LIMIT 80"; break;
    case 3: sql += " ORDER BY modtime DESC LIMIT 80"; break;
    case 4: sql += " ORDER BY cnt DESC LIMIT 80"; break;
    case 5: sql += " ORDER BY tstamp ASC LIMIT 80"; break;
    case 6: sql += " ORDER BY modtime ASC LIMIT 80"; break;
    case 7: sql += " ORDER BY cnt ASC LIMIT 80"; break;
    }
    sql.push_back(';');
    persistence_dump(sql.c_str(), &sbuf, (props::get().confd_path + DBFNAME).c_str());
    sbuf += "</table>\n";
    return sbuf;
}

static const char *phasename[] = {"","Mupatxer","Musynchro","Winscp"};
std::string persistence_dump1(int phase, int order)
{
    std::string sbuf = "<h3>"; sbuf += phasename[phase];
    sbuf += " phase stats</h3><table><tr>"
            "<th>Path</th><th>Time</th><th>Duration</th><th>CPU</th>"
            "<th>IO</th><th>NET</th><th>PSIZE</th></tr>\n";
    std::string sql = "SELECT * FROM Stats0";
    sql[sql.size()-1] += phase;
    switch (order) {
    case 1: sql += " ORDER BY File ASC LIMIT 80"; break;
    case 2: sql += " ORDER BY tstamp ASC LIMIT 80"; break;
    case 3: sql += " ORDER BY duration DESC LIMIT 80"; break;
    case 4: sql += " ORDER BY cpu DESC LIMIT 80"; break;
    case 5: sql += " ORDER BY io DESC LIMIT 80"; break;
    case 6: sql += " ORDER BY psize DESC LIMIT 80"; break;
    }
    sql.push_back(';');
    persistence_dump(sql.c_str(), &sbuf, (props::get().confd_path + DBFNAME).c_str());
    sbuf += "</table>\n";
    return sbuf;
}

std::string diff_stats_dump(int order)
{
    std::string sbuf = "<h3>Processed files stats</h3><table><tr>"
            "<th>File</th><th>First</th><th>Last</th><th>Count</th>"
            "<th>Size</th><th>E[Size]</th><th>Time</th><th>E[Time]</th>\n";
    std::string sql = "SELECT * FROM diff_stats";
    switch (order) {
    case 0: sql += " ORDER BY last DESC LIMIT 80"; break;
    case 1: sql += " ORDER BY File ASC LIMIT 80"; break;
    case 2: sql += " ORDER BY File DESC LIMIT 80"; break;
    case 3: sql += " ORDER BY cnt DESC LIMIT 80"; break;
    case 4: sql += " ORDER BY lsize DESC LIMIT 80"; break;
    case 5: sql += " ORDER BY msize DESC LIMIT 80"; break;
    case 6: sql += " ORDER BY ldur DESC LIMIT 80"; break;
    case 7: sql += " ORDER BY mdur DESC LIMIT 80"; break;
    }
    sql.push_back(';');
    persistence_dump(sql.c_str(), &sbuf, (props::get().confd_path + DIFF_STATS_FNAME).c_str());
    sbuf += "</table>\n";
    return sbuf;
}
#endif
