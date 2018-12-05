
#ifndef _RDIFF_SCHED_H
#define _RDIFF_SCHED_H

#include <string>

int rdiff_sign(const std::string &src, std::size_t size, const std::string &sgn);
int rdiff_delta(const std::string &sgn, const std::string &src, const std::string &delta);
int rdiff_patch(const std::string &src, const std::string &delta, const std::string &dst);

int rdiff_sign_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &sgn);
int rdiff_delta_m(const std::vector<unsigned char> &sgn, const std::vector<unsigned char> &src, std::vector<unsigned char> &delta);

int rdiff_sign_m1(const std::string &src, std::size_t size, std::vector<unsigned char> &sgn);
int rdiff_delta_m1(const std::vector<unsigned char> &sgn, const std::string &src, const std::string &delta);

#endif
