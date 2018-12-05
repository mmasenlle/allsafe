
#ifndef _HTTPCLIENT_H
#define _HTTPCLIENT_H

#include <string>

//int http_client_get(const std::string &host, int port, const std::string &path, std::string *resp = NULL);
int http_client_get(const std::string &url, std::string *resp = NULL);
int http_client_post(const std::string &url, const std::string &data, std::string *resp = NULL);
int http_client_get_file(const std::string &url, const std::string &fname);

#endif
