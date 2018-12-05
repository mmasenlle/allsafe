
#ifndef _SMTP_COMM_H_
#define _SMTP_COMM_H_

#include <string>

class smtp_comm
{
public:
    virtual int command(const std::string &cmd) = 0;
    virtual int send(const std::string &data) = 0;
    virtual ~smtp_comm() {};
};

#endif
