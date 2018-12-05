
#ifndef _OLD_FILES_DATA_H_
#define _OLD_FILES_DATA_H_

#include <string>
#include <vector>

int ofd_init();
int ofd_end();

// get id of file, create if not exists
int ofd_get(const std::string &fname);
int ofd_find(const std::string &fname); //not create

std::string ofd_get_info(int fid);
int ofd_set_info(int fid, const std::string &checksum);
// get/set signatures
int ofd_get_sign(int fid, std::vector<unsigned char> &sign);
int ofd_set_sign(int fid, const std::vector<unsigned char> &sign);

int ofd_get_childs(const std::string &fname,
        std::vector<std::string> *folders, std::vector<std::string> *files);

int ofd_move(const std::string &fname_old, const std::string &fname_new);
int ofd_rm(const std::string &fname);
int ofd_move2(const std::string &name, const std::string &path_old, const std::string &path_new);

// get parent id and name
int ofd_get_entry(int fid, std::string &name);

int ofd_reset(const std::string &magic);
const std::string &ofd_get_magic();

#endif
