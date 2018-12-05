
#ifndef _RDIFF_PROCESSER_H_
#define _RDIFF_PROCESSER_H_

#include "processer.h"
#include "backend.h"


class RdiffProcesser : public Processer
{
    Backend *bend;

public:
    RdiffProcesser(int n);
    ~RdiffProcesser();
    int process (boost::shared_ptr<fentry_t> &fentry);
    void release();

    std::map<std::string, std::string> get_versions(const std::string &f, const std::string &fn);
    int restore(const std::string &ts, const std::string &rpath, const std::string &fname, const std::string &fn);
};

#endif
