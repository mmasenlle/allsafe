
#include <boost/filesystem.hpp>
#include "fstream_utf8.h"
#include <set>
#include "props_sched.h"
#include "web_utils.h"
#include "log_sched.h"


static const std::string shead = "<body><h2>Explore Days</h2>\n"
    "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/expl_days class=ml>days</a>";
static const std::string stail = //"<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/expl_days class=ml>days</a>"
    "</body></html>";

static std::string explore_day_ok(std::string d = "")
{
    std::string s = wserver_pages_head() + shead + "<h3>OK " + d + "</h3>\n";
    try {
        ifstream_utf8 ifs((props::get().confd_path + "logs/files_ok_" + d + ".log").c_str());
        if (ifs) {
            std::string line;
            s += "<table class=\"in_process\">";
            while (std::getline(ifs, line)) {
                if (line.size() > 40) {
                    s += "<tr><td>";
                    s += line.substr(1, 26);
                    s += "</td><th>";
                    int i = 28; for (; i < line.size(); i++) if (line[i]=='(') break; i++;
                    int j = i; for (; j < line.size(); j++) if (line[j]==')') break;
                    s += line.substr(i, j-i);
                    s += "</th>";
                    for (i = j+1; i < line.size(); i++) if (line[i]=='\'') break; i++;
                    if (line.size() > i) {
                        j = 0; if (line[i]=='/') j=1;
                        s += "<td><a href=\"/explore?f=";
                        s += line.substr(i+j, line.size()-i-j-1); s += "\">";
                        s += line.substr(i, line.size()-i-1); s += "</a>";
                        s += "</td>";
                    }
                    s += "</tr>\n";
                }
            }
            s += "</table><br/>\n";
        }
    } catch(std::exception &e) {
        WLOG << "explore_day_err("<<d<<")->Error: " << e.what();
        s += "<br/>"; s += e.what(); s += "<br/>\n";
    }
    s += stail;
    return s;
}

static std::string explore_day_err(std::string d = "")
{
    std::string s = wserver_pages_head() + shead + "<h3>ERROR " + d + "</h3>\n";
    try {
        ifstream_utf8 ifs((props::get().confd_path + "logs/files_err_" + d + ".log").c_str());
        if (ifs) {
            std::string line;
            s += "<table class=\"in_process\">";
            while (std::getline(ifs, line)) {
                if (line.size() > 40) {
                    s += "<tr><td>";
                    s += line.substr(1, 26);
                    s += "</td><th>";
                    int i = 28; for (; i < line.size(); i++) if (line[i]=='(') break; i++;
                    int j = i; for (; j < line.size(); j++) if (line[j]==')') break;
                    s += line.substr(i, j-i);
                    s += "</th><td>";
                    for (i = j+1; i < line.size(); i++) if (line[i]=='\'') break; i++;
                    if (line.size() > i) s += line.substr(i, line.size()-i-1);
                    s += "</td></tr>\n";
                }
            }
            s += "</table><br/>\n";
        }
    } catch(std::exception &e) {
        WLOG << "explore_day_err("<<d<<")->Error: " << e.what();
        s += "<br/>"; s += e.what(); s += "<br/>\n";
    }
    s += stail;
    return s;
}

static std::string explore_day_main()
{
    std::string s = wserver_pages_head() + shead;
    try {
    std::string fpok = "files_ok_";
    std::string fperr = "files_err_";
    std::set<std::string> vfok,vferr;
    boost::filesystem::directory_iterator end_itr, itr(props::get().confd_path + "logs");
    for (; itr != end_itr; itr++) {
        if (boost::filesystem::is_regular_file(itr->status())) {
            std::string fname = itr->path().filename().string();
            if (fname.size() > fpok.size() + 10 && fname.compare(0, fpok.size(), fpok) == 0) {
                vfok.insert(fname.substr(fpok.size(), 10));
            } else if (fname.size() > fperr.size() + 10 && fname.compare(0, fperr.size(), fperr) == 0) {
                vferr.insert(fname.substr(fperr.size(), 10));
            }
        }
    }
    s += "<table class=\"in_process\"><tr><th>OK</th><td>&nbsp;</td><th>ERROR</th></tr>\n";
    std::set<std::string>::reverse_iterator iok = vfok.rbegin();
    std::set<std::string>::reverse_iterator ierr = vferr.rbegin();
    while (iok != vfok.rend() || ierr != vferr.rend()) {
        s += "<tr><td>";
        if (iok != vfok.rend()) {
            s += "<a href=\"/expl_days?d=" + *iok + "\">" + *iok + "</a>";
            ++iok;
        }
        s += "</td><td></td><td>";
        if (ierr != vferr.rend()) {
            s += "<a href=\"/expl_days?e=" + *ierr + "\">" + *ierr + "</a>";
            ++ierr;
        }
        s += "</td></tr>\n";
    }
    s += "</table><br/>\n";
    } catch(std::exception &e) {
        WLOG << "explore_day_main()->Error: " << e.what();
        s += "<br/>"; s += e.what(); s += "<br/>\n";
    }
    s += stail;
    return s;
}

std::string explore_day_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "d") {
                return explore_day_ok(web_url_decode(vuri[2]));
            }
            if (vuri[1] == "e") {
                return explore_day_err(web_url_decode(vuri[2]));
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

    return explore_day_main();
}
