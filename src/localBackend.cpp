
#include <boost/filesystem.hpp>
#include "fstream_utf8.h"
#include "localBackend.h"
#include "dict.h"
#include "log_sched.h"

static std::string local_path = "local/";

void LocalBackend::init()
{
    dict_set("localBackend.path", &local_path);
}

LocalBackend::LocalBackend() {}

int LocalBackend::put(const std::string &lpath, const std::string &rpath)
{
    try {
        boost::filesystem::copy_file(lpath, local_path + rpath, boost::filesystem::copy_option::overwrite_if_exists);
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::put(" << lpath << ", " << rpath << "): " << e.what();
    }
    return -1;
}
int LocalBackend::get(const std::string &rpath, const std::string &lpath)
{
    try {
        boost::filesystem::copy_file(local_path + rpath, lpath, boost::filesystem::copy_option::overwrite_if_exists);
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::get(" << rpath << ", " << lpath << "): " << e.what();
    }
    return -1;
}
int LocalBackend::mkdir (const std::string &path)
{
    try {
        boost::filesystem::create_directories(local_path + path);
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::mkdir(" << local_path<<path << "): " << e.what();
    }
    return -1;
}
int LocalBackend::rmdir (const std::string &path)
{
    try {
        boost::filesystem::remove_all(local_path + path);
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::rmdir(" << local_path<<path << "): " << e.what();
    }
    return -1;
}
int LocalBackend::list (const std::string &path, std::string *resp)
{
    try {
        boost::filesystem::directory_iterator it(boost::filesystem::path(local_path + path).parent_path()), itEnd;
        for (; it != itEnd; it++) {
            if (resp) {
                resp->append(it->path().filename().string()); resp->push_back('\n');
            }
        }
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::list(" << path << ")->Error: " << e.what();
    }
    return -1;
}
int LocalBackend::get_s (const std::string &rpath, std::string &str)
{
    try {
        str.resize(boost::filesystem::file_size(local_path + rpath));
        ifstream_utf8 ifs((local_path + rpath).c_str());
        ifs.read((char*)str.data(), str.size());
        TLOG << "LocalBackend::get_s(" << local_path + rpath << ", '" << str << "')";
        return 0;
    } catch (std::exception &e) {
        if (str.size()) ELOG << "LocalBackend::get_s(" << local_path + rpath << "): " << e.what();
        str.clear();
    }
    return -1;
}
int LocalBackend::put_s (const std::string &str, const std::string &rpath)
{
    TLOG << "LocalBackend::put_s('" << str << "', " << local_path + rpath << ")";
    try {
        ofstream_utf8 ofs((local_path + rpath).c_str());
        if (!ofs) throw std::runtime_error("Cant create file");
        ofs.write(str.data(), str.size());
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::put_s(" << str.size() << ", " << local_path + rpath << "): " << e.what();
    }
    return -1;
}
int LocalBackend::put_m (const std::vector<unsigned char> &data, const std::string &rpath)
{
    try {
        ofstream_utf8 ofs((local_path + rpath).c_str(), ofstream_utf8::binary);
        if (!ofs) throw std::runtime_error("Cant create file");
        ofs.write((const char *)&data.front(), data.size());
        return 0;
    } catch (std::exception &e) {
        ELOG << "LocalBackend::put_m(" << data.size() << ", " << rpath << "): " << e.what();
    }
    return -1;
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_local_backend)

BOOST_AUTO_TEST_CASE (local_backend_tests)
{
    LocalBackend lb;
    LocalBackend::init();
    std::string lbpath = "lbpath_for_autoteses_";
    dict_setv("localBackend.path", lbpath + "/local/");
    std::string f1 = lbpath + "/test1.txt";
    boost::filesystem::create_directories(lbpath + "/local");
    BOOST_CHECK( lb.mkdir(lbpath) == 0 );
    std::string s = "kañlsjfdaksd389479071935109837akjsfjsk_oiewruhhjsghurehafakj";
    BOOST_CHECK( lb.put_s(s, f1) == 0 );
    std::string s2;
    BOOST_CHECK( lb.get_s(f1, s2) == 0 );
    BOOST_CHECK( s == s2 );
    std::string f2 = lbpath + "/test2.txt";
    BOOST_CHECK( lb.get(f1, f2) == 0 );
    std::string f3 = lbpath + "/test3.txt";
    BOOST_CHECK( lb.put(f2, f3) == 0 );
    BOOST_CHECK( lb.get_s(f3, s2) == 0 );
    BOOST_CHECK( s == s2 );
    std::vector<unsigned char> data;
    data.assign(s.begin(), s.end());
    BOOST_CHECK( lb.put_m(data, f1) == 0 );
    BOOST_CHECK( lb.get_s(f1, s2) == 0 );
    BOOST_CHECK( s == s2 );
    BOOST_CHECK( lb.get(f1, f2) == 0 );
    BOOST_CHECK( lb.put(f2, f3) == 0 );
    BOOST_CHECK( lb.get_s(f3, s2) == 0 );
    BOOST_CHECK( s == s2 );

    BOOST_CHECK( lb.rmdir(lbpath) == 0 );
    boost::filesystem::remove_all(lbpath);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif // NDEBUG

