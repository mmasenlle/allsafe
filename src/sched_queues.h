
#ifndef _SCHED_QUEUES_H_
#define _SCHED_QUEUES_H_

#include <boost/shared_ptr.hpp>
#include "fentry.h"

extern bool volatile squeues_stop;
enum thstate_t { sth_nul, sth_diff, sth_sync, sth_send, sth_chec,
        sth_copy, sth_post, sth_init, sth_erro, sth_dele, sth_zip,
    sth_last };
void squeues_set_thstate(int thn, thstate_t sth_chec);

void squeues_put_pending(boost::shared_ptr<fentry_t> &fentry);
void squeues_put_blacklist(boost::shared_ptr<fentry_t> &fentry);
void squeues_refish(boost::shared_ptr<fentry_t> &fentry, int tries);
long long squeues_get_file_size(const std::string &fname);
void squeues_rm_file(const std::string &fpath);
void squeues_try_rm_file(const std::string &fpath);
void squeues_add_obytes(long bytes);
void squeues_sync_stats(boost::shared_ptr<fentry_t> &fentry);

#endif
