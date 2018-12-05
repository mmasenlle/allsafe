/* zpipe.c: example of proper use of zlib's inflate() and deflate()
   Not copyrighted -- provided to the public domain
   Version 1.4  11 December 2005  Mark Adler */

/* Version history:
   1.0  30 Oct 2004  First version
   1.1   8 Nov 2004  Add void casting for unused return values
                     Use switch statement for inflate() return values
   1.2   9 Nov 2004  Add assertions to document zlib guarantees
   1.3   6 Apr 2005  Remove incorrect assertion in inf()
   1.4  11 Dec 2005  Add hack to avoid MSDOS end-of-line conversions
                     Avoid some compiler warnings for input and output buffers
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define CHUNK 16384

#include <vector>
#include <string>
#include "fstream_utf8.h"
#include "log_sched.h"

/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
//static int def(FILE *source, FILE *dest, int level)
static int def(ifstream_utf8 &ifs, ofstream_utf8 &ofs, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return ret;
try {
    /* compress until end of file */
    do {
        bool eof = !ifs.read((char*)in, CHUNK);
        strm.avail_in = ifs.gcount();
        flush = eof ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

//        strm.avail_in = fread(in, 1, CHUNK, source);
//        if (ferror(source)) {
//            (void)deflateEnd(&strm);
//            return Z_ERRNO;
//        }
//        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
//        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;

            ofs.write((const char*)out, have);

//            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
//                (void)deflateEnd(&strm);
//                return Z_ERRNO;
//            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */
} catch (std::exception &e) {
    (void)deflateEnd(&strm);
    throw e;
}

    /* clean up and return */
    (void)deflateEnd(&strm);
    return Z_OK;
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
//static int inf(FILE *source, FILE *dest)
static int inf(ifstream_utf8 &ifs, ofstream_utf8 &ofs)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

try {
    /* decompress until deflate stream ends or end of file */
    do {
        ifs.read((char*)in, CHUNK);
        strm.avail_in = ifs.gcount();

//        strm.avail_in = fread(in, 1, CHUNK, source);
//        if (ferror(source)) {
//            (void)inflateEnd(&strm);
//            return Z_ERRNO;
//        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;

            ofs.write((const char*)out, have);
//            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
//                (void)inflateEnd(&strm);
//                return Z_ERRNO;
//            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

} catch (std::exception &e) {
    (void)inflateEnd(&strm);
    throw e;
}

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

/* report a zlib or i/o error */
static void zerr(int ret)
{
    switch (ret) {
    case Z_ERRNO: throw std::runtime_error("ZLIB: IO ERROR");
    case Z_STREAM_ERROR: throw std::runtime_error("ZLIB: invalid compression level");
    case Z_DATA_ERROR: throw std::runtime_error("ZLIB: invalid or incomplete deflate data");
    case Z_MEM_ERROR: throw std::runtime_error("ZLIB: out of memory");
    case Z_VERSION_ERROR: throw std::runtime_error("ZLIB: version mismatch!");
    }
    throw std::runtime_error("ZLIB: unknown error");
}

int zlib_def(const std::string &src, const std::string &dst)
{
    TLOG << "zlib_def() INIT";
    ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
    if (!ifs) throw std::runtime_error("zlib_def: Can't open " + src);
    ofstream_utf8 ofs(dst.c_str(), ofstream_utf8::binary);
    if (!ofs) throw std::runtime_error("zlib_def: Can't open " + dst);
    TLOG << "zlib_def() INIT def";
    int r = def(ifs, ofs, Z_DEFAULT_COMPRESSION);
    if (r != Z_OK) zerr(r);
    TLOG << "zlib_def() END";
    return 0;
}

int zlib_inf(const std::string &src, const std::string &dst)
{
    TLOG << "zlib_inf() INIT";
    ifstream_utf8 ifs(src.c_str(), ifstream_utf8::binary);
    if (!ifs) throw std::runtime_error("zlib_inf: Can't open " + src);
    ofstream_utf8 ofs(dst.c_str(), ofstream_utf8::binary);
    if (!ofs) throw std::runtime_error("zlib_inf: Can't open " + dst);
    TLOG << "zlib_inf() INIT inf";
    int r = inf(ifs, ofs);
    if (r != Z_OK) zerr(r);
    TLOG << "zlib_inf() END";
    return 0;
}

int zlib_def_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &dst)
{
    TLOG << "zlib_def_m() INIT";
    int ret;
    z_stream strm;
    unsigned char out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) zerr(ret);

    dst.clear();
    strm.avail_in = src.size();
    strm.next_in = (unsigned char *)&src.front();
    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = deflate(&strm, Z_FINISH);
        dst.insert(dst.end(), out, out + CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    (void)deflateEnd(&strm);

    TLOG << "zlib_def_m() END";
    return 0;
}

int zlib_inf_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &dst)
{
    TLOG << "zlib_inf_m() INIT";
    int ret;
    z_stream strm;
    unsigned char out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) zerr(ret);

    strm.avail_in = src.size();
    strm.next_in = (unsigned char *)&src.front();
    dst.clear();
    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = inflate(&strm, Z_NO_FLUSH);
        switch (ret) {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            (void)inflateEnd(&strm);
            zerr(ret);
        }
        dst.insert(dst.end(), out, out + CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    (void)inflateEnd(&strm);
    if (ret != Z_STREAM_END) zerr(ret);
    TLOG << "zlib_inf_m() END";
    return 0;
}

int zlib_def_str(const std::string &src, std::string &dst)
{
    TLOG << "zlib_def_str() INIT";
    if (src.size()) {
    int ret;
    z_stream strm;
    unsigned char out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) zerr(ret);

    dst.clear();
    strm.avail_in = src.size();
    strm.next_in = (unsigned char *)&(src[0]);
    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = deflate(&strm, Z_FINISH);
        dst.insert(dst.end(), out, out + CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    (void)deflateEnd(&strm);
    }
    TLOG << "zlib_def_str() END";
    return 0;
}

int zlib_inf_str(const std::string &src, std::string &dst)
{
    TLOG << "zlib_inf_str() INIT";
    if (src.size()) {
    int ret;
    z_stream strm;
    unsigned char out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) zerr(ret);

    strm.avail_in = src.size();
    strm.next_in = (unsigned char *)&(src[0]);
    dst.clear();
    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = inflate(&strm, Z_NO_FLUSH);
        switch (ret) {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            (void)inflateEnd(&strm);
            zerr(ret);
        }
        dst.insert(dst.end(), out, out + CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    (void)inflateEnd(&strm);
    if (ret != Z_STREAM_END) zerr(ret);
    }
    TLOG << "zlib_inf_str() END";
    return 0;
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

BOOST_AUTO_TEST_SUITE (main_test_suite_zlib)

BOOST_AUTO_TEST_CASE (zlib_tests)
{
    //log_trace_level = 12;
    std::string s1,s2,s = "kañlsjfdaksd389479071935109837akjsfjsk_oiewruhhjsghurehafakj";
    BOOST_CHECK( zlib_def_str(s, s1) == 0 );
    BOOST_CHECK( zlib_inf_str(s1, s2) == 0 );
    BOOST_CHECK( s == s2 );

    std::vector<unsigned char> data,data2,data3;
    data.assign(s.begin(), s.end());
    BOOST_CHECK( zlib_def_m(data, data2) == 0 );
    BOOST_CHECK( zlib_inf_m(data2, data3) == 0 );
    BOOST_CHECK( data == data3 );
    std::string f1 = "zlib_autotests_test1.dat", f2 = "zlib_autotests_test2.dat", f3 = "zlib_autotests_test3.dat";
    BOOST_CHECK( v2f(data, f1) == data.size() );
    BOOST_CHECK( zlib_def(f1, f2) == 0 );
    BOOST_CHECK( zlib_inf(f2, f3) == 0 );
    BOOST_CHECK( f2v(f3, data3) == data3.size() );
    BOOST_CHECK( data == data3 );
    BOOST_CHECK( f2v(f2, data3) == data3.size() );
    BOOST_CHECK( data2 == data3 );
    srand(time(0));
    data.clear();
    for (int i = 0; i < 156321; ++i) {
        unsigned long n = rand();
        unsigned char *b = (unsigned char *)&n;
        for (int k = 0; k < 7; ++k) for (int j = 0; j < sizeof(n); ++j) {
            data.push_back(b[j]);data.push_back(b[j]);data.push_back(b[j]);
        }
    }
    data2 = data;
    data.insert(data.end(), data2.begin(), data2.end());
    data.insert(data.end(), data2.begin(), data2.end());
    BOOST_CHECK( zlib_def_m(data, data2) == 0 );
    BOOST_CHECK( zlib_inf_m(data2, data3) == 0 );
    BOOST_CHECK( data == data3 );
    BOOST_CHECK( v2f(data, f1) == data.size() );
    BOOST_CHECK( zlib_def(f1, f2) == 0 );
    BOOST_CHECK( zlib_inf(f2, f3) == 0 );
    BOOST_CHECK( f2v(f3, data3) == data3.size() );
    BOOST_CHECK( data == data3 );
    BOOST_CHECK( f2v(f2, data3) == data3.size() );
    BOOST_CHECK( data2 == data3 );
    boost::filesystem::remove(f1);
    boost::filesystem::remove(f2);
    boost::filesystem::remove(f3);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif // NDEBUG
