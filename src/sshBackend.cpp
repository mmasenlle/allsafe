
#include "dict.h"
#include "log_sched.h"
#include "sshBackend.h"

static std::string ssh_path;
static std::string ssh_host;
static std::string ssh_user;
static std::string ssh_pass;
static size_t ssh_port = 22;

void SshBackend::init()
{
    dict_set("sshBackend.path", &ssh_path);
    dict_set("sshBackend.host", &ssh_host);
    dict_set("sshBackend.user", &ssh_user);
    dict_set_enc("sshBackend.pass", &ssh_pass);
    dict_set("sshBackend.port", &ssh_port);
}

SshBackend::SshBackend()
{
    sses.host = &ssh_host;
    sses.user = &ssh_user;
    sses.pass = &ssh_pass;
    sses.port = &ssh_port;
}
SshBackend::~SshBackend()
{
    sftp_end_session(&sses);
}
void SshBackend::release()
{
    if (sses.opened) sftp_end_session(&sses);
}

int SshBackend::put(const std::string &lpath, const std::string &rpath)
{
    return sftp_swrite(&sses, lpath, ssh_path + rpath);
}
int SshBackend::get(const std::string &rpath, const std::string &lpath)
{
    return sftp_sread(&sses, ssh_path + rpath, lpath);
}
int SshBackend::mkdir (const std::string &path)
{
    return ssh_exec(&sses, "mkdir -m a=rwx -p " + ssh_path + path, NULL);
}
int SshBackend::rmdir (const std::string &path)
{
    if (path.size() < 20 || (path[0]=='/' && path.size() < 50)) return -1; // prevent removing when path is not correct
    return ssh_exec(&sses, "rm -rf " + ssh_path + path, NULL);
}
int SshBackend::list (const std::string &path, std::string *resp)
{
    return ssh_exec(&sses, "ls -l " + ssh_path + path + "*", resp);
}
int SshBackend::get_s (const std::string &rpath, std::string &str)
{
    return sftp_sread_s(&sses, ssh_path + rpath, str);
}
int SshBackend::put_s (const std::string &str, const std::string &rpath)
{
    return sftp_swrite_m(&sses, str.c_str(), str.size(), ssh_path + rpath);
}
int SshBackend::put_m (const std::vector<unsigned char> &data, const std::string &rpath)
{
    return sftp_swrite_m(&sses, (const char *)&data.front(), data.size(), ssh_path + rpath);
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_ssh_backend)

BOOST_AUTO_TEST_CASE (ssh_backend_tests)
{
    SshBackend sb;
    SshBackend::init();
    dict_setv("sshBackend.path", "");
    dict_setv("sshBackend.host", "192.168.11.153");
    dict_setv("sshBackend.user", "manu");
    dict_setv("sshBackend.pass", "manu");
    dict_setv("sshBackend.port", 22);
    std::string sbpath = "sbpath_for_autoteses_";
    std::string f1 = sbpath + "/test1.txt";
    boost::filesystem::create_directories(sbpath);
    BOOST_CHECK( sb.mkdir(sbpath) == 0 );
    std::string s = "kañlsjfdaksd389479071935109837akjsfjsk_oiewruhhjsghurehafakj";
    BOOST_CHECK( sb.put_s(s, f1) == 0 );
    std::string s2;
    BOOST_CHECK( sb.get_s(f1, s2) == 0 );
    BOOST_CHECK( s == s2 );
    std::string f2 = sbpath + "/test2.txt";
    BOOST_CHECK( sb.get(f1, f2) == 0 );
    std::string f3 = sbpath + "/test3.txt";
    BOOST_CHECK( sb.put(f2, f3) == 0 );
    BOOST_CHECK( sb.get_s(f3, s2) == 0 );
    BOOST_CHECK( s == s2 );
    std::vector<unsigned char> data;
    data.assign(s.begin(), s.end());
    BOOST_CHECK( sb.put_m(data, f1) == 0 );
    BOOST_CHECK( sb.get_s(f1, s2) == 0 );
    BOOST_CHECK( s == s2 );
    BOOST_CHECK( sb.get(f1, f2) == 0 );
    BOOST_CHECK( sb.put(f2, f3) == 0 );
    BOOST_CHECK( sb.get_s(f3, s2) == 0 );
    BOOST_CHECK( s == s2 );

    BOOST_CHECK( sb.rmdir(sbpath) == 0 );
    boost::filesystem::remove_all(sbpath);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif // NDEBUG


