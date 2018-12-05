
/**
*   Gestiona la ejecucion de las diferentes fases del backup
*/

#include <list>
#include <string>
#include <sstream>
#include <set>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/log/sources/channel_logger.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/atomic.hpp>
#include <boost/scoped_ptr.hpp>
#include "fstream_utf8.h"
#include "persistence.h"
#include "sched_queues.h"
#include "props_sched.h"
#include "prios.h"
#include "log_sched.h"
#include "cc_rules.h"
#include "dict.h"
#include "web_utils.h"
#include "allsafe.h"


/// order by file name
struct shfentry_less {
  bool operator() (const boost::shared_ptr<fentry_t>& lhs,
                   const boost::shared_ptr<fentry_t>& rhs) const
  {return lhs->fname<rhs->fname;}
};

static boost::mutex mtx_jobs, mtx_eraser, mtx_blist, mtx_thfpath, mtx_lasts, mtx_fshdw;
static boost::condition_variable cond_jobs, cond_eraser;
static std::list<std::string> files_to_erase;
static std::list<std::string> lasts_files;
static std::list<boost::shared_ptr<fentry_t> > pending_jobs;
static std::set<std::string> black_list;
typedef std::set<boost::shared_ptr<fentry_t>, shfentry_less > in_list_t;
static in_list_t in_queue_jobs;
static in_list_t in_process_jobs;
static std::map<char, in_list_t > for_shadow_jobs;

bool volatile squeues_stop = false;
bool volatile stop_eraser = false;
bool pausa = false;
int nthreads = 7;
static int cnthreads = 0;
size_t def_prio = 0;
static volatile bool shadows_running = false;

static boost::posix_time::ptime sched_t0;
static boost::atomic<int> nfcnt(0);
static boost::atomic<int> mfcnt(0);
static boost::atomic<int> dfcnt(0);
static boost::atomic<int> ercnt(0);
static boost::atomic<long long> ibytes(0);
static boost::atomic<long long> obytes(0);

static int processing_new_cnt = 0;
static int processing_mod_cnt = 0;

//constantes
static size_t min_threads = 2;
static size_t max_threads = 8;
static size_t hold_by_perc = 100;
static size_t low_mean_perc = 30;
static size_t high_mean_perc = 60;
static size_t deleted_secs = 300*24*3600;


#define MAXTHN 64
static thstate_t thstate[MAXTHN];
static const std::string *thfpath[MAXTHN];

extern void wserver_notify_event();

// exported
void squeues_sync_stats(boost::shared_ptr<fentry_t> &fentry)
{
    if (fentry->diff_stats.msize <= 0) fentry->diff_stats.msize = fentry->diff_stats.lsize;
    else fentry->diff_stats.msize = (fentry->diff_stats.msize*(fentry->diff_stats.cnt-1.0) + fentry->diff_stats.lsize)/fentry->diff_stats.cnt;
    if (fentry->diff_stats.mdur <= 0) fentry->diff_stats.mdur = fentry->diff_stats.ldur;
    else fentry->diff_stats.mdur = (fentry->diff_stats.mdur*(fentry->diff_stats.cnt-1.0) + fentry->diff_stats.ldur)/fentry->diff_stats.cnt;
    diff_stats_sync(fentry->sname.c_str(), &fentry->diff_stats);
}
void squeues_set_thstate(int thn, thstate_t state)
{
    thstate[thn] = state;
    wserver_notify_event();
}
void squeues_put_pending(boost::shared_ptr<fentry_t> &fentry)
{
    boost::mutex::scoped_lock scoped_lock(mtx_jobs);
    if (in_queue_jobs.find(fentry) == in_queue_jobs.end()) {
        pending_jobs.push_back(fentry);
        in_queue_jobs.insert(fentry);
    }
}
void squeues_put_blacklist(boost::shared_ptr<fentry_t> &fentry)
{
    boost::mutex::scoped_lock bl_scoped_lock(mtx_blist);
    black_list.insert(fentry->fname);
}
void squeues_refish(boost::shared_ptr<fentry_t> &fentry, int tries)
{
    if (fentry->nfails > tries) {
#ifndef WIN32
        squeues_put_blacklist(fentry);
#else
        boost::mutex::scoped_lock scoped_lock(mtx_fshdw);
        for_shadow_jobs[fentry->fname[0]].insert(fentry);
#endif
    } else {
        fentry->nfails++;
        fentry->trytime_last = props::current_time_ms();
        squeues_put_pending(fentry);
    }
}
long long  squeues_get_file_size(const std::string &fname)
{
    long long n = -1;
    try {
        n = boost::filesystem::file_size(fname);
    } catch(std::exception &e) {
        WLOG << "get_file_size("<<fname<<"):" << e.what();
    }
    return n;
}
void squeues_rm_file(const std::string &fpath)
{
    try {
        if (boost::filesystem::is_regular_file(fpath)) {
            boost::mutex::scoped_lock scoped_lock(mtx_eraser);
            files_to_erase.push_back(fpath);
            cond_eraser.notify_all();
        }
    } catch(const boost::filesystem::filesystem_error& e) {
        WLOG << "squeues_rm_file(" << fpath << "): " << e.what();
    }
}
void squeues_try_rm_file(const std::string &fpath)
{
    try {
        boost::filesystem::remove(fpath);
    } catch (const boost::filesystem::filesystem_error& e) {
        WLOG << "squeues_try_rm_file(" << fpath << "): " << e.what();
    }
}
void squeues_add_obytes(long bytes)
{
    obytes += bytes;
}

void sched_counters_update(const boost::shared_ptr<fentry_t>& fentry)
{
    if (fentry->status > 0) return; // refishing
    if (fentry->status) {
        if (fentry->nfails < 2 && fentry->event != "DEL") ercnt++;
    } else if (fentry->event == "DEL") {
        dfcnt++;
    } else {
        if (fentry->event == "NEW") nfcnt++;
        else if (fentry->event == "MOD") mfcnt++;
        else return; // ignore RM1
        if (fentry->fsize == 0) fentry->fsize = squeues_get_file_size(fentry->fname);
        ibytes += fentry->fsize;
    }
}

std::string sched_dump_lasts()
{
    std::string s = "<table class=\"in_process\">";
    boost::mutex::scoped_lock scoped_lock(mtx_lasts);
    BOOST_FOREACH(const std::string &f, lasts_files) {
        s += f;
    }
    s += "</table><br/>\n";
    return s;
}
static void sched_set_last(const boost::shared_ptr<fentry_t> &fentry)
{
    std::string s = "<tr><td>[" +
        boost::posix_time::to_simple_string(boost::date_time::c_local_adjustor<boost::posix_time::ptime>::utc_to_local(boost::posix_time::from_time_t(fentry->initime_last/1000)));
    s += "]</td><td>(" + boost::lexical_cast<std::string>(fentry->status) +
        ")</td><td><a href=\"/explore?f=" +
        ((fentry->fname[0] == '/')? fentry->fname.substr(1):fentry->fname) +
        "\">" + fentry->fname + "</a></td></tr>\n";
    boost::mutex::scoped_lock scoped_lock(mtx_lasts);
    if (lasts_files.size() > 36) {
        lasts_files.pop_back();
    }
    lasts_files.push_front(s);
}

extern int mod_files_not_empty();
extern int mod_files_not_empty_n();
extern int file_watcher_not_empty();
extern int file_watcher_not_empty_n();
void stringstream_bytes(long long bytes, std::stringstream &ss)
{
    if (bytes > 10000000000) ss << bytes/(1024*1024*1024) << "GB";
    else if (bytes > 10000000) ss << bytes/(1024*1024) << "MB";
    else if (bytes > 10000) ss << bytes/(1024) << "KB";
    else ss << bytes << "B";
}
std::string sched_counters_str()
{
    std::stringstream ss;
    ss << "&nbsp;&nbsp;" << (boost::posix_time::second_clock::local_time() - sched_t0)
        << " " << nfcnt + mfcnt + dfcnt + ercnt << " (" << nfcnt << "/" << mfcnt << "/"
        << dfcnt << "/" << ercnt << ") ";
    stringstream_bytes(ibytes, ss); ss << "=>"; stringstream_bytes(obytes, ss);
//    if (ibytes > 10000000000) ss << ibytes/(1024*1024*1024) << "GB=>";
//    else if (ibytes > 10000000) ss << ibytes/(1024*1024) << "MB=>";
//    else if (ibytes > 10000) ss << ibytes/(1024) << "KB=>";
//    else ss << ibytes << "B=>";
//    if (obytes > 10000000) ss << obytes/(1024*1024) << "MB";
//    else if (obytes > 10000) ss << obytes/(1024) << "KB";
//    else ss << obytes << "B";

    int n1 = mod_files_not_empty();
    int n2 = mod_files_not_empty_n();
    int n3 = file_watcher_not_empty();
    int n4 = file_watcher_not_empty_n();
    int n5 = pending_jobs.size();
    int n6 = black_list.size();

    ss << " [" << (n1+n2+n3+n4+n5) << " " << n2 << "/" << n4 << "/" << n1
        << "/" << n3 << "/" << n5 << "/" << n6 << "]&nbsp;&nbsp;&nbsp;" << nthreads
        << " (" << processing_new_cnt << "/" << processing_mod_cnt << ")"
        << std::endl;
    return ss.str();
}
void sched_counters_clear()
{
    sched_t0 = boost::posix_time::second_clock::local_time();
    nfcnt = 0;
    mfcnt = 0;
    ercnt = 0;
    dfcnt = 0;
    ibytes = 0;
    obytes = 0;
}

static const char *sth_str[] = {"","DIFF","SYNC","SEND","CHEC","COPY","POST","INIT","ERRO","DELE","ZIP "};
std::string sched_inprocess_str()
{
    std::string s = sched_counters_str() +
        "<br/><br/><table class=\"in_process\">";
    char buf[256]; int n, color = 0;
    for (int i = 0; i < MAXTHN; i++) {
        boost::mutex::scoped_lock scoped_lock(mtx_thfpath);
        if (thstate[i] && thfpath[i]) {
            if (color) s += "<tr bgcolor=\"#EEEEEE\">"; else s += "<tr>"; color = !color;
            n = sprintf(buf, "<td>%02d)&nbsp;<b>%s</b>&nbsp;&nbsp;", i, sth_str[thstate[i]]);
            s.append(buf, n); s += *thfpath[i]; s += "</td></tr>\n";
        }
    }
    s += "</table>";
    return s;
}

std::string sched_debug_str()
{
	std::stringstream ss;
    ss << "<li><b>sched_debug_str()</b></li>";
    ss << "<li>pending_jobs.size(): "<<pending_jobs.size()<<"</li>\n";
	ss << "<li>squeues_stop: "<<squeues_stop<<"</li>\n";
	ss << "<li>stop_eraser: "<<stop_eraser<<"</li>\n";
	ss << "<li>pausa: "<<pausa<<"</li>\n";
	ss << "<li>nthreads: "<<nthreads<<"</li>\n";
	ss << "<li>cnthreads: "<<cnthreads<<"</li>\n";
	ss << "<li>processing_new_cnt: "<<processing_new_cnt<<"</li>\n";
	ss << "<li>processing_mod_cnt: "<<processing_mod_cnt<<"</li>\n";
	ss << "<li>log_trace_level: "<<log_trace_level<<"</li>\n";
	ss << "<li>shadows_running: "<<shadows_running<<"</li>\n";
    return ss.str();
}

std::string sched_pending_jobs_str()
{
    std::stringstream ss;
    ss << "<h3>Pending jobs ("<<pending_jobs.size()<<"):</h3>\n";
    if (mtx_jobs.try_lock()) {
        BOOST_FOREACH(const boost::shared_ptr<fentry_t> &j, pending_jobs) {
            ss << j->fname << "<br/>\n";
        }
        ss << "- - - - - - - - - - - - - -<br/>\n";
        BOOST_FOREACH(const boost::shared_ptr<fentry_t> &j, in_process_jobs) {
            ss << j->fname << "<br/>\n";
        }
        mtx_jobs.unlock();
    }
    ss << "<h3>Pending for shadow ("<<for_shadow_jobs.size()<<"):</h3>\n";
    if (mtx_fshdw.try_lock()) {
        for (std::map<char, in_list_t >::iterator i = for_shadow_jobs.begin(), n = for_shadow_jobs.end(); i != n; ++i) {
            BOOST_FOREACH (const boost::shared_ptr<fentry_t> &j, i->second) {
                ss << j->fname << "<br/>\n";
            }
        }
        mtx_fshdw.unlock();
    }
    ss << "<h3>Pending to erase ("<<files_to_erase.size()<<"):</h3>\n";
    if (mtx_eraser.try_lock()) {
        BOOST_FOREACH(const std::string &f, files_to_erase) {
            ss << f << "<br/>\n";
        }
        mtx_eraser.unlock();
    }
    ss << "<h3>Black list ("<<black_list.size()<<"):</h3>\n";
    if (mtx_blist.try_lock()) {
        BOOST_FOREACH(const std::string &f, black_list) {
            ss << f << "<br/>\n";
        }
        mtx_blist.unlock();
    }
    return ss.str();
}

static bool is_in_black_list(const boost::shared_ptr<fentry_t> &fentry)
{
    boost::mutex::scoped_lock scoped_lock(mtx_blist);
    return (black_list.find(fentry->fname) != black_list.end());
}

static void eraser()
{
    int nfiles = 0;
    while (!stop_eraser) {
        boost::mutex::scoped_lock scoped_lock(mtx_eraser);
        if ((nfiles = files_to_erase.size())) {
            std::string fpath = files_to_erase.front();
            files_to_erase.pop_front();
            scoped_lock.unlock();
            try {
                boost::filesystem::remove(fpath);
            } catch (const boost::filesystem::filesystem_error& e) {
                WLOG << "eraser()->Error removing files '" << fpath << "' " << e.code().message();
                if (nfiles < 3) boost::this_thread::sleep(boost::posix_time::seconds(5));
                else boost::this_thread::sleep(boost::posix_time::seconds(1));
                scoped_lock.lock();
                files_to_erase.push_back(fpath);
            }
        } else {
            cond_eraser.wait(scoped_lock);
        }
    }
}

static boost::log::sources::channel_logger_mt<>
    lg_ok(boost::log::keywords::channel = "lg_ok"),
    lg_err(boost::log::keywords::channel = "lg_err");

/// Adapter for a fentry in progress
struct in_process_job_t
{
    int n;
    const boost::shared_ptr<fentry_t> &fentry;
    in_process_job_t(int i, const boost::shared_ptr<fentry_t> &f) : n(i), fentry(f) {
        mtx_thfpath.lock();
        thfpath[n] = &f->fname;
        thstate[n] = sth_init;
        mtx_thfpath.unlock();
        in_process_jobs.insert(fentry);
        if (fentry->event == "NEW") processing_new_cnt++; else processing_mod_cnt++;
        wserver_notify_event();
    };
    ~in_process_job_t() {
    if (fentry->status < 1000) {
        sched_counters_update(fentry);
        allsafe_t::push(fentry);
        sched_set_last(fentry);
        if (fentry->status) {
            BOOST_LOG(lg_err) << "Error("<<fentry->status<<") in '" << fentry->fname << "'";
        } else {
            BOOST_LOG(lg_ok) << "Done("<<fentry->event<<") '" << fentry->fname << "'";
        }
    }
        mtx_thfpath.lock();
        thstate[n] = sth_nul;
        thfpath[n] = NULL;
        mtx_thfpath.unlock();
    boost::mutex::scoped_lock scoped_lock(mtx_jobs);
        in_process_jobs.erase(fentry);
        if (fentry->event == "NEW") processing_new_cnt--; else processing_mod_cnt--;
        wserver_notify_event();
    }
};

extern int ncpu;
extern boost::shared_ptr<fentry_t> mod_files_get_next();
extern boost::shared_ptr<fentry_t> mod_files_get_next_n();
extern boost::shared_ptr<fentry_t> file_watcher_get_next();
extern boost::shared_ptr<fentry_t> file_watcher_get_next_n();

static size_t delay_ms = 5000;
static boost::shared_ptr<fentry_t> pending_get_next()
{
    long long ctms = props::current_time_ms();
    for (std::list<boost::shared_ptr<fentry_t> >::iterator i = pending_jobs.begin(),
          n = pending_jobs.end(); i != n; ++i) {
        if (((*i)->trytime_last + delay_ms) < ctms || (*i)->trytime_last > ctms) {
            if (!ccrules_hold(*i)) {
                if (in_process_jobs.find(*i) == in_process_jobs.end()) {
                    boost::shared_ptr<fentry_t> f = *i;
                    in_queue_jobs.erase(f);
                    pending_jobs.erase(i);
                    return f;
                }
            }
        }
    }
    return boost::shared_ptr<fentry_t>();
}

#ifdef COMP1
extern volatile int epsilon_process_srv_timeout_cnt;
#endif
static int round_robin = -1;
static boost::shared_ptr<fentry_t> get_next_fentry()
{
#ifdef COMP1
    if (epsilon_process_srv_timeout_cnt > 10 && pending_jobs.size() > 20) {
        return pending_get_next();
    }
#endif
    boost::shared_ptr<fentry_t> fe;
    for (int i = 0; i < 2; i++) {
        round_robin = (round_robin + 1) % 5;
        switch (round_robin) {
        case 0: if (!pending_jobs.empty() && (fe = pending_get_next())) return fe; else round_robin = 1;
        case 1: if (mod_files_not_empty() && (fe = mod_files_get_next())) return fe; else round_robin = 2;
        case 2: if (file_watcher_not_empty() && (fe = file_watcher_get_next())) return fe; else round_robin = 3;
        case 3: if (mod_files_not_empty_n() && (fe = mod_files_get_next_n())) return fe; else round_robin = 4;
        case 4: if (file_watcher_not_empty_n() && (fe = file_watcher_get_next_n())) return fe;
        }
    }
    return fe;
}
static bool check_fentry_exists_file(boost::shared_ptr<fentry_t> &fentry)
{
    try {
        if (boost::filesystem::is_regular_file(fentry->fname)) {
            return true;
        }
    } catch(const boost::filesystem::filesystem_error& e) {
        WLOG << "check_fentry_exists_file(" << fentry->fname << ")->" << e.code().message();
    }
    return false;
}
static bool check_fentry(boost::shared_ptr<fentry_t> &fentry)
{
    if (fentry && fentry->fname.size()) {
        try {
            if (check_fentry_exists_file(fentry)) {
                if (!(boost::filesystem::status(fentry->fname).permissions() & 00600)) {
                    WLOG << "check_fentry(" << fentry->fname << ")-> not enough permissions";
                    return false;
                }
                return true;
            }
        } catch(const boost::filesystem::filesystem_error& e) {
            WLOG << "check_fentry(" << fentry->fname << ")->" << e.code().message();
        }
    }
    return false;
}

extern bool resch_loaded_now(float threshld);
extern bool resch_loaded(float threshld);
extern bool resch_free(float threshld);
extern void fsevent_put_tx(const boost::shared_ptr<fentry_t> &fentry, int psize);

void do_work()
{
    int thn = 0;
    try {

    int th_prio = def_prio;
    prios_set_thread_prio(th_prio);

    mtx_jobs.lock();
    thn = cnthreads++;
    mtx_jobs.unlock();

    ILOG << "INIT Worker thread #" << thn;

//    boost::scoped_ptr<Backend> bend(props::createBackend());
    boost::scoped_ptr<Processer> prsser(props::createProcesser(thn));

    while (!squeues_stop) {
            if (th_prio != def_prio) {
                th_prio = def_prio;
                prios_set_thread_prio(th_prio);
            }
        boost::mutex::scoped_lock scoped_lock(mtx_jobs);
        if (thn >= nthreads) { cnthreads--; break; }
        boost::shared_ptr<fentry_t> fentry;
        if (!pausa && cnthreads <= nthreads && !resch_loaded_now((float)hold_by_perc)
                && (fentry = get_next_fentry())) {
            if (is_in_black_list(fentry)) continue;
        //comprobar que no se este procesando este fichero
            if (in_process_jobs.find(fentry) != in_process_jobs.end()) {
//                    || ccrules_hold(fentry)) {
                in_list_t::iterator it = in_queue_jobs.find(fentry);
                if (it == in_queue_jobs.end()) {
                    pending_jobs.push_back(fentry);
                    in_queue_jobs.insert(fentry);
                } else {
                    (*it)->count += fentry->count;
                }
        //evitar entrar en bucle cuando solo quedan ficheros en proceso
                if (pending_jobs.size() <= in_process_jobs.size()) {
                    cond_jobs.wait(scoped_lock);
                }
                continue;
            }
    fentry->status = 10000; // turn around to prevent spurious logs
            in_process_job_t _in_process_job(thn, fentry);
            scoped_lock.unlock();

            if (fentry->event == "DEL") {
                deleted_del(fentry->fname);
                if (check_fentry_exists_file(fentry)) continue;
            } else if (!check_fentry(fentry)) continue;

            ILOG << "About to process ("<<fentry->event<<") '" << fentry->fname << "'";
    fentry->status = 0;
            prsser->process(fentry);

        } else { //if we are going to sleep, release connections
            prsser->release();
            cond_jobs.wait(scoped_lock);
        }
    }

    } catch (std::exception &e) {
        static const std::string _error = "****** ERROR *******";
        FLOG << "Exception '" << e.what() << "' in thread " << thn;
        thstate[thn] = sth_erro;
        thfpath[thn] = &_error;
    } catch (...) {
        static const std::string _error = "****** ERROR no STD *******";
        FLOG << "Exception no STD in thread " << thn;
        thstate[thn] = sth_erro;
        thfpath[thn] = &_error;
    }

    ILOG << "END Worker thread #" << thn;
}

static boost::thread_group calc_threads;
void sched_reinit()
{
   {boost::mutex::scoped_lock scoped_lock(mtx_blist);
        black_list.clear(); }
    squeues_stop = false;
    pausa = false;
    cond_jobs.notify_all();
}
void sched_nthreads()
{
    mtx_jobs.lock();
    nthreads++;
    mtx_jobs.unlock();
    calc_threads.create_thread(do_work);
}
void sched_nthreads_()
{
    boost::mutex::scoped_lock scoped_lock(mtx_jobs);
    if (nthreads > 0) --nthreads;
    cond_jobs.notify_all();
}

#ifdef WIN32
/*
static std::string vshadow_path = "C:\\Backup-Remoto\\EpsilonClient\\bin64\\vshadow.exe";
static void sched_process_shadows()
{
    std::map<char, in_list_t > fshdw_lists;
    { boost::mutex::scoped_lock scoped_lock(mtx_fshdw);
        fshdw_lists = for_shadow_jobs;
        for_shadow_jobs.clear(); }
    for (std::map<char, in_list_t >::iterator i = fshdw_lists.begin(), n = fshdw_lists.end(); i != n; ++i) {
        ILOG << "About to create shadow copy for " << i->first;
// non-persistent shadow mode test
// files are copied in their original location with .vss extension and then the normal procedure takes place
        { ofstream_utf8 ofs((props::get().confd_path + "copy_files1.cmd").c_str());
        if (ofs) {
            ofs << "call "<<props::get().confd_path<<"setvar1.cmd" << std::endl;
            BOOST_FOREACH (const boost::shared_ptr<fentry_t> &fentry, i->second) {
                std::string wfname = boost::algorithm::replace_all_copy(fentry->fname, "/", "\\");
                ofs << "copy \"%SHADOW_DEVICE_1%" << wfname.substr(2) << "\" \"" << wfname << ".shdw\" || echo bad" << std::endl;
            }
        }}
        std::string vshadow_cmd = vshadow_path + " -nw -exec=" + props::get().confd_path +
                "copy_files1.cmd -script=" + props::get().confd_path + "setvar1.cmd ";
        vshadow_cmd.push_back(i->first); vshadow_cmd.push_back(':');
        vshadow_cmd += " >>" + props::get().confd_path + "logs\\vshadow_cmd.log" +
                       " 2>>" + props::get().confd_path + "logs\\vshadow_cmd_err.log";
        DLOG << "sched_process_shadows()->system(" << vshadow_cmd << ")";
        int r = system(vshadow_cmd.c_str());
        if (r < 0) WLOG << "sched_process_shadows()->system(" << vshadow_cmd << "): " << r;
    }
// have to unmount or there are shadows that remove themselves
    shadows_running = false;
}
*/
extern std::string vshadow_create(char drive);
static void sched_process_shadows()
{
    std::map<char, in_list_t > fshdw_lists;
    { boost::mutex::scoped_lock scoped_lock(mtx_fshdw);
        fshdw_lists = for_shadow_jobs;
        for_shadow_jobs.clear(); }
    for (std::map<char, in_list_t >::iterator i = fshdw_lists.begin(), n = fshdw_lists.end(); i != n; ++i) {
        ILOG << "About to create shadow copy for " << i->first;
        std::string vs_path = vshadow_create(i->first);
        if (!vs_path.empty()) {
            BOOST_FOREACH (const boost::shared_ptr<fentry_t> &fentry, i->second) { try {
                std::string wfname = boost::algorithm::replace_all_copy(fentry->fname, "/", "\\");
                std::string forg = vs_path + wfname.substr(2);
                std::string fdst = wfname + ".shdw";
                boost::filesystem::copy_file(forg, fdst, boost::filesystem::copy_option::overwrite_if_exists);
//                ifstream_utf8 ifs(forg.c_str(), ifstream_utf8::binary);
//                if (ifs) {
//                    char buffer[0x8000];
//                    ifs.read(buffer, sizeof(buffer));
//                    if (ifs.gcount() > 0) {
//                        std::string fdst = wfname + ".shdw";
//                        DLOG << "sched_process_shadows_1("<<forg<<")->" << fdst;
//                        ofstream_utf8 ofs(fdst.c_str(), ofstream_utf8::binary);
//                        do {
//                            ofs.write(buffer, ifs.gcount());
//                            ifs.read(buffer, sizeof(buffer));
//                        } while (ifs.gcount() > 0);
//                    }
//                }
            } catch(std::exception &e) { WLOG << "sched_process_shadows_1("<<fentry->fname<<"): " << e.what(); }}
        }
    }
    shadows_running = false;
}
#endif
extern int mssql_backup_logs();
extern int oracle_backup_1();
extern int ptasks_run();
void sched_update_counters();
static size_t shadow_secs = 1800;
void sched_push_jobs()
{
    static int runmtx = 0, runmtx2 = 0;
    if (!pausa && ++runmtx % 15 == 0) {
// ajuste del numero de hilos en funcion de la carga media y el numero de cores
#ifdef COMP1
        if (nthreads > min_threads && epsilon_process_srv_timeout_cnt > 10) sched_nthreads_();
        else if (nthreads > min_threads && resch_loaded((float)high_mean_perc)) sched_nthreads_();
        else if (nthreads < max_threads && epsilon_process_srv_timeout_cnt <= 3
             && ((processing_mod_cnt + processing_new_cnt) >= (nthreads/2))
             && resch_free((float)low_mean_perc)) sched_nthreads();
#else
        if (nthreads > min_threads && resch_loaded((float)high_mean_perc)) sched_nthreads_();
        else if (nthreads < max_threads
             && ((processing_mod_cnt + processing_new_cnt) >= (nthreads/2))
             && resch_free((float)low_mean_perc)) sched_nthreads();
#endif

// recuperacion de ficheros borrados para notificarlo al server
        if (pending_jobs.size() < 10) { // let pending jobs drain a bit
        time_t tstamp = time(0) - deleted_secs;
        std::vector<std::string> files = deleted_get(tstamp);
        TLOG << "sched_push_jobs()->deleted_get(): " << tstamp << ", " << files.size();
        BOOST_FOREACH(const std::string &f, files) { try {
            DLOG << "deleted_get(): " << f;
            boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
            fentry->fname = f;
            fentry->initime = tstamp;
            fentry->initime = fentry->initime * 1000;
            fentry->trytime_last = fentry->initime_last = fentry->initime;
            fentry->count = 1;
#ifdef COMP1
            if (props::get().processer != "ofd1") {
//                fentry->sname = md5(f.substr(0, f.find_last_of("/"))) + "@" + md5(f);
                fentry->sname = props::get().smd5_name(md5(f.substr(0, f.find_last_of("/"))), md5(f));
            }
#endif
            fentry->event = "DEL";
            boost::mutex::scoped_lock scoped_lock(mtx_jobs);
            if (in_queue_jobs.find(fentry) == in_queue_jobs.end()) {
                pending_jobs.push_back(fentry);
                in_queue_jobs.insert(fentry);
            }
        } catch (std::exception &e) {
            WLOG << "Exception '" << e.what() << "' delete_get " << f;
        }}TLOG << "sched_push_jobs()->deleted_get() END";}
    }
    if (!pausa && (processing_mod_cnt+processing_new_cnt < nthreads)) {
        if (!pending_jobs.empty() || mod_files_not_empty() || mod_files_not_empty_n()
                || file_watcher_not_empty() || file_watcher_not_empty_n()) {
            cond_jobs.notify_all();
        }
    }
#ifdef COMP1
    if (!pausa ) mssql_backup_logs();
    if (!pausa ) oracle_backup_1();
#endif

    if (++runmtx2 % 60 == 0) { // 2*60=2'
        sched_update_counters();
    }

    allsafe_t::run();

    ptasks_run();
#ifdef WIN32
    if (shadow_secs > 0 && !pausa && ((runmtx % shadow_secs) == 0) && !for_shadow_jobs.empty() && !shadows_running) {
        shadows_running = true;
        DLOG << "New thread sched_process_shadows()";
        boost::thread th(sched_process_shadows);
        th.detach();
    }
#endif // WIN32
}

void sched_init_counters();
void sched_init()
{
    DLOG << "sched_init()";
    sched_t0 = boost::posix_time::second_clock::local_time();
    squeues_stop = false;
    stop_eraser = false;

dict_set("sched.min_threads", &min_threads);
dict_set("sched.max_threads", &max_threads);
dict_set("sched.hold_by_perc", &hold_by_perc);
dict_set("sched.low_mean_perc", &low_mean_perc);
dict_set("sched.high_mean_perc", &high_mean_perc);
//dict_set("sched.send_tries", &send_tries);
dict_set("sched.delay_ms", &delay_ms);
dict_set("sched.deleted_secs", &deleted_secs);
dict_set("sched.def_prio", &def_prio);
dict_set("sched.shadow_secs", &shadow_secs);
//dict_set("sched.vshadow_path", &vshadow_path);

    sched_init_counters();

    calc_threads.create_thread(eraser);
    for (int i = 0; i < nthreads; ++i)
        calc_threads.create_thread(do_work);
}

extern void epsilon_process_terminate_childs();
void sched_end()
{
    squeues_stop = true;
    stop_eraser = true;
    cond_jobs.notify_all();
    cond_eraser.notify_all();
#ifdef COMP1
    epsilon_process_terminate_childs();
#endif
    calc_threads.join_all();
    cnthreads = 0;
}
void sched_reset()
{
    sched_end();
    stop_eraser = false;
    squeues_stop = false;
    calc_threads.create_thread(eraser);
    for (int i = 0; i < nthreads; ++i)
        calc_threads.create_thread(do_work);
}

struct counters_t { long long counter[8]; counters_t() { memset(counter,0,sizeof(counter));}};
static counters_t last_counters;
static std::map<int, counters_t> counting;
extern int current_h; //hh*100 + min
void sched_init_counters()
{
    for (int i = 0; i < 24; i++) for (int j = 0; j < 60; j += 2) counting[i*100+j];
}
void sched_update_counters()
{
    int tt = current_h & 0x0FFFFE; // make it even
    counting[tt].counter[0] = nfcnt - last_counters.counter[0]; last_counters.counter[0] = nfcnt;
    counting[tt].counter[1] = mfcnt - last_counters.counter[1]; last_counters.counter[1] = mfcnt;
    counting[tt].counter[2] = ibytes - last_counters.counter[2]; last_counters.counter[2] = ibytes;
    counting[tt].counter[3] = obytes - last_counters.counter[3]; last_counters.counter[3] = obytes;
    counting[tt].counter[4] = nthreads;
    counting[tt].counter[5] = dfcnt - last_counters.counter[5]; last_counters.counter[5] = dfcnt;
    counting[tt].counter[6] = ercnt - last_counters.counter[6]; last_counters.counter[6] = ercnt;
}
std::string sched_dump_counters()
{
    std::stringstream ss;
    for (std::map<int, counters_t>::iterator i = counting.begin(), n = counting.end(); i != n; ++i) {
        ss << i->first << " " << i->second.counter[0] << " " << i->second.counter[1] << " "
            << i->second.counter[2] << " " << i->second.counter[3] << " " << i->second.counter[4]
            << "<br/>\n";
    }
    return ss.str();
}
#include <math.h>
#define _str(_n) boost::lexical_cast<std::string>(_n)
#define log10min1(_v) ((_v < 0) ? 0 : log10(_v+1))
static const std::string itemna[] = {"Nuevos","Modificados","Leidos","Escritos","Hilos","Eliminados","Error"};
std::string sched_graph_counters()
{
    static const std::string colour[] = {"#ff0000","#00ff00","#0000ff","#ffff00","#ff00ff","#00ffff","#ffa500"};
    std::string s; // = wserver_ws_title("Counters");  bgcolor=\"#F7F7FF\"
    s += "<table><tr><td class=\"graph counts\"><canvas id=\"canvas_counters\" width=\"750\" height=\"224\">"
        "</canvas></td><td><ul style=\"font:12px Georgia\">\n";
    for (int j=0; j<5; j++) s += "<li style=\"color:"+colour[j]+"\">"+itemna[j]+"</li>\n";
    s += "</ul></td></tr></table><script>\n"
      "var canvas = document.getElementById('canvas_counters');\n"
      "var ctx = canvas.getContext('2d');\n"
       "ctx.beginPath();\nctx.moveTo(30, 20);ctx.lineTo(30,200);ctx.lineTo(740,200);\n"
       "ctx.strokeStyle='#777777';ctx.stroke();\n";
    for (int j=1; j<5; j++) {
       s += "ctx.beginPath(); ctx.moveTo(" + _str(j%2==0?20:25) + "," + _str(j*50) + ");";
        s += "ctx.lineTo(30," + _str(j*50) + ");ctx.stroke();\n";
    }
    for (int j=0; j<24; j++) {
       s += "ctx.beginPath(); ctx.moveTo(" + _str((j*30)+30) + ", 200);";
        s += "ctx.lineTo(" + _str((j*30)+30) + "," + _str(j%6==0?210:205) + ");ctx.stroke();\n";
    }
static float _por[] = { 1, 1, .000001, .001, 1, 0 };
    for (int j=0; j<7; j++) {
        s += "ctx.beginPath(); ctx.moveTo(30, 200);\n"; int t = 0; //i->first;
        for (std::map<int, counters_t>::iterator i = counting.begin(), n = counting.end(); i != n; ++i) {
            float y = i->second.counter[j];
            s += "ctx.lineTo(" + _str((t++)+30) + "," + _str((int)(200-(55*log10min1(y*_por[j])))) + ");\n";
        }
        s += "ctx.strokeStyle = '" + colour[j] + "';\nctx.stroke();\n";
    }
    s += "ctx.font=\"10px Georgia\";ctx.fillStyle='#777777';\n"
            "ctx.fillText(\"2000\", 0, 35);ctx.fillText(\"20\", 12, 130);"
            "ctx.fillText(\"0\", 35, 215);ctx.fillText(\"6\", 215, 215);"
            "ctx.fillText(\"12\", 395, 215);ctx.fillText(\"18\", 575, 215);\n";
    s += "</script>"; // + wserver_pages_tail();
    return s;
}

static const boost::posix_time::time_duration tdday = boost::posix_time::hours(24);
std::string sched_report()
{
    std::stringstream ss; long long sum[8]; memset(sum,0,sizeof(sum));
//    ss << "<table class=\"report\"><tr><td></td><th>Hoy</th><th>Total</th></tr>\n";
//    ss << "<tr><th>Tiempo (s)</th><td>" << (current_h/100) << ":"  << (current_h%100) << ":00</td><td>"
    ss << "<table class=\"report\"><tr><td></td><th>D&iacute;a</th><th>Total</th></tr>\n";
    boost::posix_time::time_duration ttotal = (boost::posix_time::second_clock::local_time() - sched_t0);
    ss << "<tr><th>Tiempo</th><td>" << ((ttotal < tdday) ? ttotal:tdday) << "</td><td>" << ttotal << "</td></tr>\n";
    for (std::map<int, counters_t>::iterator i = counting.begin(), n = counting.end(); i != n && i->first <= current_h; ++i) { for (int j=0; j<7; j++) sum[j] += i->second.counter[j]; }
    ss << "<tr><th>" << itemna[0] << "</th><td>" << sum[0] << "</td><td>" << nfcnt << "</td></tr>\n";
    ss << "<tr><th>" << itemna[1] << "</th><td>" << sum[1] << "</td><td>" << mfcnt << "</td></tr>\n";
    ss << "<tr><th>" << itemna[2] << "</th><td>"; stringstream_bytes(sum[2], ss); ss << "</td><td>"; stringstream_bytes(ibytes, ss); ss << "</td></tr>\n";
    ss << "<tr><th>" << itemna[3] << "</th><td>"; stringstream_bytes(sum[3], ss); ss << "</td><td>"; stringstream_bytes(obytes, ss); ss << "</td></tr>\n";
    ss << "<tr><th>" << itemna[5] << "</th><td>" << sum[5] << "</td><td>" << dfcnt << "</td></tr>\n";
    ss << "<tr><th>" << itemna[6] << "</th><td>" << sum[6] << "</td><td>" << ercnt << "</td></tr>\n";
    ss << "</table>\n";
    return ss.str();
}
