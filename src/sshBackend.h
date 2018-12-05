
#ifndef _SSHBACKEND_H
#define _SSHBACKEND_H

#include "backend.h"
#include "sftp_sched.h"

class SshBackend : public Backend
{
    sftp_session_t sses;
public:
    SshBackend();
    ~SshBackend();
    int put (const std::string &lpath, const std::string &rpath);
    int get (const std::string &rpath, const std::string &lpath);
    int mkdir (const std::string &path);
    int rmdir (const std::string &path);
    int list (const std::string &path, std::string *resp);
    void release();
    static void init();

    int get_s (const std::string &rpath, std::string &str);
    int put_s (const std::string &str, const std::string &rpath);
    int put_m (const std::vector<unsigned char> &data, const std::string &rpath);
};

#endif
