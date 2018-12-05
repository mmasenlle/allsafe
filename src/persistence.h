
#include <string>
#include <vector>

int persistence_init();
int persistence_end();
#ifndef NDEBUG // stats.db staff, not very useful
int persistence_insert(const std::string &fpath, int phase, int tt, int cpu, int io, int net, int ps);
std::string persistence_dump0(int op);
std::string persistence_dump1(int phase, int op);
std::string persistence_stats();
std::string diff_stats_dump(int order);
#else
#define persistence_insert(...) (0)
#define persistence_dump0(...) ("")
#define persistence_dump1(...) ("")
#define persistence_stats(...) ("")
#define diff_stats_dump(...) ("")
#endif

std::vector<std::string> deleted_get(time_t tstamp);
int deleted_put(const std::string &fname, time_t tstamp);
int deleted_del(const std::string &fname);
std::string md5(const std::string &str);
