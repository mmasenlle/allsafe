
#ifndef _SSL_SMTP_COMM_H_
#define _SSL_SMTP_COMM_H_

#include "smtp_comm.h"

class comm_priv_ssl;

enum { prot_plain, prot_tls1, prot_ssl };

class ssl_smtp_comm : public smtp_comm
{
    comm_priv_ssl *comm;
    int port;
    int prot;
    std::string server;

public:
    ssl_smtp_comm(int port, const std::string &server, int prot = prot_tls1);
    ~ssl_smtp_comm();
    int command(const std::string &cmd);
    int send(const std::string &data);
};

#endif
