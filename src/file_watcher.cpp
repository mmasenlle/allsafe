#include <efsw/efsw.hpp>
#include <iostream>
#include <list>
#include <string>
#include <map>
#include "fstream_utf8.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include "persistence.h"
#include "old_files_data.h"
#include "props_sched.h"
#include "log_sched.h"
#include "dict.h"
#include "cc_rules.h"

struct strptr_less {
  bool operator() (const std::string * const lhs,
                   const std::string * const rhs) const
  {return *lhs < *rhs;}
};
typedef std::map<const std::string * const, boost::shared_ptr<fentry_t>, strptr_less> changed_files_set_t;
static changed_files_set_t changed_files_set;
static std::list<boost::shared_ptr<fentry_t> > changed_files;
static std::list<boost::shared_ptr<fentry_t> > new_files;
static std::list<boost::shared_ptr<fentry_t> > deleted_files;
static boost::mutex mtx_changed_files, mtx_deleted, mtx_mvdir;
static boost::condition_variable cond_deleted;

struct filew_mvdir_t
{
    time_t t;
    efsw::Action a;
    std::string p;
    std::string f;
    filew_mvdir_t() : t(0) {};
};
static std::map<efsw::WatchID, filew_mvdir_t> filew_mvdir_reg;

static efsw::FileWatcher *fw;

static size_t delay_ms = 3500;
static size_t delay_cnt = 10000; // creation of big files

static std::map<int, efsw::WatchID> dir_ids;

#ifndef NDEBUG
#define MAX_DIR_DEBUG 8
static int totalf[MAX_DIR_DEBUG],totalm[MAX_DIR_DEBUG],totald[MAX_DIR_DEBUG],totald2[MAX_DIR_DEBUG];
static int total_put_deleted[1];
#define ADD_DEBUG_VAR(_var, _i) do { if (_i <= MAX_DIR_DEBUG) _var[_i-1]++; } while(0)
#else
#define ADD_DEBUG_VAR(_var, _i)
#endif // NDEBUG

std::string file_watcher_debug_str()
{
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    std::string s = "<li><b>file_watcher_debug_str()</b></li>";
    s += "<li>changed_files: " + boost::lexical_cast<std::string>(changed_files.size()) + "</li>\n";
    s += "<li>new_files: " + boost::lexical_cast<std::string>(new_files.size()) + "</li>\n";
    s += "<li>changed_files_set: " + boost::lexical_cast<std::string>(changed_files_set.size()) + "</li>\n";
    s += "<li>deleted_files: " + boost::lexical_cast<std::string>(deleted_files.size()) + "</li>\n";
#ifndef NDEBUG
    s += "<li>put_deleted: " + boost::lexical_cast<std::string>(total_put_deleted[0]) + "</li>\n";
    for (int i = 0; i < dtw.size() && i < MAX_DIR_DEBUG; i++) {
    s += "<li>dirtowatch: " + dtw[i] + "</li>\n";
    s += "<li>_total files: " + boost::lexical_cast<std::string>(totalf[i]) + "</li>\n";
    s += "<li>_total mods: " + boost::lexical_cast<std::string>(totalm[i]) + "</li>\n";
    s += "<li>_total deleted: " + boost::lexical_cast<std::string>(totald[i]) + "</li>\n";
    s += "<li>_total del2: " + boost::lexical_cast<std::string>(totald2[i]) + "</li>\n";
    }
#endif // NDEBUG
    std::list<std::string> dirs = fw->directories();
    BOOST_FOREACH(const std::string &d, dirs) {
        s += "<li>directories: " + d + "</li>\n";
    }
    return s;
}

bool stop_putting_deleted = false;
static void put_deleted()
{
    while (!stop_putting_deleted) {
        boost::mutex::scoped_lock scoped_lock(mtx_deleted);
        if (!deleted_files.empty()) {
            boost::shared_ptr<fentry_t> fentry = deleted_files.front();
            deleted_files.pop_front();
            scoped_lock.unlock();
            deleted_put(fentry->fname, fentry->initime/1000);
            ADD_DEBUG_VAR(total_put_deleted, 1);
        } else {
            cond_deleted.wait(scoped_lock);
        }
    }
}
static void filew_add_fentry(std::string &path, efsw::Action action)
{
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    if (changed_files_set.find(&path) == changed_files_set.end()) {
        boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
        fentry->fname = boost::move(path);
        changed_files_set[&(fentry->fname)] = fentry;
        fentry->trytime_last = fentry->initime_last = fentry->initime = props::current_time_ms();
        fentry->count = 1;
#ifdef COMP1
        if (props::get().processer != "ofd1") {
            std::string dir1 = path.substr(0, fentry->fname.find_last_of("/"));
            fentry->sname = props::get().smd5_name(md5(dir1), md5(fentry->fname));
//            fentry->sname = md5(dir1).substr(0, props::get().smd5_len) + "@" +
//                    md5(fentry->fname).substr(0, props::get().smd5_len);
        }
#endif
        if (action==efsw::Actions::Add) {
            fentry->event = "NEW";
            new_files.push_back(fentry);
        } else {
            fentry->event = "MOD";
            changed_files.push_back(fentry);
        }
        TLOG << "filew_add_fentry(" << path << "," << fentry->event << ")";
    } else {
        changed_files_set[&path]->initime_last = props::current_time_ms();
        changed_files_set[&path]->count++;
    }
}
static inline void update_entry(const std::string &opath, const std::string &path, boost::shared_ptr<fentry_t> &fe)
{
    std::string &s = fe->fname;
    if (s.size() > opath.size() && s.compare(0, opath.size(), opath) == 0) {
        changed_files_set.erase(&s);
        s = path + s.substr(opath.size());
        changed_files_set[&(fe->fname)] = fe;
    }
}
static void update_lists(const std::string &opath, const std::string &path)
{
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    for (std::list<boost::shared_ptr<fentry_t> >::iterator i = changed_files.begin(),n = changed_files.end(); i != n; ++i)
        update_entry(opath, path, *i);
    for (std::list<boost::shared_ptr<fentry_t> >::iterator i = new_files.begin(),n = new_files.end(); i != n; ++i)
        update_entry(opath, path, *i);
}
static void filew_put_deleted(std::string &path)
{
    TLOG << "filew_put_deleted(" << path << ")";
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = boost::move(path);
    fentry->initime = props::current_time_ms();
    boost::mutex::scoped_lock scoped_lock(mtx_deleted);
    deleted_files.push_back(fentry);
    cond_deleted.notify_all();
}

static std::string getActionName( efsw::Action action ) {
    switch ( action ) {
        case efsw::Actions::Add:		return "Add";
        case efsw::Actions::Modified:	return "Modified";
        case efsw::Actions::Delete:		return "Delete";
        case efsw::Actions::Moved:		return "Moved";
        default:						return "Bad Action";
    }
}

static void filew_mvdir_check(efsw::WatchID watchid, efsw::Action action, const std::string& path)
{
    if (props::get().processer == "ofd1" && ((action == efsw::Actions::Delete) || (action == efsw::Actions::Add && boost::filesystem::is_directory(path)))) {
        size_t n = path.find_last_of('/');
        if (n != std::string::npos) {
            std::string p = path.substr(0, n);
            std::string f = path.substr(n+1);
            DLOG << "filew_mvdir_check(" << path << ")->" << getActionName( action ) << ", " << p << ", " << f;
            filew_mvdir_t md;
            {boost::mutex::scoped_lock scoped_lock(mtx_mvdir);
                md = filew_mvdir_reg[watchid];}
            time_t t = time(0);
            if ((md.t != t && (md.t + 1) != t) || (md.f != f) || (md.a == action)) {
                boost::mutex::scoped_lock scoped_lock(mtx_mvdir);
                filew_mvdir_reg[watchid].t = t;
                filew_mvdir_reg[watchid].a = action;
                filew_mvdir_reg[watchid].p = p;
                filew_mvdir_reg[watchid].f = f;
            } else {
                std::string po = p, pn = md.p;
                if (action == efsw::Actions::Add) { po = md.p, pn = p; }
                ILOG << "filew_mvdir_check()->move(" << f << "),from(" << po << "),to(" << pn << ")";
                if (ofd_move2(f, md.p, p) < 0) {
                    WLOG << "filew_mvdir_check()->error moving(" << f << "),from(" << po << "),to(" << pn << ")";
                }
                boost::mutex::scoped_lock scoped_lock(mtx_mvdir);
                filew_mvdir_reg[watchid].t = 0;
            }
        }
    }
}

/// Processes a file action
class UpdateListener : public efsw::FileWatchListener
{
public:
    UpdateListener() {}
    void handleFileAction( efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename = ""  )
    {
    try {
        int i = 0; while (i < dir.size() && dir[dir.size()-1-i] == '/' || dir[dir.size()-1-i] == '\\') i++;
DLOG << watchid << ") DIR (" << dir + ") FILE (" + ( oldFilename.empty() ? "" : "from file " + oldFilename + " to " ) + filename + ") has event " << getActionName( action );
            std::string dir_ =  dir.substr(0, dir.size()-i) + "/";
            std::string path = dir_ + boost::algorithm::replace_all_copy(filename, "\\", "/");
        if (action==efsw::Actions::Delete) {
            filew_mvdir_check(watchid, action, path);
            ADD_DEBUG_VAR(totald2, watchid);
#ifdef COMP1
            if (props::get().processer == "ofd1" || boost::filesystem::exists(props::oldd_path(path))) { // check if there is something to delete
#endif
                filew_put_deleted(path);
                ADD_DEBUG_VAR(totald, watchid);
#ifdef COMP1
            }
#endif
        } else {
            if (action == efsw::Actions::Moved && oldFilename.size()) {
                std::string opath = dir_ + boost::algorithm::replace_all_copy(oldFilename, "\\", "/");
                if (props::get().processer == "ofd1" && boost::filesystem::is_directory(path)) {
                    update_lists(opath, path);
                    if (ofd_move(opath, path) > 0) {
                        return;
                    } else { // handle as new
                        WLOG << "UpdateListener::handleFileAction(MOVE)->ofd_move("<<opath<<","<<path<<") not found";
                    }
                } else {
                    TLOG << "UpdateListener::handleFileAction(MOVE,"<<opath<<","<<path<<")-> handling as rm + new";
#ifdef COMP1
                    if (props::get().processer == "ofd1" || boost::filesystem::exists(props::oldd_path(opath))) { // check if there is something to delete
#endif
                        filew_put_deleted(path);
                        ADD_DEBUG_VAR(totald, watchid);
#ifdef COMP1
                    }
#endif
                }
            }
            if (!ccrules_exclude(path)) {
                if (boost::filesystem::is_regular_file(path)) {
                    filew_add_fentry(path, action);
                    if (action == efsw::Actions::Add) ADD_DEBUG_VAR(totalf, watchid); else ADD_DEBUG_VAR(totalm, watchid);
                } else {
                    filew_mvdir_check(watchid, action, path);
                }
            }
        }
    } catch (std::exception &e) {
        ELOG << "Exception in UpdateListener::handleFileAction(): " << e.what();
    }}
};
static UpdateListener * ul;

int file_watcher_not_empty()
{
    return changed_files.size();
}
int file_watcher_not_empty_n()
{
    return new_files.size();
}

boost::shared_ptr<fentry_t> file_watcher_get_next()
{
    long long ctms = props::current_time_ms();
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    for (std::list<boost::shared_ptr<fentry_t> >::iterator i = changed_files.begin(),n = changed_files.end(); i != n; ++i) {
        TLOG << "file_watcher_get_next()->" << (*i)->count << "/" << delay_cnt << "/" << ((*i)->initime_last + delay_ms) << "/" << ctms << " ("<<(*i)->fname<<")";
        if (((*i)->count > delay_cnt || ((*i)->initime_last + delay_ms) < ctms || (*i)->initime_last > ctms)) {
            if (!ccrules_hold(*i)) {
                boost::shared_ptr<fentry_t> f = *i;
                changed_files_set.erase(&(f->fname));
                changed_files.erase(i);
                return f;
            }
        }
    }
    return boost::shared_ptr<fentry_t>();
}

boost::shared_ptr<fentry_t> file_watcher_get_next_n()
{
    long long ctms = props::current_time_ms();
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    for (std::list<boost::shared_ptr<fentry_t> >::iterator i = new_files.begin(),n = new_files.end(); i != n; ++i) {
        TLOG << "file_watcher_get_next_n()->" << (*i)->count << "/" << delay_cnt << "/" << ((*i)->initime_last + delay_ms) << "/" << ctms << " ("<<(*i)->fname<<")";
        if (((*i)->count > delay_cnt || ((*i)->initime_last + delay_ms) < ctms || (*i)->initime_last > ctms)) {
            if (!ccrules_hold(*i)) {
                boost::shared_ptr<fentry_t> f = *i;
                changed_files_set.erase(&(f->fname));
                new_files.erase(i);
                return f;
            }
        }
    }
    return boost::shared_ptr<fentry_t>();
}

static boost::thread *put_deleted_th = NULL;
int file_watcher_init_dir(int i)
{
    TLOG << "file_watcher_init_dir(" << i << ")";
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    if (i < dtw.size()) {
            try {
        efsw::WatchID did;
		if ( ( did = fw->addWatch(dtw[i], ul, true) ) > 0 )	{
            ILOG << "Watching directory: " << dtw[i];
            dir_ids[i] = did;
            fw->watch();
            if (!put_deleted_th) {
                stop_putting_deleted = false;
                put_deleted_th = new boost::thread(put_deleted);
            }
		} else {
            ELOG << "Error trying to watch directory '" << dtw[i] << "': " << efsw::Errors::Log::getLastErrorLog();
		}
                } catch (std::exception &e) {
            ELOG << "Exception in file_watcher_init_dir(" << dtw[i] << "): " << e.what();
                }
	}
	return 0;
}

static std::string fwdump_fname = "file_watcher_dump.txt";

int file_watcher_init()
{
    TLOG << "file_watcher_init()";

    dict_set("file_watcher.delay_ms", &delay_ms);
    dict_set("file_watcher.delay_cnt", &delay_cnt);
#ifdef COMP1
    dict_set("file_watcher.fwdump_fname", &fwdump_fname);
#endif
    try {
        ul = new UpdateListener();
        fw = new efsw::FileWatcher(false);
        if (props::get().dirstowatch.size()) {
            for (int i = 0; i < props::get().dirstowatch.size(); i++)
                file_watcher_init_dir(i);
        }
    } catch (std::exception &e) {
        ELOG << "Exception in file_watcher_init(): " << e.what();
    }
	return 0;
}

void file_watcher_del_dir(int i)
{
    TLOG << "file_watcher_del_dir(" << i << ")";
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    if (i < dtw.size()) {
        try {
            fw->removeWatch(dir_ids[i]);
            dir_ids.erase(i);
            if (dir_ids.empty() && put_deleted_th) {
                stop_putting_deleted = true;
                cond_deleted.notify_all();
                put_deleted_th->join();
                delete put_deleted_th;
                put_deleted_th = NULL;
            }
        } catch (std::exception &e) {
            ELOG << "Exception in file_watcher_del_dir(" << dtw[i] << "): " << e.what();
        }
	}
}

bool file_watcher_find(const std::string * const fn, diff_stats_t *df)
{
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    if (changed_files_set.find(fn) == changed_files_set.end()) return false;
    df->first = changed_files_set[fn]->initime;
    df->last = changed_files_set[fn]->initime_last;
    df->cnt = changed_files_set[fn]->count;
    return true;
}

void file_watcher_dump()
{
    ILOG << "file_watcher_dump(" << fwdump_fname << "): " << changed_files_set.size();
    try {ofstream_utf8 ofs(fwdump_fname.c_str());
    if (ofs) {
        boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
        BOOST_FOREACH (const changed_files_set_t::value_type &f, changed_files_set)
            ofs << *(f.first) << std::endl;
    }} catch (std::exception &e) {
        ELOG << "Exception '" << e.what() << "' dumping to file " << fwdump_fname;
    }
}

std::string file_watcher_nexts()
{
    std::string s = "<h4>New files</h4>\n";
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    int j=0; for (std::list<boost::shared_ptr<fentry_t> >::iterator i = new_files.begin(),n = new_files.end(); i != n && j < 20; ++i,j++) {
        s += (*i)->fname + "<br/>\n";
    }
    s += "<h4>Modified files</h4>\n";
    j=0; for (std::list<boost::shared_ptr<fentry_t> >::iterator i = changed_files.begin(),n = changed_files.end(); i != n && j < 20; ++i,j++) {
        s += (*i)->fname + "<br/>\n";
    }
    return s;
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include "fstream_utf8.h"
#include <boost/thread.hpp>

extern int ccrules_get_rule_id(const std::string &rule_name);
extern void ccrules_add_rule(const std::string &fname, int action, int param);

BOOST_AUTO_TEST_SUITE (main_test_suite_file_watcher)

BOOST_AUTO_TEST_CASE (file_watcher_tests_update_lists)
{
    const std::string fnames1[] = { "C:/pepito/manganito/Nueva Carpeta/roberto/capriani.txt",
        "C:/pepito/manganito/Nueva Carpeta/manganeso/cobre.zip",
        "C:/pepito/manganito/Vieja Carpeta/roberto/capriani.txt",
        "C:/pepito/manganito/Nueva Carpeta/triptofan2.0",
        "E:/pepito/manganito/Nueva Carpeta/triptofan2.0",
        "C:/pepito/manganito/Nueva Carpeta/roberto/a/hasta aquí puedo leer",
    };
    const std::string fnames2[] = { "C:/pepito/manganito/prueba3/roberto/capriani.txt",
        "C:/pepito/manganito/prueba3/manganeso/cobre.zip",
        "C:/pepito/manganito/Vieja Carpeta/roberto/capriani.txt",
        "C:/pepito/manganito/prueba3/triptofan2.0",
        "E:/pepito/manganito/Nueva Carpeta/triptofan2.0",
        "C:/pepito/manganito/prueba3/roberto/a/hasta aquí puedo leer",
    };
    for (int i = 0; i < 6; i++) {
        boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
        fentry->fname = fnames1[i];
        changed_files_set[&(fentry->fname)] = fentry;
        changed_files.push_back(fentry);
        fentry = boost::make_shared<fentry_t>();
        fentry->fname = fnames1[i];
        new_files.push_back(fentry);
    }
    for (int i = 0; i < 6; i++) {
        BOOST_CHECK( changed_files_set.find(&(fnames1[i])) != changed_files_set.end() );
    }

    update_lists("C:/pepito/manganito/Nueva Carpeta", "C:/pepito/manganito/prueba3");

    BOOST_CHECK( changed_files_set.find(&(fnames1[0])) == changed_files_set.end() );
    BOOST_CHECK( changed_files_set.find(&(fnames1[1])) == changed_files_set.end() );
    BOOST_CHECK( changed_files_set.find(&(fnames1[2])) != changed_files_set.end() );
    BOOST_CHECK( changed_files_set.find(&(fnames1[3])) == changed_files_set.end() );
    BOOST_CHECK( changed_files_set.find(&(fnames1[4])) != changed_files_set.end() );
    BOOST_CHECK( changed_files_set.find(&(fnames1[5])) == changed_files_set.end() );

    for (int i = 0; i < 6; i++)
        BOOST_CHECK( changed_files_set.find(&(fnames2[i])) != changed_files_set.end() );

    for (int i = 0; i < 6; i++) {
        BOOST_CHECK( changed_files_set.find(&(fnames2[i])) != changed_files_set.end() );
        BOOST_CHECK( changed_files.front()->fname == fnames2[i] );
        BOOST_CHECK( new_files.front()->fname == fnames2[i] );
        changed_files_set.erase(&(fnames2[i]));
        changed_files.pop_front();
        new_files.pop_front();
    }
}

BOOST_AUTO_TEST_CASE (file_watcher_tests)
{
    props::init();
    persistence_init();
    BOOST_CHECK( ofd_init() == 0 );
    dict_setv("props.processer", "ofd1");
    std::string testdir = "file_watcher_tests_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    boost::filesystem::create_directories(testdir);
    changed_files_set.clear();
    changed_files.clear();
    new_files.clear();
    size_t dms = delay_ms;
    delay_ms = 0;

    ul = new UpdateListener();
    fw = new efsw::FileWatcher(false);
    int i = props::add_dir(testdir.substr(0,testdir.size()-1));
    file_watcher_init_dir(i);
    boost::this_thread::sleep(boost::posix_time::seconds(2));
//log_trace_level=9;
    boost::shared_ptr<fentry_t> fe;
    std::string fname = testdir + "test_fw1.txt";
    { ofstream_utf8 ofs(fname.c_str()); ofs << "6"; }
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( new_files.size() == 1 );
    BOOST_REQUIRE( (fe = file_watcher_get_next_n()) );
    BOOST_CHECK( fe->fname == fname );
    BOOST_CHECK( fe->event == "NEW" );
    { ofstream_utf8 ofs(fname.c_str(), ofstream_utf8::app); ofs << "3"; }
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( changed_files.size() == 1 );
    BOOST_REQUIRE( (fe = file_watcher_get_next()) );
    BOOST_CHECK( changed_files.size() == 0 );
    BOOST_CHECK( fe->fname == fname );
    BOOST_CHECK( fe->event == "MOD" );
    int fid = ofd_get(fname);
    BOOST_CHECK( fid > 0 );
    total_put_deleted[0] = 0;
    std::string fname_new = fname + ".new";
    boost::filesystem::rename(fname, fname_new);
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    //BOOST_CHECK( fid == ofd_find(fname + ".new") ); // regular files are not moved, only directories
    BOOST_CHECK( changed_files.size() == 1 );
    BOOST_REQUIRE( (fe = file_watcher_get_next()) );
    BOOST_CHECK( changed_files.size() == 0 );
    BOOST_CHECK( fe->fname == fname_new );
    BOOST_CHECK( fe->event == "MOD" );
    //fid = ofd_find(fname_new); BOOST_CHECK( fid > 0 ); it does not exists yet
    BOOST_CHECK( total_put_deleted[0] == 1 );
    boost::filesystem::remove(fname_new);
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( total_put_deleted[0] == 2 );
    deleted_del(fname_new);
    ofd_rm(fname);
    boost::filesystem::create_directories(testdir + "test2");
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( changed_files_set.empty() );
    BOOST_CHECK( changed_files.empty() );
    BOOST_CHECK( new_files.empty() );
    boost::filesystem::create_directories(testdir + "test4/test5");
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( changed_files_set.empty() );
    BOOST_CHECK( changed_files.empty() );
    BOOST_CHECK( new_files.empty() );
    BOOST_CHECK( ofd_get(testdir + "test2/test.txt") > 0 );
    BOOST_CHECK( ofd_get(testdir + "test4/test5/test6.txt") > 0 );
    BOOST_CHECK( (fid = ofd_find(testdir + "test2")) > 0 );
    { ofstream_utf8 ofs((testdir + "test2/test.txt").c_str()); ofs << "6"; }
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( new_files.size() == 1 );
    boost::filesystem::rename(testdir + "test2", testdir + "test3");
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( fid == ofd_find(testdir + "test3") );
    BOOST_CHECK( new_files.size() == 1 );
//log_trace_level=9;
    boost::filesystem::rename(testdir + "test3", testdir + "test4/test5/test3");
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    BOOST_CHECK( fid == ofd_find(testdir + "test4/test5/test3") );


    ofd_rm(testdir + "test4/test5/test3/test.txt");
    ofd_rm(testdir + "test4/test5/test6.txt");
    //ofd_rm(fname_new);
    BOOST_CHECK( ofd_find(testdir) < 0 );

    if (dir_ids[i] == i+1) {
        BOOST_CHECK( totald2[i] == 2 );
        BOOST_CHECK( totald[i] == 3 );
        BOOST_CHECK( (totalf[i] + totalm[i]) == 6 );
        BOOST_CHECK( totalm[i] >= 3 );
    }
    BOOST_CHECK( deleted_files.empty() );
//std::cout << file_watcher_debug_str() << std::endl;

    file_watcher_del_dir(i);
    delete fw;
    delete ul;
    boost::filesystem::remove_all(testdir);
    changed_files.clear();
    new_files.clear();
    changed_files_set.clear();
    delay_ms = dms;
    BOOST_CHECK( ofd_end() == 0 );
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
