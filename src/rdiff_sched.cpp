#include <vector>
#include <string>
#include "fstream_utf8.h"
#include "librsync.h"
#include "log_sched.h"

//static size_t block_len = RS_DEFAULT_BLOCK_LEN;
//static size_t strong_len = 0;
//static rs_magic_number sig_magic = RS_BLAKE2_SIG_MAGIC;

#define FBUFF_BUF_SIZE 0x4000
struct fbuff_t
{
	void *opaopaque;
	char buf[FBUFF_BUF_SIZE];
};

static rs_result rs_infilebuf_fill_ifstream(rs_job_t *job, rs_buffers_t *buf,
                            void *opaque)
{
	if (buf->avail_in || buf->eof_in)
        /* Still some data remaining.  Perhaps we should read
           anyhow? */
        return RS_DONE;
	fbuff_t *fbuff = (fbuff_t *)opaque;
	ifstream_utf8 *ifs = (ifstream_utf8 *)fbuff->opaopaque;
	bool eof = !ifs->read(fbuff->buf, FBUFF_BUF_SIZE);
	buf->avail_in = ifs->gcount();
    buf->next_in = fbuff->buf;
	buf->eof_in = (ifs->gcount() == 0);
    return RS_DONE;
}

/*
 * The buf is already using BUF for an output buffer, and probably
 * contains some buffered output now.  Write this out to F, and reset
 * the buffer cursor.
 */
static rs_result rs_outfilebuf_drain_ofstream(rs_job_t *job, rs_buffers_t *buf, void *opaque)
{
    int present;
    fbuff_t *fbuff = (fbuff_t *)opaque;
	ofstream_utf8 *ofs = (ofstream_utf8 *)fbuff->opaopaque;

    /* This is only allowed if either the buf has no output buffer
     * yet, or that buffer could possibly be BUF. */
    if (buf->next_out == NULL) {
//        assert(buf->avail_out == 0);

        buf->next_out = fbuff->buf;
        buf->avail_out = FBUFF_BUF_SIZE;

        return RS_DONE;
    }


    present = buf->next_out - fbuff->buf;
    if (present > 0) {
		ofs->write(fbuff->buf, present);

        buf->next_out = fbuff->buf;
        buf->avail_out = FBUFF_BUF_SIZE;
    }

    return RS_DONE;
}

static rs_result rs_file_copy_cb_ifstream(void *arg, rs_long_t pos, size_t *len, void **buf)
{
	ifstream_utf8 *ifs = (ifstream_utf8 *)arg;
	ifs->seekg(pos);
	ifs->read((char*)*buf, *len);
	*len = ifs->gcount();
	return *len ? RS_DONE : RS_INPUT_ENDED;
}


int rdiff_sign(const std::string &src, std::size_t fs, const std::string &sgn)
{
    TLOG << "rdiff_sign() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;

	try {
	fbuff_t inb, outb;
	ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
	ofstream_utf8 ofs(sgn.c_str(), ofstream_utf8::binary);

	if (!ifs || !ofs) throw std::runtime_error("Opening file");
	inb.opaopaque = &ifs;
	outb.opaopaque = &ofs;

    size_t block_len = (fs >> 12);
    if (block_len < RS_DEFAULT_BLOCK_LEN) block_len = RS_DEFAULT_BLOCK_LEN;
TLOG << "rdiff_sign(" << src << ", " << fs << ", " << sgn << ")->block_len: " << (fs >> 12) << "/" << block_len;
    job = rs_sig_begin(block_len, 0, RS_BLAKE2_SIG_MAGIC);
    if (!job) throw std::runtime_error("rs_sig_begin->NULL");
	r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_ifstream, &inb,
                          rs_outfilebuf_drain_ofstream, &outb);

	} catch (std::exception &e) {
        ELOG << "rdiff_sign(" << src << ", " << fs << ", " << sgn << "): " << e.what();
 	}
	if (job) rs_job_free(job);
	TLOG << "rdiff_sign() END";
	return r;
}

int rdiff_delta(const std::string &sgn, const std::string &src, const std::string &delta)
{
    TLOG << "rdiff_delta() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;
	rs_signature_t  *sumset = 0;

	try {
            {fbuff_t inb;
        ifstream_utf8 ifs(sgn.c_str(), ifstream_utf8::binary);
        if (!ifs) throw std::runtime_error("Opening signatures file");
        inb.opaopaque = &ifs;
        job = rs_loadsig_begin(&sumset);
        if (!job) throw std::runtime_error("rs_loadsig_begin->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_ifstream, &inb,
                          NULL, NULL);
		rs_job_free(job); job = 0;}
		if ((r = rs_build_hash_table(sumset)) != RS_DONE) throw std::runtime_error("Building hash table");
		r = RS_INTERNAL_ERROR;
            {fbuff_t inb, outb;
        ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
        ofstream_utf8 ofs(delta.c_str(), ofstream_utf8::binary);
        if (!ifs || !ofs) throw std::runtime_error("Opening file");
        inb.opaopaque = &ifs;
        outb.opaopaque = &ofs;
        job = rs_delta_begin(sumset);
        if (!job) throw std::runtime_error("rs_delta_begin->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_ifstream, &inb,
                          rs_outfilebuf_drain_ofstream, &outb);}
	} catch (std::exception &e) {
        ELOG << "rdiff_delta(" << sgn << ", " << src << ", " << delta << "): " << e.what();
 	}
 	if (sumset) rs_free_sumset(sumset);
	if (job) rs_job_free(job);
	TLOG << "rdiff_delta() END";
	return r;
}

int rdiff_patch(const std::string &src, const std::string &delta, const std::string &dst)
{
    TLOG << "rdiff_patch() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;

	try {
        fbuff_t inb, outb;
        ifstream_utf8 ifs1(src.c_str(), ifstream_utf8::binary);
        ifstream_utf8 ifs(delta.c_str(), ifstream_utf8::binary);
        ofstream_utf8 ofs(dst.c_str(), ofstream_utf8::binary);

        if (!ifs1 || !ifs || !ofs) throw std::runtime_error("Opening file");
        inb.opaopaque = &ifs;
        outb.opaopaque = &ofs;

        job = rs_patch_begin(rs_file_copy_cb_ifstream, &ifs1);
        if (!job) throw std::runtime_error("rs_patch_begin->NULL");
        r = rs_job_drive(job, &buf,
                              rs_infilebuf_fill_ifstream, &inb,
                              rs_outfilebuf_drain_ofstream, &outb);
	} catch (std::exception &e) {
        ELOG << "rdiff_patch(" << src << ", " << delta << ", " << dst << "): " << e.what();
 	}
	if (job) rs_job_free(job);
	TLOG << "rdiff_patch() END";
	return r;
}

//////////////////////// mem
static rs_result rs_infilebuf_fill_m(rs_job_t *job, rs_buffers_t *buf,
                            void *opaque)
{
	if (buf->avail_in || buf->eof_in)
        return RS_DONE;
	const std::vector<unsigned char> *src = (const std::vector<unsigned char> *)opaque;
	buf->avail_in = src->size();
    buf->next_in = (char*)&src->front(); //(char*)src->data();
	buf->eof_in = (1);
    return RS_DONE;
}
static rs_result rs_outfilebuf_drain_m(rs_job_t *job, rs_buffers_t *buf, void *opaque)
{
    int present;
    fbuff_t *fbuff = (fbuff_t *)opaque;

    if (buf->next_out == NULL) {
        buf->next_out = fbuff->buf;
        buf->avail_out = FBUFF_BUF_SIZE;
        return RS_DONE;
    }
    present = buf->next_out - fbuff->buf;
    if (present > 0) {
        std::vector<unsigned char> *dst = (std::vector<unsigned char> *)fbuff->opaopaque;
        dst->insert(dst->end(), fbuff->buf, fbuff->buf + present);

        buf->next_out = fbuff->buf;
        buf->avail_out = FBUFF_BUF_SIZE;
    }
    return RS_DONE;
}

int rdiff_sign_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &sgn)
{
    TLOG << "rdiff_sign_m() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;
	try {
        fbuff_t outb;
        outb.opaopaque = &sgn;
        sgn.clear();
        size_t block_len = (src.size() >> 12);
        if (block_len < RS_DEFAULT_BLOCK_LEN) block_len = RS_DEFAULT_BLOCK_LEN;

        job = rs_sig_begin(block_len, 0, RS_BLAKE2_SIG_MAGIC);
        if (!job) throw std::runtime_error("rs_sig_begin->NULL");
        r = rs_job_drive(job, &buf,
                              rs_infilebuf_fill_m, const_cast<std::vector<unsigned char> *>(&src),
                              rs_outfilebuf_drain_m, &outb);
	} catch (std::exception &e) {
        ELOG << "rdiff_sign_m(" << src.size() << ", " << sgn.size() << "): " << e.what();
 	}
	if (job) rs_job_free(job);
	TLOG << "rdiff_sign_m() END";
	return r;
}
int rdiff_delta_m(const std::vector<unsigned char> &sgn, const std::vector<unsigned char> &src, std::vector<unsigned char> &delta)
{
    TLOG << "rdiff_delta_m() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;
	rs_signature_t  *sumset = 0;
	try {
        job = rs_loadsig_begin(&sumset);
        if (!job) throw std::runtime_error("rs_loadsig_begin->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_m, const_cast<std::vector<unsigned char> *>(&sgn),
                          NULL, NULL);
		rs_job_free(job); job = 0;
		if ((r = rs_build_hash_table(sumset)) != RS_DONE) throw std::runtime_error("Building hash table");
		r = RS_INTERNAL_ERROR;
            {fbuff_t outb; outb.opaopaque = &delta; delta.clear();
        job = rs_delta_begin(sumset);
        if (!job) throw std::runtime_error("rs_delta_begin->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_m, const_cast<std::vector<unsigned char> *>(&src),
                          rs_outfilebuf_drain_m, &outb);}
	} catch (std::exception &e) {
        ELOG << "rdiff_delta_m(" << sgn.size() << ", " << src.size() << ", " << delta.size() << "): " << e.what();
 	}
 	if (sumset) rs_free_sumset(sumset);
	if (job) rs_job_free(job);
	TLOG << "rdiff_delta_m() END";
	return r;
}
int rdiff_sign_m1(const std::string &src, std::size_t size, std::vector<unsigned char> &sgn)
{
    TLOG << "rdiff_sign_m1() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;
	try {
        fbuff_t inb, outb;
        ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
		if (!ifs) throw std::runtime_error("Opening file");
        inb.opaopaque = &ifs;
        outb.opaopaque = &sgn; sgn.clear();
        size_t block_len = (size >> 12);
        if (block_len < RS_DEFAULT_BLOCK_LEN) block_len = RS_DEFAULT_BLOCK_LEN;

        job = rs_sig_begin(block_len, 0, RS_BLAKE2_SIG_MAGIC);
        if (!job) throw std::runtime_error("rs_sig_begin->NULL");
        r = rs_job_drive(job, &buf,
                              rs_infilebuf_fill_ifstream, &inb,
                              rs_outfilebuf_drain_m, &outb);
	} catch (std::exception &e) {
        ELOG << "rdiff_sign_m1(" << src << ", " << sgn.size() << "): " << e.what();
 	}
	if (job) rs_job_free(job);
	TLOG << "rdiff_sign_m1() END";
	return r;
}
int rdiff_delta_m1(const std::vector<unsigned char> &sgn, const std::string &src, const std::string &delta)
{
    TLOG << "rdiff_delta_m1() INIT";
    rs_job_t        *job = 0;
    rs_result       r = RS_INTERNAL_ERROR;
	rs_buffers_t    buf;
	rs_signature_t  *sumset = 0;

	try {
        job = rs_loadsig_begin(&sumset);
        if (!job) throw std::runtime_error("rs_loadsig_begin->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_m, const_cast<std::vector<unsigned char> *>(&sgn),
                          NULL, NULL);
		rs_job_free(job); job = 0;
		if ((r = rs_build_hash_table(sumset)) != RS_DONE) throw std::runtime_error("Building hash table");
		r = RS_INTERNAL_ERROR;
            {fbuff_t inb, outb;
        ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
        ofstream_utf8 ofs(delta.c_str(), ofstream_utf8::binary);
        if (!ifs || !ofs) throw std::runtime_error("Opening file");
        inb.opaopaque = &ifs;
        outb.opaopaque = &ofs;
        job = rs_delta_begin(sumset);
        if (!job) throw std::runtime_error("rdiff_delta_m1->NULL");
        r = rs_job_drive(job, &buf,
                          rs_infilebuf_fill_ifstream, &inb,
                          rs_outfilebuf_drain_ofstream, &outb);}
	} catch (std::exception &e) {
        ELOG << "rdiff_delta_m1(" << sgn.size() << ", " << src << ", " << delta << "): " << e.what();
 	}
 	if (sumset) rs_free_sumset(sumset);
	if (job) rs_job_free(job);
	TLOG << "rdiff_delta_m1() END";
	return r;
}



#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

static size_t f2v(const std::string &fn, std::vector<unsigned char> &v)
{
    v.resize(boost::filesystem::file_size(fn));
    ifstream_utf8 ifs(fn.c_str(), ifstream_utf8::binary);
    ifs.read((char*)&v.front(), v.size());
    return ifs.gcount();
}
static size_t v2f(const std::vector<unsigned char> &v, const std::string &fn)
{
    {ofstream_utf8 ofs(fn.c_str(), ofstream_utf8::binary);
    ofs.write((const char*)&v.front(), v.size());}
    return boost::filesystem::file_size(fn);
}

BOOST_AUTO_TEST_SUITE (main_test_suite_rdiff)

BOOST_AUTO_TEST_CASE (rdiff_tests)
{
    std::vector<unsigned char> data,data2,data3,data1;
    srand(time(0));
    for (int i = 0; i < 321; ++i) {
        unsigned long n = rand();
        unsigned char *b = (unsigned char *)&n;
        for (int k = 0; k < 7; ++k) for (int j = 0; j < sizeof(n); ++j) {
            data.push_back(b[j]);data.push_back(b[j]);data.push_back(b[j]);
        }
    }
    data2 = data;
    data2.insert(data2.end(), data.begin(), data.end());
    data2.insert(data2.end(), data.begin(), data.end());
    data3.push_back(rand());
    data3.insert(data3.end(), data.begin(), data.end());
    data3.push_back(rand());data3.push_back(rand());
    data3.insert(data3.end(), data.begin(), data.end());
    data3.push_back(rand());data3.push_back(rand());
    data3.insert(data3.end(), data.begin(), data.end());
    data3.push_back(rand());
    std::string f1 = "rdiff_autotests_test1.dat", f2 = "rdiff_autotests_test2.dat",
        f3 = "rdiff_autotests_test3.dat", f4 = "rdiff_autotests_test4.dat", f5 = "rdiff_autotests_test5.dat";
    BOOST_CHECK( v2f(data2, f2) == data2.size() );
    BOOST_CHECK( v2f(data3, f3) == data3.size() );
    BOOST_CHECK( rdiff_sign(f2, data2.size(), f1) == 0 );
    BOOST_CHECK( rdiff_sign_m(data2, data) == 0 );
    BOOST_CHECK( f2v(f1, data1) == data1.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_sign_m1(f2, data2.size(), data1) == 0 );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_delta(f1, f3, f4) == 0 );
    BOOST_CHECK( rdiff_delta_m(data, data3, data1) == 0 );
    BOOST_CHECK( rdiff_delta_m1(data, f3, f5) == 0 );
    BOOST_CHECK( f2v(f4, data) == data.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( f2v(f5, data) == data.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_patch(f2, f4, f1) == 0 );
    BOOST_CHECK( f2v(f1, data1) == data1.size() );
    BOOST_CHECK( data1 == data3 );


    boost::filesystem::remove(f1);
    boost::filesystem::remove(f2);
    boost::filesystem::remove(f3);
    boost::filesystem::remove(f4);
    boost::filesystem::remove(f5);
}

BOOST_AUTO_TEST_CASE (rdiff_tests_2)
{
    //log_trace_level = 12;
    std::vector<unsigned char> data,data2,data3,data1;
    srand(time(0));
    int n = rand()%600;
    for (int i = 0; i < n; ++i) data.insert(data.end(), 1000, (unsigned char)rand());
    data2 = data;
    data2.insert(data2.end(), data.begin(), data.end());
    data2.insert(data2.end(), data.begin(), data.end());
    data2.insert(data2.end(), data.begin(), data.end());
    n = rand()%30;
    if (n&1) data3.push_back(rand());
    for (int i = 0; i < n; ++i) {
        data3.insert(data3.end(), data.begin(), data.end());
        for (int j = 0; j < (n*i)%13; ++j) data3.push_back(rand());
    }
    if (n&2) data3.insert(data3.end(), data.begin(), data.end());
    std::string f1 = "rdiff_autotests_test1.dat", f2 = "rdiff_autotests_test2.dat",
        f3 = "rdiff_autotests_test3.dat", f4 = "rdiff_autotests_test4.dat", f5 = "rdiff_autotests_test5.dat";
    BOOST_CHECK( v2f(data2, f2) == data2.size() );
    BOOST_CHECK( v2f(data3, f3) == data3.size() );
    BOOST_CHECK( rdiff_sign(f2, data2.size(), f1) == 0 );
    BOOST_CHECK( rdiff_sign_m(data2, data) == 0 );
    BOOST_CHECK( f2v(f1, data1) == data1.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_sign_m1(f2, data2.size(), data1) == 0 );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_delta(f1, f3, f4) == 0 );
    BOOST_CHECK( rdiff_delta_m(data, data3, data1) == 0 );
    BOOST_CHECK( rdiff_delta_m1(data, f3, f5) == 0 );
    BOOST_CHECK( f2v(f4, data) == data.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( f2v(f5, data) == data.size() );
    BOOST_CHECK( data == data1 );
    BOOST_CHECK( rdiff_patch(f2, f4, f1) == 0 );
    BOOST_CHECK( f2v(f1, data1) == data1.size() );
    BOOST_CHECK( data1 == data3 );

    boost::filesystem::remove(f1);
    boost::filesystem::remove(f2);
    boost::filesystem::remove(f3);
    boost::filesystem::remove(f4);
    boost::filesystem::remove(f5);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif // NDEBUG

