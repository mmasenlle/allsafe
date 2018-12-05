
#ifndef _DICT_H
#define _DICT_H

#include <string>

void dict_set(std::string id, size_t *v);
void dict_set(std::string id, std::string *v);

void dict_setv(std::string id, size_t v);
void dict_setv(std::string id, const std::string &v);

std::string dict_get(std::string id);

int dict_load(std::string fname);

//std::string dict_dump();

#endif
