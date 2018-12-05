
#ifndef _PROPS_SCHED_H
#define _PROPS_SCHED_H

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include "fentry.h"
#include "sftp_sched.h"
#include "backend.h"
#include "processer.h"

#define RES_LOOP_PERIOD 2

 /// Properties
struct props
{
    std::string tmpd; /// patches directory

    std::string confd_path;
    std::string backend;
    std::string processer;

    std::vector<std::string> dirstowatch;

    static bool update(const std::string &s);
    static void update();
    static const struct props & get();

    static void init_session(sftp_session_t *session);
    //static void fentry_init(servlets &svl_conn, sftp_session_t *session,
      //                  boost::shared_ptr<fentry_t> &fentry);
    static void put(std::vector<std::string> *args, int phase,
               boost::shared_ptr<fentry_t> &fentry);
    static void set_hostip_port(const std::string &h, int port);
    static void set_base_dirs(const std::string &b1, const std::string &b2);
    static void set_user(const std::string &user);
    static std::string debug_str();
    static void load_fluxu(const char *argv0);
    static void load_conf();
    static void init();
    static int add_dir(const std::string &d);
    static void del_dir(int i);
    static std::string oldd_path(const std::string &fpath);
    static std::string normalize_path(std::string fpath);
    static size_t to_seconds(const std::string &fpath); // [[[dd:]hh:]mm:]ss
    static std::string from_seconds(size_t tt);

    bool use_backend() const; // { return (!backend.empty() && backend != "epsilon"); };
    static Backend * createBackend();
    static Processer * createProcesser(int n);

    std::string smd5_path(boost::shared_ptr<fentry_t> &fentry) const;
    std::string smd5_name(const std::string &md5d, const std::string &md5f) const;

    static long long current_time_ms();

};

#endif // _PROPS_SCHED_H
