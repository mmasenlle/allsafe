
#ifndef _HTTPCLIENT_H
#define _HTTPCLIENT_H

#include <string>

int http_client_get(const std::string &host, int port, const std::string &path, std::string *resp = NULL);

#endif
