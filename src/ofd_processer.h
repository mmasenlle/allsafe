
#ifndef _OFD_PROCESSER_H_
#define _OFD_PROCESSER_H_

#include "processer.h"
#include "backend.h"


class OfdProcesser : public Processer
{
    Backend *bend;
    int fid, edn;
    std::string rpath, ststamp;
    bool reconst;
    bool no_deflate;

    int process_m(boost::shared_ptr<fentry_t> &fentry);
    int process_f(boost::shared_ptr<fentry_t> &fentry);

public:
    OfdProcesser(int n);
    ~OfdProcesser();
    int process (boost::shared_ptr<fentry_t> &fentry);
    void release();

    std::map<std::string, std::string> get_versions(const std::string &f, const std::string &fn);
    int restore(const std::string &ts, const std::string &rpath, const std::string &fname, const std::string &fn);
};

#endif
