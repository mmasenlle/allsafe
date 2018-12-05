
#ifndef _WEB_UTILS_H
#define _WEB_UTILS_H

std::string web_srefresh(std::string p = "");
std::string web_url_decode(const std::string &url);
std::string web_encode(const std::string &str);
void wserver_init();
const std::string &wserver_pages_head();
const std::string &wserver_pages_tail();
const std::string &wserver_confirm();
std::string wserver_ws_title(const std::string &h2_text);
std::string web_icon(const std::string &icon, std::string text);

#endif
