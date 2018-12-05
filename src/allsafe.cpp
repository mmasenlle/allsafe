
#include <list>
#include <sstream>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include "allsafe.h"
#include "old_files_data.h"
#include "log_sched.h"
#include "props_sched.h"
#include "dict.h"
#include "dirs.h"
#include "cc_rules.h"
#include "http_client.h"
#include "zlib_sched.h"


static const size_t allsafe_version = 10000;

static allsafe_t _allsafe;

const struct allsafe_t & allsafe_t::get() { return _allsafe; }

void allsafe_thread();

static size_t allsafe_max_send = 2000;

void allsafe_t::init()
{
    _allsafe.mode = 1;
    _allsafe.conn_min = 20;
    _allsafe.conn_max = 120;


    dict_set("allsafe.mode", &_allsafe.mode);
    dict_set("allsafe.conn_min", &_allsafe.conn_min);
    dict_set("allsafe.conn_max", &_allsafe.conn_max);
    dict_set("allsafe.max_send", &allsafe_max_send);

    dict_set("allsafe.alias", &_allsafe.alias);
    dict_set("allsafe.url", &_allsafe.url);

    boost::thread th(allsafe_thread);
    th.detach();
}


static boost::mutex allsafe_files_mtx, allsafe_th_mtx;
static boost::condition_variable allsafe_th_cond;
static std::list<boost::shared_ptr<fentry_t> > allsafe_files_list;

void allsafe_t::push(const boost::shared_ptr<fentry_t> &fentry)
{
    if (_allsafe.mode) {
        boost::mutex::scoped_lock scoped_lock(allsafe_files_mtx);
        allsafe_files_list.push_back(fentry);
    }
}

#define TAG_ID "id"
#define TAG_STATUS "st"
#define TAG_ACTIONS "actions"
#define TAG_SET "set"
#define TAG_GET "get"
#define TAG_GET_ALL "get_all"
#define TAG_ADD_DIR "add_dir"
#define TAG_RM_DIR "rm_dir"
#define TAG_GET_BACKEND "get_backend"
#define TAG_VAR "var"
#define TAG_VALUE "value"
#define TAG_VARS "vars"
#define TAG_ARG "arg"
#define TAG_GOP "gop"
#define TAG_VER "ver"
#define TAG_DW_UPD "dw_upd"
#define TAG_DO_UPD "do_upd"
#define TAG_URL "url"
#define TAG_DST "dst"
#define TAG_FID "fid"
#define TAG_NAME "name"
#define TAG_PID "pid"
#define TAG_GET_DIRS "get_dirs"
#define TAG_GET_RULES "get_rules"
#define TAG_ADD_RULE "add_rule"
#define TAG_RM_RULE "rm_rule"


static std::map<std::string, std::pair<int, std::string> > requested_fids;
static std::map<std::string, std::string> requested_vars;
static std::vector<std::string> directories;
static std::vector<ccrules_exp_t> req_rules;

static bool something_pending() { return !requested_vars.empty() || !requested_fids.empty() || !directories.empty() || !req_rules.empty(); }
static void clear_pendings() { requested_vars.clear(); requested_fids.clear(); directories.clear(); req_rules.clear(); }

extern std::string wserver_serve(const std::string &uri);

static bool save_dictionary = false;
static bool save_dirs = false;
static bool save_rules = false;

static const std::string allsafe_update_dir_tmp = "update_tmp";
static const std::string allsafe_update_dir = "update";

static void allsafe_dispatch_action(const std::string &id, const boost::property_tree::ptree &arg )
{
    DLOG << "allsafe_t::dispatch_action("<<id<<")";
    if (id == TAG_FID) {
        std::string name, sfid = arg.get<std::string>(TAG_ARG);
        requested_fids[sfid].first = ofd_get_entry(boost::lexical_cast<int>(sfid), name);
        requested_fids[sfid].second = name;
    } else if (id == TAG_SET) {
        boost::property_tree::ptree var_value = arg.get_child(TAG_ARG);
        if (dict_get(var_value.get<std::string>(TAG_VAR)) != var_value.get<std::string>(TAG_VALUE)) {
            dict_setv(var_value.get<std::string>(TAG_VAR), var_value.get<std::string>(TAG_VALUE));
            save_dictionary = true;
        }
    } else if (id == TAG_GET) {
        std::string varname = arg.get<std::string>(TAG_ARG);
        requested_vars[varname] = dict_get(varname);
    } else if (id == TAG_GET_ALL) {
        dict_get_all(requested_vars);
    } else if (id == TAG_GET_DIRS) {
        dirs_get_all(directories);
    } else if (id == TAG_GET_RULES) {
        ccrules_get_all(req_rules);
    } else if (id == TAG_ADD_DIR) {
        save_dirs = save_dirs || !!(dirs_add_dir(arg.get<std::string>(TAG_ARG)));
    } else if (id == TAG_RM_DIR) {
        save_dirs = save_dirs || !!(dirs_del_dir(arg.get<std::string>(TAG_ARG)));
    } else if (id == TAG_ADD_RULE) {
        boost::property_tree::ptree rule = arg.get_child(TAG_ARG);
        ccrules_add_rule(rule.get<std::string>("p"), rule.get<int>("t"), rule.get<int>("a"));
        save_rules = true;
    } else if (id == TAG_RM_RULE) {
        boost::property_tree::ptree rule = arg.get_child(TAG_ARG);
        ccrules_rm_rule(rule.get<std::string>("p"), rule.get<int>("t"));
        save_rules = true;
    } else if (id == TAG_GOP) {
        wserver_serve(arg.get<std::string>(TAG_ARG));
    } else if (id == TAG_DW_UPD) {
        boost::system::error_code ec;
        boost::filesystem::create_directories(allsafe_update_dir_tmp, ec);
        boost::property_tree::ptree url_dst = arg.get_child(TAG_ARG);
        http_client_get_file(url_dst.get<std::string>(TAG_URL), allsafe_update_dir_tmp + "/" + url_dst.get<std::string>(TAG_DST));
    } else if (id == TAG_DO_UPD) {
        boost::filesystem::rename(allsafe_update_dir_tmp, allsafe_update_dir);
        ILOG << "allsafe_dispatch_action()-> stopping because do_update";
        wserver_serve("/stop");
    } else {
        WLOG << "allsafe_t::dispatch_action("<<id<<")->Unknown action";
    }
}

static void pending_things(boost::property_tree::ptree &msg)
{
    if ((props::get().backend.empty() || props::get().backend == "ssh") &&
            (dict_get("sshBackend.host").empty() || dict_get("sshBackend.user").empty())) {
        WLOG << "allsafe.pending_things()-> missing proper ssh configuration";
        msg.put(TAG_GET_BACKEND, dict_get("sshBackend.host"));
    }
    if (!requested_vars.empty()) {
        ILOG << "allsafe.pending_things()-> sending " << requested_vars.size() << " variable values";
        boost::property_tree::ptree vars;
        for (std::map<std::string, std::string>::const_iterator i = requested_vars.begin(), n = requested_vars.end(); i != n; ++i) {
            boost::property_tree::ptree var;
            var.put(TAG_VAR, i->first);
            var.put(TAG_VALUE, i->second);
            vars.push_back(std::make_pair("", var));
        }
        msg.add_child("vars", vars);
    }
    if (!requested_fids.empty()) {
        ILOG << "allsafe.pending_things()-> sending " << requested_fids.size() << " fids";
        boost::property_tree::ptree fids;
        for (std::map<std::string, std::pair<int, std::string> >::const_iterator i = requested_fids.begin(), n = requested_fids.end(); i != n; ++i) {
            boost::property_tree::ptree fid;
            fid.put(TAG_FID, i->first);
            fid.put(TAG_PID, i->second.first);
            fid.put(TAG_NAME, i->second.second);
            fids.push_back(std::make_pair("", fid));
        }
        msg.add_child("fids", fids);
    }
    if (!directories.empty()) {
        ILOG << "allsafe.pending_things()-> sending " << directories.size() << " directories";
        boost::property_tree::ptree dirs;
        //BOOST_FOREACH(const std::string &s, directories) dirs.push_back(std::make_pair("", s));
        msg.add_child("dirs", dirs);
    }
    if (!req_rules.empty()) {
        ILOG << "allsafe.pending_things()-> sending " << req_rules.size() << " rules";
        boost::property_tree::ptree rules;
        BOOST_FOREACH(const ccrules_exp_t &r, req_rules) {
            boost::property_tree::ptree rule;
            rule.put("p", r.path);
            rule.put("t", r.type);
            rule.put("a", r.arg);
            rules.push_back(std::make_pair("", rule));
        }
        msg.add_child("rules", rules);
    }
}

static int process_response(const std::string &response)
{
    DLOG << "allsafe_t::process_response()->response.size(): " << response.size();
    TLOG << "allsafe_t::process_response()->response: " << response;

    try {
        std::stringstream ss; ss << response;
        boost::property_tree::ptree msg;
        boost::property_tree::read_json(ss, msg);
        if (msg.get<std::string>(TAG_ID) != _allsafe.alias) {
            WLOG << "allsafe_t::process_response()->Bad alias: " << msg.get<std::string>(TAG_ID);
            return -2;
        } else if (msg.get<int>(TAG_STATUS) != 0) {
            WLOG << "allsafe_t::process_response()->Error status: " << msg.get<int>(TAG_STATUS);
            return -3;
        } else {
            BOOST_FOREACH( boost::property_tree::ptree::value_type const& v, msg.get_child(TAG_ACTIONS) ) {
                try {
                    allsafe_dispatch_action(v.second.get<std::string>(TAG_ID), v.second);
                } catch (std::exception &e) {
                    WLOG << "allsafe_t::process_response()->Processing action: " << e.what();
                }
            }
            if (save_dictionary) { dict_save(); save_dictionary = false; }
            if (save_dirs) { dirs_save(); save_dirs = false; }
            if (save_rules) { ccrules_save(); save_rules = false; }
        }
    } catch (std::exception &e) {
        ELOG << "allsafe_t::process_response()->Parsing: " << e.what();
        return -1;
    }
    return 0;
}


static unsigned short res_means[8];
static size_t fail_cnt = 0;

void allsafe_thread()
{
    for (;;) try {
    {
        boost::mutex::scoped_lock scoped_lock(allsafe_th_mtx);
        allsafe_th_cond.wait(scoped_lock);
    }
    DLOG << "allsafe_thread()-> awake";
    std::list<boost::shared_ptr<fentry_t> > flist;
    {
        boost::mutex::scoped_lock scoped_lock(allsafe_files_mtx);
//        flist = allsafe_files_list;
//        allsafe_files_list.clear();
        for (int i = 0; !allsafe_files_list.empty() && i < allsafe_max_send; i++) {
            flist.push_back(allsafe_files_list.front());
            allsafe_files_list.pop_front();
        }
    }
    boost::property_tree::ptree msg;
    msg.put(TAG_ID, _allsafe.alias);
    msg.put(TAG_VER, allsafe_version);

    pending_things(msg);

static const std::string itemna[] = {"CPU","Mem","Swap","DiskR","DiskW","NetR","NetW"};
    for (int j=0; j<7; j++) msg.put(itemna[j], res_means[j]);

    if (flist.size()) {
        boost::property_tree::ptree works;
        BOOST_FOREACH(const boost::shared_ptr<fentry_t> &f, flist) {
            boost::property_tree::ptree work;
            work.put("path", f->fname);
            work.put("fid", ofd_get_magic() + f->sname);
            work.put("date", f->initime);
            work.put("size", f->fsize);
            work.put("event", f->event);
            work.put("err", f->status);
            work.put("psize", f->diff_stats.lsize);
            work.put("ptime", f->diff_stats.ldur);
            works.push_back(std::make_pair("", work));
        }
        msg.add_child("works", works);
    }
    std::stringstream ss;
    boost::property_tree::write_json(ss, msg, false);

    std::string zreq,req = ss.str();
    TLOG << "allsafe_t::run()->request: " << req;
    zlib_def_str(req, zreq);
    DLOG << "allsafe_t::run()->request.size(): " << req.size() << " / " << zreq.size();

    std::string resp,zresp;
    int r = http_client_post(_allsafe.url, zreq, &zresp);
    DLOG << "allsafe_t::run()->http_client_post(): "<<r<<" /zresp.size(): " << zresp.size();
    if (r > 0) { try {
            zlib_inf_str(zresp, resp);
            TLOG << "allsafe_t::run()->response: " << resp;
        } catch (std::exception &e) { r = -1; WLOG << "allsafe_t::run()->" << e.what(); }
    }
    if (r > 0) {
        fail_cnt = 0;
        clear_pendings();
        r = process_response(resp);
    } else {
        fail_cnt++;
        size_t sleep_seconds = fail_cnt*fail_cnt*10; if (sleep_seconds > 300) sleep_seconds = 300;
        if (fail_cnt > 3) WLOG << "allsafe_thread()->http_client_post("<<_allsafe.url<<"): " << fail_cnt << " consecutive fails, waiting " << sleep_seconds << " seconds or more";
        boost::this_thread::sleep(boost::posix_time::seconds(sleep_seconds));
    }
    if (r < 0 && flist.size()) {
        boost::mutex::scoped_lock scoped_lock(allsafe_files_mtx);
        allsafe_files_list.insert(allsafe_files_list.begin(), flist.begin(), flist.end());
    }
    } catch (std::exception &e) {
        ELOG << "allsafe_thread()->" << e.what();
    }
}

extern float *resources_mean();
static size_t allsafe_sec_cur = 1000, allsafe_sec_last = 0;
void allsafe_t::run()
{
    if (_allsafe.mode) {
        allsafe_sec_cur += RES_LOOP_PERIOD;
        size_t seconds = (allsafe_sec_cur - allsafe_sec_last);
        if (seconds > _allsafe.conn_max || (seconds > _allsafe.conn_min && allsafe_files_list.size()) || something_pending()) {
            DLOG << "allsafe_t::run()->seconds: " << seconds;
            allsafe_sec_last = allsafe_sec_cur;
            const float * const means = resources_mean();
            for (int j=0; j<7; j++) res_means[j] = (unsigned short)means[j];
            allsafe_th_cond.notify_all();
        }
    }
}

void allsafe_debug_str(std::stringstream &ss)
{
    ss << "<li><b>allsafe_debug_str()</b></li>";
	ss << "<li>allsafe_version: "<<allsafe_version<<"</li>\n";
	ss << "<li>allsafe_files_list.size(): "<<allsafe_files_list.size()<<"</li>\n";
	ss << "<li>requested_fids.size(): "<<requested_fids.size()<<"</li>\n";
	ss << "<li>save_dictionary: "<<save_dictionary<<"</li>\n";
	ss << "<li>save_dirs: "<<save_dirs<<"</li>\n";
	ss << "<li>allsafe_sec_cur: "<<allsafe_sec_cur<<"</li>\n";
	ss << "<li>allsafe_sec_last: "<<allsafe_sec_last<<"</li>\n";
	ss << "<li>fail_cnt: "<<fail_cnt<<"</li>\n";
}
