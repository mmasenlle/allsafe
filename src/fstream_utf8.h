
#ifndef _FSTREAM_UTF8_H_
#define _FSTREAM_UTF8_H_

#ifdef __MINGW32__

#include <nowide/fstream.hpp>

#define ifstream_utf8 nowide::ifstream
#define ofstream_utf8 nowide::ofstream

#else

#include <boost/filesystem/fstream.hpp>

#define ifstream_utf8 boost::filesystem::ifstream
#define ofstream_utf8 boost::filesystem::ofstream

#endif // __MINGW32__

#endif // _FSTREAM_UTF8_H_
