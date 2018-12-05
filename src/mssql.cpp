
#ifdef COMP1

#endif // COMP1
#include <time.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#ifndef NO_PROCESS
#include <boost/process.hpp>
#endif
#include <boost/tokenizer.hpp>
#include "fstream_utf8.h"
#include "stats_dbs.h"
#include "props_sched.h"
#include "dict.h"
#include "log_sched.h"
#include "web_utils.h"

static size_t last_time = 0;
static size_t period_secs = 0; // <=0 desactivado
static size_t start_time = 0;
static size_t stop_time = 24*3600;

static std::string script;
static std::string osql_path = "C:\\Program Files\\Microsoft SQL Server\\120\\Tools\\Binn\\OSQL.EXE";

static time_t tt = 0;
static time_t tday = 0;
static inline void get_time() { if (!tt) {tt = time(0); tday = tt % (24*3600);}}

static std::map<std::string,std::string> mssql_get_trn_versions(const std::string &trn)
{
    std::map<std::string,std::string> trns;
    try {
        boost::filesystem::directory_iterator it(boost::filesystem::path(trn).parent_path()), itEnd;
        for (; it != itEnd; it++) {
            if (it->path().string().size() >= trn.size() + 14 && it->path().extension().string() == ".trn"
                    && it->path().string().compare(0, trn.size(), trn) == 0) {
                std::string stamp = it->path().string().substr(it->path().string().size() - 14, 10);
                trns[stamp] = it->path().string();
            }
        }
    } catch (std::exception &e) {
        WLOG << "mssql_get_trn_versions(" << trn << ")->Error: " << e.what();
    }
    return trns;
}

namespace {

struct db_t
{
    time_t last_time;
    size_t period_secs; // <=0 disabled
    size_t start_time;
    size_t stop_time;
    size_t last_id;
    int min_changes; // <=0 skipes get_tr_id()

    std::string conn_args; // -S "<hostname>:<port>\<instance>" -E | -U sa -P sa_pass
    std::string dbname;
    std::string trnfile;
    std::string restfile;

    volatile bool running;

    db_t() : last_time(0), period_secs(900), start_time(8*3600), stop_time(23*3600),
                last_id(0), min_changes(3), running(false) {};

    bool is_time()
    {
        get_time();
        if (!last_time) { last_time = tt; return false; } // start easy
        if (tday > start_time && tday < stop_time &&
                (tt > last_time + period_secs || tt < last_time)) {
            last_time = tt;
            return true;
        }
        return false;
    }
    int run_osql(const std::string &sql, std::string *resp)
    {
        TLOG << "db_t::run_osql(" << sql << ")";
        boost::char_separator<char> sep(" ");
        std::vector<std::string> args;
        args.push_back(osql_path);
        {   boost::tokenizer<boost::char_separator<char> > tok(conn_args, sep);
         BOOST_FOREACH(const std::string &_s, tok) { args.push_back(_s); }   }
        args.push_back("-d"); args.push_back(dbname);
        args.push_back("-Q"); args.push_back(sql);
#ifndef NDEBUG
if (log_trace_level > 5) {
    std::string ss = "db_t::run_osql->launch(";
    BOOST_FOREACH(const std::string &s1, args) ss += s1 + " ";
    TLOG << ss << ")";
}
#endif
#ifndef NO_PROCESS
        try {
            boost::process::context ctx;
            ctx.environment = boost::process::self::get_environment();
            if (resp) ctx.stdout_behavior = boost::process::capture_stream();
            else if (log_trace_level > 5) ctx.stdout_behavior = boost::process::inherit_stream();
            boost::process::child c = boost::process::launch(args[0], args, ctx);
            if (resp) {
                std::string line;
                boost::process::pistream &is = c.get_stdout();
                while (std::getline(is, line)) { resp->append(line); resp->push_back('\n'); }
            }
            boost::process::status s = c.wait();
            return s.exited() ? s.exit_status() : -1;
        } catch(std::exception &e) {
            ELOG << "db_t::run_osql(" << args[0] << ", " << dbname << ")->launch() Exception: " << e.what();
        }
#endif
        return -1;
    }
    size_t get_tr_id()
    {
        std::string resp; int r = -1; size_t tr_id = 0;
        if ((r = run_osql("select max([Transaction ID]) from fn_dblog(NULL,NULL)", &resp))==0 && resp.size() > 30) {
            size_t i = 20, n = resp.size()-10;
            for (; i < n; i++) if (resp.at(i) == ':') { tr_id = strtol(resp.c_str() + i + 1, NULL, 16); break; }
            DLOG << "db_t::get_tr_id(" << dbname << "): " << tr_id << ", " << last_id;
        } else {
            WLOG << "db_t::get_tr_id(" << dbname << ")->error: " << r;
        }
        return tr_id;
    }
    void backup_logs()
    {
        DLOG << "db_t::backup_logs(" << dbname << ")";
        long tr_id = 0; int r = -1;
        if (min_changes > 0) tr_id = get_tr_id();
//WLOG << "db_t::backup_logs(" << dbname << ") " << tr_id << ", " << last_id << ", " << min_changes << ", " << (tr_id >= (last_id + min_changes));
        if ((tr_id > (last_id + min_changes)) || (min_changes <= 0) || (tr_id < last_id) || !tr_id) {
            ILOG << "About to backup logs mssql_db(" << dbname << ")";
            std::string trnf0 = trnfile + boost::lexical_cast<std::string>(time(0)) + ".trn";
            if ((r = run_osql("BACKUP LOG " + dbname + " TO DISK='" + trnf0 + "'", NULL))) {
                WLOG << "db_t::backup_logs(" << dbname << ")->r: " << r;
            } else if (min_changes > 0) {
                tr_id = get_tr_id();
                last_id = tr_id ? tr_id : last_id + 2; // +2..4 depending on whether there are connections established or not
            }
            if (r == 0) try { stats_dbs_put(dbname, boost::filesystem::file_size(trnf0)); } catch (std::exception &e) { WLOG << "db_t::backup_logs(" << trnf0 << ")->file_size: " << e.what(); }
        } else if (tr_id > last_id) last_id++; // compensate the possible connection
        running = false;
    }
    void run()
    {
        if (period_secs > 0 && !running && is_time()) {
            running = true;
            boost::thread th(boost::bind(&db_t::backup_logs, this));
            th.detach();
        }
    }
    void backup_0()
    {
        ILOG << "About to backup database mssql_db(" << dbname << ")";
        run_osql("ALTER DATABASE " + dbname + "  SET RECOVERY FULL", NULL);
        int r = run_osql("BACKUP DATABASE " + dbname + " TO DISK='" + trnfile + "0.bck' WITH INIT", NULL);
        DLOG << "db_t::backup_0(" << dbname << "): " << r;
        if (r == 0) { try {
            boost::filesystem::directory_iterator end_itr, itr(boost::filesystem::path(trnfile).branch_path());
            for (; itr != end_itr; itr++) { try {
                std::string p = itr->path().string();
                if (boost::filesystem::is_regular_file(itr->status()) && p.size() > trnfile.size()
                        && (p.compare(0, trnfile.size(), trnfile) == 0)
                        && (p.compare(p.size() - 4, 4, ".trn") == 0)) {
                    DLOG << "backup_0()->deleting: " << itr->path();
                    boost::filesystem::remove(itr->path());
                }} catch (std::exception &e) {
                    WLOG << "db_t::backup_0("<<dbname<<")->Error removing '"<<itr->path()<<"' - " << e.what();
                }
            }
        } catch (std::exception &e) {
            WLOG << "db_t::backup_0("<<dbname<<")->Error removing old trn files - " << e.what();
        }}
        running = false;
    }
    void restore(const std::string &vt)
    {
        running = true;
        ILOG << "About to restore database mssql_db(" << dbname << ", " << vt << ")";
        std::string resp, lf_data, lf_log;
        int r = run_osql("RESTORE FILELISTONLY FROM DISK='" + trnfile + "0.bck'", &resp);
        if (r == 0) { try {
            std::size_t n = resp.find("\n ", 1980);
            if (n < std::string::npos) n += 2;
            for (; n < resp.size() && !isspace(resp[n]); n++) lf_data.push_back(resp[n]);
            if (n < std::string::npos) n = resp.find("\n ", n);
            if (n < std::string::npos) n += 2;
            for (; n < resp.size() && !isspace(resp[n]); n++) lf_log.push_back(resp[n]);
        } catch (std::exception &e) { WLOG << "db_t::restore(" << dbname << ")->get logical files: " << e.what(); }}
        if (r != 0 || lf_data.empty() || lf_log.empty()) {
            ELOG << "db_t::restore(" << dbname << "): Error retrieving logical file names";
        } else {
            std::string st = boost::lexical_cast<std::string>(time(0));
            std::string rest_cmd = "RESTORE DATABASE " + dbname + "_" + st +
                " FROM DISK = '" + trnfile + "0.bck' "
                "WITH MOVE '" + lf_data + "' TO '" + restfile + "_" + st + ".mdf'" +
                ", MOVE '" + lf_log + "' TO '" + restfile + "_" + st + "_Log.ldf'";
            if (vt != "0") rest_cmd += ", NORECOVERY";
            resp.clear();
            r = run_osql(rest_cmd, &resp);
TLOG << "db_t::restore("<<dbname<<")->run_osql("<<rest_cmd<<"): "<<r<<"\nresp:" << resp;
            if (r == 0 && vt != "0") {
                std::map<std::string,std::string> vers = mssql_get_trn_versions(trnfile);
                for (std::map<std::string,std::string>::const_iterator i = vers.begin(), n = vers.end(); i != n; ++i) {
                    if (i->first <= vt) { try {
                        resp.clear();
                        rest_cmd = "RESTORE LOG " + dbname + "_" + st + " FROM DISK = '" + i->second + "' WITH ";
                        if (i->first == vt) rest_cmd += "RECOVERY"; else rest_cmd += "NORECOVERY";
                        r = run_osql(rest_cmd, &resp);
TLOG << "db_t::restore("<<dbname<<")->run_osql("<<rest_cmd<<"): "<<r<<"\nresp:" << resp;
                        if (r != 0) {
                            ELOG << "restore(" << dbname << ")->Error restoring logs";
                            break;
                        }
                    } catch (std::exception &e) { ELOG << "restore(" << dbname << ")->Error: " << e.what(); }}
                }
            }
            if (r == 0) ILOG << "Database mssql_db(" << dbname << ", " << vt << ") successfully restored";
        }
        running = false;
    }
};
}

static boost::mutex mtx_dbs;
static std::map<std::string, db_t> dbs;

static bool is_time()
{
    get_time();
    if (tday > start_time && tday < stop_time &&
		(tt > last_time + period_secs || tt < last_time)) {
        last_time = tt;
        return true;
    }
    return false;
}


static volatile int running = 0;
static void mssql_backup_logs_run()
{
    TLOG << "mssql_backup_logs()->system(" << script << ")";
    int r = system(script.c_str());
    DLOG << "mssql_backup_logs()->system(" << script << "):" << r;
    running = 0;
}

int mssql_backup_logs()
{
    tt = 0;
    if (period_secs > 0 && !script.empty() && !running && is_time()) {
        running = 1;
        boost::thread th(mssql_backup_logs_run);
        th.detach();
    }

    boost::mutex::scoped_lock scoped_lock(mtx_dbs);
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        i->second.run();
    }
    return 0;
}

void mssql_add_db(const std::string &n, const std::string &c, const std::string &t, const std::string &r,
            size_t p, size_t s0, size_t s1, int m)
{
    db_t db;
    db.dbname = n;
    db.conn_args = c;
    db.trnfile = boost::algorithm::replace_all_copy(t, "/", "\\");
    db.restfile = boost::algorithm::replace_all_copy(r, "/", "\\");
    db.period_secs = p;
    db.start_time = s0;
    db.stop_time = s1;
    db.min_changes = m;
    boost::mutex::scoped_lock scoped_lock(mtx_dbs);
    dbs[db.dbname] = db;
}

#define MSSQL_FILENAME "mssql.xml"
void mssql_save()
{
    TLOG << "mssql_save(): " << dbs.size();
    std::string mssql_fname = props::get().confd_path + MSSQL_FILENAME;
    try {
        ofstream_utf8 ofs(mssql_fname.c_str());
        if (ofs) {
            ofs << "<?xml version=\"1.0\"?>\n<mssql_dbs>\n";
            for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
                ofs << "\t<db name=\"" << i->second.dbname << "\">\n";
                ofs << "\t\t<conn_args>" << i->second.conn_args << "</conn_args>\n";
                ofs << "\t\t<trnfile>" << i->second.trnfile << "</trnfile>\n";
                ofs << "\t\t<restfile>" << i->second.restfile << "</restfile>\n";
                ofs << "\t\t<period_secs>" << i->second.period_secs << "</period_secs>\n";
                ofs << "\t\t<start_time>" << i->second.start_time << "</start_time>\n";
                ofs << "\t\t<stop_time>" << i->second.stop_time << "</stop_time>\n";
                ofs << "\t\t<min_changes>" << i->second.min_changes << "</min_changes>\n";
                ofs << "\t</db>\n";
            }
            ofs << "</mssql_dbs>\n";
        }
    } catch (std::exception &e) {
        ELOG << "mssql_save()->Exception '" << e.what() << "' saving file '" <<mssql_fname<< "'";
    }
    TLOG << "mssql_save() END";
}

int mssql_load()
{
    TLOG << "mssql_load() INIT";
    std::string mssql_fname = props::get().confd_path + MSSQL_FILENAME;
    try {
        ifstream_utf8 ifs(mssql_fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("mssql_dbs") ) {
            try { if( v.first == "db" ) {
                mssql_add_db(v.second.get<std::string>("<xmlattr>.name"),
                       v.second.get<std::string>("conn_args"),
                       v.second.get<std::string>("trnfile"), v.second.get<std::string>("restfile"),
                       v.second.get<int>("period_secs"), v.second.get<int>("start_time"),
                       v.second.get<int>("stop_time"), v.second.get<int>("min_changes"));
            }} catch (std::exception &e) {
                WLOG << "mssql_load()->Exception '" << e.what() << "' parsing file '" <<mssql_fname<< "'";
            }}
        }
    } catch (std::exception &e) {
        ELOG << "mssql_load()->Exception '" << e.what() << "' loading file '" <<mssql_fname<< "'";
    }
    TLOG << "mssql_load(): " << dbs.size();
    return 0;
}

//extern const std::string &wserver_confirm();
std::string mssql_main()
{
    std::string s = wserver_pages_head();
    s += "<body><h2>Bases de datos</h2>\n"
        "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
//    std::string s = "<h2>Bases de datos</h2>\n"
        "<a href=/mssql?save=1 class=ml>save</a><a href=/mssql?load=1 class=ml>load</a><a href=/mssql?edit=1 class=ml>nueva</a><br/><br/>\n";
    s += "<table>";
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        s += "<tr><th>" + i->first;
        s += "</th><td>&nbsp;&nbsp;<a href=\"mssql?edit=" + i->first + "\">edit</a></td>";
        s += "<td>&nbsp;&nbsp;<a href=# onclick='go_if(\"Delete ?\",\"mssql?remove=" + i->first + "\")'>remove</a></td>";
        s += "<td>&nbsp;&nbsp;<a href=# onclick='go_if(\"Reset backups ?\",\"mssql?init=" + i->first + "\")'>reset</a></td>";
        s += "</th><td>&nbsp;&nbsp;<a href=\"mssql?rest=" + i->first + "\">restore</a></td>"
            "</tr>\n";
    }
    s += "</table><br/><br/>\n";
//    s += wserver_confirm();
//    s += "</table><br/>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/mssql?edit=1 class=ml>nueva</a>\n";
    s += wserver_confirm() + "</body></html>";
    return s;
}

static std::string mssql_edit(const std::string &n)
{
    db_t db;
    std::string s = wserver_pages_head();
    if (n.size() > 1 && dbs.find(n) != dbs.end()) {
        db = dbs[n];
        s += "<body><h2>" + n + "</h2>\n";
    } else s += "<body><h2>NEW DB</h2>\n";
    s += "<form action=mssql method=get><table class=\"new_db\">"
        "<tr><th>Name:</th><td><input type=text name=db size=60 value=\"" + db.dbname + "\"></td></tr>\n";
    s += "<tr><th>Connection:</th><td><input type=text name=con size=60 value='" + db.conn_args + "'></td></tr>\n";
    s += "<tr><th>Bck File:</th><td><input type=text name=trn size=60 value=\"" + db.trnfile + "\"></td></tr>\n";
    s += "<tr><th>Rest File:</th><td><input type=text name=rest size=60 value=\"" + db.restfile + "\"></td></tr>\n";
    s += "<tr><th>Period  (s):</th><td><input type=text name=p size=10 value=\"" + props::from_seconds(db.period_secs) + "\"></td></tr>\n";
    s += "<tr><th>Start (h:m:s):</th><td><input type=text name=s0 size=10 value=\"" + props::from_seconds(db.start_time) + "\"></td></tr>\n";
    s += "<tr><th>Stop  (h:m:s):</th><td><input type=text name=s1 size=10 value=\"" + props::from_seconds(db.stop_time) + "\"></td></tr>\n";
    s += "<tr><th>Min changes:</th><td><input type=text name=m size=10 value=\"" + boost::lexical_cast<std::string>(db.min_changes) + "\"></td></tr>\n";
    s += "<tr><td>"; s += db.running ? "running" : "&nbsp;";
    s += "</td><td><input type=submit value=SET></td></tr></table></form>\n";
    s += "<br/><br/><br/>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/mssql class=ml>cancel</a>\n"
        "</body></html>";
    return s;
}

static std::string mssql_rest(const std::string &db)
{
    std::string s = wserver_pages_head();
    s += "<body><h2>" + db + "</h2>\n";
    if (dbs.find(db) == dbs.end()) {
        s += "Error: DB not found</body></html>";
        return s;
    }
    s += "<ul><li><a href=\"/mssql?rv=0&db="+db+"\">base</a></li>\n";
    std::map<std::string,std::string> vers = mssql_get_trn_versions(dbs[db].trnfile);
    for (std::map<std::string,std::string>::const_iterator i = vers.begin(), n = vers.end(); i != n; ++i) { try {
        std::string stst = boost::posix_time::to_simple_string(boost::posix_time::from_time_t(boost::lexical_cast<time_t>(i->first)));
            s += "<li><a href=\"/mssql?rv="+i->first+"&db="+db+"\">"+stst+"</a></li>\n";
        } catch (std::exception &e) { WLOG << "mssql_rest(" << db << ")->Error: " << e.what(); }
    }
    s += "</ul><br/><br/>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/mssql class=ml>cancel</a>\n"
        "</body></html>";
    return s;
}

extern std::string conf_main();
std::string mssql_page(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "load") {
                mssql_load();
                return web_srefresh("mssql");
            }
            if (vuri[1] == "save") {
                mssql_save();
                return web_srefresh("mssql");
            }
            if (vuri[1] == "remove") {
                std::string s = web_url_decode(vuri[2]);
                boost::mutex::scoped_lock scoped_lock(mtx_dbs);
                dbs.erase(s);
                return web_srefresh("mssql");
            }
            if (vuri[1] == "init") {
                std::string s = web_url_decode(vuri[2]);
                if (dbs.find(s) != dbs.end()) {
                    if (!dbs[s].running) {
                        dbs[s].running = true;
                        boost::thread th(boost::bind(&db_t::backup_0, &dbs[s]));
                        th.detach();
                        return web_srefresh("mssql");
                    }
                }
                return s + " --- not found or processing backups";
            }
            if (vuri[1] == "edit") {
                return mssql_edit(web_url_decode(vuri[2]));
            }
            if (vuri[1] == "rest") {
                return mssql_rest(web_url_decode(vuri[2]));
            }
            if (vuri.size() > 4 && vuri[1] == "rv") {
                std::string dbn = web_url_decode(vuri[4]);
                if (dbs.find(dbn) != dbs.end()) dbs[dbn].restore(vuri[2]);
                return web_srefresh("mssql");
            }
            if (vuri.size() > 16 && vuri[1] == "db") {
                try {
                    mssql_add_db(web_url_decode(vuri[2]), web_url_decode(vuri[4]), web_url_decode(vuri[6]),
                        web_url_decode(vuri[8]), props::to_seconds(web_url_decode(vuri[10])),
                        props::to_seconds(web_url_decode(vuri[12])), props::to_seconds(web_url_decode(vuri[14])),
                        boost::lexical_cast<int>(vuri[16]));
                } catch (std::exception &e) {
                    WLOG << "mssql_page->mssql_add_db()->Exception: " << e.what();
                }
                return web_srefresh("mssql");
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

    return mssql_main();
//    return conf_main();
}

int mssql_init()
{
    dict_set("mssql.last_time", &last_time);
	dict_set("mssql.start_time", &start_time);
	dict_set("mssql.stop_time", &stop_time);
    dict_set("mssql.period_secs", &period_secs);

    dict_set("mssql.script", &script);
    dict_set("mssql.osql_path", &osql_path);

    mssql_load();

    return 0;
}

std::string mssql_dump()
{
    std::stringstream ss;
    ss << "<li><b>mssql_dump()</b></li>\n";
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        ss << "<li>" << i->first << " " << (i->second.running ? "running":"") << "</li>\n<li>_ "
            << i->second.last_time << " " << i->second.last_id << " " << i->second.period_secs
            << " " << i->second.min_changes << "</li>\n";
    }
    return ss.str();
}

#endif
