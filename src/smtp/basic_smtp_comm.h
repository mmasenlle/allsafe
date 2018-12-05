
#ifndef _BASIC_SMTP_COMM_H_
#define _BASIC_SMTP_COMM_H_

#include "smtp_comm.h"

class comm_priv;

class basic_smtp_comm : public smtp_comm
{
    comm_priv *comm;
    int port;
    std::string server;

public:
    basic_smtp_comm(int port, const std::string &server);
    ~basic_smtp_comm();
    int command(const std::string &cmd);
    int send(const std::string &data);
};

#endif
