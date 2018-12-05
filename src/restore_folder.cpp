
#include <string>
#include <vector>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>
#include <boost/foreach.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/thread.hpp>
#include "fstream_utf8.h"
#include "web_utils.h"
#include "props_sched.h"
#include "log_sched.h"
#include "old_files_data.h"


static const std::string shead = "<body><h2>Restaurar directorio</h2>\n"
    "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=\"/restdir?s=1\" class=ml>estado</a><br/><br/><br/>\n";
static const std::string stail = "</body></html>";
static boost::local_time::time_zone_ptr const utc_time_zone(new boost::local_time::posix_time_zone("GMT"));

struct restoration_t
{
    size_t state;
    size_t cntf;
    size_t cntd;
    size_t cnte;
    long long bytes;
    std::string fpath;
    std::string rpath;
    std::string date_time;
    std::string ts;
    void init() { state = 1; cntf = cntd = cnte = 0; bytes = 0; }
};

static restoration_t current_rest;

static std::string restore_folder_main(std::string d = "")
{
    std::stringstream ss;
    ss.imbue(std::locale(std::locale::classic(), new boost::local_time::local_time_facet("%Y-%m-%d %H:%M:%S")));
    boost::local_time::local_date_time now(boost::posix_time::second_clock::universal_time(), utc_time_zone);
    ss << wserver_pages_head() << shead;
    ss << "<form action=restdir method=get><table>\n"
            "<tr><th>Directorio:</th><td><input type=text name=d size=60 value=\"" << d << "\" /></td></tr>\n"
            "<tr><th>Destino:</th><td><input type=text name=r size=60 /></td></tr>\n"
            "<tr><th>Fecha/hora:</th><td><input type=text name=t size=30 value=\"" << now << "\" /></td></tr>\n"
            "<tr><td></td><td><input type=submit value=restore></td></tr></form>\n";
    ss << stail;
    return ss.str();
}

static void restore_folder_trest_file(boost::scoped_ptr<Processer> &procsr,
                                    const std::string &ts, const std::string &r, const std::string &fn)
{
    try {
        std::string fname = boost::filesystem::path(fn).filename().string();
        int fid = ofd_find(fn);
        if (fid < 0) throw std::runtime_error(std::string("restore_folder_trest_file(")+fn+")-> not found");
        std::string sfid = boost::lexical_cast<std::string>(fid);
        procsr->restore(ts, sfid, fname, fn);
        std::string fpath = props::get().tmpd + fname;
        std::string rpath = fn;
        if (rpath[1] == ':') rpath[1] = '_';
        rpath = r + rpath;
        boost::filesystem::create_directories(boost::filesystem::path(rpath).branch_path());
        boost::filesystem::copy_file(fpath, rpath);
        current_rest.bytes += boost::filesystem::file_size(fpath);
        boost::filesystem::remove(fpath);
        current_rest.cntf++;
    } catch (std::exception &e) {
        WLOG << "restore_folder_trest_file(" + fn + "): " + e.what();
        current_rest.cnte++;
    }
}

static void inline restore_add_slash(std::string &s)
{
    if (s.size() && s[s.size()-1] != '/' && s[s.size()-1] != '\\') s.push_back('/');
}

static void restore_folder_trest_folder(boost::scoped_ptr<Processer> &procsr,
                                    const std::string &ts, const std::string &r, std::string d)
{
    std::vector<std::string> folders,files;
    ofd_get_childs(d, &folders, &files);
    if (folders.empty() && files.empty()) {
        restore_folder_trest_file(procsr, ts, r, d);
        return;
    }
    restore_add_slash(d);
    BOOST_FOREACH(const std::string &_s, files) {
        restore_folder_trest_file(procsr, ts, r, d + _s);
    }
    BOOST_FOREACH(const std::string &_s, folders) {
        restore_folder_trest_folder(procsr, ts, r, d + _s);
    }
    current_rest.cntd++;
}

extern void stringstream_bytes(long long bytes, std::stringstream &ss);
static std::string restore_folder_info()
{
    std::stringstream ss;
    ss << wserver_pages_head() << "<body><h2>Restaurando directorio</h2>\n"
        "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=\"/restdir?s=1\" class=ml>actualizar</a><br/><br/><br/>\n";
    ss << "<table class=\"report\">\n";
    ss << "<tr><th>Ruta:</th><td>" << current_rest.fpath << "</td></tr>\n";
    ss << "<tr><th>Fecha:</th><td>" << current_rest.date_time << "</td></tr>\n";
    ss << "<tr><th>Destino:</th><td>" << current_rest.rpath << "</td></tr>\n";
    ss << "<tr><th>Estado:</th><td>" << (current_rest.state?"en curso":"terminado") << "</td></tr>\n";
    ss << "<tr><th>Ficheros:</th><td>" << current_rest.cntf << "</td></tr>\n";
    ss << "<tr><th>Directorios:</th><td>" << current_rest.cntd << "</td></tr>\n";
    ss << "<tr><th>Errores:</th><td>" << current_rest.cnte << "</td></tr>\n";
    ss << "<tr><th>Volumen:</th><td>"; stringstream_bytes(current_rest.bytes, ss); ss << "</td></tr>\n";
    ss << "</table>\n" << stail;
    return ss.str();
}

static void restore_folder_doit()
{
    DLOG << "restore_folder_doit(" << current_rest.fpath << ", " << current_rest.rpath << ", " << current_rest.date_time << ")->Timestamp: " << current_rest.ts;
    if (props::get().processer != "ofd1") {
        WLOG << "restore_folder_doit(" << current_rest.fpath <<")-> Operation not supported for this processer '" + props::get().processer + "'";
    } else {
        boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
        if (procsr) {
            restore_folder_trest_folder(procsr, current_rest.ts, current_rest.rpath, current_rest.fpath);
        }
    }
    current_rest.state = 0;
}

static const boost::posix_time::ptime t0(boost::gregorian::date(1970,1,1));
static std::string restore_folder_trest(const std::string &d, const std::string &r, const std::string &t)
{
    if (current_rest.state) {
        return restore_folder_info();
    }
    current_rest.init();
    std::string s = wserver_pages_head() + shead;
    ILOG << "Restoring folder '" << d << "', on '" << r << "', of date '" << t << "'";
    current_rest.fpath = d;
    current_rest.rpath = r; restore_add_slash(current_rest.rpath);
    current_rest.date_time = t;
    current_rest.ts = boost::lexical_cast<std::string>((boost::posix_time::time_from_string(t)-t0).total_seconds() + 1) + "000";
    TLOG << "restore_folder_trest(" << current_rest.fpath << ", " << current_rest.rpath << ", " << current_rest.date_time << ")->Timestamp: " << current_rest.ts;
    boost::thread th(restore_folder_doit);
    th.detach();
    return web_srefresh("restdir?s=1");
}

std::string restore_folder_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri.size() > 6 && vuri[3] == "r") {
                return restore_folder_trest(web_url_decode(vuri[2]),
                            web_url_decode(vuri[4]), web_url_decode(vuri[6]));
            }
            if (vuri[1] == "d") {
                return restore_folder_main(web_url_decode(vuri[2]));
            }
        }
        if (vuri[1] == "s") {
            return restore_folder_info();
        }
#ifndef NDEBUG
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
#endif
    }

    return restore_folder_main();
}


#ifndef NDEBUG
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include "dict.h"
#include "persistence.h"

BOOST_AUTO_TEST_SUITE (main_test_suite_restore_folder)

BOOST_AUTO_TEST_CASE (restore_folder_tests)
{
    props::init();
    persistence_init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "restore_folder_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.tmpd", testdir + "patchd/");
    boost::filesystem::create_directories(dict_get("props.tmpd"));
    dict_setv("localBackend.path", testdir + "local/");
    boost::filesystem::create_directories(dict_get("localBackend.path"));
    dict_setv("props.backend", "local");
    dict_setv("props.processer", "ofd1");
    props::update();

    boost::filesystem::create_directories(testdir + "dir1/dir2");
    boost::filesystem::create_directories(testdir + "dir3");
    std::string testdir_dest = testdir + "rest_dest/";
    boost::filesystem::create_directories(testdir_dest);

    std::string data = "12kajlksdfkasdfjds3kasdkfasdf4kalñkdfafjs5skdfaklsjfkajsdfsdsa";
    std::string fname = testdir + "f1.txt";
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = fname;
    boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    fentry->initime_last = 1000000000001;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fname = testdir + "f2.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fname = testdir + "dir1/f3.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fname = testdir + "dir3/f4.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fname = testdir + "dir1/dir2/f5.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fname = testdir + "dir1/dir2/f6.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << data << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fentry->initime_last = 1000000000002;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    fentry->initime_last = 3000000000002;
    fname = testdir + "dir3/f4.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );


    restore_folder_trest(testdir, testdir_dest, "2017-11-23 13:11:59");
    while (current_rest.state) boost::this_thread::sleep(boost::posix_time::seconds(1));

    BOOST_CHECK( current_rest.cntd == 4 );
    BOOST_CHECK( current_rest.cntf == 6 );
    BOOST_CHECK( current_rest.cnte == 0 );
    BOOST_CHECK( current_rest.bytes == (data.size()*7) );

    testdir_dest = testdir_dest + testdir;
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "f1.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "f2.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "dir1/f3.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "dir3/f4.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "dir1/dir2/f5.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file(testdir_dest + "dir1/dir2/f6.txt") );

    std::string data2;
    { ifstream_utf8 ifs((testdir_dest + "dir3/f4.txt").c_str()); ifs >> data2; }
    BOOST_CHECK( data2 == (data + data) );
    { ifstream_utf8 ifs((testdir_dest + "dir1/dir2/f6.txt").c_str()); ifs >> data2; }
    BOOST_CHECK( data2 == data );

    BOOST_CHECK( ofd_rm(testdir + "f1.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "f2.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "dir1/f3.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "dir3/f4.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "dir1/dir2/f5.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "dir1/dir2/f6.txt") > 0);

    boost::filesystem::remove_all(testdir);
    persistence_end();
    ofd_end();
}

BOOST_AUTO_TEST_SUITE_END( )

#endif

