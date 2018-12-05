
#ifndef _CC_RULES_H
#define _CC_RULES_H

#include <boost/shared_ptr.hpp>
#include "fentry.h"

enum {
    wait_since_last_copy,
    wait_since_oldest_mod,
    wait_since_newest_mod,
    wait_until_time_of_day,
    only_before_time_of_day,
    wait_num_modifications,
    exclude,
    reconst,
    no_deflate,
};
struct ccrules_exp_t
{
    std::string path;
    int type;
    int arg;
};

const char *ccrules_get_rule_name(unsigned int rule_id);
int ccrules_load();
void ccrules_save();
bool ccrules_hold(boost::shared_ptr<fentry_t> &fentry);
bool ccrules_no_deflate(const std::string &);
bool ccrules_exclude(const std::string &);
bool ccrules_reconst(const std::string &);
std::string ccrules_page(const std::vector<std::string> &vuri);
void ccrules_add_rule(const std::string &fname, int action, int param);
void ccrules_rm_rule(const std::string &fname, int action);
void ccrules_get_all(std::vector<ccrules_exp_t> &rules);

#endif
