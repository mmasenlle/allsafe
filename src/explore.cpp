#include <map>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include "zlib_sched.h"
#include "web_utils.h"
#include "log_sched.h"
#include "props_sched.h"
#include "rdiff_sched.h"
#include "diff_stats.h"
#include "old_files_data.h"
#include "dict.h"

extern std::string md5(const std::string &str);
extern bool file_watcher_find(const std::string * const fn, diff_stats_t *df);

static const std::string shead = "<body><h2>Explore</h2>\n"
    "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/explore class=ml>root</a>"
    "<a href=/lasts class=ml>recientes</a><a href=/expl_days class=ml>days</a>\n";
static const std::string stail = //"<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/explore class=ml>root</a>"
    "</body></html>";

#define BACK_ARROW " class=ml>&larr;"
//#define BACK_ARROW +web_icon("><i class=\"fa fa-arrow-left\" style=\"font-size:20px;color:black\" title=\"..\"></i>", " class=ml>&larr;")+
#define REST_ICON "restore"
//#define REST_ICON +web_icon("<i class=\"fa fa-download\" style=\"font-size:18px;color:black\" title=\"restore\"></i>", "restore")+

static std::string explore_main(std::string d = "")
{
    std::string s = wserver_pages_head() + shead;
    try {
#ifdef COMP1
        if (props::get().processer == "ofd1") {
#endif
            std::vector<std::string> folders,files;
            ofd_get_childs(d, &folders, &files);
            std::string parentd = boost::filesystem::path(d).branch_path().string();
            if (d.size()) { d.push_back('/');
            s += "<h3> <a href=\"/explore?d=";
            s += parentd + "\"" BACK_ARROW "</a> &nbsp;&nbsp;" + d + " &nbsp;&nbsp; "
                "<a href=\"restdir?d=" + d + "\">" REST_ICON "</a></h3>";
            } else { s += "<br/><br/>"; }
            s += "<table><tr><td valign=top><ul class=filelist>";
            BOOST_FOREACH(const std::string &_s, folders) {
                s += std::string("<li><a href=\"/explore?d=") + d + _s + "\"><strong>&bull; " + _s + "</strong></a></li>";
            }
            s += "</ul></td><td valign=top><ul class=filelist>";
            BOOST_FOREACH(const std::string &_s, files) {
                s += std::string("<li><a href=\"/explore?f=") + d + _s + "\">- " + _s + "</a></li>";
            }
            s += "</ul></td></tr></table>\n";
#ifdef COMP1
        } else {
    if (d.size() > props::get().oldd.size()) {
        std::string parentd = boost::filesystem::path(d).branch_path().string();
        s += "<h3> <a href=\"/explore?d="; s += parentd; s += "\"" BACK_ARROW "</a> &nbsp;&nbsp;";
        std::string d1 = d.substr(props::get().oldd.size());
        s += d1 + " &nbsp;&nbsp; <a href=\"restdir?d=" + d1 + "\">" REST_ICON "</a></h3>";
    } else {
        d = props::get().oldd.substr(0, props::get().oldd.size()-1);
    }
    s += "<ul class=filelist>";
    boost::filesystem::directory_iterator end_itr, itr(d);
    for (; itr != end_itr; itr++) {
        if (boost::filesystem::is_directory(itr->status())) {
            s += std::string("<li><a href=\"/explore?d=") + itr->path().string() + "\">";
            s += itr->path().string().substr(d.size()+1); s+= "</a></li>";
        } else {
            s += std::string("<li><a href=\"/explore?f=") + itr->path().string() + "\">";
            s += itr->path().string().substr(d.size()+1); s+= "</a></li>";
        }
    }
    s += "</ul>\n";
        }
#endif
    } catch(std::exception &e) {
        s += e.what();
    }
    s += stail;
    return s;
}

static std::string explore_dump_stats(const std::string &sname, const std::string &fname)
{
    std::stringstream ss;
    diff_stats_t ds;
    if (diff_stats_load(sname.c_str(), &ds) == 0) {
        ss << "<table class=\"diffstats\">";
        ss << "<tr><th>First:</th><td>";
        ss << boost::posix_time::to_simple_string(boost::posix_time::from_time_t(ds.first/1000));
        ss << "." << std::setw(3) << std::setfill('0') << (ds.first % 1000) << "</td></tr>\n";
        ss << "<tr><th>Last:</th><td>";
        ss << boost::posix_time::to_simple_string(boost::posix_time::from_time_t(ds.last/1000));
		ss << "." << std::setw(3) << std::setfill('0') << (ds.last % 1000) << "</td></tr>\n";
        ss << "<tr><th>Count:</th><td>" << ds.cnt << "</td></tr>";
        ss << "<tr><th>Last size (B):</th><td>" << ds.lsize << "</td></tr>";
        ss << "<tr><th>Mean size (B):</th><td>" << ds.msize << "</td></tr>";
        ss << "<tr><th>Last time (s):</th><td>" << ds.ldur << "</td></tr>";
        ss << "<tr><th>Mean time (s):</th><td>" << ds.mdur << "</td></tr>";
        ss << "</table>\n";
    }
    if (file_watcher_find(&fname, &ds)) {
        ss << "<h4>Pending changes:</h4><table class=\"diffstats\">";
        ss << "<tr><th>First:</th><td>";
        ss << boost::posix_time::to_simple_string(boost::posix_time::from_time_t(ds.first/1000));
		ss << "." << std::setw(3) << std::setfill('0') << (ds.first % 1000) << "</td></tr>\n";
        ss << "<tr><th>Last:</th><td>";
        ss << boost::posix_time::to_simple_string(boost::posix_time::from_time_t(ds.last/1000));
		ss << "." << std::setw(3) << std::setfill('0') << (ds.last % 1000) << "</td></tr>\n";
        ss << "<tr><th>Count:</th><td>" << ds.cnt << "</td></tr>";
        ss << "</table>\n";
    }
    return ss.str();
}

static std::string explore_file(const std::string &fold)
{
    std::string s = wserver_pages_head() + shead;
    std::string f = fold;
    try {
#ifdef COMP1
        if (props::get().processer == "ofd1") {
#endif
            std::string parentd = boost::filesystem::path(f).branch_path().string();
            std::string sfid = boost::lexical_cast<std::string>(ofd_find(f));
            s += "<h3> <a href=\"/explore?d=";
            s += parentd + "\"" BACK_ARROW "</a> &nbsp;&nbsp;" + f + " ("+sfid+") "
                "&nbsp;&nbsp;<a href=\"/explore?s="+sfid+"&f=" + f + "\">" REST_ICON "</a></h3>";
            s += explore_dump_stats(sfid, f);
            //s += "&nbsp;&nbsp;<a href=\"/explore?s="+sfid+"&f=" + f + "\">" REST_ICON "</a><br/><br/>\n";
#ifdef COMP1
        } else {
        std::string parentd = boost::filesystem::path(f).branch_path().string();
        s += "<h3> <a href=\"/explore?d="; s += parentd; s += "\"" BACK_ARROW "</a> &nbsp;&nbsp;";
        f = f.substr(props::get().oldd.size()); if (f[1] == '_') f[1] = ':'; else f = "/" + f;
        s += f; s += "</h3>"; boost::algorithm::replace_all(f, "\\", "/");
        std::string pf = boost::filesystem::path(f).parent_path().string();
        std::string sname = props::get().smd5_name(md5(pf), md5(f));
        s += explore_dump_stats(sname, f);
        if (props::get().use_backend()) {
            s += "&nbsp;&nbsp;<a href=\"/explore?s="; s += sname; s += "&f=" + pf + "\">" REST_ICON "</a><br/><br/>\n";
        } else {
            s += "&nbsp; Restore <a href=\"/explore?r="; s += fold; s += "\" target=\"_blank\">local</a>";
            s += "&nbsp;&nbsp;<a href=\"/explore?s="; s += sname; s += "&f=" + f + "\">remoto</a><br/><br/>\n";
        }
        }
#endif
    } catch(std::exception &e) {
        s += e.what();
    }
    s += "<br/>"; s += stail;
    return s;
}

#ifdef COMP1
static std::string explore_rest(const std::string &f)
{
    std::string fname = boost::filesystem::path(f).filename().string();
    std::string cmd = props::get().instd + "mucopy \"" + f + "\" \"" +
        props::get().patchd + fname + "\" --oldcompress-cmd " + dict_get("props.varLibreriaCompresion");
    int r = system(cmd.c_str());
    DLOG << "explore_rest(" << f << ")->system(" << cmd << "): " << r;
    if (r) return (std::string("Error ") + boost::lexical_cast<std::string>(r) + " restoring file " + fname);
    else return web_srefresh(std::string("restore/") + fname);
}
#endif

static std::string explore_versions(const std::string &f, const std::string &fn)
{
    std::string s = wserver_pages_head() + shead;
    s += "<h3>Versions of " + fn + "</h3><ul>\n";
    boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
    if (procsr) {
        std::map<std::string, std::string> tstamps = procsr->get_versions(f, fn);
        DLOG << "explore_versions("<<f<<")->tstamps.size(): " << tstamps.size();
        for (std::map<std::string, std::string>::const_iterator i = tstamps.begin(), n = tstamps.end(); i != n; ++i) try {
            time_t tst = boost::lexical_cast<time_t>(i->first.substr(0, 10));
            std::string stst = boost::posix_time::to_simple_string(boost::posix_time::from_time_t(tst));
            s += "<li>" + stst + " <a href=\"/explore?t=" + i->first +
                "&s=" + f + "&f=" + fn + "\" target=\"_blank\">restore</a></li>\n";
        } catch (std::exception &e) { WLOG << "explore_versions("<<f<<"): " << e.what(); }
    }
    s += "</ul><br/>\n";
    s += stail;
    return s;
}

static std::string explore_trest(const std::string &ts, const std::string &f, const std::string &fn)
{
    std::string s = "Error not found";
    boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
    if (procsr) try {
        std::string fname = boost::filesystem::path(fn).filename().string();
        procsr->restore(ts, f, fname, fn);
        return web_srefresh(std::string("restore/") + fname);
    } catch (std::exception &e) {
        WLOG << "explore_trest("<<f<<"): " << e.what();
        s = e.what();
    }
    return s;
}
static std::string explore_get_magics()
{
    std::string s = wserver_pages_head() + shead;
    s += "<h3>Magics numbers in backend</h3>\n";
    s += "<h4>Current: " + ofd_get_magic() + "</h4><ul>\n";
    boost::scoped_ptr<Backend> bend(props::createBackend());
    std::string resp;
    int r = -1;
    if (bend) {
        r = bend->list("??????", &resp);
        if (r < 0) {
            WLOG << "explore_get_magics()->bend->list(*): " << r;
        }
        if (r == 0) {
            resp.push_back('\n');
            std::istringstream iss(resp);
            std::string line;
            while (std::getline(iss, line))
                if (line.size() == 6)
                    s += "<li>" + line + " - <a href=# onclick='go_if(\"Rebuild file entry info?\",\"/explore?mn_rebuild="
                        + line + "\")'>rebuild</a></li>\n";
        }
    }
    s += "</ul>" + wserver_confirm() + stail;
    return s;
}
extern bool pausa;
extern void ofd_processer_rebuild(const std::string &mn);
static std::string explore_mn_rebuild(const std::string &mn)
{
    std::string s = wserver_pages_head() + shead;
    s += "<h3>Rebuilding file entry info from backend with magic number " + mn + "</h3>\n";
    if (!pausa) {
        s += "The system must be in pause<br/>Pause it and try again if necessary";
#ifdef COMP1
    } else if (props::get().processer != "ofd1") {
        s += "The processer does not support rebuilding entry info";
#endif
    } else {
        boost::thread th(boost::bind(ofd_processer_rebuild, mn));
        th.detach();
        s += "<br/>Rebuilding in process ...";
    }
    s += stail;
    return s;
}

std::string explore_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "d") {
                return explore_main(web_url_decode(vuri[2]));
            }
            if (vuri[1] == "f") {
                return explore_file(web_url_decode(vuri[2]));
            }
#ifdef COMP1
            if (vuri[1] == "r") {
                return explore_rest(web_url_decode(vuri[2]));
            }
#endif
            if (vuri.size() > 4 && vuri[1] == "s") {
                return explore_versions(web_url_decode(vuri[2]), web_url_decode(vuri[4]));
            }
            if (vuri.size() > 6 && vuri[1] == "t") {
                return explore_trest(web_url_decode(vuri[2]),
                            web_url_decode(vuri[4]), web_url_decode(vuri[6]));
            }
            if (props::get().use_backend()) {
                if (vuri[1] == "mn_show") {
                    return explore_get_magics();
                }
                if (vuri[1] == "mn_rebuild") {
                    return explore_mn_rebuild(web_url_decode(vuri[2]));
                }
            }
        }
#ifndef NDEBUG
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
#endif
    }

    return explore_main();
}
