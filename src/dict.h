
#ifndef _DICT_H
#define _DICT_H

#include <map>
#include <string>

void dict_set(std::string id, size_t *v);
void dict_set(std::string id, std::string *v);
void dict_set_enc(std::string id, std::string *v);

void dict_setv(std::string id, size_t v);
void dict_setv(std::string id, const std::string &v);

std::string dict_get(std::string id);

int dict_load(std::string fname);
int dict_save();
int dict_load();

std::string dict_dump();
void dict_get_all(std::map<std::string, std::string> &vars);

#endif
