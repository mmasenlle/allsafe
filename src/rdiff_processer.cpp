
#include <boost/filesystem.hpp>
#include "fstream_utf8.h"
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include "rdiff_processer.h"
#include "props_sched.h"
#include "sched_queues.h"
#include "zlib_sched.h"
#include "log_sched.h"
#include "cc_rules.h"
#include "rdiff_sched.h"


RdiffProcesser::RdiffProcesser(int n) : Processer(n)
{
    bend = props::createBackend();
}
RdiffProcesser::~RdiffProcesser()
{
    delete bend;
}
void RdiffProcesser::release()
{
    if (bend) bend->release();
}

int RdiffProcesser::process (boost::shared_ptr<fentry_t> &fentry)
{
    fentry->status = -1;
    if (!bend) {
        ELOG << "RdiffProcesser::process()-> !bend";
        return -1;
    }
    try {
        std::string rpath = props::get().rpath + props::get().smd5_path(fentry) + "/";
//                fentry->sname.substr(0, 32) + "/" + fentry->sname.substr(33) + "/";
        std::string opath = props::get().oldd_path(fentry->fname) + "/";

        if (fentry->event == "DEL") {
            squeues_set_thstate(thn, sth_dele);
            try {
                int r = bend->rmdir(rpath);
                boost::filesystem::remove_all(opath);
                if (r < 0) throw std::runtime_error("bend->rmdir ERROR");
                fentry->status = 0;
            } catch (std::exception &e) {
                WLOG << "RdiffProcesser::process(" << fentry->fname << ")->DEL: " << e.what();
            }
            return 0;
        }
        std::string lpath = props::get().patchd + fentry->sname;
        std::string linfo_path = opath + "info.xml";
        std::string rinfo_path = rpath + "info.xml";
        std::string tinfo_path = lpath + ".xml";
        std::string spath = lpath + ".sync";
        std::string zpath = lpath + ".zip";
        std::string dpath = lpath + ".patch";
        std::string sgn_path = opath + "sgn.dat";
        std::string ststamp = boost::lexical_cast<std::string>(fentry->initime_last);


        int edn = 0;
        bool reconst = ccrules_reconst(fentry->fname);
        { fentry->status = -2;
        ifstream_utf8 linfo(linfo_path.c_str());
        if (!linfo) {
            boost::filesystem::create_directories(opath);
            bend->mkdir(rpath);
        } else if (!reconst) {
            if (bend->get(rinfo_path, tinfo_path) == 0) {
                {char lbuf[0x1000], rbuf[0x1000];
                ifstream_utf8 rinfo(tinfo_path.c_str());
                linfo.read(lbuf, sizeof(lbuf));
                rinfo.read(rbuf, sizeof(rbuf));
                if (linfo.gcount() > 50 && linfo.gcount() == rinfo.gcount()) {
                    if (memcmp(lbuf, rbuf, linfo.gcount()) == 0) {
                        edn = atoi(lbuf + 11);
    //WLOG << "rinfo == linfo ? / edn: " << edn << "lbuf: " << std::string(lbuf, 50).substr(11,10);
                        if (!edn && lbuf[11] == '0') edn = 1;
                        else if (edn > 0 && edn < 10) edn = edn + 1;
                        else edn = 0;
                        DLOG << "RdiffProcesser::process(" << fentry->fname << ")->rinfo == linfo / edn: " << edn;
                    }
                }}
                squeues_try_rm_file(tinfo_path);
            } else {
                bend->mkdir(rpath);
            }
        }}
        bool bOK = false;
        { fentry->status = -3;
            ofstream_utf8 linfo(linfo_path.c_str());
            if (linfo)
                linfo << "<info><edn>" << boost::lexical_cast<std::string>(edn)
                        << "</edn><tstamp>" << ststamp << "</tstamp></info>";
        }
        if (!reconst) {
            squeues_set_thstate(thn, sth_sync);
            boost::filesystem::copy_file(fentry->fname, spath,
                            boost::filesystem::copy_option::overwrite_if_exists);

            squeues_set_thstate(thn, sth_diff);
            fentry->status = -4;

            if (edn) {
                if (rdiff_delta(sgn_path, spath, dpath) == 0) {
                    squeues_set_thstate(thn, sth_zip);
                    if (zlib_def(dpath, zpath) == 0) {
                        squeues_set_thstate(thn, sth_send);
                        if (bend->put(zpath, rpath + "p" + ststamp) == 0)
                            bOK = true;
                    }
                }
            }
        } else {
            spath = fentry->fname;
        }
        if (!bOK) {
            squeues_set_thstate(thn, sth_zip);
            if (zlib_def(spath, zpath) == 0) {
                squeues_set_thstate(thn, sth_send);
                if (bend->put(zpath, rpath + "f" + ststamp) == 0)
                    bOK = true;
                else {
                    bend->mkdir(rpath);
                    if (bend->put(zpath, rpath + "f" + ststamp) == 0)
                        bOK = true;
                }
            }
        }
        if (bOK) squeues_add_obytes(squeues_get_file_size(zpath));

        fentry->status = -5;
        if (bOK) {
            if (!reconst) {
                squeues_set_thstate(thn, sth_post);
                if (fentry->fsize == 0) fentry->fsize = squeues_get_file_size(fentry->fname);
                if (rdiff_sign(spath, fentry->fsize, sgn_path) == 0) {
                    if (bend->put(linfo_path, rinfo_path) == 0)
                        fentry->status = 0;
                }
            } else {
                fentry->status = 0;
            }
        }

        if (!reconst) squeues_try_rm_file(spath);
        squeues_try_rm_file(dpath);
        squeues_try_rm_file(zpath);

    } catch (std::exception &e) {
        WLOG << "RdiffProcesser::process(" << fentry->fname << ")->Exception: " << e.what();
        fentry->status = -9;
    }
    return 0;
}

std::map<std::string, std::string> RdiffProcesser::get_versions(const std::string &f, const std::string &fn)
{
    std::map<std::string, std::string> tstamps;
    boost::scoped_ptr<Backend> bend(props::createBackend());
    std::string resp, fpath = f.substr(0, props::get().smd5_len) + "/" + f.substr(props::get().smd5_len+1) + "/";
    int r = -1;
    size_t tlen = 13, lmin = tlen, tailsize = 0;
    if (bend) {
        r = bend->list(fpath + "*", &resp);
        if (r < 0) {
            WLOG << "RdiffProcesser::get_versions("<<f<<")->bend->list(*): " << r;
        }
TLOG << "RdiffProcesser::get_versions("<<f<<")->bend->list("<<fpath<<"*"<<"): " << r << "\n" << resp;
    } else {
        ELOG << "RdiffProcesser::get_versions("<<f<<")->ssh_exec: " << r;
    }
    if (r == 0) {
        resp.push_back('\n');
        std::istringstream iss(resp);
        std::string line;
        while (std::getline(iss, line)) { try {
            if (line.size() > lmin) {
                tstamps[line.substr(line.size() - tailsize - tlen, tlen)] = line;
            }} catch (std::exception &e) { WLOG << "RdiffProcesser::get_versions("<<f<<")->line("<<line<<"): " << e.what(); }
        }
    }
    return tstamps;
}
int RdiffProcesser::restore(const std::string &ts, const std::string &f, const std::string &fname, const std::string &fn)
{
    boost::scoped_ptr<Backend> bend(props::createBackend());
    std::string fpath = props::get().patchd + fname;
    boost::filesystem::path rpath = f.substr(0, props::get().smd5_len); rpath /= f.substr(props::get().smd5_len+1);

    if (!bend) throw std::runtime_error("RdiffProcesser::restore()->No backend defined");
    std::map<std::string, std::string> tstamps = get_versions(f, fn);
    if (tstamps.find(ts) == tstamps.end()) throw std::runtime_error("RdiffProcesser::restore()->No match found for time stamp");
    std::string rfn = tstamps[ts];
    std::string zpath = props::get().patchd + rfn;
    boost::system::error_code ec;
    if (rfn.size() > 10 && rfn[0] == 'f') { // whole file
        if (bend->get(rpath.generic_string() + "/" + rfn, zpath) < 0)
            throw std::runtime_error("RdiffProcesser::restore()->Error getting file");
        if (zlib_inf(zpath, fpath) != 0)
            throw std::runtime_error("RdiffProcesser::restore()->Error decompressing file");
    } else { // patch file
        std::map<std::string, std::string>::const_iterator i,j = tstamps.find(ts);
        for (i = j; i != tstamps.end(); --i) if (i->second[0] == 'f') break;
        if (i == tstamps.end()) throw std::runtime_error("RdiffProcesser::restore()->Base version not found");
        if (bend->get(rpath.generic_string() + "/" + i->second, zpath) < 0)
            throw std::runtime_error("RdiffProcesser::restore()->Error getting file");
        std::string bpath = zpath + ".base";
        std::string ppath = zpath + ".patch";
        std::string tpath = zpath + ".tmp";
        if (zlib_inf(zpath, bpath) != 0)
            throw std::runtime_error("RdiffProcesser::restore()->Error decompressing file");
        for (++i; ; ++i) {
            if (bend->get(rpath.generic_string() + "/" + i->second, zpath) < 0)
                throw std::runtime_error("RdiffProcesser::restore()->Error getting file");
            if (zlib_inf(zpath, ppath) != 0)
                throw std::runtime_error("RdiffProcesser::restore()->Error decompressing file");
            if (rdiff_patch(bpath, ppath, tpath) != 0)
                throw std::runtime_error("RdiffProcesser::restore()->Error applying patch");
            if (i == j) {
                boost::filesystem::copy_file(tpath, fpath,
                    boost::filesystem::copy_option::overwrite_if_exists);
                break;
            }
            boost::filesystem::copy_file(tpath, bpath,
                boost::filesystem::copy_option::overwrite_if_exists);
        }
        boost::system::error_code ec;
        boost::filesystem::remove(bpath, ec);
        boost::filesystem::remove(ppath, ec);
        boost::filesystem::remove(tpath, ec);
    }
    boost::filesystem::remove(zpath, ec);
    return ec.value();
}




#ifndef NDEBUG
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include "dict.h"

extern std::string md5(const std::string &str);
extern int ccrules_get_rule_id(const std::string &rule_name);
extern void ccrules_add_rule(const std::string &fname, int action, int param);

BOOST_AUTO_TEST_SUITE (main_test_suite_rdiff_processer)

BOOST_AUTO_TEST_CASE (rdiff_processer_tests)
{
    props::init();
    std::string testdir = "rdiff_processer_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.bold", testdir + "old/");
    boost::filesystem::create_directories(dict_get("props.bold"));
    dict_setv("props.patchd", testdir + "patchd/");
    boost::filesystem::create_directories(dict_get("props.patchd"));
    dict_setv("localBackend.path", testdir + "local/");
    boost::filesystem::create_directories(dict_get("localBackend.path"));
    dict_setv("props.backend", "local");
    dict_setv("props.processer", "rdiff1");
    props::update();

    std::string data = "12345";
    std::string fname = testdir + "f1.txt";
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = fname;
    fentry->sname = props::get().smd5_name(md5(testdir), md5(fname));
//    fentry->sname = md5(testdir) + "@" + md5(fname);
    boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    fentry->initime_last = 1000000000001;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "6"; }
    fentry->initime_last = 1000000000002;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "67"; }
    fentry->initime_last = 1000000000003;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "678"; }
    fentry->initime_last = 1000000000004;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "4321"; }
    fentry->initime_last = 1000000000005;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "99"; }
    fentry->initime_last = 1000000000006;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );

    std::map<std::string, std::string> versions = procsr->get_versions(fentry->sname, fentry->fname);
    BOOST_CHECK( versions.size() == 6 );

    std::string fr = dict_get("props.patchd") + "fr.txt";
    BOOST_CHECK( procsr->restore("1000000000001", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "12345" );
    BOOST_CHECK( procsr->restore("1000000000002", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "123456" );
    BOOST_CHECK( procsr->restore("1000000000003", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "1234567" );
    BOOST_CHECK( procsr->restore("1000000000004", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "12345678" );
    BOOST_CHECK( procsr->restore("1000000000005", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "123454321" );
    BOOST_CHECK( procsr->restore("1000000000006", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "1234599" );


    fname = testdir + "f2.txt";
    fentry->fname = fname;
//    fentry->sname = md5(testdir) + "@" + md5(fname);
    fentry->sname = props::get().smd5_name(md5(testdir), md5(fname));
    data = "añskfjasjfakjfñkajkfasj9325894395870892374097ajklajajsñlf8741'5293ajñjfakjfasjak90238910'8518akljfa40349090ds'a8'n8' vq283vnc23kjksjlkf89q4 23984923 jkjsaifu0q38rajalkjf";
    { ofstream_utf8 ofs(fname.c_str()); for (int i = 0; i < 100000; ++i) ofs << data; }
    fentry->initime_last = 1464261190846;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str(), std::fstream::app ); for (int i = 0; i < 10000; ++i) ofs << data; }
    fentry->initime_last += 1;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str(), std::fstream::app ); for (int i = 0; i < 30000; ++i) ofs << data; }
    fentry->initime_last += 1;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    versions.clear();
    versions = procsr->get_versions(fentry->sname, fentry->fname);
    BOOST_CHECK( versions.size() == 3 );

    BOOST_CHECK( procsr->restore(boost::lexical_cast<std::string>(fentry->initime_last), fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs2(fr.c_str()),ifs3(fname.c_str());
        char data2[4096],data3[4096];
        do {
             ifs2.read(data2, sizeof(data2));
             ifs3.read(data3, sizeof(data3));
             BOOST_CHECK( ifs2.gcount() == ifs3.gcount() );
             BOOST_CHECK( memcmp(data2, data3, ifs2.gcount()) == 0 );
        } while (ifs2 && ifs3);
        BOOST_CHECK( !ifs2 && !ifs3 );
    }
    data = "12345";
    fname = testdir + "f3.txt";
    fentry->fname = fname;
    ccrules_add_rule(fname, ccrules_get_rule_id("reconst"), 0);
//    fentry->sname = md5(testdir) + "@" + md5(fname);
    fentry->sname = props::get().smd5_name(md5(testdir), md5(fname));
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    fentry->initime_last = 1000000000001;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "6"; }
    fentry->initime_last = 1000000000002;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "67"; }
    fentry->initime_last = 1000000000003;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "678"; }
    fentry->initime_last = 1000000000004;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "4321"; }
    fentry->initime_last = 1000000000005;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "99"; }
    fentry->initime_last = 1000000000006;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    versions.clear();
    versions = procsr->get_versions(fentry->sname, fentry->fname);
    BOOST_CHECK( versions.size() == 6 );

    fr = dict_get("props.patchd") + "fr.txt";
    BOOST_CHECK( procsr->restore("1000000000001", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "12345" );
    BOOST_CHECK( procsr->restore("1000000000002", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "123456" );
    BOOST_CHECK( procsr->restore("1000000000003", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "1234567" );
    BOOST_CHECK( procsr->restore("1000000000004", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "12345678" );
    BOOST_CHECK( procsr->restore("1000000000005", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "123454321" );
    BOOST_CHECK( procsr->restore("1000000000006", fentry->sname, "fr.txt", fentry->fname) == 0 );
    { ifstream_utf8 ifs(fr.c_str()); ifs >> data; }
    BOOST_CHECK( data == "1234599" );


    boost::filesystem::remove_all(testdir);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
