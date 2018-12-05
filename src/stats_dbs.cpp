
#include <map>
#include <list>
#include <string>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include "web_utils.h"

struct stats_db_record_t
{
    long long bytes;
    time_t tstamp;
    stats_db_record_t(long long b, time_t t) : bytes(b), tstamp(t) {};
};
struct stats_db_t
{
    int cnt;
    long long bytes;
    time_t last;
    std::list<stats_db_record_t> records;
    stats_db_t() : cnt(0), bytes(0), last(0) {};
};

static boost::mutex mtx_changed_files, mtx_stats_dbs;
static std::map<std::string, stats_db_t> stats_dbs;


void stats_dbs_put(const std::string &dbn, long long b)
{
    boost::mutex::scoped_lock scoped_lock(mtx_stats_dbs);
    stats_db_t &stats_db = stats_dbs[dbn];
    stats_db.cnt++;
    stats_db.bytes += b;
    stats_db.last = time(NULL);
    if (stats_db.records.size() > 25) stats_db.records.pop_back();
    stats_db.records.push_front(stats_db_record_t(b, stats_db.last));
}

extern void stringstream_bytes(long long bytes, std::stringstream &ss);
std::string stats_dbs_dump(const std::vector<std::string> &vuri)
{
    std::stringstream ss;
    if (vuri.size() > 2) {
        if (vuri[1] == "db") {
            std::string dbn = web_url_decode(vuri[2]);
            ss << "<h3>" << dbn << "</h3><table class=\"gridtable\"><tr><th>Bytes</th><th>Time</th></tr>\n";
            boost::mutex::scoped_lock scoped_lock(mtx_stats_dbs);
            stats_db_t &stats_db = stats_dbs[dbn];
            for (std::list<stats_db_record_t>::iterator i = stats_db.records.begin(), n = stats_db.records.end(); i != n; ++i) {
                ss << "<tr><td>"; stringstream_bytes(i->bytes, ss); ss << "</td><td>"
                 << boost::posix_time::to_simple_string(boost::date_time::c_local_adjustor<boost::posix_time::ptime>::utc_to_local(boost::posix_time::from_time_t(i->tstamp)))
                 << "</td></tr>\n";
            }
            scoped_lock.unlock();
            ss << "</table>\n";
            return ss.str();
        }
    }
    ss << "<table class=\"gridtable\"><tr><th>Nombre</th><th>#</th><th>Bytes</th><th>&Uacute;ltimo</th></tr>\n";
    boost::mutex::scoped_lock scoped_lock(mtx_stats_dbs);
    for (std::map<std::string, stats_db_t>::iterator i = stats_dbs.begin(), n = stats_dbs.end(); i != n; ++i) {
        ss << "<tr><td><a href=\"db_stats?db=" << i->first << "\">" << i->first << "</a></td><td>" << i->second.cnt << "</td><td>";
        stringstream_bytes(i->second.bytes, ss); ss << "</td><td>"
         << boost::posix_time::to_simple_string(boost::date_time::c_local_adjustor<boost::posix_time::ptime>::utc_to_local(boost::posix_time::from_time_t(i->second.last)))
         << "</td></tr>\n";
    }
    scoped_lock.unlock();
    ss << "</table>\n";
    return ss.str();
}
