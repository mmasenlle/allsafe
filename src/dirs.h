
#ifndef _DIRS_H_
#define _DIRS_H_

#include <string>

int dirs_add_dir(std::string d);
int dirs_del_dir(std::string d);
int dirs_load();
void dirs_save();
void dirs_get_all(std::vector<std::string> &dirs);

#endif
