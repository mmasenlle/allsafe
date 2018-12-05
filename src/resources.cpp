

#include <stdio.h>
#include <sigar.h>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>
#include <boost/foreach.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/asio.hpp>
#include "fstream_utf8.h"
#include <boost/serialization/map.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include "dict.h"
#include "web_utils.h"
#include "props_sched.h"
#include "log_sched.h"


struct process_data_t {
    int k,secs;
    float means[3];
    process_data_t():k(1),secs(0) {};
    void update(float v0,float v1,float v2)
    {
        float a = 1.0/k++;
        if (a < .001) a = .001;
        means[0] = means[0]*(1.0-a) + v0*a;
        means[1] = means[1]*(1.0-a) + v1*a;
        means[2] = means[2]*(1.0-a) + v2*a;
        secs++;
    }
    const std::string uptime() const
    {
        std::stringstream ss;
        int days = secs*RES_LOOP_PERIOD/(24*3600);
        if (days) ss << days << " days ";
        int hours = ((secs*RES_LOOP_PERIOD)%(24*3600))/3600;
        if (days || hours) ss << hours << ":";
        int mins = ((secs*RES_LOOP_PERIOD)%3600)/60;
        if (days || hours || mins) ss << std::setw(2) << std::setfill('0') << mins << ":";
        ss << std::setw(2) << std::setfill('0') << ((secs*RES_LOOP_PERIOD)%60);
        return ss.str();
    }
private:
    // Allow serialization to access non-public data members.
    friend class boost::serialization::access;
    template<class Archive>
//    void serialize(Archive & ar, const unsigned int version)
    void save(Archive & ar, const unsigned int version) const
    {
      ar & k;
      ar & means;
      ar & secs;
    }
    template<class Archive>
    void load(Archive & ar, const unsigned int version)
    {
      ar & k;
      ar & means;
      if (version > 0) ar & secs;
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
};
BOOST_CLASS_VERSION(process_data_t, 1)
struct bytime_data_t {
    float data[8];
    void set(float *d)
    {
        memcpy(data,d,sizeof(data));
    };
    bytime_data_t() { memset(data, 0, sizeof(data)); };
private:
    // Allow serialization to access non-public data members.
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      ar & data;
    }
};

#define PROGRAMS_FNAME "programs.dat"
#define BYTIME_FNAME "bytime.dat"

static boost::mutex mutex;
std::map<int,process_data_t> all_pids;
std::map<std::string,process_data_t> all_programs;
std::set<std::string> programs;
std::map<int,bytime_data_t> bytime;

boost::accumulators::accumulator_set<double,
    boost::accumulators::stats<boost::accumulators::tag::mean,
    boost::accumulators::tag::max,
    boost::accumulators::tag::median,
//    boost::accumulators::tag::moment<2> > > acc[8];
    boost::accumulators::tag::variance > > acc[8];
static float perc[8],mean[8];
int current_h;
static sigar_uint64_t memtotal = 1;
int ncpu = 1;

static sigar_uint64_t last_idle = 0, last_total = 1, maxio = 1;
static sigar_uint64_t last_totr = 0, last_totw = 0, maxreads = 1, maxwrites = 1;
static sigar_uint64_t last_totnr = 0, last_totnw = 0, maxnreads = 1, maxnwrites = 1;
//static char *ndev = "";
//static std::string ndev;
static std::vector<std::string> network_devices;

#ifdef lnx
        static std::string drive = "/";
        static size_t idev = 1;
#else
        static std::string drive = "C:";
        static size_t idev = 6;
#endif // lnx

float *resources_perc()
{
    return perc;
}
float *resources_mean()
{
    return mean;
}
/*
void dump()
{
    printf("\n\n***************************\n****************************\n\n");
    boost::mutex::scoped_lock scoped_lock(mutex);
    printf("Processes (%d):\n", all_pids.size());
    for (std::map<int,process_data_t>::iterator i = all_pids.begin(), n = all_pids.end(); i != n; ++i) {
        printf("  %05d) (%04d) CPU: %7.2f %% Mem: %7.2fMB up %s\n", i->first, i->second.k,
               i->second.means[0], i->second.means[1], i->second.uptime().c_str());
    }
    printf("\nPrograms (%d):\n", all_programs.size());
    for (std::map<std::string,process_data_t>::iterator i = all_programs.begin(), n = all_programs.end(); i != n; ++i) {
        printf(" %s (%04d)\n CPU: %7.2f %% Mem: %7.2fMB up %s\n", i->first.c_str(), i->second.k,
               i->second.means[0], i->second.means[1], i->second.uptime().c_str());
    }
}
*/
bool resch_loaded_now(float threshld)
{
    if (perc[0] > threshld) return true; //se excluye la memoria
    for (int i=2; i<7; i++)
        if (perc[i] > threshld) return true;
    return false;
}
bool resch_loaded(float threshld)
{ //cpu and disk
    if (mean[0] > threshld) return true;
    if (mean[3] > threshld) return true;
    if (mean[4] > threshld) return true;
    return false;
}
bool resch_free(float threshld)
{ //cpu and disk
    if (mean[0] > threshld) return false;
    if (mean[3] > threshld) return false;
    if (mean[4] > threshld) return false;
    return true;
}

extern void epsilon_process_collect_data(int pid, float ct, float iot, int s);
extern std::string sched_pending_jobs_str();
extern std::string sched_job_stats_str();
extern std::string sched_debug_str();
extern std::string epsilon_process_debug_str();
extern std::string fsevent_debug_str();
extern std::string mod_files_debug_str();
extern std::string file_watcher_debug_str();
extern std::string wserver_main_menu();
extern std::string wserver_graph(float *data);
extern std::string sched_inprocess_str();
extern std::string mssql_dump();
extern std::string ptasks_dump();
extern std::string oracle_dump();
extern void ofd_debug_str(std::stringstream &ss);
extern void allsafe_debug_str(std::stringstream &ss);
extern void sched_push_jobs();

#define _str(_n) boost::lexical_cast<std::string>(_n)
std::string perf_graph()
{
    static const std::string colour[] = {"#ff0000","#00ff00","#0000ff","#ffff00","#ff00ff","#00ffff","#ffa500"};
    static const std::string itemna[] = {"CPU","Mem","Swap","DiskR","DiskW","NetR","NetW"};
//    std::string s = wserver_pages_head();
//    s += "<body><h2>Performance</h2>\n<a href=/>home</a>&nbsp;&nbsp;<a href=/menu>menu</a><br/><br/>\n";
    std::string s; // = wserver_ws_title("Performance");  bgcolor=\"#F7FFF7\"
    s += "<table><tr><td class=\"graph perf\"><canvas id=\"canvas_perf_graph\" width=\"750\" height=\"224\">"
        "</canvas></td><td><ul style=\"font:12px Georgia\">\n";
    for (int j=0; j<7; j++) s += "<li style=\"color:"+colour[j]+"\">"+itemna[j]+"</li>\n";
    s += "</ul></td></tr></table><script>\n"
      "var canvas = document.getElementById('canvas_perf_graph');\n"
      "var ctx = canvas.getContext('2d');\n"
       "ctx.beginPath();\nctx.moveTo(30, 0);ctx.lineTo(30,200);ctx.lineTo(740,200);\n"
       "ctx.strokeStyle='#777777';ctx.stroke();\n";
    for (int j=0; j<5; j++) {
       s += "ctx.beginPath(); ctx.moveTo(" + _str(j%2==0?20:25) + "," + _str(j*50) + ");";
        s += "ctx.lineTo(30," + _str(j*50) + ");ctx.stroke();\n";
    }
    for (int j=0; j<24; j++) {
       s += "ctx.beginPath(); ctx.moveTo(" + _str((j*30)+30) + ", 200);";
        s += "ctx.lineTo(" + _str((j*30)+30) + "," + _str(j%6==0?210:205) + ");ctx.stroke();\n";
    }
    for (int j=0; j<7; j++) {
        s += "ctx.beginPath(); ctx.moveTo(30, 200);\n"; int t = 0; //i->first;
        for (std::map<int,bytime_data_t>::iterator i = bytime.begin(), n = bytime.end(); i != n; ++i) {
            float y = i->second.data[j];
            s += "ctx.lineTo(" + _str((t++/2)+30) + "," + _str((int)(200-(2*y))) + ");\n";
        }
        s += "ctx.strokeStyle = '" + colour[j] + "';\nctx.stroke();\n";
    }
    s += "ctx.font=\"10px Georgia\";ctx.fillStyle='#777777';\n"
            "ctx.fillText(\"100%\", 0, 15);ctx.fillText(\"50%\", 0, 115);"
            "ctx.fillText(\"0\", 35, 215);ctx.fillText(\"6\", 215, 215);"
            "ctx.fillText(\"12\", 395, 215);ctx.fillText(\"18\", 575, 215);\n";
    s += "</script>"; // + wserver_pages_tail();
    return s;
}

static const char *ssf(char *buf, float v, const char *fmt = "%06.2f")
{
	sprintf(buf, fmt, v); return buf;
}
std::string sdump(int op)
{
    std::stringstream ss; char buf[256];
    if (op < 10) ss << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"/><title>M U S C H E D</title>\n";
    if (op < 1) ss << "<meta http-equiv=\"refresh\" content=\"3\"/>\n";
    ss << wserver_graph(perc);
    ss << "<th>CPU</th><th>Mem</th><th>Swap</th><th>DiskR</th><th>DiskW</th><th>NetR</th><th>NetW</th></tr>\n"
           "<tr><th>Actual</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, perc[i]) << "</td>";
    ss << "</tr>\n<tr><th>Media 1h</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, mean[i]) << "</td>";
    ss << "</tr>\n<tr><th>Media total</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, boost::accumulators::mean(acc[i])) << "</td>";
    ss << "</tr>\n<tr><th>Desviaci&oacute;n</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, std::sqrt(boost::accumulators::variance(acc[i]))) << "</td>";
    ss << "</tr>\n<tr><th>Mediana</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, boost::accumulators::median(acc[i])) << "</td>";
    ss << "</tr>\n<tr><th>M&aacute;ximo</th>";
    for (int i=0; i<7; i++) ss << "<td>" << ssf(buf, boost::accumulators::max(acc[i])) << "</td>";
    ss << "</tr></table>\n";
    ss << sched_inprocess_str();
//    sbuf += wserver_main_menu();
    if (op == 1) {
            boost::mutex::scoped_lock scoped_lock(mutex);
        ss << "<h3>Programas ("<<all_programs.size()<<"):</h3>";
        for (std::map<std::string,process_data_t>::iterator i = all_programs.begin(); i != all_programs.end(); i++) {
			ss << "[" << std::setw(4) << std::setfill('0') << i->second.k << "] "
				"CPU: " << ssf(buf, i->second.means[0], "%05.2f") << " % "
				"Mem: " << ssf(buf, i->second.means[1], "%08.2f") << "MB"
				" IO: " << ssf(buf, i->second.means[2], "%05.2f") << " %"
				"  up " << i->second.uptime() << " - " << i->first << "<br/>\n";
        }
    } else if (op == 2) {
        boost::mutex::scoped_lock scoped_lock(mutex);
        ss << "<h3>Procesos ("<<programs.size()<<"):</h3>";
        for (std::set<std::string>::iterator it = programs.begin(); it != programs.end(); it++) {
			ss << "[" << std::setw(4) << std::setfill('0') << all_programs[*it].k << "] "
				"CPU: " << ssf(buf, all_programs[*it].means[0], "%05.2f") << " % "
				"Mem: " << ssf(buf, all_programs[*it].means[1], "%08.2f") << "MB"
				" IO: " << ssf(buf, all_programs[*it].means[2], "%05.2f") << " %"
				"  up " << all_programs[*it].uptime() << " - " << *it << "<br/>\n";
        }
    } else if (op == 3) {
        boost::mutex::scoped_lock scoped_lock(mutex);
        ss << "<h3>Horarios ("<<bytime.size()<<"):</h3><table>";
        for (std::map<int,bytime_data_t>::iterator i = bytime.begin(); i != bytime.end(); i++) {
            ss << "<tr><td>" << std::setw(4) << std::setfill('0') << i->first << "</td>";
            for (int j=0; j<7; j++) ss << "<td>" << ssf(buf, i->second.data[j], "%7.2f") << "</td>";
            ss << "</tr>\n";
        }
    } else if (op == 4) {
        double cpu=0,mem=0,io=0;
        int cnt = 0;
        boost::mutex::scoped_lock scoped_lock(mutex);
        for (std::set<std::string>::iterator it = programs.begin(); it != programs.end(); it++) {
            cpu += all_programs[*it].means[0];
            mem += all_programs[*it].means[1];
            io += all_programs[*it].means[2];
        }
        ss << "<h3>Predicci&oacute;n</h3><h4>  CPU: " << ssf(buf, cpu/ncpu, "%7.2f") <<
			" % Mem: " << ssf(buf, mem*1024*102400.0/memtotal, "%7.2f") <<
			" % IO: " << ssf(buf, io, "%05.2f") << " %</h4>";
    } else if (op == 5) {
        ss << "<h3>Estado de variables:</h3><ul>";
        ss << "<li>current_h: "<<current_h<<"</li>\n";
		ss << "<li>memtotal: "<<memtotal<<"</li>\n";
		ss << "<li>last_idle: "<<last_idle<<"</li>\n";
		ss << "<li>last_total: "<<last_total<<"</li>\n";
		ss << "<li>maxio: "<<maxio<<"</li>\n";
		ss << "<li>last_totr: "<<last_totr<<"</li>\n";
		ss << "<li>last_totw: "<<last_totw<<"</li>\n";
		ss << "<li>maxreads: "<<maxreads<<"</li>\n";
		ss << "<li>maxwrites: "<<maxwrites<<"</li>\n";
		ss << "<li>last_totnr: "<<last_totnr<<"</li>\n";
		ss << "<li>last_totnw: "<<last_totnw<<"</li>\n";
		ss << "<li>maxnreads: "<<maxnreads<<"</li>\n";
		ss << "<li>maxnwrites: "<<maxnwrites<<"</li>\n";
		ss << "<li>drive: "<<drive<<"</li>\n";
		ss << "<li>idev: "<<idev<<"</li>\n";
        for(int i = 0; i < network_devices.size(); i++)
            ss << "<li>network_device["<<i<<"]: "<<network_devices[i]<<"</li>\n";
		ss << "<li>log_trace_level: "<<log_trace_level<<"</li>\n";
        ss << sched_debug_str();
#ifdef COMP1
        ss << epsilon_process_debug_str();
#endif
        ss << props::debug_str();
		ofd_debug_str(ss);
		allsafe_debug_str(ss);
        ss << mod_files_debug_str();
        ss << file_watcher_debug_str();
        ss << ptasks_dump();
#ifdef COMP1
        ss << mssql_dump();
        ss << oracle_dump();
#endif
#ifdef WIN32
//        sbuf += fsevent_debug_str();
#endif // WIN32
        ss << "</ul>";
    } else if (op == 6) {
        ss << sched_pending_jobs_str();
/*    } else if (op == 7) {
        sbuf += sched_job_stats_str();
*/    }
    if (op < 10) ss << "</body></html>";
    return ss.str();
}

void save_data()
{
    DLOG << "resources::save_data()";
    boost::mutex::scoped_lock scoped_lock(mutex);
    {ofstream_utf8 ofs((props::get().confd_path + BYTIME_FNAME).c_str());
    if (ofs) {
        boost::archive::text_oarchive ar(ofs);
        ar & bytime;
        ILOG << "Saving file (" << bytime.size() << ") ->" << (props::get().confd_path + BYTIME_FNAME);
    }}
#ifdef COMP1
    {ofstream_utf8 ofs((props::get().confd_path + PROGRAMS_FNAME).c_str());
    if (ofs) {
        boost::archive::text_oarchive ar(ofs);
        ar & all_programs;
        ILOG << "Saving file (" << all_programs.size() << ") ->" << (props::get().confd_path + PROGRAMS_FNAME);
    }}
#endif
}

extern void wmi__preload();

extern int wserver_debug_state;
bool resch_stop = false;
void resched()
{
    int status,i,maxpc = 0,j=0,k=1;
    sigar_t *sg = NULL;
    sigar_proc_list_t list;

    dict_set("resources.idev", &idev);
//    dict_set("resources.ndev", &ndev);
    dict_set("resources.drive", &drive);


    sigar_open(&sg);
    DLOG << "sigar_open()->sg: " << sg;

    sigar_net_interface_list_t nl;
    status = sigar_net_interface_list_get(sg, &nl);
    if (status != SIGAR_OK) {
        WLOG << "sigar_net_interface_list_get()->ERROR: " << status;
    } else {
        TLOG << "sigar_net_interface_list_get(): " << nl.number;
        for (i=0; i<nl.number; i++) {
            sigar_net_interface_config_t ncfg;
            TLOG << "sigar_net_interface_list_get("<<i<<").name: " << nl.data[i];
            sigar_net_interface_config_get(sg, nl.data[i], &ncfg);
            TLOG << "interface("<<i<<").name: "<<nl.data[i]<<" - " << ncfg.description;
            network_devices.push_back(nl.data[i]);
        }
//        ndev = nl.data[idev];
    }
    sigar_cpu_list_t cpul;
    status = sigar_cpu_list_get(sg, &cpul);
    if (status != SIGAR_OK) {
        WLOG << "sigar_cpu_list_get()->ERROR: " << status;
    } else {
        ILOG << "sigar_cpu_list_get(): " << cpul.number;
        ncpu = cpul.number;
    }
    DLOG << "resources::load_data()";
    try {ifstream_utf8 ifs((props::get().confd_path + BYTIME_FNAME).c_str());
    if (ifs) {
        boost::archive::text_iarchive ar(ifs);
        ar & bytime;
        ILOG << "Loading file (" << bytime.size() << ") ->" << (props::get().confd_path + BYTIME_FNAME);
    }} catch (std::exception &e) {
        ELOG << "Exception '" << e.what() << "' loading file " << (props::get().confd_path + BYTIME_FNAME);
    }
#ifdef COMP1
    try {ifstream_utf8 ifs((props::get().confd_path + PROGRAMS_FNAME).c_str());
    if (ifs) {
        boost::archive::text_iarchive ar(ifs);
        ar & all_programs;
        ILOG << "Loading file (" << all_programs.size() << ") ->" << (props::get().confd_path + PROGRAMS_FNAME);
    }} catch (std::exception &e) {
        ELOG << "Exception '" << e.what() << "' loading file " << (props::get().confd_path + PROGRAMS_FNAME);
    }
#endif
    for (int i = 0; i < 24; i++) for (int j = 0; j < 60; j++) bytime[i*100+j];

    std::set<int> pids;

    sigar_mem_t mem;
    sigar_swap_t swap;
//    sigar_cpu_list_t cpul;
    sigar_disk_usage_t du;
    sigar_net_interface_stat_t ns;

#ifdef WIN32
#ifdef GETPROCNAME
  wmi__preload();
#endif
// Se usa para precargar dlls de wmi y que no se esten cargando y descargando todo
// el rato. De todos modos, la funcion que lo usa sigar_proc_exe_get() parece que
// pierde memoria
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif // WIN32


    while (sg && !resch_stop) {
        const boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
        int hm = now.time_of_day().hours() * 100 + now.time_of_day().minutes();
        if (!current_h) {
            current_h = hm;
        } else if (current_h != hm) {
            bytime[current_h].set(mean);
            current_h = hm;
        }


        if (sigar_mem_get(sg, &mem) == SIGAR_OK) {
            perc[1] = mem.used*100.0/mem.total;
            memtotal = mem.total;
        }
        if (sigar_swap_get(sg, &swap) == SIGAR_OK) {
            perc[2] = swap.used*100.0/swap.total;
        }

        if (sigar_cpu_list_get(sg, &cpul) == SIGAR_OK) {
            sigar_uint64_t idle_acum = 0, total_acum = 0;
            for (int i = 0; i < cpul.number; i++) {
                idle_acum += cpul.data[i].idle;
                total_acum += cpul.data[i].total;
            }
            sigar_uint64_t idle = idle_acum > last_idle ? idle_acum - last_idle : 0;
            sigar_uint64_t total = total_acum > last_total ? total_acum - last_total : 0;
            last_idle = idle_acum;
            last_total = total_acum;
            perc[0] = 100.0 - (idle*100.0/total);
            sigar_cpu_list_destroy(sg, &cpul);
        }
        if (sigar_disk_usage_get(sg, drive.c_str(), &du) == SIGAR_OK) {
            //printf("sigar_disk_usage_get(): %f\n", du.queue);
            sigar_uint64_t r = du.read_bytes > last_totr ? du.read_bytes - last_totr : 0;
            if (last_totr == 0) {
                r = 0;
            }
            sigar_uint64_t w = du.write_bytes > last_totw ? du.write_bytes - last_totw : 0;
            if (last_totw == 0) {
                w = 0;
            }
            last_totr = du.read_bytes;
            last_totw = du.write_bytes;
            if (r > maxreads) {
                maxreads = r;
            }
            if (w > maxwrites) {
                maxwrites = w;
            }
            if (r) perc[3] = r*100.0/maxreads;
            if (w) perc[4] = w*100.0/maxwrites;

            if (r+w > maxio) maxio = r+w;
        }
        if (idev < network_devices.size()) {
        if ((status = sigar_net_interface_stat_get(sg, network_devices[idev].c_str(), &ns)) == SIGAR_OK) {
            sigar_uint64_t r = ns.rx_bytes > last_totnr ? ns.rx_bytes - last_totnr : 0;
            if (last_totnr == 0) {
                r = 0;
            }
            sigar_uint64_t w = ns.tx_bytes > last_totnw ? ns.tx_bytes - last_totnw : 0;
            if (last_totnw == 0) {
                w = 0;
            }
            last_totnr = ns.rx_bytes;
            last_totnw = ns.tx_bytes;
            if (r > maxnreads) {
                maxnreads = r;
            }
            if (w > maxnwrites) {
                maxnwrites = w;
            }
            if (r) perc[5] = r*100.0/maxnreads;
            if (w) perc[6] = w*100.0/maxnwrites;
        } else {
            WLOG << "sigar_net_interface_stat_get(" << network_devices[idev] << ")->ERROR: " << status;
        }
        } else {
        WLOG << "network device configured: " << idev << ", max: " << network_devices.size();
        }

        float a = 1.0/k++;
        if (a < .001) {
            a = .001;
        }
        for (i = 0; i < 8; i++) {
            mean[i] = mean[i]*(1.0-a) + perc[i]*a;
            acc[i](perc[i]);
        }

        status = sigar_proc_list_get(sg, &list);
        if (status != SIGAR_OK) {
            WLOG << "sigar_proc_list_get()->ERROR: " << status;
        } else {
            std::set<int> current_pids = pids;
            std::set<std::string> current_progs = programs;
            if (list.number > maxpc) {
                maxpc = list.number;
            }
//        printf("sigar_proc_list_get()->OK: %x list.number: %d, list.size: %d\n", status, list.number, list.size);
//        printf("Number of processes: %d (%d)\n", list.number, maxpc);
            for (i=0; i<list.number; i++) {
                bool is_new = false;
                std::string s = "UNKNOWN";
                sigar_proc_cpu_t pcpu;
                sigar_proc_exe_t pexe;
                sigar_proc_mem_t pmem;
                sigar_proc_disk_io_t pio;
//           printf("Found PID: %d\n", list.data[i]);

                if (pids.find(list.data[i]) == pids.end()) {
                    //              printf("New process %d\n", list.data[i]);
                    pids.insert(list.data[i]);
//               all_pids.insert(list.data[i]);
                    is_new = true;
                }
                current_pids.erase(list.data[i]);

                status = sigar_proc_cpu_get(sg, list.data[i], &pcpu);
                if (status != SIGAR_OK) {
                    continue;
                } else {
//                if (!is_new && pcpu.percent < .05) continue; // less printing
//               printf("%d) PID: %d\n", i, list.data[i]);
                    //             printf(" CPU: %7.2f %% (%llu/%llu)\n", pcpu.percent*100, pcpu.start_time, pcpu.last_time);
                }
#ifdef GETPROCNAME
                /*  Esta funcion casca a veces  */
                status = sigar_proc_exe_get(sg, list.data[i], &pexe);
                if (status != SIGAR_OK) {
 //                   printf("sigar_proc_exe_get()->ERROR: %x\n", status);
                    s = "UNKNOWN";
                } else {
                    s = pexe.name;
                }
                if (programs.find(s) == programs.end()) {
                    boost::mutex::scoped_lock scoped_lock(mutex);
                    programs.insert(s);
                    if (all_programs.find(s) == all_programs.end()) all_programs[s];
                    is_new = true;
                }
                current_progs.erase(s);
#endif // GETPROCNAME

                status = sigar_proc_mem_get(sg, list.data[i], &pmem);
                if (status != SIGAR_OK) {
                    if (status != 40001) WLOG << "sigar_proc_mem_get()->ERROR: " << status;
                } else {
//                printf(" Mem: %7.2fMB %llu %llu\n", pmem.size/(1024.0*1024), pmem.resident, pmem.share);
                }
                if (is_new || pcpu.percent > .15) {
//               printf("%d) PID: %lld %s\n", i, list.data[i], s.c_str());
//               printf(" CPU: %7.2f %% Mem: %7.2fMB\n", pcpu.percent*100, pmem.size/(1024.0*1024));
                }
#ifdef WIN32
                status = sigar_proc_disk_io_get(sg, list.data[i], &pio); //SIGSEGV en linux
                if (status != SIGAR_OK) {
//                    WLOG << "sigar_proc_disk_io_get()->ERROR: " << status;
//A lot of noise in linux when not root
                }
                else {
                    if (pio.bytes_total > 10000) {
//                    printf(" IO: %llu %llu %llu  %s\n", pio.bytes_read, pio.bytes_written, pio.bytes_total, s.c_str());
                    TLOG << " IO: " << pio.bytes_read << " " << pio.bytes_written << " "  << pio.bytes_total << "  " << s;
                    }
                }
#endif
 //               all_pids[list.data[i]].update(pcpu.percent*100.0, pmem.size/(1024.0*1024));
 //               if (s.size()) {
 #ifdef COMP1
                all_programs[s].update(pcpu.percent*100.0, pmem.size/(1024.0*1024), pio.bytes_total>maxio?100.0:pio.bytes_total*100.0/maxio);
                epsilon_process_collect_data(list.data[i], pcpu.percent*100.0, pio.bytes_total>maxio?100.0:pio.bytes_total*100.0/maxio, RES_LOOP_PERIOD);
#endif
 //               }

            }
//        printf("------ dead %d\n", current_pids.size());
            for (std::set<int>::iterator it = current_pids.begin(); it != current_pids.end(); it++) {
                pids.erase(*it);
                TLOG << "Process " << *it << " missing";
            }
            for (std::set<std::string>::iterator it = current_progs.begin(); it != current_progs.end(); it++) {
                boost::mutex::scoped_lock scoped_lock(mutex);
                programs.erase(*it);
                TLOG << "Process " << *it << " missing";
//                printf("Process %s missing\n", it->c_str());
            }
            sigar_proc_list_destroy(sg, &list);
        }
        boost::posix_time::time_duration et = boost::posix_time::microsec_clock::local_time() - now;
//        std::cout << et; //(boost::posix_time::microsec_clock::local_time() - now);
        char sbuf[256];
        sprintf(sbuf, " %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f",
               perc[0], perc[1], perc[2], perc[3], perc[4], perc[5], perc[6]);
        DLOG << et << sbuf;

    sched_push_jobs();
//if (wserver_debug_state) printf("\n**** wserver_debug_state: %d !!!\n\n", wserver_debug_state);
    boost::posix_time::time_duration ts = boost::posix_time::seconds(RES_LOOP_PERIOD) - et;
    if (ts > boost::posix_time::milliseconds(0)) {
            DLOG << "Sleeping " << ts;
            boost::this_thread::sleep(ts);
    }
 //       boost::this_thread::sleep(boost::posix_time::seconds(RES_LOOP_PERIOD));
//        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }
    sigar_close(sg);
}

