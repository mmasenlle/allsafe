#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <map>
#include <vector>
#include "fstream_utf8.h"
#include "allsafe_dict.h"

typedef std::map<std::string, size_t *> int_vars_t;
static int_vars_t int_vars;
typedef std::map<std::string, std::string *> str_vars_t;
static str_vars_t str_vars;
//static std::map<std::string, std::string> preloaded;

void dict_set(std::string id, size_t *v)
{
    int_vars[id] = v;
//    if (preloaded.find(id) != preloaded.end()) { try {
//            *v = boost::lexical_cast<int>(preloaded[id]);
//        } catch (std::exception &e) {
//            WLOG << "dict_set(" << id << "," << preloaded[id] << ")->Exception: " << e.what();
//        }
//    }
}
void dict_set(std::string id, std::string *v)
{
    str_vars[id] = v;
//    if (preloaded.find(id) != preloaded.end()) {
//        *v = preloaded[id];
//    }
}

void dict_setv(std::string id, size_t v)
{
    if (int_vars.find(id) != int_vars.end()) {
        *(int_vars[id]) = v;
    } else {
//        preloaded[id] = boost::lexical_cast<std::string>(v);
    }
}
void dict_setv(std::string id, const std::string &v)
{
    try {
    if (str_vars.find(id) != str_vars.end()) {
        *(str_vars[id]) = v;
    } else if (int_vars.find(id) != int_vars.end()) {
        *(int_vars[id]) = boost::lexical_cast<int>(v);
    } else {
//        preloaded[id] = v;
    }
    } catch (std::exception &e) {
//        WLOG << "dict_setv(" << id << "," << v << ")->Exception: " << e.what();
    }
}

std::string dict_get(std::string id)
{
    std::string s;
    if (str_vars.find(id) != str_vars.end()) {
        s = *(str_vars[id]);
    }
    else if (int_vars.find(id) != int_vars.end()) {
        s = boost::lexical_cast<std::string>(*(int_vars[id]));
    }
    return s;
}
/*
std::string dict_dump()
{
    int color = 0;
    std::string s = wserver_pages_head();
    s += "<body><h2>Diccionario</h2>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
        "<a href=/dict?save=1 class=ml>save</a><a href=/dict?load=1 class=ml>load</a><br/><br/>\n<table>";
    BOOST_FOREACH (const int_vars_t::value_type &v, int_vars) {
        if (color) s += "<tr bgcolor=\"#EEEEEE\">"; else s += "<tr>"; color = !color;
        s += "<th><a href=\"dict?edit=" + v.first + "\">" + v.first + "</a></th><td>"
                + boost::lexical_cast<std::string>(*v.second) + "</td></tr>\n";
    }
    BOOST_FOREACH (const str_vars_t::value_type &v, str_vars) {
        if (color) s += "<tr bgcolor=\"#EEEEEE\">"; else s += "<tr>"; color = !color;
        s += "<th><a href=\"dict?edit=" + v.first + "\">" + v.first + "</a></th><td>"
                + *v.second + "</td></tr>\n";
    }
    s += "</table><br/><a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
        "<a href=/dict?save=1 class=ml>save</a><a href=/dict?load=1 class=ml>load</a><br/>\n"
        "</body></html>";
    return s;
}

#define DICT_FILENAME "dict.xml"
void dict_save(std::string fname)
{
    TLOG << "dict_save(" <<fname<< "): " << str_vars.size() << "/" << int_vars.size();
    try {
        ofstream_utf8 ofs(fname.c_str());
        if (ofs) {
            ofs << "<?xml version=\"1.0\"?>\n<dict>\n";
            BOOST_FOREACH (const int_vars_t::value_type &v, int_vars) {
                ofs << "\t<entry name=\"" << v.first << "\" type=\"int\">" << *v.second << "</entry>\n";
            }
            BOOST_FOREACH (const str_vars_t::value_type &v, str_vars) {
                ofs << "\t<entry name=\"" << v.first << "\" type=\"str\">" << *v.second << "</entry>\n";
            }
            ofs << "</dict>\n";
        }
    } catch (std::exception &e) {
        ELOG << "dict_save(" <<fname<< ")->Exception '" << e.what();
    }
    TLOG << "dict_save(" <<fname<< ") END";
}
*/

int dict_load(std::string fname)
{
//    TLOG << "dict_load(" <<fname<< ") INIT";
    try {
        ifstream_utf8 ifs(fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("dict") ) {
            try { if( v.first == "entry" ) {
                if (v.second.get<std::string>("<xmlattr>.type") == "int") {
                    dict_setv(v.second.get<std::string>("<xmlattr>.name"), v.second.data());
                } else {
                    dict_setv(v.second.get<std::string>("<xmlattr>.name"), v.second.data());
                }
            }} catch (std::exception &e) {
//                WLOG << "dict_load()->Exception '" << e.what() << "' parsing file '" <<fname<< "'";
            }}
        }
    } catch (std::exception &e) {
//        ELOG << "dict_load()->Exception '" << e.what() << "' loading file '" <<fname<< "'";
    }
//    TLOG << "dict_load(" <<fname<< "): " << str_vars.size() << "/" << int_vars.size();
    return 0;
}

/*
std::string dict_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 2) {
        if (vuri[1] == "save") {
            dict_save(props::get().confd_path + DICT_FILENAME);
        } else if (vuri[1] == "load") {
            dict_load(props::get().confd_path + DICT_FILENAME);
        } else if (vuri[1] == "edit") {
            std::string s = wserver_pages_head();
            s += "<body><a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/dict class=ml>dict</a><br/>\n";
            s += "<h4>" + vuri[2] + "</h4><form action=dict method=get>"
                "<input type=hidden name=set value=" + vuri[2] + ">";
            std::string val = dict_get(vuri[2]);
            size_t n = val.size()+10; if (n < 24) n = 24;
            s += "<input type=text name=val size=" + boost::lexical_cast<std::string>(n) +
                " value=\"" + val + "\"><input type=submit value=set></form>\n"
                "</body></html>";
            return s;
        } else if (vuri[1] == "set" && vuri.size() > 3) {
            dict_setv(vuri[2], vuri.size() > 4 ? web_url_decode(vuri[4]) : "");
        }
    }

    return dict_dump();
}

#ifndef NDEBUG
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_dict)

BOOST_AUTO_TEST_CASE (dict_tests)
{
    std::string s1;
    dict_setv("s1", "s1val");
    dict_set("s1", &s1);
    BOOST_CHECK( s1 == "s1val" );
    BOOST_CHECK( dict_get("s1") == "s1val" );
    s1 = "diff s1val";
    BOOST_CHECK( dict_get("s1") == "diff s1val" );
    BOOST_CHECK( s1 == "diff s1val" );
    dict_setv("s1", " s1val 22");
    BOOST_CHECK( s1 == " s1val 22" );
    BOOST_CHECK( dict_get("s1") == " s1val 22" );
    std::string s2("s2value");
    dict_set("s2", &s2);
    BOOST_CHECK( dict_get("s2") == "s2value" );
    BOOST_CHECK( s2 == "s2value" );

    size_t i1 = 1221;
    dict_set("i1", &i1);
    BOOST_CHECK( i1 == 1221 );
    BOOST_CHECK( dict_get("i1") == "1221" );

    size_t i2 = 0;
    dict_setv("i2", "2112");
    dict_set("i2", &i2);
    BOOST_CHECK( i2 == 2112 );
    BOOST_CHECK( dict_get("i2") == "2112" );
    i2 = 21012;
    BOOST_CHECK( dict_get("i2") == "21012" );
    BOOST_CHECK( i2 == 21012 );
    dict_setv("i2", "21712");
    BOOST_CHECK( i2 == 21712 );
    BOOST_CHECK( dict_get("i2") == "21712" );
    dict_setv("i2", 71217);
    BOOST_CHECK( i2 == 71217 );
    BOOST_CHECK( dict_get("i2") == "71217" );

    s1 = "s1 value";
    s2 = "s2 abcde";
    i1 = 11111;
    i2 = 22222;
    std::string dfname = "dict_auto_test.xml";
    dict_save(dfname);
    s1 = "s1 value ---";
    s2 = "s2 abcde ----";
    i1 = 1111100;
    i2 = 2222200;
    dict_load(dfname);
    BOOST_CHECK( s1 == "s1 value" );
    BOOST_CHECK( s2 == "s2 abcde" );
    BOOST_CHECK( i1 == 11111 );
    BOOST_CHECK( i2 == 22222 );
    BOOST_CHECK( dict_get("s1") == "s1 value" );
    BOOST_CHECK( dict_get("s2") == "s2 abcde" );
    BOOST_CHECK( dict_get("i1") == "11111" );
    BOOST_CHECK( dict_get("i2") == "22222" );

    s1 = "s1 ---value";
    s2 = "s2 ab ----cde";
    i1 = 1110011;
    i2 = 2220022;
    BOOST_CHECK( dict_get("s1") == "s1 ---value" );
    BOOST_CHECK( dict_get("s2") == "s2 ab ----cde" );
    BOOST_CHECK( dict_get("i1") == "1110011" );
    BOOST_CHECK( dict_get("i2") == "2220022" );
    int_vars.clear();
    str_vars.clear();
    preloaded.clear();
    dict_load(dfname);
    dict_set("i1", &i1);
    dict_set("i2", &i2);
    dict_set("s1", &s1);
    dict_set("s2", &s2);
    BOOST_CHECK( s1 == "s1 value" );
    BOOST_CHECK( s2 == "s2 abcde" );
    BOOST_CHECK( i1 == 11111 );
    BOOST_CHECK( i2 == 22222 );
    BOOST_CHECK( dict_get("s1") == "s1 value" );
    BOOST_CHECK( dict_get("s2") == "s2 abcde" );
    BOOST_CHECK( dict_get("i1") == "11111" );
    BOOST_CHECK( dict_get("i2") == "22222" );

    boost::filesystem::remove(dfname);
    int_vars.clear();
    str_vars.clear();
    preloaded.clear();

    BOOST_CHECK( dict_get("s1").empty() );
    BOOST_CHECK( dict_get("s2").empty() );
    BOOST_CHECK( dict_get("i1").empty() );
    BOOST_CHECK( dict_get("i2").empty() );
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
*/
