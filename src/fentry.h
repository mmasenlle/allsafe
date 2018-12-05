
#ifndef _FENTRY_H
#define _FENTRY_H

#include "diff_stats.h"

/// Information of a file to be processed
struct fentry_t
{
    std::string fname; /// File path
    std::string sname; /// Short name
    std::string event; /// Event type (NEW,MOD,...)
#ifdef COMP1
    std::string relfp; /// Relative path
    std::string opca;
    std::string ofname;
    std::string path_rev;
    std::string path_base;
    std::string shadow_path;
#endif
    long long initime;
    long long initime_last;
    long long trytime_last;
    long long fsize;
    int count;
#ifdef COMP1
    int revn;
#endif
    int status;
    int nfails;
    diff_stats_t diff_stats;
    fentry_t():fsize(0),
#ifdef COMP1
        revn(1),
#endif
        status(0),nfails(0) {};
};

// fentry_t::status
enum {
    fentry_status_ok = 0,
    fentry_status_no_changes = 1,
    fentry_status_gone = 2,

    fentry_status_no_perm = 10,
    fentry_status_read_error = 11,

    fentry_status_host_connection = 100,
    fentry_status_host_time_out = 101,

    fentry_status_internal_error_backend = 501,
    fentry_status_internal_error_processer = 502,
};

#endif // _FENTRY_H
