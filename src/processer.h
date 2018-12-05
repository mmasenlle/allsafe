
#ifndef _PROCESSER_H_
#define _PROCESSER_H_

#include <map>
#include <boost/shared_ptr.hpp>
#include "fentry.h"

class Processer
{
protected:
    int thn;
public:
    Processer(int n) : thn(n) {};
    virtual int process(boost::shared_ptr<fentry_t> &fentry) = 0;
    virtual void release() {};
    virtual ~Processer() {};

    virtual std::map<std::string, std::string> get_versions(const std::string &f, const std::string &fn) = 0;
    virtual int restore(const std::string &ts, const std::string &rpath, const std::string &fname, const std::string &fn) = 0;
};

#endif
