

#ifndef _QUICKMAIL_CPP_H
#define _QUICKMAIL_CPP_H

#include <string>
#include <vector>


class quickmail_t {
  std::string from;
  std::vector<std::string> to;
  std::vector<std::string> cc;
  std::vector<std::string> bcc;
  std::string subject;
  std::string header;
  std::vector<std::pair<std::string, std::string> > bodylist;
  std::vector<std::pair<std::string, std::string> > attachmentlist;
  std::string mime_boundary_body;
  std::string mime_boundary_part;

  std::string get_data();

public:
    static void init();
    quickmail_t(const std::string &from, const std::string &subject);
    void add_to(const std::string &addr);
    void add_cc(const std::string &addr);
    void add_bcc(const std::string &addr);
    void add_header(const std::string &header);
    void add_body_mem(const std::string &mime, const std::string &body);
    void add_attachment(const std::string &mime, const std::string &fname);
    void send(const std::string &svr, const size_t port, const std::string &usr, const std::string &pass);
};

#endif
