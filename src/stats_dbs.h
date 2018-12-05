
#ifndef _STATS_DBS_H
#define _STATS_DBS_H

#include <string>
#include <vector>

void stats_dbs_put(const std::string &dbn, long long b);
std::string stats_dbs_dump(const std::vector<std::string> &vuri);

#endif
