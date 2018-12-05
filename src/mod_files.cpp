
#include <set>
#include <stack>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include "persistence.h"
#include "props_sched.h"
#include "old_files_data.h"
#include "log_sched.h"
#include "dict.h"
#include "cc_rules.h"

#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif

/// order by initime
struct shfentry_less_initime {
  bool operator() (const boost::shared_ptr<fentry_t>& lhs,
                   const boost::shared_ptr<fentry_t>& rhs) const
    {  return ((lhs->initime) < (rhs->initime));     }
};
static std::multiset<boost::shared_ptr<fentry_t>, shfentry_less_initime > changed_files;
static std::multiset<boost::shared_ptr<fentry_t>, shfentry_less_initime > new_files;
static boost::mutex mtx_changed_files;

static size_t enable = 1;

#define MAX_DIR_DEBUG 8
static int totalf[MAX_DIR_DEBUG],totald[MAX_DIR_DEBUG],
    mods[MAX_DIR_DEBUG],news[MAX_DIR_DEBUG],state[MAX_DIR_DEBUG],totalclean=0,clean_state=0,totalcd=0;
std::string mod_files_debug_str()
{
    const std::vector<std::string> &dtw = props::get().dirstowatch;
    std::string s = "<li><b>mod_files_debug_str()</b></li>";
    s += "<li>changed_files: " + boost::lexical_cast<std::string>(changed_files.size()) + "</li>\n";
    s += "<li>new_files: " + boost::lexical_cast<std::string>(new_files.size()) + "</li>\n";
    for (int i = 0; i < dtw.size() && i < MAX_DIR_DEBUG; i++) {
    s += "<li>dirtowatch: " + dtw[i] + "</li>\n";
    s += "<li>_total files: " + boost::lexical_cast<std::string>(totalf[i]) + "</li>\n";
    s += "<li>_total folders: " + boost::lexical_cast<std::string>(totald[i]) + "</li>\n";
    s += "<li>_mod files: " + boost::lexical_cast<std::string>(mods[i]) + "</li>\n";
    s += "<li>_new files: " + boost::lexical_cast<std::string>(news[i]) + "</li>\n";
    s += "<li>_state: " + boost::lexical_cast<std::string>(state[i]) + "</li>\n";
    }
    s += "<li>totalclean: " + boost::lexical_cast<std::string>(totalclean) + "</li>\n";
    s += "<li>clean_state: " + boost::lexical_cast<std::string>(clean_state) + "</li>\n";
    return s;
}


extern bool resch_stop;
extern bool pausa;
//extern int persistence_insert0(const std::string &fpath, int tt, int cnt);
extern std::string md5(const std::string &str);
/// Processes a file action
void find_mod_files(const std::string &fpath, int i)
{
    TLOG << "find_mod_files(" << i << ")";
        try {
    if (i < MAX_DIR_DEBUG) { totalf[i] = totald[i] = mods[i] = news[i] = state[i] = 0; }
    ILOG << "find_mod_files(" << fpath << ") INIT";
    std::stack<boost::shared_ptr<boost::filesystem::directory_iterator> > folders;
    boost::filesystem::directory_iterator end_itr;
    folders.push(boost::shared_ptr<boost::filesystem::directory_iterator>
                         (new boost::filesystem::directory_iterator(fpath)));
#ifdef COMP1
    std::string cur_dir_md5;
    bool bend = props::get().use_backend();
    bool bofd = (props::get().processer == "ofd1");
#endif
    while (!folders.empty() && !resch_stop) {
        if (pausa) { if (i < MAX_DIR_DEBUG) state[i] = 2; boost::this_thread::sleep(boost::posix_time::seconds(3)); continue; };
        if (i < MAX_DIR_DEBUG) state[i] = 1;
        boost::shared_ptr<boost::filesystem::directory_iterator> itr = folders.top();
        if (*itr == end_itr) {
            folders.pop();
#ifdef COMP1
            cur_dir_md5.clear();
#endif
            continue;
        }
    try {
        std::string fname = (*itr)->path().generic_string();
        if (!ccrules_exclude(fname)) {
        if ( boost::filesystem::is_directory((*itr)->status()) ) {
            folders.push(boost::shared_ptr<boost::filesystem::directory_iterator>
                         (new boost::filesystem::directory_iterator((*itr)->path())));
#ifdef COMP1
            cur_dir_md5.clear();
#endif
            if (i < MAX_DIR_DEBUG) totald[i]++;
            TLOG << "find_mod_files()-> looking in folder: " << (*itr)->path();
        } else if ( boost::filesystem::is_regular_file((*itr)->status())) {
            if (i < MAX_DIR_DEBUG) totalf[i]++;
#ifdef COMP1
            if (bofd) {
#endif
                int fid = ofd_find(fname);
                std::size_t lwt = boost::filesystem::last_write_time((*itr)->path());
                if (fid > 0) {
                    std::string info = ofd_get_info(fid);
                    long long tstamp = 0;
                    if (info.size() > 1 && info.size() < 20) {
                        try { tstamp = boost::lexical_cast<long long>(info); }
                        catch(std::exception &e) { WLOG << "find_mod_files()->bad tstamp1 '" << info << "': " << e.what(); }
                    } else {
                        static const std::string stamp_label = "\"tstamp\":\"";
                        std::size_t n = info.find(stamp_label);
                        if (n != std::string::npos && ((n+25) < info.size())) {
                            tstamp = strtoll(info.substr(n+stamp_label.size(),15).c_str(), NULL, 10);
//                            try { tstamp = boost::lexical_cast<long long>(info.substr(n+stamp_label.size(),13)); }
//                            catch(std::exception &e) { WLOG << "find_mod_files()->bad tstamp2 '" << info.substr(n+stamp_label.size(),13) << "': " << e.what(); }
                        }
                    }
                    tstamp = tstamp / 1000;
                    if (tstamp && tstamp < lwt) fid = -10;
                }
                if (fid <= 0) {
                    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
                    fentry->fname = boost::move(fname);
                    fentry->initime = lwt; fentry->initime *= 1000;
                    fentry->trytime_last = fentry->initime_last = fentry->initime;
                    fentry->event = (fid<-1)?"MOD":"NEW";
                    if (i < MAX_DIR_DEBUG) { if (fid<-1) mods[i]++; else news[i]++; }
    DLOG << fentry->event << " " << fentry->fname << " at " << fentry->initime;
                    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
                    if (fid<-1) changed_files.insert(fentry); else new_files.insert(fentry);
                }
#ifdef COMP1
            } else {
            std::string oldfile = props::oldd_path((*itr)->path().string());
            if (bend) oldfile += "/info.xml";
            bool exists = boost::filesystem::is_regular_file(oldfile);
            std::size_t lwt = boost::filesystem::last_write_time((*itr)->path());
            if (!exists || (lwt > boost::filesystem::last_write_time(oldfile))) {
//                std::string fname = boost::algorithm::replace_all_copy((*itr)->path().string(), "\\", "/");
//                if (!ccrules_exclude(fname)) {
                    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
                    fentry->fname = boost::move(fname);
                    fentry->initime = lwt; fentry->initime *= 1000;
                    fentry->trytime_last = fentry->initime_last = fentry->initime;
    //persistence_insert0(fentry->fname, milliseconds/1000, 1);
                    if (!cur_dir_md5.size()) cur_dir_md5 = md5(boost::algorithm::replace_all_copy((*itr)->path().parent_path().string(), "\\", "/"));
//                    fentry->sname = cur_dir_md5 + "@" + md5(fentry->fname);
                    fentry->sname = props::get().smd5_name(cur_dir_md5, md5(fentry->fname));
                    fentry->event = (exists)?"MOD":"NEW";
                    if (i < MAX_DIR_DEBUG) { if (exists) mods[i]++; else news[i]++; }
    DLOG << fentry->event << " " << fentry->fname << " / " << fentry->sname << " at " << fentry->initime;
                    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
                    if (exists) changed_files.insert(fentry); else new_files.insert(fentry);
//                }
            }
            }
#endif
        }
        }
    } catch (std::exception &e) {
        WLOG << "find_mod_files()=> Exception processing file: " << e.what();
    }
        (*itr)++;
    }
    if (i < MAX_DIR_DEBUG) state[i] = 3;
    ILOG << "find_mod_files(" << fpath << ") DONE";

    } catch (std::exception &e) {
        FLOG << "Exception '" << e.what() << "' in find_mod_files(" << i << ")";
        if (i < MAX_DIR_DEBUG) state[i] = -1;
    }
}

int mod_files_not_empty()
{
    return changed_files.size();
}

boost::shared_ptr<fentry_t> mod_files_get_next()
{
    boost::shared_ptr<fentry_t> s;
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    if (!changed_files.empty()) {
        s = *(changed_files.begin());
        changed_files.erase(changed_files.begin());
    }
    return s;
}

int mod_files_not_empty_n()
{
    return new_files.size();
}

boost::shared_ptr<fentry_t> mod_files_get_next_n()
{
    boost::shared_ptr<fentry_t> s;
    boost::mutex::scoped_lock scoped_lock(mtx_changed_files);
    if (!new_files.empty()) {
        s = *(new_files.begin());
        new_files.erase(new_files.begin());
    }
    return s;
}

static size_t force_rm = 0;
int mod_files_init()
{
    TLOG << "mod_files_init()";
	boost::filesystem::path p("dummy");
	dict_set("mod_files.enable", &enable);
	dict_set("mod_files.force_rm", &force_rm);
	return 0;
}

int mod_files_init_dir(int i)
{
    TLOG << "mod_files_init_dir(" << i << ")";
    if (enable) {
        const std::vector<std::string> &dtw = props::get().dirstowatch;
        if (i >= dtw.size()) {
            WLOG << "mod_files_init_dir(" << i << ") : directorio fuera de rango " << dtw.size();
        } else if (state[i] == 1 || state[i] == 2) {
            WLOG << "mod_files_init_dir(" << i << "): directorio en proceso " << state[i] << " " << dtw[i];
        } else if (!dtw[i].empty()) {
            boost::thread th(boost::bind(find_mod_files, dtw[i], i));
            th.detach();
        }
    }
	return 0;
}

int mod_files_start()
{
    TLOG << "mod_files_start()";
	if (enable) {
        for (int i = 0; i < props::get().dirstowatch.size(); i++)
            mod_files_init_dir(i);
	}
	return 0;
}

static void clean_ofd(const std::string &dir)
{
    time_t t0 = time(NULL);
    std::vector<std::string> folders,files;
    ofd_get_childs(dir, &folders, &files);
    std::string d = dir;
#ifdef WIN32
    if (!d.empty())
#endif // WIN32
        d.push_back('/');
    BOOST_FOREACH(const std::string &_s, files) {
        std::string fpath = d + _s;
        if (!boost::filesystem::is_regular_file(fpath)) {
            ILOG << "clean_ofd(" << fpath << ")->REMOVING";
            if (force_rm) ofd_rm(fpath);
            else deleted_put(fpath, t0);
            totalclean++;
        }
    }
    files.clear();
    BOOST_FOREACH(const std::string &_s, folders) {
        std::string fpath = d + _s;
        clean_ofd(fpath);
    }
}

/// Cleans stale files on old/
void clean_old_files(const std::string &dir)
{
    totalcd = 0;
    totalclean = 0;
    clean_state = 1;
#ifdef COMP1
    if ((props::get().processer == "ofd1")) {
#endif
        clean_ofd(dir);
        clean_state = 0;
        return;
#ifdef COMP1
    }
    std::string odir = props::oldd_path(dir);
    try {
    ILOG << "clean_old_files("<<dir<<") INIT files on " << odir;
    std::stack<boost::shared_ptr<boost::filesystem::directory_iterator> > folders;
    boost::filesystem::directory_iterator end_itr;
    folders.push(boost::shared_ptr<boost::filesystem::directory_iterator>
                         (new boost::filesystem::directory_iterator(odir)));
    time_t t0 = time(NULL);
    bool bend = props::get().use_backend();
    while (!folders.empty() && !resch_stop) {
        boost::shared_ptr<boost::filesystem::directory_iterator> itr = folders.top();
        if (*itr == end_itr) {
            folders.pop();
            continue;
        }
        try {
            if ( boost::filesystem::is_directory((*itr)->status()) ) {
                folders.push(boost::shared_ptr<boost::filesystem::directory_iterator>
                             (new boost::filesystem::directory_iterator((*itr)->path())));
                TLOG << "clean_old_files()-> looking in folder: " << (*itr)->path();
            } else if ( boost::filesystem::is_regular_file((*itr)->status()) ) {
                std::string orgfile; std::size_t n = (*itr)->path().string().size();
                if (bend && (n < (props::get().oldd.size() + 8) || (*itr)->path().string().compare(n - 8, 8, "info.xml"))) { (*itr)++; continue; }
                else if (bend) orgfile = (*itr)->path().string().substr(props::get().oldd.size(), n - 9 - props::get().oldd.size());
                else orgfile = (*itr)->path().string().substr(props::get().oldd.size());
                if (orgfile[1] == '_') orgfile[1] = ':';
                bool exists = boost::filesystem::is_regular_file(orgfile);
                boost::algorithm::replace_all(orgfile, "\\", "/");
                TLOG << "clean_old_files(" << orgfile << "): " << (exists?"exists":"not found");
                if (!exists) {
                    ILOG << "clean_old_files(" << (*itr)->path() << ")->REMOVING";
                    if (force_rm) boost::filesystem::remove((*itr)->path());
                    else deleted_put(orgfile, t0);
                    totalclean++;
                }
            }
        } catch (std::exception &e) {
            WLOG << "clean_old_files()=> Exception processing file: " << e.what();
        }
        (*itr)++;
    }
    clean_state = 2;
    ILOG << "clean_old_files("<<odir<<") INIT directories";
    for (int i=0,c=-1; i < 10 && c != totalcd; i++) {
         boost::filesystem::recursive_directory_iterator it(odir);
        boost::filesystem::recursive_directory_iterator itEnd; c=totalcd;
        while (it != itEnd) {
           boost::system::error_code  ec;
           const boost::filesystem::path& rPath = it->path();
           if (boost::filesystem::is_directory(rPath, ec) && boost::filesystem::is_empty(rPath, ec)) {
              const boost::filesystem::path pth = rPath;
              ++it;
              boost::filesystem::remove(pth, ec);
              totalcd++;
           } else {
              ++it;
           }
        }
    }
    ILOG << "clean_old_files("<<odir<<") DONE " << totalclean <<"/"<< totalcd;
    clean_state = 0;
    } catch (std::exception &e) {
        ELOG << "clean_old_files("<<odir<<")=> Exception processing folder: " << e.what();
        clean_state = -1;
    }
#endif
}

/// Cleans patches directory
void clean_patchd()
{
    try {
        ILOG << "clean_patchd() INIT";
        boost::filesystem::directory_iterator it(props::get().tmpd), itEnd;
        for (; it != itEnd; it++) {
            boost::filesystem::remove_all(it->path());
        }
        ILOG << "clean_patchd() DONE ";
    } catch (std::exception &e) {
        ELOG << "clean_patchd(): " << e.what();
    }
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include "fstream_utf8.h"

extern int ccrules_get_rule_id(const std::string &rule_name);
extern void ccrules_add_rule(const std::string &fname, int action, int param);

BOOST_AUTO_TEST_SUITE (main_test_suite_mod_files)

BOOST_AUTO_TEST_CASE (mod_files_tests_clean_ofd)
{
    props::init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "mod_files_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.processer", "ofd1");
    boost::filesystem::create_directories(testdir + "test2");
    { ofstream_utf8 ofs((testdir + "test1si.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test3si.txt").c_str()); ofs << "--"; }
    size_t fr = force_rm; force_rm = 1;
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1si.txt"),"") > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1no.txt"),"") > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test3no.txt"),"") > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test3si.txt"),"") > 0 );
    std::vector<std::string> folders,files;
    BOOST_CHECK( ofd_get_childs(testdir, &folders, &files) > 0 );
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 2 );
    folders.clear();files.clear();
    BOOST_CHECK( ofd_get_childs(testdir + "test2", &folders, &files) > 0 );
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 2 );
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 2 );
    folders.clear();files.clear();
    BOOST_CHECK( ofd_get_childs(testdir, &folders, &files) > 0 );
    BOOST_CHECK( folders.size() == 1 );
    BOOST_CHECK( files.size() == 1 );
    folders.clear();files.clear();
    BOOST_CHECK( ofd_get_childs(testdir + "test2", &folders, &files) > 0 );
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 1 );

    boost::filesystem::remove(testdir + "test2/test3si.txt");
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 1 );
    folders.clear();files.clear();
    BOOST_CHECK( ofd_get_childs(testdir, &folders, &files) > 0 );
    BOOST_CHECK( folders.size() == 0 );
    BOOST_CHECK( files.size() == 1 );
    boost::filesystem::remove(testdir + "test1si.txt");
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 1 );
    BOOST_CHECK( ofd_find(testdir) < 0 );

    boost::filesystem::remove_all(testdir);
    force_rm = fr;
    BOOST_CHECK( ofd_end() == 0 );
}
#ifdef COMP1
BOOST_AUTO_TEST_CASE (mod_files_tests_clean_old)
{
    props::init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "mod_files_old_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.processer", "");
    dict_setv("props.backend", "");
    dict_setv("props.bold", testdir + "old/");
    dict_setv("props.oldd", testdir + "old/");
    props::update();
    BOOST_CHECK( !props::get().use_backend() );
    boost::filesystem::create_directories(testdir + "old/" + testdir + "test2");
    boost::filesystem::create_directories(testdir + "test2");
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1si.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test3si.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1si.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test3si.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1no.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test3no.txt").c_str()); ofs << "--"; }
    size_t fr = force_rm; force_rm = 1;
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 2 );
    BOOST_CHECK( totalcd == 0 );
    boost::filesystem::remove(testdir + "test2/test3si.txt");
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 1 );
    BOOST_CHECK( totalcd == 1 );
    boost::filesystem::remove(testdir + "test1si.txt");
    clean_old_files(testdir);
    BOOST_CHECK( totalclean == 1 );
    BOOST_CHECK( totalcd == 0 );

    boost::filesystem::remove_all(testdir);
    force_rm = fr;
    BOOST_CHECK( ofd_end() == 0 );
}
#endif // COMP1

BOOST_AUTO_TEST_CASE (mod_files_tests_mod_ofd)
{
    props::init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "mod_ofd1_files_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.processer", "ofd1");
    boost::filesystem::create_directories(testdir + "test2");

    long long tstamp = props::current_time_ms();
    std::string ststamp_menos = boost::lexical_cast<std::string>(tstamp - 1000);
    { ofstream_utf8 ofs((testdir + "test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1mas2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1menos2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1new.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1mas2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1menos2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1new.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test2new.txt").c_str()); ofs << "--"; }
    ccrules_add_rule(testdir + "test2/test2*", ccrules_get_rule_id("exclude"), 0);
    tstamp = props::current_time_ms();
    std::string ststamp_mas = boost::lexical_cast<std::string>(tstamp + 1000);
    std::string info_menos = "{\"edn\":\"0\",\"tstamp\":\"" + ststamp_menos + "\",\"path\":\""+testdir+"test\"}";
    std::string info_mas = "{\"edn\":\"1271\",\"tstamp\":\"" + ststamp_mas + "\",\"path\":\""+testdir+"test\"}";
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1mas1.txt"), ststamp_mas) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1mas2.txt"), info_mas) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1menos1.txt"), info_menos) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test1menos2.txt"), ststamp_menos) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test1mas1.txt"), ststamp_mas) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test1mas2.txt"), info_mas) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test1menos1.txt"), ststamp_menos) > 0 );
    BOOST_CHECK( ofd_set_info(ofd_get(testdir + "test2/test1menos2.txt"), info_menos) > 0 );
    changed_files.clear();
    new_files.clear();
    find_mod_files(testdir, 7);
    BOOST_CHECK( totald[7] == 1 );
    BOOST_CHECK( totalf[7] == 10 );
    BOOST_CHECK( news[7] == 2 );
    BOOST_CHECK( mods[7] == 4 );
//std::cout << mod_files_debug_str() << std::endl;
    BOOST_CHECK( changed_files.size() == 4 );
    BOOST_CHECK( new_files.size() == 2 );
    size_t fr = force_rm; force_rm = 1;
    boost::filesystem::remove_all(testdir);
    clean_old_files(testdir);
    changed_files.clear();
    new_files.clear();
    BOOST_CHECK( ofd_find(testdir) < 0 );
    force_rm = fr;
    BOOST_CHECK( ofd_end() == 0 );
}
#ifdef COMP1
BOOST_AUTO_TEST_CASE (mod_files_tests_mod_old)
{
    props::init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "mod2_files_old_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.processer", "");
    dict_setv("props.backend", "");
    dict_setv("props.bold", testdir + "old/");
    dict_setv("props.oldd", testdir + "old/");
    props::update();
    BOOST_CHECK( !props::get().use_backend() );
    boost::filesystem::create_directories(testdir + "old/" + testdir + "test2");
    boost::filesystem::create_directories(testdir + "test2");

    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1mas2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test1mas2.txt").c_str()); ofs << "--"; }
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    { ofstream_utf8 ofs((testdir + "test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1mas2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1menos2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test1new.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1mas1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1mas2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1menos2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test1new.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "test2/test2new.txt").c_str()); ofs << "--"; }
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test1menos2.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test1menos1.txt").c_str()); ofs << "--"; }
    { ofstream_utf8 ofs((testdir + "old/" + testdir + "test2/test1menos2.txt").c_str()); ofs << "--"; }

    ccrules_add_rule(testdir + "test2/test2*", ccrules_get_rule_id("exclude"), 0);
    ccrules_add_rule(testdir + "old/*", ccrules_get_rule_id("exclude"), 0);

    changed_files.clear();
    new_files.clear();
    find_mod_files(testdir, 7);
    BOOST_CHECK( totald[7] == 2 );
    BOOST_CHECK( totalf[7] == 10 );
    BOOST_CHECK( news[7] == 2 );
    BOOST_CHECK( mods[7] == 4 );
    BOOST_CHECK( changed_files.size() == 4 );
    BOOST_CHECK( new_files.size() == 2 );

    boost::filesystem::remove_all(testdir);
    changed_files.clear();
    new_files.clear();
    BOOST_CHECK( ofd_end() == 0 );
}
#endif // COMP1

BOOST_AUTO_TEST_SUITE_END( )

#endif

