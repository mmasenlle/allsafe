
#ifdef COMP1

#endif // COMP1
#include <time.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
//#include <boost/process.hpp>
#include <boost/filesystem.hpp>
#include "fstream_utf8.h"
//#include <boost/tokenizer.hpp>
#include "stats_dbs.h"
#include "props_sched.h"
#include "dict.h"
#include "log_sched.h"
#include "web_utils.h"


static std::string rman_path = "C:\\app\\oracle_user\\product\\12.1.0\\dbhome_2\\BIN\\rman.exe";

static time_t tt = 0;
static time_t tday = 0;
static inline void get_time() { if (!tt) {tt = time(0); tday = tt % (24*3600);}}

static std::map<std::string,std::string> oracle_get_trn_versions(const std::string &trn)
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
        WLOG << "oracle_get_trn_versions(" << trn << ")->Error: " << e.what();
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
    int min_changes; // <=0 skips get_tr_id()

    std::string conn_args; // TARGET SYS/<password>@<dbname>
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
    int run_rman(const std::string &cmd)
    {
        DLOG << "db_t::run_rman(" << dbname << ")";
        std::string rcmd_fname = "rman_script_" + dbname + ".txt";
        { ofstream_utf8 ofs(rcmd_fname.c_str()); ofs.write(cmd.data(), cmd.size()); }
        std::string rcmd = rman_path + " " + conn_args + " @" + rcmd_fname;
        int r = system(rcmd.c_str());
        TLOG << "db_t::run_rman()->system(" << rcmd << "): " << r;
        return r;
    }
    size_t get_tr_id()
    {
        size_t tr_id = 0;
        DLOG << "db_t::get_tr_id(" << dbname << "): " << tr_id << ", " << last_id;
//TODO: get somehow tr id
        return tr_id;
    }
    void backup_logs()
    {
        DLOG << "db_t::backup_logs(" << dbname << ")";
        long tr_id = 0; int r = -1;
        if (min_changes > 0) tr_id = get_tr_id();
//WLOG << "db_t::backup_logs(" << dbname << ") " << tr_id << ", " << last_id << ", " << min_changes << ", " << (tr_id >= (last_id + min_changes));
        if ((tr_id > (last_id + min_changes)) || (min_changes <= 0) || (tr_id < last_id) || !tr_id) {
            ILOG << "About to backup logs oracle_db(" << dbname << ")";
//TODO: generate and run backup script for this db
            std::string s = "BACKUP INCREMENTAL LEVEL 1 DATABASE FORMAT \"";
                s += trnfile + "_" + boost::lexical_cast<std::string>(time(0)) + "__%U.TRN\";\n";
            r = run_rman(s);
            DLOG << "db_t::backup_logs(" << dbname << "): " << r;

            stats_dbs_put(dbname, 712435); // FIXME: get the size from files
        }
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
        ILOG << "About to backup database oracle_db(" << dbname << ")";
        std::string s = "BACKUP INCREMENTAL LEVEL 0 DATABASE FORMAT \"";
            s += trnfile + "__%U.BCK\";\nDELETE NOPROMPT FORCE OBSOLETE;\n";
        int r = run_rman(s);
//TODO: generate and run backup_0 script for this db
        DLOG << "db_t::backup_0(" << dbname << "): " << r;
//        if (r == 0) { try {
//            boost::filesystem::directory_iterator end_itr, itr(boost::filesystem::path(trnfile).branch_path());
//            for (; itr != end_itr; itr++) { try {
//                std::string p = itr->path().string();
//                if (boost::filesystem::is_regular_file(itr->status()) && p.size() > trnfile.size()
//                        && (p.compare(0, trnfile.size(), trnfile) == 0)
//                        && (p.compare(p.size() - 4, 4, ".TRN") == 0)) {
//                    DLOG << "backup_0()->deleting: " << itr->path();
//                    boost::filesystem::remove(itr->path());
//                }} catch (std::exception &e) {
//                    WLOG << "db_t::backup_0("<<dbname<<")->Error removing '"<<itr->path()<<"' - " << e.what();
//                }
//            }
//        } catch (std::exception &e) {
//            WLOG << "db_t::backup_0("<<dbname<<")->Error removing old trn files - " << e.what();
//        }}
        last_time = time(0);
        running = false;
    }
    void restore(const std::string &vt)
    {
        running = true;
        ILOG << "About to restore database oracle_db(" << dbname << ", " << vt << ")";
        std::string s = "RESTORE DATABASE;\nRECOVER DATABASE;\n";
        int r = run_rman(s);
//TODO: generate and run restore script for this db
        DLOG << "db_t::restore(" << dbname << "): " << r;
        running = false;
    }
};
}

static boost::mutex mtx_dbs;
static std::map<std::string, db_t> dbs;


int oracle_backup_1()
{
    tt = 0;
    boost::mutex::scoped_lock scoped_lock(mtx_dbs);
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        i->second.run();
    }
    return 0;
}

void oracle_add_db(const std::string &n, const std::string &c, const std::string &t, const std::string &r,
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

#define ORACLE_FILENAME "oracle.xml"
void oracle_save()
{
    TLOG << "oracle_save(): " << dbs.size();
    std::string orcl_fname = props::get().confd_path + ORACLE_FILENAME;
    try {
        ofstream_utf8 ofs(orcl_fname.c_str());
        if (ofs) {
            ofs << "<?xml version=\"1.0\"?>\n<oracle_dbs>\n";
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
            ofs << "</oracle_dbs>\n";
        }
    } catch (std::exception &e) {
        ELOG << "oracle_save()->Exception '" << e.what() << "' saving file '" <<orcl_fname<< "'";
    }
    TLOG << "oracle_save() END";
}

int oracle_load()
{
    TLOG << "oracle_load() INIT";
    std::string orcl_fname = props::get().confd_path + ORACLE_FILENAME;
    try {
        ifstream_utf8 ifs(orcl_fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("oracle_dbs") ) {
            try { if( v.first == "db" ) {
                oracle_add_db(v.second.get<std::string>("<xmlattr>.name"),
                       v.second.get<std::string>("conn_args"),
                       v.second.get<std::string>("trnfile"), v.second.get<std::string>("restfile"),
                       v.second.get<int>("period_secs"), v.second.get<int>("start_time"),
                       v.second.get<int>("stop_time"), v.second.get<int>("min_changes"));
            }} catch (std::exception &e) {
                WLOG << "oracle_load()->Exception '" << e.what() << "' parsing file '" <<orcl_fname<< "'";
            }}
        }
    } catch (std::exception &e) {
        ELOG << "oracle_load()->Exception '" << e.what() << "' loading file '" <<orcl_fname<< "'";
    }
    TLOG << "oracle_load(): " << dbs.size();
    return 0;
}

extern const std::string &wserver_confirm();
std::string oracle_main()
{
    std::string s = wserver_pages_head();
    s += "<body><h2>Bases de datos Oracle</h2>\n"
        "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
        "<a href=/oracle?save=1 class=ml>save</a><a href=/oracle?load=1 class=ml>load</a><a href=/oracle?edit=1 class=ml>nueva</a><br/><br/>\n";
    s += "<table>";
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        s += "<tr><th>" + i->first;
        s += "</th><td>&nbsp;&nbsp;<a href=\"oracle?edit=" + i->first + "\">edit</a></td>";
        s += "<td>&nbsp;&nbsp;<a href=# onclick='go_if(\"Delete ?\",\"oracle?remove=" + i->first + "\")'>remove</a></td>";
        s += "<td>&nbsp;&nbsp;<a href=# onclick='go_if(\"Reset backups ?\",\"oracle?init=" + i->first + "\")'>reset</a></td>";
        s += "</th><td>&nbsp;&nbsp;<a href=\"oracle?rest=" + i->first + "\">restore</a></td>"
            "</tr>\n";
    }
    s += wserver_confirm();
    s += "</table><br/>\n"; //"<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/oracle?edit=1 class=ml>nueva</a>\n";
        "</body></html>";
    return s;
}

static std::string oracle_edit(const std::string &n)
{
    db_t db;
    std::string s = wserver_pages_head();
    if (n.size() > 1 && dbs.find(n) != dbs.end()) {
        db = dbs[n];
        s += "<body><h2>" + n + "</h2>\n";
    } else s += "<body><h2>NEW DB</h2>\n";
    s += "<form action=oracle method=get><table class=\"new_db\">"
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
    s += "<br/><br/><br/>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/oracle class=ml>cancel</a>\n"
        "</body></html>";
    return s;
}

static std::string oracle_rest(const std::string &db)
{
    std::string s = wserver_pages_head();
    s += "<body><h2>" + db + "</h2>\n";
    if (dbs.find(db) == dbs.end()) {
        s += "Error: DB not found</body></html>";
        return s;
    }
    s += "<ul><li><a href=\"/oracle?rv=0&db="+db+"\">base</a></li>\n";
    std::map<std::string,std::string> vers = oracle_get_trn_versions(dbs[db].trnfile);
    for (std::map<std::string,std::string>::const_iterator i = vers.begin(), n = vers.end(); i != n; ++i) { try {
        std::string stst = boost::posix_time::to_simple_string(boost::posix_time::from_time_t(boost::lexical_cast<time_t>(i->first)));
            s += "<li><a href=\"/oracle?rv="+i->first+"&db="+db+"\">"+stst+"</a></li>\n";
        } catch (std::exception &e) { WLOG << "oracle_rest(" << db << ")->Error: " << e.what(); }
    }
    s += "</ul><br/><br/>\n"; // "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/oracle class=ml>cancel</a>\n"
        "</body></html>";
    return s;
}

extern std::string conf_main();
std::string oracle_page(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "load") {
                oracle_load();
                return web_srefresh("oracle");
            }
            if (vuri[1] == "save") {
                oracle_save();
                return web_srefresh("oracle");
            }
            if (vuri[1] == "remove") {
                std::string s = web_url_decode(vuri[2]);
                boost::mutex::scoped_lock scoped_lock(mtx_dbs);
                dbs.erase(s);
                return web_srefresh("oracle");
            }
            if (vuri[1] == "init") {
                std::string s = web_url_decode(vuri[2]);
                if (dbs.find(s) != dbs.end()) {
                    if (!dbs[s].running) {
                        dbs[s].running = true;
                        boost::thread th(boost::bind(&db_t::backup_0, &dbs[s]));
                        th.detach();
                        return web_srefresh("oracle");
                    }
                }
                return s + " --- not found or processing backups";
            }
            if (vuri[1] == "edit") {
                return oracle_edit(web_url_decode(vuri[2]));
            }
            if (vuri[1] == "rest") {
                return oracle_rest(web_url_decode(vuri[2]));
            }
            if (vuri.size() > 4 && vuri[1] == "rv") {
                std::string dbn = web_url_decode(vuri[4]);
                if (dbs.find(dbn) != dbs.end()) dbs[dbn].restore(vuri[2]);
                return web_srefresh("oracle");
            }
            if (vuri.size() > 16 && vuri[1] == "db") {
                try {
                    oracle_add_db(web_url_decode(vuri[2]), web_url_decode(vuri[4]), web_url_decode(vuri[6]),
                        web_url_decode(vuri[8]), props::to_seconds(web_url_decode(vuri[10])),
                        props::to_seconds(web_url_decode(vuri[12])), props::to_seconds(web_url_decode(vuri[14])),
                        boost::lexical_cast<int>(vuri[16]));
                } catch (std::exception &e) {
                    WLOG << "oracle_page->oracle_add_db()->Exception: " << e.what();
                }
                return web_srefresh("oracle");
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

    return oracle_main();
//    return conf_main();
}

int oracle_init()
{
    dict_set("oracle.rman_path", &rman_path);

    oracle_load();

    return 0;
}

std::string oracle_dump()
{
    std::stringstream ss;
    ss << "<li><b>oracle_dump()</b></li>\n";
    for (std::map<std::string, db_t>::iterator i = dbs.begin(), n = dbs.end(); i != n; ++i) {
        ss << "<li>" << i->first << " " << (i->second.running ? "running":"") << "</li>\n<li>_ "
            << i->second.last_time << " " << i->second.last_id << " " << i->second.period_secs
            << " " << i->second.min_changes << "</li>\n";
    }
    return ss.str();
}

#endif
