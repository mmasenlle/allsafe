
#ifndef _ALLSAFE_H
#define _ALLSAFE_H

#include <string>
#include <boost/shared_ptr.hpp>
#include "fentry.h"

struct allsafe_t
{
    size_t mode;
    std::string alias;
    std::string url;

    size_t conn_min;
    size_t conn_max;

    static const struct allsafe_t & get();

    static void init();
    static void run();
    static void push(const boost::shared_ptr<fentry_t> &fentry);
};

#endif // _ALLSAFE_H
