#include <exception>
#include <iostream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include "fstream_utf8.h"
#include "localBackend.h"
#include "sshBackend.h"
#include "ofd_processer.h"
#include "props_sched.h"
#include "log_sched.h"
#include "dict.h"
#include "allsafe.h"
#include "cc_rules.h"

static props _props;

const struct props & props::get() { return _props; }

extern const char *main_version;

std::string props::debug_str()
{
    std::string s = "<li><b>props::debug_str()</b></li>";
    s += "<li>version: " + std::string(main_version) + "</li>\n";
    for (int i=0; i<_props.dirstowatch.size(); i++)
        s += "<li>dirtowatch: " + _props.dirstowatch[i] + "</li>\n";
    return s;
}

static inline const boost::property_tree::ptree::path_type pt_path(const char *s)
{
    return boost::property_tree::ptree::path_type(s, '/');
}
static std::string trim_backslash(const std::string &s)
{
    std::string ss;
    for (int i = 0; i < s.size(); i++) {
        if (s[i] == '\\') {
            if (++i < s.size()) ss.push_back(s[i]);
            continue;
        }
        ss.push_back(s[i]);
    }
    return ss;
}
static std::vector<std::string> vectorize(const std::string &s)
{
    std::vector<std::string> vs;
    boost::char_separator<char> sep(";");
    boost::tokenizer<boost::char_separator<char> > tok(s, sep);
    BOOST_FOREACH(const std::string &_s, tok) {
        vs.push_back(_s);
    }
    return vs;
}

bool props::update(const std::string &s)
{
    bool change = false;
    if (_props.backend != s) {
        std::string old_backend = _props.backend;
        if (s == "local" || s == "ssh") _props.backend = s;
        if (old_backend != _props.backend) {
            ILOG << "props::update(" << s << ")-> Backend updated";
            change = true;
            update();
        }
    }
    return change;
}
void props::update()
{

}

std::string props::normalize_path(std::string fpath)
{
    boost::algorithm::trim_if(fpath, boost::algorithm::is_any_of("\""));
    bool begins_slash = (fpath.size() && fpath[0] == '/');
    boost::algorithm::trim_if(fpath, boost::algorithm::is_any_of(" /\\\t\""));
    boost::algorithm::replace_all(fpath, "\\", "/");
    if (fpath.size() > 1 && fpath[1] == ':' && islower(fpath[0])) fpath[0] = toupper(fpath[0]);
    if (begins_slash) fpath = "/" + fpath;
    return fpath;
}
static const size_t props_second_factor[4] = { 1, 60, 3600, 24*3600 };
size_t props::to_seconds(const std::string &stime) // [[[dd:]hh:]mm:]ss
{
    DLOG << "props::to_seconds(" << stime << ")";
    std::list<size_t> parts;
    boost::char_separator<char> sep(":- ,.;\\|/");
    boost::tokenizer<boost::char_separator<char> > tok(stime, sep);
    BOOST_FOREACH(const std::string &_s, tok) {
        try { parts.push_front(boost::lexical_cast<size_t>(_s)); } catch (std::exception &e) { WLOG << "props::to_seconds(" << stime << ")->" << e.what(); }
    }
    int i = 0; size_t seconds = 0;
    BOOST_FOREACH(size_t n, parts) {
        if (i < 4) seconds += props_second_factor[i] * n; i++;
    }
    return seconds;
}
std::string props::from_seconds(size_t tt) // [[[dd:]hh:]mm:]ss
{
    std::string s;
    if (tt <= 0) s = "0";
    else for (int i = 3; i >= 0; i--) {
        if (tt >= props_second_factor[i]) {
            size_t n = tt / props_second_factor[i];
            if (!s.empty()) s += ((i==2)?" ":":");
            s += boost::lexical_cast<std::string>(n);
            tt = tt % props_second_factor[i];
        } else if (!s.empty()) s += ((i==2)?" 0":":0");
    }
    return s;
}
void props::init()
{
    if (_props.tmpd.empty()) _props.tmpd = "tmp/";
    dict_set("props.tmpd", &_props.tmpd);

    dict_set("props.backend", &_props.backend);
    dict_set("props.processer", &_props.processer);

    allsafe_t::init();

    LocalBackend::init();
    SshBackend::init();
}

bool props::use_backend() const
{

    return true;
}

Backend * props::createBackend()
{
    if (_props.backend == "local") return new LocalBackend;
    if (_props.backend == "ssh") return new SshBackend;

    if (allsafe_t::get().mode) return new SshBackend;

    return NULL;
}
Processer * props::createProcesser(int n)
{

    return new OfdProcesser(n);
}


int props::add_dir(const std::string &d)
{
    if (std::find(_props.dirstowatch.begin(), _props.dirstowatch.end(), d) == _props.dirstowatch.end())
        _props.dirstowatch.push_back(d);
    return _props.dirstowatch.size() - 1;
}
void props::del_dir(int i)
{
    if (i < _props.dirstowatch.size()) _props.dirstowatch[i].clear();
}

static const boost::posix_time::ptime t0(boost::gregorian::date(1970,1,1));
long long props::current_time_ms()
{
    return (boost::posix_time::microsec_clock::universal_time()-t0).total_milliseconds();
}

