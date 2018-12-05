
#ifndef _DIFF_STATS_H
#define _DIFF_STATS_H

#include <string>

/// Differentiation statistics for a file
struct diff_stats_t
{
    long long first;
    long long last;
    int cnt;
    int lsize;
    int msize;
    int ldur;
    int mdur;
    diff_stats_t() : first(-1) {}; // para indicar que no se ha cargado
};

extern int diff_stats_load(const std::string &file, diff_stats_t *diff_stats);
extern int diff_stats_sync(const std::string &file, diff_stats_t *diff_stats);
extern int diff_stats_del(const std::string &file);

#endif // _DIFF_STATS_H
