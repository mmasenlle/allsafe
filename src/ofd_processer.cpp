
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include "fstream_utf8.h"
#include "ofd_processer.h"
#include "props_sched.h"
#include "sched_queues.h"
#include "zlib_sched.h"
#include "log_sched.h"
#include "cc_rules.h"
#include "rdiff_sched.h"
#include "old_files_data.h"

static size_t file_size_th = 30*1024*1024;

OfdProcesser::OfdProcesser(int n) : Processer(n)
{
    bend = props::createBackend();
}
OfdProcesser::~OfdProcesser()
{
    delete bend;
}
void OfdProcesser::release()
{
    if (bend) bend->release();
}

static std::string ofdproc_rpath(const std::string &sname)
{
    std::string s = ofd_get_magic();
    if (s.size()) s += "/";
    if (sname.size() > 1) {
        s += sname.substr(sname.size()-2,2) + "/" + sname + "/";
    } else {
        s += "0" + sname.substr(sname.size()-1,1) + "/" + sname + "/";
    }
    return s;
}

int OfdProcesser::process_m (boost::shared_ptr<fentry_t> &fentry)
{
    fentry->status = -3;
    squeues_set_thstate(thn, sth_sync);
    std::vector<unsigned char> fdata(fentry->fsize);
    { ifstream_utf8 ifs(fentry->fname.c_str(), ifstream_utf8::binary);
        ifs.read((char*)&fdata.front(), fdata.size());
        if (ifs.gcount() != fdata.size()) {
            WLOG << "OfdProcesser::process_m(" << fentry->fname << ")->ifs.gcount() != fdata.size(): " << ifs.gcount() << "/" << fdata.size();
            squeues_refish(fentry, 3);
            return -3;
        }
    }
    bool bOK = false;
    if (!reconst) {
        squeues_set_thstate(thn, sth_diff);
        fentry->status = -4;
        if (edn) {
            std::vector<unsigned char> sdata, ddata, zdata;
            if (ofd_get_sign(fid, sdata) == fid) {
                if (rdiff_delta_m(sdata, fdata, ddata) == 0) {
                    squeues_set_thstate(thn, sth_zip);
                    if (zlib_def_m(ddata, zdata) == 0) {
                        squeues_set_thstate(thn, sth_send);
                        if (bend->put_m(zdata, rpath + "p" + ststamp) == 0) {
                            bOK = true;
                            squeues_add_obytes(zdata.size());
                            fentry->diff_stats.lsize = zdata.size();
                        }
                    }
                }
            }
        }
    }
//DLOG << "OfdProcesser::process_m(" << fentry->fname << ")->reconst: "<<reconst<<" no_deflate: "<<no_deflate;
    if (!bOK) {
        if (no_deflate) {
            squeues_set_thstate(thn, sth_send);
            if (bend->put_m(fdata, rpath + "F" + ststamp) == 0)
                bOK = true;
            else {
                bend->mkdir(rpath);
                if (bend->put_m(fdata, rpath + "F" + ststamp) == 0)
                    bOK = true;
            }
            if (bOK) squeues_add_obytes(fdata.size());
            fentry->diff_stats.lsize = fdata.size();
        } else {
            squeues_set_thstate(thn, sth_zip);
            std::vector<unsigned char> zdata;
            if (zlib_def_m(fdata, zdata) == 0) {
                squeues_set_thstate(thn, sth_send);
                if (bend->put_m(zdata, rpath + "f" + ststamp) == 0)
                    bOK = true;
                else {
                    bend->mkdir(rpath);
                    if (bend->put_m(zdata, rpath + "f" + ststamp) == 0)
                        bOK = true;
                }
            }
            if (bOK) squeues_add_obytes(zdata.size());
            fentry->diff_stats.lsize = zdata.size();
        }
    }
    fentry->status = -5;
    if (bOK) {
        if (!reconst) {
            squeues_set_thstate(thn, sth_post);
            std::vector<unsigned char> sdata;
            if (rdiff_sign_m(fdata, sdata) == 0) {
                if (ofd_set_sign(fid, sdata) == fid) {
                    fentry->status = 0;
                }
            }
        } else {
            fentry->status = 0;
        }
    }
    return fentry->status;
}
int OfdProcesser::process_f (boost::shared_ptr<fentry_t> &fentry)
{
    std::string lpath = props::get().tmpd + fentry->sname;
    std::string spath = lpath + ".sync";
    std::string zpath = lpath + ".zip";
    std::string dpath = lpath + ".patch";

    {boost::system::error_code ec;
    boost::filesystem::create_directories(props::get().tmpd, ec);}

    bool bOK = false;
    if (!reconst) {
        squeues_set_thstate(thn, sth_sync);

        boost::filesystem::copy_file(fentry->fname, spath,
                        boost::filesystem::copy_option::overwrite_if_exists);

        squeues_set_thstate(thn, sth_diff);
        fentry->status = -4;

        if (edn) {
            std::vector<unsigned char> sdata;
            if (ofd_get_sign(fid, sdata) == fid) {
                if (rdiff_delta_m1(sdata, spath, dpath) == 0) {
                    squeues_set_thstate(thn, sth_zip);
                    if (zlib_def(dpath, zpath) == 0) {
                        squeues_set_thstate(thn, sth_send);
                        if (bend->put(zpath, rpath + "p" + ststamp) == 0)
                            bOK = true;
                    }
                }
            }
        }
    } else {
        spath = fentry->fname;
    }
    if (!bOK) {
        if (no_deflate) {
            squeues_set_thstate(thn, sth_send);
            if (bend->put(spath, rpath + "F" + ststamp) == 0)
                bOK = true;
            else {
                bend->mkdir(rpath);
                if (bend->put(spath, rpath + "F" + ststamp) == 0)
                    bOK = true;
            }
            fentry->diff_stats.lsize = squeues_get_file_size(spath);
        } else {
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
            fentry->diff_stats.lsize = squeues_get_file_size(zpath);
        }
    }
    if (bOK) squeues_add_obytes(fentry->diff_stats.lsize);


    fentry->status = -5;
    if (bOK) {
        if (!reconst) {
            squeues_set_thstate(thn, sth_post);
            std::vector<unsigned char> sdata;
            if (rdiff_sign_m1(spath, fentry->fsize, sdata) == 0) {
                if (ofd_set_sign(fid, sdata) == fid) {
                    fentry->status = 0;
                }
            }
        } else {
            fentry->status = 0;
        }
    }
    if (!reconst) squeues_try_rm_file(spath);
    squeues_try_rm_file(dpath);
    squeues_try_rm_file(zpath);
    return fentry->status;
}

int OfdProcesser::process (boost::shared_ptr<fentry_t> &fentry)
{
    fentry->status = -1;
    if (!bend) {
        ELOG << "OfdProcesser::process()-> !bend";
        return -1;
    }
    fid = ofd_get(fentry->fname);
    if (fid <= 0) {
        WLOG << "OfdProcesser::process("<<fentry->fname<<")-> bad fid: " << fid;
        return -1;
    }
    try {
        fentry->sname = boost::lexical_cast<std::string>(fid);
        //rpath = props::get().rpath + ofdproc_rpath(fentry->sname);
        rpath = ofdproc_rpath(fentry->sname);

        if (fentry->event == "DEL") {
            squeues_set_thstate(thn, sth_dele);
            try {
                //int r = bend->rmdir(rpath); DON'T REMOVE FROM SERVER FOR THE TIME BEING
                diff_stats_del(fentry->sname);
                if (ofd_rm(fentry->fname) != fid) throw std::runtime_error("bend->ofd_rm ERROR");
                //if (r < 0) throw std::runtime_error("bend->rmdir ERROR");
                fentry->status = 0;
            } catch (std::exception &e) {
                WLOG << "OfdProcesser::process(" << fentry->fname << ")->DEL: " << e.what();
            }
            return 0;
        }
        std::string rinfo_path = rpath + "info";
        ststamp = boost::lexical_cast<std::string>(fentry->initime_last);

        edn = 0;
        no_deflate = ccrules_no_deflate(fentry->fname);
        reconst = no_deflate || ccrules_reconst(fentry->fname);
//DLOG << "OfdProcesser::process(" << fentry->fname << ")->reconst: "<<reconst<<" no_deflate: "<<no_deflate;
        fentry->status = -2;
        std::string linfo = ofd_get_info(fid);
        if (linfo.empty()) {
            bend->mkdir(rpath);
        } else if (!reconst) {
            std::string rinfo;
            if (bend->get_s(rinfo_path, rinfo) == 0) {
                if (linfo == rinfo && linfo.size() > 15) {
                    edn = atoi(linfo.c_str() + 8);
//WLOG << "rinfo == linfo ? / edn: " << edn << "linfo: " << linfo;
                    if (!edn && linfo[8] == '0') edn = 1;
                    else if (edn > 0 && edn < 10) edn = edn + 1;
                    else edn = 0;
                    DLOG << "OfdProcesser::process(" << fentry->fname << ")->rinfo == linfo / edn: " << edn;
                }
            } else {
                bend->mkdir(rpath);
            }
        }
        linfo = "{\"edn\":\"" + boost::lexical_cast<std::string>(edn) + "\","
            "\"tstamp\":\"" + ststamp + "\",\"path\":\"" + fentry->fname + "\"}";
        if (ofd_set_info(fid, reconst ? ststamp : linfo) != fid)
            WLOG << "OfdProcesser::process(" << fentry->fname << ")-> error setting info";
        fentry->status = -3;
        fentry->fsize = squeues_get_file_size(fentry->fname);
        if (fentry->fsize < 0) { squeues_refish(fentry, 3); return -3; }
        if (fentry->fsize == 0 && fentry->nfails < 5) { squeues_refish(fentry, 8); fentry->status = 3; return 3; }

        diff_stats_load(fentry->sname.c_str(), &fentry->diff_stats);
        fentry->diff_stats.last = fentry->initime_last;
        fentry->diff_stats.cnt = fentry->diff_stats.cnt + 1;

        const time_t t0 = time(0);
        if (fentry->fsize < file_size_th) {
            process_m(fentry);
        } else {
            try { process_f(fentry); } catch (std::exception &e) {
                WLOG << "OfdProcesser::process_f(" << fentry->fname << ")->Exception: " << e.what();
                squeues_refish(fentry, 3);
            }
        }
        fentry->diff_stats.ldur = time(0) - t0;
        if (fentry->status == 0) {
            bend->put_s(linfo, rinfo_path);
            squeues_sync_stats(fentry);
        }

    } catch (std::exception &e) {
        WLOG << "OfdProcesser::process(" << fentry->fname << ")->Exception: " << e.what();
        fentry->status = -9;
    }
    return 0;
}

std::map<std::string, std::string> OfdProcesser::get_versions(const std::string &f, const std::string &fn)
{
    std::map<std::string, std::string> tstamps;
    boost::scoped_ptr<Backend> bend(props::createBackend());
    std::string resp;
    int r = -1;
    size_t tlen = 13, lmin = tlen, tailsize = 0;
    if (bend) {
        std::string fpath = ofdproc_rpath(f);
        r = bend->list(fpath, &resp);
        if (r < 0) {
            WLOG << "OfdProcesser::get_versions("<<f<<")->bend->list(*): " << r;
        }
TLOG << "OfdProcesser::get_versions("<<f<<")->bend->list("<<fpath<<"*"<<"): " << r << "\n" << resp;
    } else {
        ELOG << "OfdProcesser::get_versions("<<f<<")->ssh_exec: " << r;
    }
    if (r == 0) {
        resp.push_back('\n');
        std::istringstream iss(resp);
        std::string line;
        while (std::getline(iss, line)) { try {
            std::size_t n = line.find_last_of('/'); // ssh backend lines with path
            if (n != std::string::npos) line = line.substr(n+1);
            if (line.size() > lmin) {
                tstamps[line.substr(line.size() - tailsize - tlen, tlen)] = line;
            }} catch (std::exception &e) { WLOG << "OfdProcesser::get_versions("<<f<<")->line("<<line<<"): " << e.what(); }
        }
    }
    return tstamps;
}
static std::map<std::string, std::string>::const_iterator less_nearer_tstamp(const std::string &ts, const std::map<std::string, std::string> &tstamps)
{
    std::map<std::string, std::string>::const_iterator i = tstamps.lower_bound(ts);
    if (i == tstamps.end()) --i;
    if (i == tstamps.end()) throw std::runtime_error("OfdProcesser::less_nearer_tstamp()->No match found for time stamp");
    if ((i->first > ts) && (i != tstamps.begin())) --i;
    return i;
}
int OfdProcesser::restore(const std::string &ts, const std::string &f, const std::string &fname, const std::string &fn)
{
    ILOG << "OfdProcesser::restore(" << ts << ", " << fn << ")->" << fname;
    boost::scoped_ptr<Backend> bend(props::createBackend());
    std::string fpath = props::get().tmpd + fname;
    if (!bend) throw std::runtime_error("OfdProcesser::restore()->No backend defined");
    std::map<std::string, std::string> tstamps = get_versions(f, fn);
    if (tstamps.empty()) throw std::runtime_error("OfdProcesser::restore()->No files found");
    std::map<std::string, std::string>::const_iterator i = less_nearer_tstamp(ts, tstamps);
    DLOG << "OfdProcesser::restore(" << ts << ")->" << i->first;
    boost::filesystem::path rpath = ofdproc_rpath(f);
    std::string rfn = i->second; // tstamps[ts];
    DLOG << "OfdProcesser::restore(" << ts << ")->" << rfn;
    std::string zpath = props::get().tmpd + rfn;
    boost::system::error_code ec;
    boost::filesystem::create_directories(props::get().tmpd, ec);
    if (rfn.size() > 10 && (rfn[0]|0x020) == 'f') { // whole file
        if (rfn[0] == 'f') {
            if (bend->get(rpath.generic_string() + rfn, zpath) < 0)
                throw std::runtime_error("OfdProcesser::restore()->Error getting file");
            if (zlib_inf(zpath, fpath) != 0)
                throw std::runtime_error("OfdProcesser::restore()->Error decompressing file");
        } else {
            if (bend->get(rpath.generic_string() + rfn, fpath) < 0)
                throw std::runtime_error("OfdProcesser::restore()->Error getting file");
        }
    } else { // patch file
        std::map<std::string, std::string>::const_iterator j;
        for (j = i; i != tstamps.end(); --i) if ((i->second[0]|0x020) == 'f') break;
        if (i == tstamps.end()) throw std::runtime_error("OfdProcesser::restore()->Base version not found");
        if (bend->get(rpath.generic_string() + i->second, zpath) < 0)
            throw std::runtime_error("OfdProcesser::restore()->Error getting file");
        std::string bpath = zpath + ".base";
        std::string ppath = zpath + ".patch";
        std::string tpath = zpath + ".tmp";
        if (i->second[0] == 'f') {
            if (zlib_inf(zpath, bpath) != 0)
                throw std::runtime_error("OfdProcesser::restore()->Error decompressing file");
        } else {
            boost::filesystem::copy_file(zpath, bpath, boost::filesystem::copy_option::overwrite_if_exists);
        }
        for (++i; ; ++i) {
            DLOG << "OfdProcesser::restore()->applying patch " << i->second;
            if (bend->get(rpath.generic_string() + i->second, zpath) < 0)
                throw std::runtime_error("OfdProcesser::restore()->Error getting file");
//            if (i->second[0] != 'F') {
                if (zlib_inf(zpath, ppath) != 0)
                    throw std::runtime_error("OfdProcesser::restore()->Error decompressing file");
//            } else {
//                boost::filesystem::copy_file(zpath, ppath, boost::filesystem::copy_option::overwrite_if_exists);
//            }
            if (rdiff_patch(bpath, ppath, tpath) != 0)
                throw std::runtime_error("OfdProcesser::restore()->Error applying patch");
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
extern void sched_end();
extern void sched_reset();
static std::string refill_item(int i, int j) {
    std::string s = "__refill_item__" + boost::lexical_cast<std::string>(i) + "_" +
        boost::lexical_cast<std::string>(j) + "__";
    return s;
}
void ofd_processer_rebuild(const std::string &mn)
{
    boost::scoped_ptr<Backend> bend(props::createBackend());
    if (!bend) return;
    sched_end();
    ILOG << "ofd_processer_rebuild("<<mn<<") INIT";
    ofd_reset(mn);
    int i, not_found = 0;
    for (i = 0; not_found < 1000; i++) { try {
        //std::string rinfo, rinfo_path = props::get().rpath + ofdproc_rpath(boost::lexical_cast<std::string>(i)) + "info";
        std::string rinfo, rinfo_path = ofdproc_rpath(boost::lexical_cast<std::string>(i)) + "info";
        if (bend->get_s(rinfo_path, rinfo) == 0) {
            std::stringstream ss; ss << rinfo;
            boost::property_tree::ptree info;
            boost::property_tree::read_json(ss, info);
            std::string path = info.get<std::string>("path");
            int fid = ofd_get(path);
            for (int j = 0; fid > 0 && fid < i && j < 1000; j++) {
                ofd_rm(path);
                fid = ofd_get(path);
            }
            if (fid == i) {
                ofd_set_info(fid, info.get<std::string>("tstamp"));
            } else {
                ofd_rm(path);
                WLOG << "ofd_processer_rebuild("<<mn<<", "<<i<<")->got fid: " << fid;
            }
            not_found = 0;
        } else not_found++;
        } catch (std::exception &e) {
            DLOG << "ofd_processer_rebuild("<<mn<<", "<<i<<"): " << e.what();
        }
    }
    sched_reset();
    ILOG << "ofd_processer_rebuild("<<mn<<") "<<i<<" END";
}



#ifndef NDEBUG
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include "dict.h"
#include "persistence.h"

//#include <iostream>

extern int ccrules_get_rule_id(const std::string &rule_name);
extern void ccrules_add_rule(const std::string &fname, int action, int param);

BOOST_AUTO_TEST_SUITE (main_test_suite_ofd_processer)

BOOST_AUTO_TEST_CASE (ofd_processer_tests_rebuild)
{
    size_t fsth = file_size_th;
    file_size_th = 1024*1024*1024;
    props::init();
    persistence_init();
    BOOST_CHECK( ofd_reset("") == 0 );
    std::string testdir = "ofd_processer_test_rebuild/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.tmpd", testdir + "tmpd/");
    boost::filesystem::create_directories(dict_get("props.tmpd"));
    dict_setv("localBackend.path", testdir + "local/");
    boost::filesystem::create_directories(dict_get("localBackend.path"));
    dict_setv("props.backend", "local");
    dict_setv("props.processer", "ofd1");
    props::update();

    int fid[16];
    std::string data = "12345";
    std::string fname = testdir + "f1.txt";
    boost::filesystem::create_directories(testdir + "/one/bi/tres/four/sei");
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = fname;
    boost::scoped_ptr<Processer> procsr(props::createProcesser(0));
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    fentry->initime_last = 1000000000001;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[0] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/f2.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[1] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/f3.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[2] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/bi/tres/four/sei/f7.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[3] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/bi/tres/four/sei/f17.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[4] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/bi/f7.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[5] = ofd_find(fname)) > 0 );
    fname = testdir + "/one/bi/tres/f7.txt";
    fentry->fname = fname;
    { ofstream_utf8 ofs(fname.c_str()); ofs << data; }
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    BOOST_CHECK( (fid[6] = ofd_find(fname)) > 0 );

    BOOST_CHECK( (fid[7] = ofd_find(testdir)) > 0 );
    BOOST_CHECK( (fid[8] = ofd_find(testdir + "/one")) > 0 );
    BOOST_CHECK( (fid[9] = ofd_find(testdir + "/one/bi")) > 0 );
    BOOST_CHECK( (fid[10] = ofd_find(testdir + "/one/bi/tres")) > 0 );
    BOOST_CHECK( (fid[11] = ofd_find(testdir + "/one/bi/tres/four")) > 0 );
    BOOST_CHECK( (fid[12] = ofd_find(testdir + "/one/bi/tres/four/sei")) > 0 );

    std::string mn = ofd_get_magic();
    BOOST_CHECK( ofd_reset("") == 0 );
    ofd_processer_rebuild(mn);

    fname = testdir + "f1.txt";
    BOOST_CHECK( fid[0] == ofd_find(fname) );
    fname = testdir + "/one/f2.txt";
    BOOST_CHECK( fid[1] == ofd_find(fname) );
    fname = testdir + "/one/f3.txt";
    BOOST_CHECK( fid[2] == ofd_find(fname) );
    fname = testdir + "/one/bi/tres/four/sei/f7.txt";
    BOOST_CHECK( fid[3] == ofd_find(fname) );
    fname = testdir + "/one/bi/tres/four/sei/f17.txt";
    BOOST_CHECK( fid[4] == ofd_find(fname) );
    fname = testdir + "/one/bi/f7.txt";
    BOOST_CHECK( fid[5] == ofd_find(fname) );
    fname = testdir + "/one/bi/tres/f7.txt";
    BOOST_CHECK( fid[6] == ofd_find(fname) );
    BOOST_CHECK( (fid[8] == ofd_find(testdir + "/one")) );
//std::cout << "fid: " << fid[9] << " / " << ofd_find(testdir + "/one/bi") << std::endl;
    BOOST_CHECK( (fid[9] == ofd_find(testdir + "/one/bi")) );
//std::cout << "fid: " << fid[10] << " / " << ofd_find(testdir + "/one/bi/tres") << std::endl;
    BOOST_CHECK( (fid[10] == ofd_find(testdir + "/one/bi/tres")) );
//std::cout << "fid: " << fid[11] << " / " << ofd_find(testdir + "/one/bi/tres/four") << std::endl;
    BOOST_CHECK( (fid[11] == ofd_find(testdir + "/one/bi/tres/four")) );
//std::cout << "fid: " << fid[12] << " / " << ofd_find(testdir + "/one/bi/tres/four/sei") << std::endl;
    BOOST_CHECK( (fid[12] == ofd_find(testdir + "/one/bi/tres/four/sei")) );


    boost::filesystem::remove_all(testdir);
    file_size_th = fsth;
    persistence_end();
    BOOST_CHECK( ofd_reset("") == 0 );
    ofd_end();
}


BOOST_AUTO_TEST_CASE (ofd_processer_tests)
{
    size_t fsth = file_size_th;
    file_size_th = 1024*1024*1024;
    props::init();
    persistence_init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "ofd_processer_test_dir/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.tmpd", testdir + "tmpd/");
    boost::filesystem::create_directories(dict_get("props.tmpd"));
    dict_setv("localBackend.path", testdir + "local/");
    boost::filesystem::create_directories(dict_get("localBackend.path"));
    dict_setv("props.backend", "local");
    dict_setv("props.processer", "ofd1");
    props::update();

    std::string data = "12345";
    std::string fname = testdir + "f1.txt";
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = fname;
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
    { ofstream_utf8 ofs(fname.c_str()); ofs << data << "99"; }
    fentry->initime_last = 1000000000008;
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );

    std::string lpath = dict_get("localBackend.path") + ofdproc_rpath(boost::lexical_cast<std::string>(ofd_find(fname)));
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000001") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000002") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000003") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000004") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000005") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000006") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "p1000000000008") );

    std::map<std::string, std::string> versions = procsr->get_versions(fentry->sname, fentry->fname);
    BOOST_CHECK( versions.size() == 7 );

    {std::map<std::string, std::string>::const_iterator i;
    i = less_nearer_tstamp("0", versions); BOOST_CHECK( i->first == "1000000000001" );
    i = less_nearer_tstamp("1000000000000", versions); BOOST_CHECK( i->first == "1000000000001" );
    i = less_nearer_tstamp("1000000000001", versions); BOOST_CHECK( i->first == "1000000000001" );
    i = less_nearer_tstamp("1000000000001", versions); BOOST_CHECK( i->first == "1000000000001" );
    i = less_nearer_tstamp("1000000000003", versions); BOOST_CHECK( i->first == "1000000000003" );
    i = less_nearer_tstamp("1000000000004", versions); BOOST_CHECK( i->first == "1000000000004" );
    i = less_nearer_tstamp("1000000000006", versions); BOOST_CHECK( i->first == "1000000000006" );
    i = less_nearer_tstamp("1000000000007", versions); BOOST_CHECK( i->first == "1000000000006" );
    i = less_nearer_tstamp("1000000000008", versions); BOOST_CHECK( i->first == "1000000000008" );
    i = less_nearer_tstamp("1000000000009", versions); BOOST_CHECK( i->first == "1000000000008" );
    i = less_nearer_tstamp("9990000000009", versions); BOOST_CHECK( i->first == "1000000000008" );}


    std::string fr = dict_get("props.tmpd") + "fr.txt";
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

    lpath = dict_get("localBackend.path") + ofdproc_rpath(boost::lexical_cast<std::string>(ofd_find(fname)));
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000001") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000002") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000003") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000004") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000005") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "f1000000000006") );

    fr = dict_get("props.tmpd") + "fr.txt";
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
    size_t lgl = log_trace_level;
log_trace_level = 1;
    fentry->event = "DEL";
    boost::filesystem::remove(fentry->fname);
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    versions.clear();
    versions = procsr->get_versions(fentry->sname, fentry->fname);
    //BOOST_CHECK( versions.size() == 0 );
log_trace_level = lgl;
    data = "12345";
    fname = testdir + "f4.txt";
    fentry->event = "MOD";
    fentry->fname = fname;
    ccrules_add_rule(fname, ccrules_get_rule_id("no_deflate"), 0);
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

    lpath = dict_get("localBackend.path") + ofdproc_rpath(boost::lexical_cast<std::string>(ofd_find(fname)));
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000001") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000002") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000003") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000004") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000005") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000006") );

    fr = dict_get("props.tmpd") + "fr.txt";
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

    BOOST_CHECK( ofd_rm(testdir + "f1.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "f2.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "f4.txt") > 0);
log_trace_level = 2;
    BOOST_CHECK( ofd_rm(testdir + "f3.txt") < 0);

    boost::filesystem::remove_all(testdir);
    file_size_th = fsth;
    persistence_end();
    ofd_end();
}

BOOST_AUTO_TEST_CASE (ofd_processer_tests_disk)
{
    size_t fsth = file_size_th;
    file_size_th = 1;
    props::init();
    persistence_init();
    BOOST_CHECK( ofd_init() == 0 );
    std::string testdir = "ofd_processer_test_dir_disk/";
    boost::system::error_code ec;
    boost::filesystem::remove_all(testdir, ec);
    dict_setv("props.tmpd", testdir + "tmpd/");
    boost::filesystem::create_directories(dict_get("props.tmpd"));
    dict_setv("localBackend.path", testdir + "local/");
    boost::filesystem::create_directories(dict_get("localBackend.path"));
    dict_setv("props.backend", "local");
    dict_setv("props.processer", "ofd1");
    props::update();

    std::string data = "12345";
    std::string fname = testdir + "f1.txt";
    boost::shared_ptr<fentry_t> fentry = boost::make_shared<fentry_t>();
    fentry->fname = fname;
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
     std::string fr = dict_get("props.tmpd") + "fr.txt";
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
    fname = testdir + "f4.txt";
    fentry->fname = fname;
    ccrules_add_rule(fname, ccrules_get_rule_id("no_deflate"), 0);
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

    fr = dict_get("props.tmpd") + "fr.txt";
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
    std::string lpath = dict_get("localBackend.path") + ofdproc_rpath(boost::lexical_cast<std::string>(ofd_find(fname)));
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000001") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000002") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000003") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000004") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000005") );
    BOOST_CHECK( boost::filesystem::is_regular_file(lpath + "F1000000000006") );


    data = "12345";
    fname = testdir + "f3.txt";
    fentry->fname = fname;
    ccrules_add_rule(fname, ccrules_get_rule_id("reconst"), 0);
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

    fr = dict_get("props.tmpd") + "fr.txt";
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
log_trace_level = 1;
    fentry->event = "DEL";
    boost::filesystem::remove(fentry->fname);
    BOOST_CHECK( procsr->process(fentry) == 0 );
    BOOST_CHECK( fentry->status == 0 );
    versions.clear();
    versions = procsr->get_versions(fentry->sname, fentry->fname);
    //BOOST_CHECK( versions.size() == 0 );

    BOOST_CHECK( ofd_rm(testdir + "f1.txt") > 0);
    BOOST_CHECK( ofd_rm(testdir + "f2.txt") > 0);
log_trace_level = 2;
    BOOST_CHECK( ofd_rm(testdir + "f3.txt") < 0);

    boost::filesystem::remove_all(testdir);
    file_size_th = fsth;
    persistence_end();
    ofd_end();
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
