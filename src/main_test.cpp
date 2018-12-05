#ifndef NDEBUG
#include <boost/test/unit_test.hpp>

#include "fstream_utf8.h"
#include <iostream>
#include <boost/filesystem.hpp>
#include "sshBackend.h"
#include "zlib_sched.h"
#include "rdiff_sched.h"
#include "servlets.h"
#include "dict.h"
#include "web_utils.h"
#include "log_sched.h"
#include "props_sched.h"
#include "diff_stats.h"
#include "persistence.h"

extern std::string wserver_serve(const std::string &uri);

BOOST_AUTO_TEST_SUITE (main_test_suite)

BOOST_AUTO_TEST_CASE (ssh)
{
    std::string fname = "main_test.txt";
    std::string dbase = "ssh_test____________kajsdflkajsdjfasjk60_____erasemeifiexist_____________________/";
    std::string dname = dbase + "dir1/ssh_test_dir/";
    std::string data = "main_test_ssh::"+dname+fname;
    {ofstream_utf8 ofs(fname.c_str()); ofs << data;}

    SshBackend::init();
    dict_setv("sshBackend.path", "");
    dict_setv("sshBackend.host", "192.168.11.153");
    dict_setv("sshBackend.user", "manu");
    dict_setv("sshBackend.pass", "manu");
    dict_setv("sshBackend.port", 22);

    SshBackend sshb;

    BOOST_CHECK( sshb.mkdir(dname) == 0 );
    BOOST_CHECK( sshb.put(fname, dname + fname) == 0 );

    boost::filesystem::remove(fname);

    std::string fl;
    BOOST_CHECK( sshb.list(dname + fname, &fl) == 0 );

    BOOST_CHECK( fl.find(fname) != std::string::npos );

    BOOST_CHECK( sshb.get(dname + fname, fname) == 0 );
    BOOST_CHECK( sshb.rmdir(dbase) == 0 );

    std::string data2;
    {ifstream_utf8 ifs(fname.c_str()); ifs >> data2;}

    BOOST_CHECK( data == data2 );

    boost::filesystem::remove(fname);
}

BOOST_AUTO_TEST_CASE (zip)
{
    std::string fname = "main_test.txt";
    std::string data = "main_test_ssh::in_a_place_in_la_mancha_such_name_i_dont_want_remember";
    {ofstream_utf8 ofs(fname.c_str()); ofs << data;}

    BOOST_CHECK( zlib_def(fname, fname + ".zip") == 0 );
    boost::filesystem::remove(fname);
    BOOST_CHECK( zlib_inf(fname + ".zip", fname) == 0 );

    std::string data2;
    {ifstream_utf8 ifs(fname.c_str()); ifs >> data2;}

    BOOST_CHECK( data == data2 );

    boost::filesystem::remove(fname);
    boost::filesystem::remove(fname + ".zip");
}

BOOST_AUTO_TEST_CASE (diff1)
{
    std::string fname1 = "main1_test.txt", fname2 = "main_test2.txt", fname3 = "main_test3.txt";
    std::string data = "main_test_ssh::in_a_place_in_la_mancha_such_name_i_dont_want_remember";
    {ofstream_utf8 ofs(fname1.c_str()); for (int i=0;i<1000;i++) ofs << data;}
    {ofstream_utf8 ofs(fname2.c_str()); for (int i=0;i<10;i++) {for (int j=0;j<80;j++) ofs << data; ofs << "do not tell what to do";}}

    BOOST_CHECK( rdiff_sign(fname1, 0, fname1 + ".sign") == 0 );
    BOOST_CHECK( rdiff_delta(fname1 + ".sign", fname2, fname1 + ".patch") == 0 );
    BOOST_CHECK( rdiff_patch(fname1, fname1 + ".patch", fname3) == 0 );

{   std::string data2,data3;
    ifstream_utf8 ifs2(fname2.c_str());
    ifstream_utf8 ifs3(fname3.c_str());
    do {
         ifs2 >> data2;
         ifs3 >> data3;
         BOOST_CHECK( data2 == data3 );
    } while (ifs2 && ifs3);
    BOOST_CHECK( !ifs2 && !ifs3 );}

    boost::filesystem::remove(fname1);
    boost::filesystem::remove(fname1 + ".sign");
    boost::filesystem::remove(fname1 + ".patch");
    boost::filesystem::remove(fname2);
    boost::filesystem::remove(fname3);
}

BOOST_AUTO_TEST_CASE (diff2)
{
    srand(time(NULL));
    std::string fname1 = "main1_test.txt", fname2 = "main_test2.txt", fname3 = "main_test3.txt";
    std::string data = "main_test_ssh::in_a_place_in_la_mancha_such_name_i_dont_want_remember";
    {ofstream_utf8 ofs(fname1.c_str()); for (int i=0;i<5000;i++) ofs << data; }
    {ofstream_utf8 ofs(fname2.c_str()); for (int i=0;i<20;i++) { ofs << rand() << " ... " << rand();for (int j=0;j<3000;j++) ofs << data; ofs << rand();}}

    BOOST_CHECK( rdiff_sign(fname1, boost::filesystem::file_size(fname1), fname1 + ".sign") == 0 );
    BOOST_CHECK( rdiff_delta(fname1 + ".sign", fname2, fname1 + ".patch") == 0 );
    BOOST_CHECK( boost::filesystem::file_size(fname1) > (boost::filesystem::file_size(fname1 + ".patch")*10) );
    BOOST_CHECK( rdiff_patch(fname1, fname1 + ".patch", fname3) == 0 );
    BOOST_CHECK( boost::filesystem::file_size(fname2) == boost::filesystem::file_size(fname3) );

{   char data2[4096],data3[4096];
    ifstream_utf8 ifs2(fname2.c_str());
    ifstream_utf8 ifs3(fname3.c_str());
    do {
         ifs2.read(data2, sizeof(data2));
         ifs3.read(data3, sizeof(data3));
         BOOST_CHECK( ifs2.gcount() == ifs3.gcount() );
         BOOST_CHECK( memcmp(data2, data3, ifs2.gcount()) == 0 );
    } while (ifs2 && ifs3);
    BOOST_CHECK( !ifs2 && !ifs3 );}

    boost::filesystem::remove(fname1);
    boost::filesystem::remove(fname1 + ".sign");
    boost::filesystem::remove(fname1 + ".patch");
    boost::filesystem::remove(fname2);
    boost::filesystem::remove(fname3);
}

BOOST_AUTO_TEST_CASE (ws_tests)
{
    std::string enc = "%24+%26+%3C+%3E+%3F+%3B+%23+%3A+%3D+%2C+%22+%27+%7E+%2B+%25";
    BOOST_CHECK( web_url_decode(enc) == "$ & < > ? ; # : = , \" ' ~ + %" );
    std::string dec = "\\/\\h ó l ä -ñoño %@|€*+{}";
    BOOST_CHECK( web_url_decode(web_encode(dec)) == dec );

    wserver_serve("/lvl_");
    size_t log_trace_lvl = log_trace_level;
    wserver_serve("/lvl_");
    wserver_serve("/lvl");
    wserver_serve("/lvl");
    wserver_serve("/lvl_");
    wserver_serve("/lvl");
    BOOST_CHECK( log_trace_lvl == (log_trace_level - 1) );

    BOOST_CHECK( wserver_serve("/menu?pepe=3") == wserver_serve("/menu?fu=man&chu=0&stop=1") );
}

BOOST_AUTO_TEST_CASE (diff_stats_test)
{
    diff_stats_t diffstats;
    BOOST_CHECK( diffstats.first == (-1) );

    BOOST_CHECK( diff_stats_load("diffstats_auto_test__", &diffstats) == 1 );
    BOOST_CHECK( diffstats.first == 0 );
    BOOST_CHECK( diffstats.last == 0 );
    BOOST_CHECK( diffstats.cnt == 0 );
    BOOST_CHECK( diffstats.lsize == 0 );
    BOOST_CHECK( diffstats.msize == 0 );
    BOOST_CHECK( diffstats.ldur == 0 );
    BOOST_CHECK( diffstats.mdur == 0 );

    diffstats.first = 0;
    diffstats.last = 1;
    diffstats.cnt = 2;
    diffstats.lsize = 3;
    diffstats.msize = 4;
    diffstats.ldur = 5;
    diffstats.mdur = 6;
    BOOST_CHECK( persistence_init() == 0);
    BOOST_CHECK( diff_stats_sync("diffstats_auto_test_2_", &diffstats) == 0 );
    diff_stats_t diffstats3;
    BOOST_CHECK( diffstats3.first == (-1) );
    BOOST_CHECK( diff_stats_load("diffstats_auto_test_2_", &diffstats3) == 0 );
    BOOST_CHECK( diffstats.last == diffstats3.first );
    BOOST_CHECK( diffstats.last == diffstats3.last );
    BOOST_CHECK( diffstats.cnt == diffstats3.cnt );
    BOOST_CHECK( diffstats.lsize == diffstats3.lsize );
    BOOST_CHECK( diffstats.msize == diffstats3.msize );
    BOOST_CHECK( diffstats.ldur == diffstats3.ldur );
    BOOST_CHECK( diffstats.mdur == diffstats3.mdur );

    srand(time(0));
    diffstats.first = props::current_time_ms() - 6408974;
    diffstats.last = props::current_time_ms();
    diffstats.cnt = rand() % 10000000;
    diffstats.lsize = rand() % 2000*1000*1000;
    diffstats.msize = rand() % 2000*1000*1000;
    diffstats.ldur = rand() % 1000*3600;
    diffstats.mdur = rand() % 1000*3600;
    BOOST_CHECK( persistence_init() == 0);
    BOOST_CHECK( diff_stats_sync("diffstats_auto_test_2_", &diffstats) == 0 );

    diff_stats_t diffstats2;
    BOOST_CHECK( diffstats2.first == (-1) );
    BOOST_CHECK( diff_stats_load("diffstats_auto_test_2_", &diffstats2) == 0 );
    BOOST_CHECK( diffstats3.first == diffstats2.first ); // first does not change
    BOOST_CHECK( diffstats.last == diffstats2.last );
    BOOST_CHECK( diffstats.cnt == diffstats2.cnt );
    BOOST_CHECK( diffstats.lsize == diffstats2.lsize );
    BOOST_CHECK( diffstats.msize == diffstats2.msize );
    BOOST_CHECK( diffstats.ldur == diffstats2.ldur );
    BOOST_CHECK( diffstats.mdur == diffstats2.mdur );

    BOOST_CHECK( diff_stats_load("diffstats_auto_test__", &diffstats) == 1 );
    BOOST_CHECK( diffstats.first == 0 );
    BOOST_CHECK( diffstats.last == 0 );
    BOOST_CHECK( diffstats.cnt == 0 );
    BOOST_CHECK( diffstats.lsize == 0 );
    BOOST_CHECK( diffstats.msize == 0 );
    BOOST_CHECK( diffstats.ldur == 0 );
    BOOST_CHECK( diffstats.mdur == 0 );

    BOOST_CHECK( diff_stats_del("diffstats_auto_test_2_") == 101 ); //SQLITE_DONE
    BOOST_CHECK( diff_stats_load("diffstats_auto_test_2_", &diffstats2) == 1 );
    BOOST_CHECK( diffstats.first == diffstats2.first );
    BOOST_CHECK( diffstats.last == diffstats2.last );
    BOOST_CHECK( diffstats.cnt == diffstats2.cnt );
    BOOST_CHECK( diffstats.lsize == diffstats2.lsize );
    BOOST_CHECK( diffstats.msize == diffstats2.msize );
    BOOST_CHECK( diffstats.ldur == diffstats2.ldur );
    BOOST_CHECK( diffstats.mdur == diffstats2.mdur );

    BOOST_CHECK( persistence_end() == 0 );
}

BOOST_AUTO_TEST_CASE (deleted_test)
{
    bool exists = false;
    BOOST_CHECK( persistence_end() == 0 );
    std::string delfname("deleted.db");
    std::string cpfname = delfname + ".org";
    try {
        boost::filesystem::copy(delfname, cpfname);
        boost::filesystem::remove(delfname);
        exists = true;
    } catch (...) {}

    BOOST_CHECK( persistence_init() == 0 );
    BOOST_CHECK( deleted_get(0).empty() );

    BOOST_CHECK( deleted_put("deleted_auto_test_1_", 1) == 0 );
    BOOST_CHECK( deleted_get(0).size() == 0 );
    std::vector<std::string> vs = deleted_get(2);
    BOOST_CHECK( vs.size() == 1 );
    if (vs.size() >= 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    BOOST_CHECK( deleted_del("deleted_auto_test_1_") == 101 );
    BOOST_CHECK( deleted_get(10).size() == 0 );

    BOOST_CHECK( deleted_put("deleted_auto_test_1_", 1) == 0 );
    BOOST_CHECK( deleted_put("deleted_auto_test_2_", 2) == 0 );
    BOOST_CHECK( deleted_put("deleted_auto_test_3_", 3) == 0 );
    BOOST_CHECK( deleted_get(1).size() == 0 );
    BOOST_CHECK( deleted_get(2).size() == 1 );
    BOOST_CHECK( deleted_get(3).size() == 2 );
    BOOST_CHECK( deleted_get(4).size() == 3 );
    BOOST_CHECK( deleted_get(0).size() == 0 );
    vs = deleted_get(2);
    BOOST_CHECK( vs.size() == 1 );
    if (vs.size() == 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    vs = deleted_get(3);
    BOOST_CHECK( vs.size() == 2 );
    if (vs.size() >= 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    if (vs.size() >= 2) BOOST_CHECK( vs[1] == "deleted_auto_test_2_" );
    vs = deleted_get(4);
    BOOST_CHECK( vs.size() == 3 );
    if (vs.size() >= 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    if (vs.size() >= 2) BOOST_CHECK( vs[1] == "deleted_auto_test_2_" );
    if (vs.size() >= 3) BOOST_CHECK( vs[2] == "deleted_auto_test_3_" );
    BOOST_CHECK( deleted_del("deleted_auto_test_2_") == 101 );
    vs = deleted_get(10);
    BOOST_CHECK( vs.size() == 2 );
    if (vs.size() >= 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    if (vs.size() >= 2) BOOST_CHECK( vs[1] == "deleted_auto_test_3_" );
    BOOST_CHECK( deleted_del("deleted_auto_test_3_") == 101 );
    vs = deleted_get(2);
    BOOST_CHECK( vs.size() == 1 );
    if (vs.size() >= 1) BOOST_CHECK( vs[0] == "deleted_auto_test_1_" );
    BOOST_CHECK( deleted_del("deleted_auto_test_1_") == 101 );
    BOOST_CHECK( deleted_get(0).empty() );

    BOOST_CHECK( persistence_end() == 0 );

    boost::filesystem::remove(delfname);
    if (exists) {
        boost::filesystem::copy(cpfname, delfname);
        boost::filesystem::remove(cpfname);
    }
}

/*
 no funciona ??????????
BOOST_AUTO_TEST_CASE (servlet)
{
//sftp_init();
    std::string res = wserver_serve("/menu");
log_trace_level=7;
    {servlets svl; std::string resp;
//    int r = svl.http_get("http://192.168.11.153:8680/menu", &resp);
    int r = svl.http_get("http://194.30.16.14:8080/", &resp);
    BOOST_CHECK( r == resp.size() );
//    BOOST_CHECK( res.size() == resp.size() );
//    BOOST_CHECK( res == resp );
std::cout << "r: " << r << " res.size(): " << res.size() << " resp.size(): " << resp.size() << std::endl;
std::cout << res.substr(0,10) << std::endl;
std::cout << resp.substr(0,10) << std::endl;
}    resp.clear();
    r = svl.http_get("http://192.168.11.153:8680/menu?op=2", &resp);
    BOOST_CHECK( r == resp.size() );
    BOOST_CHECK( res.size() == resp.size() );
    BOOST_CHECK( res == resp );}
    {servlets svl; std::string resp;
    int r = svl.http_get("http://192.168.11.153:8680/menu?op=3", &resp);
    BOOST_CHECK( r == resp.size() );
    BOOST_CHECK( res.size() == resp.size() );
    BOOST_CHECK( res == resp );
    resp.clear();
    r = svl.http_get("http://www.google.es", &resp);
    BOOST_CHECK( r > 100 );
    BOOST_CHECK( r == resp.size() );
std::cout << resp.substr(0,20) << std::endl;
    BOOST_CHECK( resp.compare(0, 20, "<!doctype html><html") == 0 );}
    servlets svl; std::string resp;
    int r = svl.http_get("http://194.30.16.14:8080/", &resp);
    BOOST_CHECK( r > 100 );
    BOOST_CHECK( r == resp.size() );
std::cout << resp.substr(resp.size() - 9, 7) << std::endl;
    BOOST_CHECK( resp.compare(resp.size() - 9, 7, "</html>") == 0 );
}
*/
BOOST_AUTO_TEST_SUITE_END( )

#endif
