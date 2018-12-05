
//#define WITH_OPENSSL
#ifdef WITH_OPENSSL

#include <openssl/evp.h>

#define EVP_BF_BLOCK_LENGTH 8

static int bf_cipher(const unsigned char * const ibuf, int len, unsigned char *obuf,
    const unsigned char key[16], const unsigned char iv[8], int enc)
{
	int olen, ret = -1;
	EVP_CIPHER_CTX ctx;
	EVP_CIPHER_CTX_init(&ctx);
	if (!EVP_CipherInit(&ctx, EVP_bf_cbc(), key, iv, enc))
		goto cleanup;
	if (!EVP_CipherUpdate(&ctx, obuf, &olen, ibuf, len))
		goto cleanup;
	if (!EVP_CipherFinal(&ctx, obuf + olen, &ret))
		goto cleanup;
	ret += olen;

cleanup:
	EVP_CIPHER_CTX_cleanup(&ctx);
	return ret;
}

static const unsigned char cipher_key1[16] = { 0x9d,0x9d,0x70,0x26,0xfa,0xd1,0x8a,0x40,0xbc,0xaa,0x0a,0x12,0xec,0x2f,0x43,0xfe };
static const unsigned char cipher_iv1[8] = { 0x99,0x81,0x4f,0xee,0xfe,0xa7,0xe9,0x60 };



#include <vector>
#include <string>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
static std::string encode64(std::vector<unsigned char>::const_iterator i, std::vector<unsigned char>::const_iterator j) {
    return std::string(boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::vector<unsigned char>::const_iterator, 6, 8> >(i), boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::vector<unsigned char>::const_iterator, 6, 8> >(j));
}
static std::vector<unsigned char> decode64(const std::string &val) {
    return std::vector<unsigned char>(boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6>(val.begin()), boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6>(val.end()));
}

std::string cipher_encript1(const std::string &v)
{
    if (v.empty()) return std::string();
    std::vector<unsigned char> tmp(v.size() + EVP_BF_BLOCK_LENGTH);
    int r = bf_cipher((const unsigned char*)&(v[0]), v.size(), &(tmp[0]), cipher_key1, cipher_iv1, 1);
    return encode64(tmp.begin(), tmp.begin() + r);
}
std::string cipher_decript1(const std::string &v)
{
    if (v.empty()) return std::string();
    std::vector<unsigned char> tmp = decode64(v);
    std::string s(tmp.size() + EVP_BF_BLOCK_LENGTH, 0);
    int r = bf_cipher(&(tmp[0]), tmp.size(), (unsigned char*)&(s[0]), cipher_key1, cipher_iv1, 0);
    s.resize(r);
    return s;
}
#else
std::string cipher_encript1(const std::string &v) { return v; }
std::string cipher_decript1(const std::string &v) { return v; }
#endif

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include <iostream>

BOOST_AUTO_TEST_SUITE (main_test_suite_cipher)

BOOST_AUTO_TEST_CASE (cipher_tests1)
{
    std::string s = "駍afjk@sa|f9847,.;:-_85234򑓓8akj'fk\\sd//57u\"tf?縥m!#3yeh>d19iqw sn�cg%fa<ls駅&=83ei+-/*dj";
//    s = "hola";
//    std::string es = cipher_encript1(s);
//std::cout << "cipher_encript1("<<s<<"): " << es << std::endl;
//std::string des = cipher_decript1(es);
//std::cout << "cipher_decript1("<<es<<"): " << des << std::endl;

    BOOST_CHECK( cipher_decript1(cipher_encript1(s)) == s );
}

BOOST_AUTO_TEST_SUITE_END( )

#endif

