/*
 * Sample showing how to do SFTP write transfers.
 *
 */

#ifdef WIN32
# include "libssh2_config.h"
#else
# include <sys/socket.h>
# include <netinet/in.h>
# include <unistd.h>
# include <arpa/inet.h>
#endif
#include <libssh2.h>
#include <libssh2_sftp.h>

#ifdef HAVE_WINSOCK2_H
# include <winsock2.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
# ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

#include <string>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp>
#include "fstream_utf8.h"
#include "log_sched.h"
#define LIBSSH2
#include "props_sched.h"


int sftp_init()
{
#ifdef WIN32
    WSADATA wsadata;
    int err = WSAStartup(MAKEWORD(2,0), &wsadata);
    if (err != 0) {
        FLOG << "WSAStartup failed with error: " << err;
        return 1;
    }
#endif
    int rc = libssh2_init (0);
    if (rc != 0) {
        FLOG << "libssh2 initialization failed: " << rc;
        return 1;
    }
    return 0;
}
void sftp_end()
{
    libssh2_exit();
}

#ifdef WIN32
#define sclose closesocket
#else
#define sclose close
#endif

static int connect_w_to(sftp_session_t *sses)
{
    int res = socket(AF_INET, SOCK_STREAM, 0);
    if (res < 0) {
         ELOG << "connect_w_to()->socket(): " << errno << "-" << strerror(errno);
         return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(*sses->port);
    addr.sin_addr.s_addr = inet_addr(sses->host->c_str());

    // Set non-blocking
#ifdef WIN32
    u_long flags = 1;
    if (NO_ERROR != ioctlsocket(res, FIONBIO, &flags)) {
        ELOG << "connect_w_to()->ioctlsocket(no blocking)";
         sclose(res);
         return -1;
    }
#else
    long arg = fcntl(res, F_GETFL, NULL);
    if (arg < 0) {
         ELOG << "connect_w_to()->fcntl(F_GETFL): " << errno << "-" << strerror(errno);
         sclose(res);
         return -1;
    }
    arg |= O_NONBLOCK;
    if( fcntl(res, F_SETFL, arg) < 0) {
         ELOG << "connect_w_to()->fcntl(F_SETFL->O_NONBLOCK): " << errno << "-" << strerror(errno);
         sclose(res);
         return -1;
    }
#endif
    // Trying to connect with timeout
    int r = connect(res, (struct sockaddr *)&addr, sizeof(addr));
#ifdef WIN32
    if (1) {
#else
    if (r == 0 || (r < 0 && errno == EINPROGRESS)) {
#endif // WIN32
            TLOG << "connect_w_to()->connect(): EINPROGRESS " << r << ", " << errno;
        for (;;) {
            struct timeval tv;
                tv.tv_sec = 45;
                tv.tv_usec = 0;
            fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(res, &fdset);
           int r = select(res+1, NULL, &fdset, NULL, &tv);
           if (r < 0 && errno != EINTR) {
                ELOG << "connect_w_to()->select(): " << errno << "-" << strerror(errno);
                sclose(res);
                return -1;
           } else if (r > 0) { // Socket selected for write
                int valopt;
                socklen_t lon = sizeof(valopt);
                if (getsockopt(res, SOL_SOCKET, SO_ERROR, (char*)(&valopt), &lon) < 0) {
                    ELOG << "connect_w_to()->getsockopt(): " << errno << "-" << strerror(errno);
                    sclose(res);
                    return -1;
                } // Check the value returned...
                if (valopt) {
                    ELOG << "connect_w_to()->delayed connection()(): " << valopt << "-" << strerror(valopt);
                    sclose(res);
                    return -1;
                }
                TLOG << "connect_w_to()->connect(): SUCCESS";
                break;
           } else {
                ELOG << "connect_w_to()-> TIMEOUT";
                sclose(res);
                return -1;
           }
           WLOG << "connect_w_to()->select(): EINTR";
        }
     } else {
        ELOG << "connect_w_to()->connect(): " << errno << "-" << strerror(errno);
        sclose(res);
        return -1;
     }

#if 0
#ifdef WIN32
    flags = 0;
    if (NO_ERROR != ioctlsocket(res, FIONBIO, &flags)) {
        WLOG << "connect_w_to()->ioctlsocket(blocking)";
    }
#else
    arg = fcntl(res, F_GETFL, NULL);
    if (arg < 0) {
        WLOG << "connect_w_to()->fcntl(F_GETFL): " << errno << "-" << strerror(errno);
    }
    arg &= (~O_NONBLOCK);
    if( fcntl(res, F_SETFL, arg) < 0) {
        WLOG << "connect_w_to()->fcntl(F_SETFL->~O_NONBLOCK): " << errno << "-" << strerror(errno);
    }
#endif // WIN32
#endif
    return res;
}

int sftp_init_session(sftp_session_t *sses)
{
    DLOG << "sftp_init_session() INIT";

    if (!sses->host || !sses->user || !sses->pass || !sses->port)
        return -2;

    sses->opened = 0;
    sses->session = 0;

    if ((sses->sock = connect_w_to(sses)) < 0)
        return -1;

    if (!(sses->session = libssh2_session_init())) {
        ELOG << "sftp_init_session() -> libssh2_session_init()";
    }
    libssh2_session_set_blocking(sses->session, 1);
    libssh2_session_set_timeout(sses->session, 50000);

    int rc = libssh2_session_handshake(sses->session, sses->sock);
    if (rc) {
        ELOG << "sftp_init_session() -> Failure establishing SSH session: " << rc;
        sftp_end_session(sses);
        return -1;
    }
    if (libssh2_userauth_password(sses->session, sses->user->c_str(), sses->pass->c_str())) {
        ELOG << "sftp_init_session() -> Authentication by password failed";
        sftp_end_session(sses);
        return -1;
    }
    sses->sftp_session = libssh2_sftp_init(sses->session);
    if (!sses->sftp_session) {
        ELOG << "sftp_init_session() -> Unable to init SFTP session";
        sftp_end_session(sses);
        return -1;
    }

    sses->opened = 1;

    TLOG << "sftp_init_session() END";

    return 0;
}
int sftp_end_session(sftp_session_t *sses)
{
    DLOG << "sftp_end_session() INIT";

    if (sses->sftp_session) libssh2_sftp_shutdown(sses->sftp_session);
    if (sses->session) libssh2_session_disconnect(sses->session,
                               "Normal Shutdown, Thank you for playing");
    if (sses->session) libssh2_session_free(sses->session);

    sclose(sses->sock);

    sses->opened = 0;
    sses->sock = -1;
    sses->session = 0;
    sses->sftp_session = 0;

    TLOG << "sftp_end_session() END";

    return 0;
}
sftp_session_t::sftp_session_t()
{
    opened = 0;
    sock = -1;
    session = 0;
    sftp_session = 0;

    host = NULL;
    user = NULL;
    pass = NULL;
    port = NULL;
}
sftp_session_t::~sftp_session_t()
{
    sftp_end_session(this);
}

extern bool volatile squeues_stop;

int sftp_swrite(sftp_session_t *sses, const std::string &forg, const std::string &fdest)
{
    DLOG << "sftp_swrite(" << forg << ", " << fdest << ")";

    char mem[1024*32];
    size_t nread;
    char *ptr;
    int rc;

    if (!sses->opened) {
        if (sftp_init_session(sses)) {
            ELOG << "sftp_swrite()->sftp_init_session() ERROR";
            return -1;
        }
    }

    ifstream_utf8 local; // el open falla en mingw cuando hay caracteres raros. MSVC va bien.
    for (int i = 0; i < 5 && !squeues_stop; i++) {
        local.open(forg.c_str(), ifstream_utf8::binary);
        if (!local) {
            DLOG << "sftp_swrite()->open("<<forg<<")#" << i << ": " << strerror(errno);
            boost::this_thread::sleep(boost::posix_time::seconds(3*i));
        } else break;
    }
    if (!local) {
        ELOG << "sftp_swrite()->open("<<forg<<"): " << strerror(errno);
        return -1;
    }

    LIBSSH2_SFTP_HANDLE *sftp_handle =
        libssh2_sftp_open(sses->sftp_session, fdest.c_str(),
                      LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                      LIBSSH2_SFTP_S_IRWXU|LIBSSH2_SFTP_S_IRWXG|LIBSSH2_SFTP_S_IRWXO);

    if (!sftp_handle) {
        ELOG << "sftp_swrite()-> Unable to open file with SFTP '" << fdest << "'";
        return -1;
    }

    do {
        local.read(mem, sizeof(mem));
        nread = local.gcount();
        if (nread <= 0) {     /* end of file */
            break;
        }
        ptr = mem;

        do {   /* write data in a loop until we block */
            rc = libssh2_sftp_write(sftp_handle, ptr, nread);
            if(rc < 0)
                break;
            ptr += rc;
            nread -= rc;
        } while (nread);

    } while (rc > 0);

    libssh2_sftp_close(sftp_handle);
    TLOG << "sftp_swrite(" << forg << ", " << fdest << ") END";

    return 0;
}
int sftp_sread(sftp_session_t *sses, const std::string &forg, const std::string &fdest)
{
    DLOG << "sftp_sread(" << forg << ", " << fdest << ")";

//    int rc;
    LIBSSH2_SFTP_HANDLE *sftp_handle;

    size_t nread;
    if (!sses->opened) {
        if (sftp_init_session(sses)) {
            ELOG << "sftp_sread()-> sftp_init_session";
            return -1;
        }
    }
    ofstream_utf8 local(fdest.c_str(), ofstream_utf8::binary);
    if (!local) {
        ELOG << "sftp_sread()-> Can't open local file " << fdest;
        return -1;
    }
    sftp_handle =
        libssh2_sftp_open(sses->sftp_session, forg.c_str(), LIBSSH2_FXF_READ, 0);

    if (!sftp_handle) {
        ELOG << "sftp_sread()-> Unable to open file with SFTP '" << forg << "'";
        return -1;
    }

    do {
        char mem[2048];

        nread = libssh2_sftp_read(sftp_handle, mem, sizeof(mem));
        if (nread <= 0) {
            /* end of file */
            break;
        }
        local.write(mem, nread);

    } while (nread > 0);

    libssh2_sftp_close(sftp_handle);
    TLOG << "sftp_sread(" << forg << ", " << fdest << ") END";

    return 0;
}

int ssh_exec(sftp_session_t *sses, const std::string &cmd, std::string *resp)
{
    DLOG << "ssh_exec(" << cmd << ") INIT";
    if (!sses->opened) {
        if (sftp_init_session(sses)) {
            return -1;
        }
    }

    LIBSSH2_CHANNEL *channel;
    if( (channel = libssh2_channel_open_session(sses->session)) == NULL) {
        ELOG << "ssh_exec()->libssh2_channel_open_session()";
        sftp_end_session(sses);
        return -1;
    }
    int rc;
    if( (rc = libssh2_channel_exec(channel, cmd.c_str())) != 0 ) {
        ELOG << "ssh_exec()->libssh2_channel_exec(" << cmd << ")";
        return -1;
    }
    do {
        char buffer[0x400];
        rc = libssh2_channel_read(channel, buffer, sizeof(buffer) );
        if (rc < 0) {
            ELOG << "ssh_exec()->libssh2_channel_read returned: " << rc;
            return -1;
        } else if (rc > 0) {
            if (resp) resp->append(buffer, rc);
        }
    }
    while( rc > 0 );

    if( (rc = libssh2_channel_close(channel)) == 0 ) {
        int exitcode = libssh2_channel_get_exit_status( channel );
        char *exitsignal=(char *)"none";
        libssh2_channel_get_exit_signal(channel, &exitsignal,
                                        NULL, NULL, NULL, NULL, NULL);
        if (exitcode)
            WLOG << "ssh_exec()->exitcode: " << exitcode << ", exitsignal: " << exitsignal;
    } else {
        ELOG << "ssh_exec()->libssh2_channel_close()";
        sftp_end_session(sses);
        return -1;
    }

    libssh2_channel_free(channel);

    TLOG << "ssh_exec(" << cmd << ") END";
    if (resp) TLOG << *resp;
    return 0;
}

int sftp_swrite_m(sftp_session_t *sses, const char *buf, size_t n, const std::string &fdest)
{
    DLOG << "sftp_swrite_m(" << n << ", " << fdest << ")";

    if (!sses->opened) {
        if (sftp_init_session(sses)) {
            ELOG << "sftp_swrite_m()->sftp_init_session() ERROR";
            return -1;
        }
    }
    LIBSSH2_SFTP_HANDLE *sftp_handle =
        libssh2_sftp_open(sses->sftp_session, fdest.c_str(),
                      LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                      LIBSSH2_SFTP_S_IRWXU|LIBSSH2_SFTP_S_IRWXG|LIBSSH2_SFTP_S_IRWXO);

    if (!sftp_handle) {
        ELOG << "sftp_swrite_m()-> Unable to open file with SFTP '" << fdest << "'";
        return -1;
    }
    do {
        int rc = libssh2_sftp_write(sftp_handle, buf, n);
        if(rc < 0) break;
        buf += rc;
        n -= rc;
    } while (n);

    libssh2_sftp_close(sftp_handle);
    TLOG << "sftp_swrite_m(" << n << ", " << fdest << ") END";

    return 0;
}
int sftp_sread_s(sftp_session_t *sses, const std::string &forg, std::string &str)
{
    DLOG << "sftp_sread_s(" << forg << ", " << str.size() << ")";

    LIBSSH2_SFTP_HANDLE *sftp_handle;

    size_t nread;
    if (!sses->opened) {
        if (sftp_init_session(sses)) {
            ELOG << "sftp_sread()-> sftp_init_session";
            return -1;
        }
    }
    sftp_handle =
        libssh2_sftp_open(sses->sftp_session, forg.c_str(), LIBSSH2_FXF_READ, 0);

    if (!sftp_handle) {
        ELOG << "sftp_sread_s()-> Unable to open file with SFTP '" << forg << "'";
        return -1;
    }
    str.clear();
    do {
        char mem[2048];

        nread = libssh2_sftp_read(sftp_handle, mem, sizeof(mem));
        if (nread <= 0) {
            /* end of file */
            break;
        }
        str.append(mem, nread);

    } while (nread > 0);

    libssh2_sftp_close(sftp_handle);
    TLOG << "sftp_sread_s(" << forg << ", " <<  str.size() << ") END";

    return 0;
}
