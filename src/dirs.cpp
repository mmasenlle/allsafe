
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include "fstream_utf8.h"
#include "web_utils.h"
#include "log_sched.h"
#include "props_sched.h"

extern int mod_files_init_dir(int i);
extern int file_watcher_init_dir(int i);
extern void file_watcher_del_dir(int i);

#define DIRS_FILENAME "dirs.xml"
void dirs_save()
{
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    TLOG << "dirs_save(): " << dtw.size();
    std::string dirs_fname = props::get().confd_path + DIRS_FILENAME;
    try {
        ofstream_utf8 ofs(dirs_fname.c_str());
        if (ofs) {
            ofs << "<?xml version=\"1.0\"?>\n<dirs>\n";
            for (int i = 0; i < dtw.size(); i++) {
                if (dtw[i].size())
                    ofs << "\t<dir>" << dtw[i] << "</dir>\n";
            }
            ofs << "</dirs>\n";
        }
    } catch (std::exception &e) {
        ELOG << "dirs_save()->Exception '" << e.what() << "' saving file '" <<dirs_fname<< "'";
    }
    TLOG << "dirs_save() END";
}

int dirs_load()
{
    TLOG << "dirs_load() INIT";
    std::string dirs_fname = props::get().confd_path + DIRS_FILENAME;
    try {
        ifstream_utf8 ifs(dirs_fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("dirs") ) {
            try { if( v.first == "dir" ) {
                props::add_dir(v.second.data());
            }} catch (std::exception &e) {
                WLOG << "dirs_load()->Exception '" << e.what() << "' parsing file '" <<dirs_fname<< "'";
            }}
        }
    } catch (std::exception &e) {
        ELOG << "dirs_load()->Exception '" << e.what() << "' loading file '" <<dirs_fname<< "'";
    }
    TLOG << "dirs_load(): " << props::get().dirstowatch.size();
    return 0;
}

#define SPACE_NB "<td>&nbsp;&nbsp;</td>"

static const std::string dirs_reload_icon = "<i class=\"fa fa-refresh\" style=\"font-size:16px;color:blue\" title=\"reload\"></i>";
static const std::string dirs_remove_icon = "<i class=\"fa fa-close\" style=\"font-size:18px;color:red\" title=\"remove\"></i>";

std::string dirs_main()
{
    const std::vector<std::string> &dtw = props::get().dirstowatch;
//    std::string s = wserver_pages_head() + "<body><h2>Directorios</h2>\n";
//    s += "<a href=/>home</a>&nbsp;&nbsp;<a href=/menu>menu</a>&nbsp;&nbsp;"
    std::string s = "<hr/><h2>Directorios</h2>\n"
        "<a href=/dirs?save=1 class=ml>save</a><a href=/dirs?load=1 class=ml>load</a><br/><br/>\n"
        "<form action=dirs method=get><input type=text name=dir size=50><input type=submit value=crear></form>"
        "<table>";
    for (int i = 0; i < dtw.size(); i++) {
        if (dtw[i].size()) {
        s += "<tr><th>" + dtw[i] + "</th>" SPACE_NB
            "<td><a href=\"dirs?reload=" + boost::lexical_cast<std::string>(i) + "\">" + web_icon(dirs_reload_icon, "reload") + "</a></td>" SPACE_NB
            "<td><a href=\"dirs?remove=" + boost::lexical_cast<std::string>(i) + "\">" + web_icon(dirs_remove_icon, "remove") + "</a></td></tr>\n";
        }
    }
    s += "</table><br/><br/>\n";
    return s;
}

int dirs_add_dir(std::string d)
{
    std::string np = props::normalize_path(d);
    if (std::find(props::get().dirstowatch.begin(), props::get().dirstowatch.end(), np) == props::get().dirstowatch.end()) {
        int i = props::add_dir(np);
        DLOG << "dirs_add_dir("<<d<<")->["<<i<<"] '"<<np<<"'";
        mod_files_init_dir(i);
        file_watcher_init_dir(i);
        return 1;
    }
    return 0;
}
static void dirs_del_dir(int i)
{
    if (i < props::get().dirstowatch.size()) {
        file_watcher_del_dir(i);
        props::del_dir(i);
    }
}
int dirs_del_dir(std::string d)
{
    std::string np = props::normalize_path(d);
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    for (int i = 0; i < dtw.size(); i++)
        if (np == dtw[i]) {
            DLOG << "dirs_del_dir("<<d<<")->["<<i<<"] '"<<np<<"'";
            dirs_del_dir(i);
            return 1;
        }
    return 0;
}
void dirs_get_all(std::vector<std::string> &dirs)
{
    dirs.clear();
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    for (int i = 0; i < dtw.size(); i++) if (!dtw[i].empty()) dirs.push_back(dtw[i]);
}

extern std::string conf_main();
std::string dirs_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
             if (vuri[1] == "load") {
                dirs_load();
                return web_srefresh("dirs");
            }
            if (vuri[1] == "save") {
                dirs_save();
                return web_srefresh("dirs");
            }
            if (vuri[1] == "reload") {
                mod_files_init_dir(boost::lexical_cast<int>(vuri[2]));
                return web_srefresh();
            }
            if (vuri[1] == "dir") {
                dirs_add_dir(web_url_decode(vuri[2]));
                return web_srefresh("dirs");
            }
            if (vuri[1] == "remove") {
                dirs_del_dir(boost::lexical_cast<int>(vuri[2]));
                return web_srefresh("dirs");
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

//    return dirs_main();
    return conf_main();
}
