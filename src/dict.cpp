#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include "fstream_utf8.h"
#include <vector>
#include "web_utils.h"
#include "log_sched.h"
#include "props_sched.h"
#include "dict.h"
#include "cipher.h"

typedef std::map<std::string, size_t *> int_vars_t;
static int_vars_t int_vars;
typedef std::map<std::string, std::string *> str_vars_t;
static str_vars_t str_vars, enc_vars;
static std::map<std::string, std::string> preloaded;

void dict_set(std::string id, size_t *v)
{
    int_vars[id] = v;
    if (preloaded.find(id) != preloaded.end()) { try {
            *v = boost::lexical_cast<int>(preloaded[id]);
        } catch (std::exception &e) {
            WLOG << "dict_set(" << id << "," << preloaded[id] << ")->Exception: " << e.what();
        }
    }
}
void dict_set(std::string id, std::string *v)
{
    str_vars[id] = v;
    if (preloaded.find(id) != preloaded.end()) {
        *v = preloaded[id];
    }
}
void dict_set_enc(std::string id, std::string *v)
{
    enc_vars[id] = v;
    if (preloaded.find(id) != preloaded.end()) {
        *v = preloaded[id];
    }
}

void dict_setv(std::string id, size_t v)
{
    if (int_vars.find(id) != int_vars.end()) {
        *(int_vars[id]) = v;
    } else {
        preloaded[id] = boost::lexical_cast<std::string>(v);
    }
}
void dict_setv(std::string id, const std::string &v)
{
    try {
    if (enc_vars.find(id) != enc_vars.end()) {
        *(enc_vars[id]) = v;
    } else if (str_vars.find(id) != str_vars.end()) {
        *(str_vars[id]) = v;
    } else if (int_vars.find(id) != int_vars.end()) {
        *(int_vars[id]) = boost::lexical_cast<int>(v);
    } else {
        preloaded[id] = v;
    }
    } catch (std::exception &e) {
        WLOG << "dict_setv(" << id << "," << v << ")->Exception: " << e.what();
    }
}

std::string dict_get(std::string id)
{
    std::string s;
    if (enc_vars.find(id) != enc_vars.end()) {
        s = *(enc_vars[id]);
    }
    else if (str_vars.find(id) != str_vars.end()) {
        s = *(str_vars[id]);
    }
    else if (int_vars.find(id) != int_vars.end()) {
        s = boost::lexical_cast<std::string>(*(int_vars[id]));
    }
    else if (preloaded.find(id) != preloaded.end()) {
        s = preloaded[id];
    }
    return s;
}

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
    BOOST_FOREACH (const str_vars_t::value_type &v, enc_vars) {
        if (color) s += "<tr bgcolor=\"#EEEEEE\">"; else s += "<tr>"; color = !color;
        s += "<th><a href=\"dict?edit=" + v.first + "\">" + v.first + "</a></th><td>************</td></tr>\n";
    }
    s += "</table><br/><a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
        "<a href=/dict?save=1 class=ml>save</a><a href=/dict?load=1 class=ml>load</a><br/>\n"
        "</body></html>";
    return s;
}

void dict_get_all(std::map<std::string, std::string> &vars)
{
    BOOST_FOREACH (const int_vars_t::value_type &v, int_vars)
        vars[v.first] = boost::lexical_cast<std::string>(*v.second);
    BOOST_FOREACH (const str_vars_t::value_type &v, str_vars)
        vars[v.first] = *v.second;
    BOOST_FOREACH (const str_vars_t::value_type &v, enc_vars)
        vars[v.first] = "******";
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
            BOOST_FOREACH (const str_vars_t::value_type &v, enc_vars) {
                ofs << "\t<entry name=\"" << v.first << "\" type=\"enc\">" << cipher_encript1(*v.second) << "</entry>\n";
            }
            ofs << "</dict>\n";
        }
    } catch (std::exception &e) {
        ELOG << "dict_save(" <<fname<< ")->Exception '" << e.what();
    }
    TLOG << "dict_save(" <<fname<< ") END";
}

int dict_load(std::string fname)
{
    TLOG << "dict_load(" <<fname<< ") INIT";
    try {
        ifstream_utf8 ifs(fname.c_str());
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("dict") ) {
            try { if( v.first == "entry" ) {
                if (v.second.get<std::string>("<xmlattr>.type") == "enc") {
                    dict_setv(v.second.get<std::string>("<xmlattr>.name"), cipher_decript1(v.second.data()));
                } else {
                    dict_setv(v.second.get<std::string>("<xmlattr>.name"), v.second.data());
                }
            }} catch (std::exception &e) {
                WLOG << "dict_load()->Exception '" << e.what() << "' parsing file '" <<fname<< "'";
            }}
        }
    } catch (std::exception &e) {
        ELOG << "dict_load()->Exception '" << e.what() << "' loading file '" <<fname<< "'";
    }
    TLOG << "dict_load(" <<fname<< "): " << str_vars.size() << "/" << int_vars.size();
    return 0;
}

int dict_save()
{
    dict_save(props::get().confd_path + DICT_FILENAME);
}

int dict_load()
{
    dict_load(props::get().confd_path + DICT_FILENAME);
}

std::string dict_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 2) {
        if (vuri[1] == "save") {
            dict_save();
        } else if (vuri[1] == "load") {
            dict_load();
        } else if (vuri[1] == "edit") {
            std::string s = wserver_pages_head();
            s += "<body><a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/dict class=ml>dict</a><br/>\n";
            s += "<h4>" + vuri[2] + "</h4><form action=dict method=get>"
                "<input type=hidden name=set value=" + vuri[2] + ">";
            if (enc_vars.find(vuri[2]) != enc_vars.end()) {
                s += "<input type=password name=val size=30>";
            } else {
                std::string val = dict_get(vuri[2]);
                size_t n = val.size()+10; if (n < 24) n = 24;
                s += "<input type=text name=val size=" + boost::lexical_cast<std::string>(n) +
                    " value=\"" + val + "\">";
            }
            s += "<input type=submit value=set></form>\n</body></html>";
            return s;
        } else if (vuri[1] == "set" && vuri.size() > 3) {
            dict_setv(vuri[2], vuri.size() > 4 ? web_url_decode(vuri[4]) : "");
            return web_srefresh("dict");
        } else if (vuri[1] == "sets" && vuri.size() > 3) {
            for (int i = 3; (i+1) < vuri.size(); i+=2)
                dict_setv(vuri[i], web_url_decode(vuri[i+1]));
            dict_save();
            return "OK";
        }
    }

    return dict_dump();
}

#ifndef NDEBUG
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_dict)

BOOST_AUTO_TEST_CASE (dict_tests_sets)
{
    std::vector<std::string> vuri;
    vuri.push_back("/dict");
    vuri.push_back("sets");
    vuri.push_back("1");
    vuri.push_back("_url_.u");
    vuri.push_back("http%3A%2F%2Flocalhost%3A8000%2Fallsafe%2Fallsafe_alias%2Fep");
    vuri.push_back("_ur_.alidas");
    vuri.push_back("fraiperico");
    vuri.push_back("alidas.nique");
    vuri.push_back("32941924");
    std::string s1,s2; size_t n;
    dict_set("_url_.u", &s1);
    dict_set("_ur_.alidas", &s2);
    dict_set("alidas.nique", &n);
    BOOST_CHECK( dict_dispatch(vuri) == "OK" );
    BOOST_CHECK( s1 == "http://localhost:8000/allsafe/allsafe_alias/ep" );
    BOOST_CHECK( s2 == "fraiperico" );
    BOOST_CHECK( n == 32941924 );
    s1.clear(); s2.clear(); n = 0;
    dict_load();
    BOOST_CHECK( s1 == "http://localhost:8000/allsafe/allsafe_alias/ep" );
    BOOST_CHECK( s2 == "fraiperico" );
    BOOST_CHECK( n == 32941924 );

    int_vars.clear();
    str_vars.clear();
    preloaded.clear();
}

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

    std::string e1("e1value");
    dict_set("e1", &e1);
    BOOST_CHECK( dict_get("e1") == "e1value" );
    BOOST_CHECK( e1 == "e1value" );
    dict_setv("e1", "e1value2");
    BOOST_CHECK( dict_get("e1") == "e1value2" );
    BOOST_CHECK( e1 == "e1value2" );
    std::string e2;
    dict_setv("e2", "e2value");
    BOOST_CHECK( dict_get("e2") == "e2value" );
    dict_set("e2", &e2);
    BOOST_CHECK( e2 == "e2value" );
    BOOST_CHECK( dict_get("e2") == "e2value" );
    e2 = "e2value2";
    BOOST_CHECK( dict_get("e2") == "e2value2" );

    s1 = "s1 value";
    s2 = "s2 abcde";
    e1 = "e1value2";
    e2 = "e2value2";
    i1 = 11111;
    i2 = 22222;
    std::string dfname = "dict_auto_test.xml";
    dict_save(dfname);
    s1 = "s1 value ---";
    s2 = "s2 abcde ----";
    e1 = "--";
    e2 = "--";
    i1 = 1111100;
    i2 = 2222200;
    dict_load(dfname);
    BOOST_CHECK( s1 == "s1 value" );
    BOOST_CHECK( s2 == "s2 abcde" );
    BOOST_CHECK( i1 == 11111 );
    BOOST_CHECK( i2 == 22222 );
    BOOST_CHECK( dict_get("s1") == "s1 value" );
    BOOST_CHECK( dict_get("s2") == "s2 abcde" );
    BOOST_CHECK( dict_get("e1") == "e1value2" );
    BOOST_CHECK( dict_get("e2") == "e2value2" );
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
