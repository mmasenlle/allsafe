
#include <string>
#include <vector>

int zlib_def(const std::string &src, const std::string &dst);
int zlib_inf(const std::string &src, const std::string &dst);

int zlib_def_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &dst);
int zlib_inf_m(const std::vector<unsigned char> &src, std::vector<unsigned char> &dst);

int zlib_def_str(const std::string &src, std::string &dst);
int zlib_inf_str(const std::string &src, std::string &dst);
