
#include <map>
#include <set>
#include "fstream_utf8.h"
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include <boost/filesystem/path.hpp>
#include "web_utils.h"
#include "log_sched.h"
#include "props_sched.h"
#include "cc_rules.h"
#include "old_files_data.h"
#include "cc_rules.h"

/// continuous copy rule
struct cc_rule_t {
    int action;
    int param;
    cc_rule_t() : action(-1) {};
};

static boost::shared_mutex mtx_rules;
static std::map<std::string, cc_rule_t> cc_rules;
static std::set<std::string> cc_rules_temp1; //blabla*
static std::set<std::string> cc_rules_temp2; //*blabla
static std::set<std::string> cc_rules_excl;
static std::set<std::string> cc_rules_excl1; //blabla*
static std::set<std::string> cc_rules_excl2; //*blabla
static std::set<std::string> cc_rules_excl3; //*blabla*
static std::set<std::string> cc_rules_recon;
static std::set<std::string> cc_rules_recon1; //blabla*
static std::set<std::string> cc_rules_recon2; //*blabla
static std::set<std::string> cc_rules_nodefl;
static std::set<std::string> cc_rules_nodefl1; //blabla*
static std::set<std::string> cc_rules_nodefl2; //*blabla

static const char *ccrules_names[] = {
    "wait_since_last_copy",
    "wait_since_oldest_mod",
    "wait_since_newest_mod",
    "wait_until_time_of_day",
    "only_before_time_of_day",
    "wait_num_modifications",
    "exclude",
    "reconst",
    "no_deflate",
};
/*  // Rule types
enum {
    wait_since_last_copy,
    wait_since_oldest_mod,
    wait_since_newest_mod,
    wait_until_time_of_day,
    only_before_time_of_day,
    wait_num_modifications,
    exclude,
    reconst,
}; */
#define CCRULES_N (sizeof(ccrules_names)/sizeof(*ccrules_names))
const char *ccrules_get_rule_name(unsigned int rule_id)
{
    if (rule_id < CCRULES_N) return ccrules_names[rule_id];
    return "unknown";
}
int ccrules_get_rule_id(const std::string &rule_name)
{
    for (int i = 0; i < CCRULES_N; i++) {
        if (strcmp(ccrules_names[i], rule_name.c_str())==0) return i;
    }
    return 0;
}

bool ccrules_no_deflate(const std::string &path)
{
    TLOG << "ccrules_no_deflate(" << path << ")";
    boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
    if (cc_rules_nodefl.find(path) != cc_rules_nodefl.end()) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_nodefl2)
        if (path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_nodefl1)
        if (path.size() >= s.size() && path.compare(0, s.size(), s) == 0) return true;

    return false;
}
bool ccrules_reconst(const std::string &path)
{
    TLOG << "ccrules_reconst(" << path << ")";
    boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
    if (cc_rules_recon.find(path) != cc_rules_recon.end()) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_recon2)
        if (path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_recon1)
        if (path.size() >= s.size() && path.compare(0, s.size(), s) == 0) return true;

    return false;
}
bool ccrules_exclude(const std::string &path)
{
    TLOG << "ccrules_exclude(" << path << ")";
    boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
    if (cc_rules_excl.find(path) != cc_rules_excl.end()) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_excl2)
        if (path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_excl1)
        if (path.size() >= s.size() && path.compare(0, s.size(), s) == 0) return true;
    BOOST_FOREACH(const std::string &s, cc_rules_excl3)
        if (path.size() >= s.size() && path.find(s) != std::string::npos) return true;

    return false;
}

static boost::mutex mtx_last_copies;
static boost::unordered_map<std::string, long long> last_copies;

bool ccrules_hold(boost::shared_ptr<fentry_t> &fentry)
{
    TLOG << "ccrules_hold(" << fentry->fname << ")";
    cc_rule_t cc_rule;
    {   boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
        if (cc_rules.find(fentry->fname) != cc_rules.end()) {
            cc_rule = cc_rules[fentry->fname];
        } else {
            BOOST_FOREACH(const std::string &s, cc_rules_temp1)
                if (fentry->fname.size() >= s.size() && fentry->fname.compare(0, s.size(), s) == 0) {
                    cc_rule = cc_rules[s];
                    break;
                }
            if (cc_rule.action < 0) {
                BOOST_FOREACH(const std::string &s, cc_rules_temp2)
                    if (fentry->fname.size() >= s.size() && fentry->fname.compare(fentry->fname.size() - s.size(), s.size(), s) == 0) {
                        cc_rule = cc_rules[s];
                        break;
                    }
            }
        }
    }
    if (cc_rule.action >= 0) {
        DLOG << "ccrules_hold(" << fentry->fname << ", " << ccrules_get_rule_name(cc_rule.action) << ", " << cc_rule.param << ") FOUND";
        switch (cc_rule.action) {
        case wait_since_last_copy: {
            boost::mutex::scoped_lock scoped_lock(mtx_last_copies);
            boost::unordered_map<std::string, long long>::iterator i = last_copies.find(fentry->fname);
            if (i != last_copies.end()) {
                if (i->second + cc_rule.param > props::current_time_ms()) return true;
                else i->second = props::current_time_ms();
                return false;
            }
            if (last_copies.size() > 1000) last_copies.clear();
            last_copies[fentry->fname] = props::current_time_ms();
            return false;
        }
        case wait_since_oldest_mod:
            return props::current_time_ms() < (fentry->initime + cc_rule.param);
        case wait_since_newest_mod:
            return props::current_time_ms() < (fentry->initime_last + cc_rule.param);
        case wait_until_time_of_day: // los segundos del día han superado los especificados
            return ((props::current_time_ms() / 1000) % (24*3600)) < cc_rule.param;
        case only_before_time_of_day:
            return ((props::current_time_ms() / 1000) % (24*3600)) > cc_rule.param;
        case wait_num_modifications:
            return fentry->count < cc_rule.param;
        }
    }
    return false;
}

void ccrules_add_rule(const std::string &fname, int action, int param)
{
    std::string fn = fname;
    boost::unique_lock< boost::shared_mutex > lock(mtx_rules);
    if (fn[fn.size()-1] == '*' && fn[0] == '*' && action == exclude) {
        fn = fn.substr(1, fn.size()-2);
        cc_rules_excl3.insert(fn);
    } else if (fn[fn.size()-1] == '*') {
        fn = fn.substr(0, fn.size()-1);
        if (action == exclude) cc_rules_excl1.insert(fn);
        else if (action == reconst) cc_rules_recon1.insert(fn);
        else if (action == no_deflate) cc_rules_nodefl1.insert(fn);
        else cc_rules_temp1.insert(fn);
    } else if (fn[0] == '*') {
        fn = fn.substr(1);
        if (action == exclude) cc_rules_excl2.insert(fn);
        else if (action == reconst) cc_rules_recon2.insert(fn);
        else if (action == no_deflate) cc_rules_nodefl2.insert(fn);
        else cc_rules_temp2.insert(fn);
    } else {
        if (action == exclude) cc_rules_excl.insert(fn);
        else if (action == reconst) cc_rules_recon.insert(fn);
        else if (action == no_deflate) cc_rules_nodefl.insert(fn);
    }
    if (action != exclude && action != reconst && action != no_deflate) {
        cc_rules[fn].action = action;
        cc_rules[fn].param = param;
    }
}

void ccrules_remove_rule(const std::string &fname)
{
    std::string fn = fname;
    boost::unique_lock< boost::shared_mutex > lock(mtx_rules);
    if (fn[fn.size()-1] == '*' && fn[0] == '*') {
        fn = fn.substr(1, fn.size()-1);
        cc_rules_excl3.erase(fn);
    } else if (fn[fn.size()-1] == '*') {
        fn = fn.substr(0, fn.size()-1);
        cc_rules_temp1.erase(fn);
        cc_rules_excl1.erase(fn);
        cc_rules_recon1.erase(fn);
        cc_rules_nodefl1.erase(fn);
        cc_rules.erase(fn);
    } else if (fn[0] == '*') {
        fn = fn.substr(1);
        cc_rules_temp2.erase(fn);
        cc_rules_excl2.erase(fn);
        cc_rules_recon2.erase(fn);
        cc_rules_nodefl2.erase(fn);
        cc_rules.erase(fn);
    } else {
        cc_rules.erase(fn);
        cc_rules_excl.erase(fn);
        cc_rules_recon.erase(fn);
        cc_rules_nodefl.erase(fn);
    }
}
void ccrules_rm_rule(const std::string &fname, int action)
{
    //TODO: switch(action) ...
    ccrules_remove_rule(fname);
}

#define CCRULES_FILENAME "ccrules.xml"
void ccrules_save(std::string fname)
{
    TLOG << "ccrules_save("<<fname<<"): " << cc_rules.size();
    try {
        ofstream_utf8 ofs(fname.c_str());
        if (ofs) {
            boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
            ofs << "<?xml version=\"1.0\"?>\n<ccrules>\n";
            for (std::map<std::string, cc_rule_t>::iterator i = cc_rules.begin(), n = cc_rules.end(); i != n; ++i) {
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(i->second.action)
                    << "\" param=\"" << i->second.param << "\">\n";
                ofs << "\t\t<file>";
                if (cc_rules_temp2.find(i->first) != cc_rules_temp2.end()) ofs << "*";
                ofs << i->first;
                if (cc_rules_temp1.find(i->first) != cc_rules_temp1.end()) ofs << "*";
                ofs << "</file>\n";
                ofs << "\t</rule>\n";
            }
            BOOST_FOREACH(const std::string &s, cc_rules_excl)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(exclude)
                    << "\" param=\"0\">\n\t\t<file>" << s << "</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_excl1)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(exclude)
                    << "\" param=\"0\">\n\t\t<file>" << s << "*</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_excl2)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(exclude)
                    << "\" param=\"0\">\n\t\t<file>*" << s << "</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_excl3)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(exclude)
                    << "\" param=\"0\">\n\t\t<file>*" << s << "*</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_recon)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(reconst)
                    << "\" param=\"0\">\n\t\t<file>" << s << "</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_recon1)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(reconst)
                    << "\" param=\"0\">\n\t\t<file>" << s << "*</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_recon2)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(reconst)
                    << "\" param=\"0\">\n\t\t<file>*" << s << "</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_nodefl)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(no_deflate)
                    << "\" param=\"0\">\n\t\t<file>" << s << "</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_nodefl1)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(no_deflate)
                    << "\" param=\"0\">\n\t\t<file>" << s << "*</file>\n\t</rule>\n";
            BOOST_FOREACH(const std::string &s, cc_rules_nodefl2)
                ofs << "\t<rule action=\"" << ccrules_get_rule_name(no_deflate)
                    << "\" param=\"0\">\n\t\t<file>*" << s << "</file>\n\t</rule>\n";
            ofs << "</ccrules>\n";
        }
    } catch (std::exception &e) {
        ELOG << "ccrules_save("<<fname<<")->Exception: " << e.what();
    }
    TLOG << "ccrules_save("<<fname<<") END";
}
static void push_rule(std::vector<ccrules_exp_t> &rules, const std::string &s, int t, int a)
{ ccrules_exp_t rule; rule.path=s; rule.type=t; rule.arg=a; rules.push_back(rule); }
void ccrules_get_all(std::vector<ccrules_exp_t> &rules)
{
    rules.clear();
    boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
    for (std::map<std::string, cc_rule_t>::iterator i = cc_rules.begin(), n = cc_rules.end(); i != n; ++i) {
        ccrules_exp_t rule;
        rule.type = i->second.action;
        rule.arg = i->second.param;
        if (cc_rules_temp2.find(i->first) != cc_rules_temp2.end()) rule.path += "*";
        rule.path += i->first;
        if (cc_rules_temp1.find(i->first) != cc_rules_temp1.end()) rule.path += "*";
        rules.push_back(rule);
    }
    BOOST_FOREACH(const std::string &s, cc_rules_excl) push_rule(rules, s, exclude, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_excl1) push_rule(rules, s+"*", exclude, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_excl2) push_rule(rules, "*"+s, exclude, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_excl3) push_rule(rules, "*"+s+"*", exclude, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_recon) push_rule(rules, s, reconst, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_recon1) push_rule(rules, s+"*", reconst, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_recon2) push_rule(rules, "*"+s, reconst, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_nodefl) push_rule(rules, s, no_deflate, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_nodefl1) push_rule(rules, s+"*", no_deflate, 0);
    BOOST_FOREACH(const std::string &s, cc_rules_nodefl2) push_rule(rules, "*"+s, no_deflate, 0);
}

int ccrules_load(std::string fname)
{
    TLOG << "ccrules_load("<<fname<<") INIT";
    try {
        ifstream_utf8 ifs(fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("ccrules") ) {
            try { if( v.first == "rule" ) {
                ccrules_add_rule(v.second.get<std::string>("file"),
                        ccrules_get_rule_id(v.second.get<std::string>("<xmlattr>.action")),
                        v.second.get<int>("<xmlattr>.param"));
            }} catch (std::exception &e) {
                WLOG << "ccrules_load("<<fname<<")->Exception: " << e.what();
            }}
        } //else {
            if (cc_rules_excl1.empty()) { // default exclusions
#ifdef COMP1
                if (props::get().instd.size() > 8) {
                    boost::filesystem::path inst_path = props::get().instd;
                    std::string inst_tmp = (inst_path.parent_path().parent_path() / "*").generic_string();
                    if (inst_tmp.size() > 5) ccrules_add_rule(inst_tmp, exclude, 0);
                }
#endif
                if (props::get().tmpd.size() > 10) {
                    boost::filesystem::path work_path = props::get().tmpd;
                    std::string work_tmp = (work_path.parent_path().parent_path() / "*").generic_string();
                    if (work_tmp.size() > 5) ccrules_add_rule(work_tmp, exclude, 0);
                }
            }
            // default rules
            if (cc_rules_excl.empty() && cc_rules_excl3.empty()) ccrules_add_rule("*/~$*", exclude, 0);
            if (cc_rules_nodefl2.empty()) { // default no deflate
                ccrules_add_rule("*.7z", no_deflate, 0);
                ccrules_add_rule("*.avi", no_deflate, 0);
                ccrules_add_rule("*.jpeg", no_deflate, 0);
                ccrules_add_rule("*.jpg", no_deflate, 0);
                ccrules_add_rule("*.mkv", no_deflate, 0);
                ccrules_add_rule("*.mp3", no_deflate, 0);
                ccrules_add_rule("*.mp4", no_deflate, 0);
                ccrules_add_rule("*.mpeg", no_deflate, 0);
                ccrules_add_rule("*.mpg", no_deflate, 0);
                ccrules_add_rule("*.png", no_deflate, 0);
                ccrules_add_rule("*.rar", no_deflate, 0);
                ccrules_add_rule("*.webm", no_deflate, 0);
                ccrules_add_rule("*.wmv", no_deflate, 0);
                ccrules_add_rule("*.zip", no_deflate, 0);
            }
        //}
    } catch (std::exception &e) {
        ELOG << "ccrules_load("<<fname<<")->Exception: " << e.what();
    }
    TLOG << "ccrules_load("<<fname<<"): " << cc_rules.size();
    return 0;
}

int ccrules_load()
{
    return ccrules_load(props::get().confd_path + CCRULES_FILENAME);
}
void ccrules_save()
{
    ccrules_save(props::get().confd_path + CCRULES_FILENAME);
}

#define SPACE_NB "<td>&nbsp;&nbsp;</td>"
static const std::string rules_remove_icon = "<i class=\"fa fa-close\" style=\"font-size:18px;color:red\" title=\"remove\"></i>";
#define REMOVE_ICON +web_icon(rules_remove_icon, "remove")+

std::string rules_main()
{
    std::string s = "<hr/><h2>Reglas</h2>\n"
        "<a href=/rules?save=1 class=ml>save</a><a href=/rules?load=1 class=ml>load</a><br/><br/>\n";
    s += "<form action=rules method=get><input type=text name=fname size=30><select name=rule>";
    for (int i = 0; i < CCRULES_N; i++) {
        s += "<option value=" + boost::lexical_cast<std::string>(i) + ">";
        s += ccrules_get_rule_name(i); s += "</option>\n";
    }
    s += "</select><input type=text name=param size=10><input type=submit value=crear></form>\n";
    s += "<table>";
    boost::shared_lock< boost::shared_mutex > lock(mtx_rules);
    for (std::map<std::string, cc_rule_t>::iterator i = cc_rules.begin(), n = cc_rules.end(); i != n; ++i) {
        s += "<tr><th>";
        if (cc_rules_temp2.find(i->first) != cc_rules_temp2.end()) s += "*";
        s += i->first;
        if (cc_rules_temp1.find(i->first) != cc_rules_temp1.end()) s += "*";
        s += "</th><td>" + std::string(ccrules_get_rule_name(i->second.action));
        s += "</td><td>" + boost::lexical_cast<std::string>(i->second.param) + "</td>" SPACE_NB;
        s += "<td><a href=\"rules?remove=" + i->first + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_excl) {
        s += "<tr><th>" + r + "</th><td>" + std::string(ccrules_get_rule_name(exclude));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_excl1) {
        s += "<tr><th>" + r + "*</th><td>" + std::string(ccrules_get_rule_name(exclude));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "*\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_excl2) {
        s += "<tr><th>*" + r + "</th><td>" + std::string(ccrules_get_rule_name(exclude));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=*" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_excl3) {
        s += "<tr><th>*" + r + "*</th><td>" + std::string(ccrules_get_rule_name(exclude));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=*" + r + "*\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_recon) {
        s += "<tr><th>" + r + "</th><td>" + std::string(ccrules_get_rule_name(reconst));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_recon1) {
        s += "<tr><th>" + r + "*</th><td>" + std::string(ccrules_get_rule_name(reconst));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "*\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_recon2) {
        s += "<tr><th>*" + r + "</th><td>" + std::string(ccrules_get_rule_name(reconst));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=*" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_nodefl) {
        s += "<tr><th>" + r + "</th><td>" + std::string(ccrules_get_rule_name(no_deflate));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_nodefl1) {
        s += "<tr><th>" + r + "*</th><td>" + std::string(ccrules_get_rule_name(no_deflate));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=" + r + "*\">" REMOVE_ICON "</a></td></tr>\n";
    }
    BOOST_FOREACH(const std::string &r, cc_rules_nodefl2) {
        s += "<tr><th>*" + r + "</th><td>" + std::string(ccrules_get_rule_name(no_deflate));
        s += "</td><td>0</td>" SPACE_NB "<td><a href=\"rules?remove=*" + r + "\">" REMOVE_ICON "</a></td></tr>\n";
    }
    s += "</table><br/><br/>\n";
    return s;
}

extern std::string conf_main();
std::string ccrules_page(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "load") {
                ccrules_load();
                return web_srefresh("rules");
            }
            if (vuri[1] == "save") {
                ccrules_save(props::get().confd_path + CCRULES_FILENAME);
                return web_srefresh("rules");
            }
            if (vuri[1] == "remove") {
                ccrules_remove_rule(web_url_decode(vuri[2]));
                return web_srefresh("rules");
            }
            if (vuri.size() > 6 && vuri[1] == "fname") {
                ccrules_add_rule(props::normalize_path(web_url_decode(vuri[2])),
                                 atoi(vuri[4].c_str()), atoi(vuri[6].c_str()));
                return web_srefresh("rules");
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

//    return rules_main();
    return conf_main();
}

#ifndef NDEBUG
#include <boost/make_shared.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

static void ccrules_clear_all()
{
        cc_rules.clear();
    cc_rules_temp1.clear();
    cc_rules_temp2.clear();
    cc_rules_excl.clear();
    cc_rules_excl1.clear();
    cc_rules_excl2.clear();
    cc_rules_excl3.clear();
    cc_rules_recon.clear();
    cc_rules_recon1.clear();
    cc_rules_recon2.clear();
    cc_rules_nodefl.clear();
    cc_rules_nodefl1.clear();
    cc_rules_nodefl2.clear();
}

BOOST_AUTO_TEST_SUITE (main_test_suite_rules)

BOOST_AUTO_TEST_CASE (rules_tests_exclude)
{
    ccrules_clear_all();
    ccrules_add_rule("C:/Users/Pepe", exclude, 0);
    ccrules_add_rule("C:/Users/Juan/*", exclude, 0);
    ccrules_add_rule("*.jpg", exclude, 0);
    ccrules_add_rule("*/~$*", exclude, 0);

    BOOST_CHECK( cc_rules_excl.size() == 1 );
    BOOST_CHECK( cc_rules_excl1.size() == 1 );
    BOOST_CHECK( cc_rules_excl2.size() == 1 );
    BOOST_CHECK( cc_rules_excl3.size() == 1 );

    BOOST_CHECK( ccrules_exclude("C:/Users/Pepe") );
    BOOST_CHECK( !ccrules_exclude("C:/Users/Pepe/test.txt") );
    BOOST_CHECK( ccrules_exclude("C:/Users/Pepe/test.jpg") );
    BOOST_CHECK( ccrules_exclude("C:/Users/Pepa/test.jpg") );
    BOOST_CHECK( !ccrules_exclude("C:/Users/Pepa/test.txt") );
    BOOST_CHECK( !ccrules_exclude("C:/Users/Pep") );
    BOOST_CHECK( !ccrules_exclude("C:/Users/Juan") );
    BOOST_CHECK( ccrules_exclude("C:/Users/Juan/test.txt") );
    BOOST_CHECK( ccrules_exclude("C:/Users/Juan/test.jpg") );
    BOOST_CHECK( ccrules_exclude("C:/Users/Juan/test.jpeg") );
    BOOST_CHECK( ccrules_exclude("a.jpg") );
    BOOST_CHECK( ccrules_exclude("1.jpg") );
    BOOST_CHECK( !ccrules_exclude("1.jpg_") );
    BOOST_CHECK( !ccrules_exclude("1._jpg") );
    BOOST_CHECK( !ccrules_exclude("file.ajpg") );
    BOOST_CHECK( ccrules_exclude("C:/prueba2/~$jacinto.doc") );
    BOOST_CHECK( !ccrules_exclude("C:/prueba2~$jacinto.doc") );
    BOOST_CHECK( !ccrules_exclude("C:/prueba2/~1$jacinto.doc") );
//ccrules_save("ccrules_test_exclude.xml");
    ccrules_clear_all();
}

BOOST_AUTO_TEST_CASE (rules_tests_reconst)
{
    ccrules_clear_all();
    ccrules_add_rule("C:/Users/Pepe", reconst, 0);
    ccrules_add_rule("C:/Users/Juan/*", reconst, 0);
    ccrules_add_rule("*.jpg", reconst, 0);

    BOOST_CHECK( cc_rules_recon.size() == 1 );
    BOOST_CHECK( cc_rules_recon1.size() == 1 );
    BOOST_CHECK( cc_rules_recon2.size() == 1 );

    BOOST_CHECK( ccrules_reconst("C:/Users/Pepe") );
    BOOST_CHECK( !ccrules_reconst("C:/Users/Pepe/test.txt") );
    BOOST_CHECK( ccrules_reconst("C:/Users/Pepe/test.jpg") );
    BOOST_CHECK( ccrules_reconst("C:/Users/Pepa/test.jpg") );
    BOOST_CHECK( !ccrules_reconst("C:/Users/Pepa/test.txt") );
    BOOST_CHECK( !ccrules_reconst("C:/Users/Pep") );
    BOOST_CHECK( !ccrules_reconst("C:/Users/Juan") );
    BOOST_CHECK( ccrules_reconst("C:/Users/Juan/test.txt") );
    BOOST_CHECK( ccrules_reconst("C:/Users/Juan/test.jpg") );
    BOOST_CHECK( ccrules_reconst("C:/Users/Juan/test.jpeg") );
    BOOST_CHECK( ccrules_reconst("a.jpg") );
    BOOST_CHECK( ccrules_reconst("1.jpg") );
    BOOST_CHECK( !ccrules_reconst("1.jpg_") );
    BOOST_CHECK( !ccrules_reconst("1._jpg") );
    BOOST_CHECK( !ccrules_reconst("file.ajpg") );
    ccrules_clear_all();
}

BOOST_AUTO_TEST_CASE (rules_tests_no_defl)
{
    ccrules_clear_all();
    ccrules_add_rule("C:/Users/Pepe", no_deflate, 0);
    ccrules_add_rule("C:/Users/Juan/*", no_deflate, 0);
    ccrules_add_rule("*.7z", no_deflate, 0);

    BOOST_CHECK( cc_rules_nodefl.size() == 1 );
    BOOST_CHECK( cc_rules_nodefl1.size() == 1 );
    BOOST_CHECK( cc_rules_nodefl2.size() == 1 );

    BOOST_CHECK( ccrules_no_deflate("C:/Users/Pepe") );
    BOOST_CHECK( !ccrules_no_deflate("C:/Users/Pepe/test.txt") );
    BOOST_CHECK( ccrules_no_deflate("C:/Users/Pepe/test.7z") );
    BOOST_CHECK( ccrules_no_deflate("C:/Users/Pepa/test.7z") );
    BOOST_CHECK( !ccrules_no_deflate("C:/Users/Pepa/test.txt") );
    BOOST_CHECK( !ccrules_no_deflate("C:/Users/Pep") );
    BOOST_CHECK( !ccrules_no_deflate("C:/Users/Juan") );
    BOOST_CHECK( ccrules_no_deflate("C:/Users/Juan/test.txt") );
    BOOST_CHECK( ccrules_no_deflate("C:/Users/Juan/test.jpg") );
    BOOST_CHECK( ccrules_no_deflate("C:/Users/Juan/test.jpeg") );
    BOOST_CHECK( ccrules_no_deflate("a.7z") );
    BOOST_CHECK( ccrules_no_deflate("1.7z") );
    BOOST_CHECK( !ccrules_no_deflate("1.7z_") );
    BOOST_CHECK( !ccrules_no_deflate("1._7z") );
    BOOST_CHECK( !ccrules_no_deflate("file.a7z") );
    ccrules_clear_all();
}

BOOST_AUTO_TEST_CASE (rules_tests_hold_mod)
{
    ccrules_clear_all();
    ccrules_add_rule("D:/Users/Pepe", wait_num_modifications, 1);
    ccrules_add_rule("D:/Users/Juan/*", wait_num_modifications, 1);
    ccrules_add_rule("*.png", wait_num_modifications, 1);
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = "D:/Users/Pepe"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepe/test.txt"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepe/test.png"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepa/test.png"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepa/test.txt"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pep"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.txt"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.png"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.pnge"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "a.png"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "1.png"; fentry->count = 0;
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->fname = "1.png_"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "1._png"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "file.apng"; fentry->count = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );

    fentry->fname = "D:/Users/Pepe"; fentry->count = 1;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepe/test.txt"; fentry->count = 2;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepe/test.png"; fentry->count = 2;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepa/test.png"; fentry->count = 1;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pepa/test.txt"; fentry->count = 100;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Pep"; fentry->count = 50;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan"; fentry->count = 30;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.txt"; fentry->count = 10;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.png"; fentry->count = 1;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "D:/Users/Juan/test.pnge"; fentry->count = 2;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "a.png"; fentry->count = 01;
    BOOST_CHECK( !ccrules_hold(fentry) );
    fentry->fname = "1.png"; fentry->count = 023;
    BOOST_CHECK( !ccrules_hold(fentry) );
    ccrules_clear_all();
}

BOOST_AUTO_TEST_CASE (rules_tests_hold_misc)
{
    ccrules_clear_all();
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();

    ccrules_add_rule("aaaaaaa", wait_since_last_copy, 10000);
    fentry->fname = "aaaaaaa";
    BOOST_CHECK( !ccrules_hold(fentry) );
    BOOST_CHECK( ccrules_hold(fentry) );
    BOOST_CHECK( ccrules_hold(fentry) );
    ccrules_add_rule("aaaaaaa", wait_since_last_copy, 100);
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( !ccrules_hold(fentry) );
    BOOST_CHECK( ccrules_hold(fentry) );
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( !ccrules_hold(fentry) );
    BOOST_CHECK( ccrules_hold(fentry) );

    ccrules_add_rule("bbbbbbb", wait_since_oldest_mod, 100000);
    fentry->fname = "bbbbbbb";
    fentry->initime = props::current_time_ms();
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->initime = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );

    ccrules_add_rule("ccccccc", wait_since_newest_mod, 100000);
    fentry->fname = "ccccccc";
    fentry->initime_last = props::current_time_ms();
    BOOST_CHECK( ccrules_hold(fentry) );
    fentry->initime_last = 0;
    BOOST_CHECK( !ccrules_hold(fentry) );

    size_t msec = 12*3600;
    bool am = ((props::current_time_ms() / 1000) % (24*3600)) < msec;
    ccrules_add_rule("dddddddd", wait_until_time_of_day, msec);
    fentry->fname = "dddddddd";
    BOOST_CHECK( ccrules_hold(fentry) == am );
    ccrules_add_rule("dddddddd2", only_before_time_of_day, msec);
    fentry->fname = "dddddddd2";
    BOOST_CHECK( ccrules_hold(fentry) != am );
    ccrules_clear_all();
}

BOOST_AUTO_TEST_CASE (rules_tests_save_load)
{
    ccrules_clear_all();
    srand (time(NULL));
    int param[10][exclude];
    for (int i = 0; i < 10; ++i)
    for (int j = 0; j < exclude; ++j) {
        param[i][j] = rand() % 100*3600*1000;
        std::string rname = "test_rule/test_rule_" +
            boost::lexical_cast<std::string>(i) + "_" +
            boost::lexical_cast<std::string>(j) + "/";
        if (param[i][j]%4 == 0) rname = "*" + rname;
        else if (param[i][j]%4 == 1) rname = rname + "*";
        ccrules_add_rule(rname, j, param[i][j]);
    }
    ccrules_add_rule("test_rule/test_rule_exc_1", exclude, 0);
    ccrules_add_rule("test_rule/test_rule_exc_2/*", exclude, 0);
    ccrules_add_rule("*/test_rule/test_rule_exc_3", exclude, 0);
    ccrules_add_rule("test_rule/test_rule_rec_1", reconst, 0);
    ccrules_add_rule("test_rule/test_rule_rec_2/*", reconst, 0);
    ccrules_add_rule("*/test_rule/test_rule_rec_3", reconst, 0);
    for (int i = 0; i < 10; ++i)
    for (int j = 0; j < exclude; ++j) {
        std::string rname = "test_rule/test_rule_" +
            boost::lexical_cast<std::string>(i) + "_" +
            boost::lexical_cast<std::string>(j) + "/";
        BOOST_CHECK( cc_rules.find(rname) != cc_rules.end() );
        if (param[i][j]%4 == 0)
            BOOST_CHECK( cc_rules_temp2.find(rname) != cc_rules_temp2.end() );
        else if (param[i][j]%4 == 1)
            BOOST_CHECK( cc_rules_temp1.find(rname) != cc_rules_temp1.end() );
        BOOST_CHECK( cc_rules[rname].action == j );
        BOOST_CHECK( cc_rules[rname].param == param[i][j] );
    }
    BOOST_CHECK( cc_rules_excl.find("test_rule/test_rule_exc_1") != cc_rules_excl.end() );
    BOOST_CHECK( cc_rules_excl1.find("test_rule/test_rule_exc_2/") != cc_rules_excl1.end() );
    BOOST_CHECK( cc_rules_excl2.find("/test_rule/test_rule_exc_3") != cc_rules_excl2.end() );
    BOOST_CHECK( cc_rules_recon.find("test_rule/test_rule_rec_1") != cc_rules_recon.end() );
    BOOST_CHECK( cc_rules_recon1.find("test_rule/test_rule_rec_2/") != cc_rules_recon1.end() );
    BOOST_CHECK( cc_rules_recon2.find("/test_rule/test_rule_rec_3") != cc_rules_recon2.end() );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_1") );
    BOOST_CHECK( !ccrules_exclude("test_rule/test_rule_exc_1/") );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_2/") );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_2/a") );
    BOOST_CHECK( !ccrules_exclude("/test_rule/test_rule_exc_2/") );
    BOOST_CHECK( ccrules_exclude("/test_rule/test_rule_exc_3") );
    BOOST_CHECK( ccrules_exclude("b/test_rule/test_rule_exc_3") );
    BOOST_CHECK( !ccrules_exclude("/test_rule/test_rule_exc_3/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_1") );
    BOOST_CHECK( !ccrules_reconst("test_rule/test_rule_rec_1/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_2/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_2/a") );
    BOOST_CHECK( !ccrules_reconst("/test_rule/test_rule_rec_2/") );
    BOOST_CHECK( ccrules_reconst("/test_rule/test_rule_rec_3") );
    BOOST_CHECK( ccrules_reconst("b/test_rule/test_rule_rec_3") );
    BOOST_CHECK( !ccrules_reconst("/test_rule/test_rule_rec_3/") );
    std::string ccrfname = "ccrules_auto_test.xml";
    ccrules_save(ccrfname);
    BOOST_CHECK( cc_rules.find("test_rule/test_rule_0_0/") != cc_rules.end() );
    ccrules_clear_all();
    BOOST_CHECK( cc_rules.find("test_rule/test_rule_0_0/") == cc_rules.end() );
    ccrules_load(ccrfname);
    for (int i = 0; i < 10; ++i)
    for (int j = 0; j < exclude; ++j) {
        std::string rname = "test_rule/test_rule_" +
            boost::lexical_cast<std::string>(i) + "_" +
            boost::lexical_cast<std::string>(j) + "/";
        BOOST_CHECK( cc_rules.find(rname) != cc_rules.end() );
        if (param[i][j]%4 == 0)
            BOOST_CHECK( cc_rules_temp2.find(rname) != cc_rules_temp2.end() );
        else if (param[i][j]%4 == 1)
            BOOST_CHECK( cc_rules_temp1.find(rname) != cc_rules_temp1.end() );
        BOOST_CHECK( cc_rules[rname].action == j );
        BOOST_CHECK( cc_rules[rname].param == param[i][j] );
    }
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_1") );
    BOOST_CHECK( !ccrules_exclude("test_rule/test_rule_exc_1/") );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_2/") );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_2/a") );
    BOOST_CHECK( !ccrules_exclude("/test_rule/test_rule_exc_2/") );
    BOOST_CHECK( ccrules_exclude("/test_rule/test_rule_exc_3") );
    BOOST_CHECK( ccrules_exclude("b/test_rule/test_rule_exc_3") );
    BOOST_CHECK( !ccrules_exclude("/test_rule/test_rule_exc_3/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_1") );
    BOOST_CHECK( !ccrules_reconst("test_rule/test_rule_rec_1/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_2/") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_2/a") );
    BOOST_CHECK( !ccrules_reconst("/test_rule/test_rule_rec_2/") );
    BOOST_CHECK( ccrules_reconst("/test_rule/test_rule_rec_3") );
    BOOST_CHECK( ccrules_reconst("b/test_rule/test_rule_rec_3") );
    BOOST_CHECK( !ccrules_reconst("/test_rule/test_rule_rec_3/") );

    ccrules_remove_rule("test_rule/test_rule_exc_1");
    BOOST_CHECK( !ccrules_exclude("test_rule/test_rule_exc_1") );
    BOOST_CHECK( ccrules_exclude("test_rule/test_rule_exc_2/a") );
    ccrules_remove_rule("test_rule/test_rule_exc_2/*");
    BOOST_CHECK( !ccrules_exclude("test_rule/test_rule_exc_2/a") );
    ccrules_remove_rule("*/test_rule/test_rule_exc_3");
    BOOST_CHECK( !ccrules_exclude("/test_rule/test_rule_exc_3") );
    ccrules_remove_rule("test_rule/test_rule_rec_1");
    BOOST_CHECK( !ccrules_reconst("test_rule/test_rule_rec_1") );
    BOOST_CHECK( ccrules_reconst("test_rule/test_rule_rec_2/a") );
    ccrules_remove_rule("test_rule/test_rule_rec_2/*");
    BOOST_CHECK( !ccrules_reconst("test_rule/test_rule_rec_2/a") );
    BOOST_CHECK( ccrules_reconst("b/test_rule/test_rule_rec_3") );
    ccrules_remove_rule("*/test_rule/test_rule_rec_3");
    BOOST_CHECK( !ccrules_reconst("b/test_rule/test_rule_rec_3") );

    BOOST_CHECK( cc_rules.find("test_rule/test_rule_0_0/") != cc_rules.end() );
    ccrules_remove_rule("test_rule/test_rule_0_0/");
    BOOST_CHECK( cc_rules.find("test_rule/test_rule_0_0/") == cc_rules.end() );

    boost::filesystem::remove(ccrfname);
    ccrules_clear_all();
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
