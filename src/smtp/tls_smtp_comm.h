
#ifndef _TLS_SMTP_COMM_H_
#define _TLS_SMTP_COMM_H_

#include "smtp_comm.h"

struct tls_priv;

class tls_smtp_comm : public smtp_comm
{
    tls_priv *tls_con;
    int port;
    std::string server;

public:
    tls_smtp_comm(int port, const std::string &server);
    ~tls_smtp_comm();
    int command(const std::string &cmd);
    int send(const std::string &data);
};

#endif
