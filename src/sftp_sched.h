
#ifndef _SFTP_SCHED_H
#define _SFTP_SCHED_H

#include <string>

#ifndef LIBSSH2
struct LIBSSH2_SESSION;
struct LIBSSH2_SFTP;
#endif


/// SFTP Session data
struct sftp_session_t
{
    int opened;
    int sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp_session;
    sftp_session_t();
    ~sftp_session_t();

    const std::string *host;
    const std::string *user;
    const std::string *pass;
    const size_t *port;
};

int sftp_init();
void sftp_end();

int sftp_init_session(sftp_session_t *session);
int sftp_end_session(sftp_session_t *session);

int sftp_swrite(sftp_session_t *session, const std::string &forg, const std::string &fdest);
int sftp_sread(sftp_session_t *session, const std::string &forg, const std::string &fdest);
int ssh_exec(sftp_session_t *session, const std::string &cmd, std::string *resp = NULL);

int sftp_swrite_m(sftp_session_t *session, const char *buf, size_t n, const std::string &fdest);
int sftp_sread_s(sftp_session_t *session, const std::string &forg, std::string &str);

#endif
