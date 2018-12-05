
#ifndef _BACKEND_H
#define _BACKEND_H

#include <vector>
#include <string>

class Backend
{
public:
    virtual int put (const std::string &lpath, const std::string &rpath) = 0;
    virtual int get (const std::string &rpath, const std::string &lpath) = 0;
    virtual int mkdir (const std::string &path) = 0;
    virtual int rmdir (const std::string &path) = 0;
    virtual int list (const std::string &path, std::string *resp) = 0;
    virtual void release() {};
    virtual ~Backend() {};

    virtual int get_s (const std::string &rpath, std::string &str) = 0;
    virtual int put_s (const std::string &str, const std::string &rpath) = 0;
    virtual int put_m (const std::vector<unsigned char> &data, const std::string &rpath) = 0;
};

#endif // _BACKEND_H
