
//#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
//#include <boost/log/sources/logger.hpp>
//#include <boost/log/sources/record_ostream.hpp>
#include "fstream_utf8.h"
#include "log_sched.h"

namespace {

class svl_priv
{
public:
    svl_priv() : io_service_(), socket_(io_service_), deadline_(io_service_),
      ec_(boost::asio::error::would_block), resp_(NULL), n_(0), len_(0), http_code(0), cl(0)
    {
        deadline_.expires_at(boost::posix_time::pos_infin);
        check_deadline();
    }

  void connect_handler(const boost::system::error_code& ec)
  {
      ec_ = ec;
  }
  void read_handler_content(const boost::system::error_code& ec, size_t length)
  {
      ec_ = ec;
      if (ec == boost::asio::error::eof) {
        socket_.close();
        return;
      } else if (ec)
        throw boost::system::system_error(ec_); // Some other error.

        if (resp_) resp_->append(sbuf_, length);
        if (cl) n_ -= length;
        len_ += length;

        if (!cl || n_ > 0) {
            ec_ = boost::asio::error::would_block;
            socket_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&svl_priv::read_handler_content, this, _1, _2));
        }
  }
  void read_handler(const boost::system::error_code& ec, size_t length)
  {
      ec_ = ec;
      if (ec == boost::asio::error::eof) {
        socket_.close();
        return;
      } else if (ec)
        throw boost::system::system_error(ec_); // Some other error.

        len_ = length;

static const char content_length[] = "Content-Length: ";
static const char headers_end[] = "\r\n\r\n";
        int n = 0, n1 = 0;
        if (length > 20) {
            int i = 10;
            http_code = atoi(sbuf_ + 9);
            if (http_code != 200) WLOG << "http_client::read_handler()->http_code: " << http_code;
            for (; i < length - 10; i++) {
                if (strncmp(sbuf_ + i, content_length, sizeof(content_length) - 1) == 0) {
                    n1 = atoi(sbuf_ + i + sizeof(content_length) - 1);
                    break;
                }
            }
            if (n1 > 0) {
                for (int j = i + sizeof(content_length) - 1; j < length - sizeof(headers_end) + 1; j++) {
                    if (strncmp(sbuf_ + j, headers_end, sizeof(headers_end) - 1) == 0) {
                        int n2 = j + sizeof(headers_end) - 1, n3 = length - n2;
                        n_ = n1 - n3;
                        if (resp_) resp_->assign(sbuf_ + n2, n3);
                        n = n_;
                        cl = n1;
                        break;
                    }
                }
            }
        }
        if (!cl && resp_) resp_->assign(sbuf_, length);
        if (n > 0 || n1 == 0) {
            ec_ = boost::asio::error::would_block;
            socket_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&svl_priv::read_handler_content, this, _1, _2));
        }
  }

  void connect(const std::string& host, const std::string& service,
      boost::posix_time::time_duration timeout)
  {
      if (!socket_.is_open()) {

    boost::asio::ip::tcp::resolver::query query(host, service);
    boost::asio::ip::tcp::resolver::iterator iter = boost::asio::ip::tcp::resolver(io_service_).resolve(query);

    deadline_.expires_from_now(timeout);

    boost::asio::async_connect(socket_, iter, boost::bind(&svl_priv::connect_handler, this, _1));
    ec_ = boost::asio::error::would_block;
    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec_ == boost::asio::error::would_block);

    if (ec_ || !socket_.is_open())
      throw boost::system::system_error(
          ec_ ? ec_ : boost::asio::error::operation_aborted);

      } else {
        //TLOG << "connect(" << host << ", " << service << ") already connected";
      }
  }

  size_t read(std::string *resp, boost::posix_time::time_duration timeout)
  {
      resp_ = resp;

    deadline_.expires_from_now(timeout);
    ec_ = boost::asio::error::would_block;
    socket_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&svl_priv::read_handler, this, _1, _2));

    do {
        io_service_.run_one();
    } while (ec_ == boost::asio::error::would_block);

    if (ec_ && ec_ != boost::asio::error::eof)
      throw boost::system::system_error(ec_);

    if (!cl && resp) {
        size_t n = resp->find("\r\n\r\n");
        if (n != std::string::npos) *resp = resp->substr(n + 4);
    }

    return cl?:len_;
  }

  void write(boost::asio::streambuf &request)
  {
      boost::asio::write(socket_, request);
  }
    void close() { socket_.close(); }

    size_t get_http_code() { return http_code; };

private:
  void check_deadline()
  {

    if (deadline_.expires_at() <= boost::asio::deadline_timer::traits_type::now())
    {
      boost::system::error_code ignored_ec;
      socket_.close(ignored_ec);
      deadline_.expires_at(boost::posix_time::pos_infin);

        ec_ = boost::asio::error::timed_out;
    }
    deadline_.async_wait(boost::bind(&svl_priv::check_deadline, this));
  }

  boost::asio::io_service io_service_;
  boost::asio::ip::tcp::socket socket_;
  boost::asio::deadline_timer deadline_;
  boost::system::error_code ec_;
  std::string *resp_;
  char sbuf_[1500];
  size_t n_, len_, http_code, cl;
};
}

#if 0
int http_client_get(const std::string &host, int port, const std::string &path, std::string *resp)
{
    try  {
        std::string sport = boost::lexical_cast<std::string>(port);
        svl_priv svlc;
        svlc.connect(host, sport, boost::posix_time::seconds(50));

        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "GET " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << host << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: keep-alive\r\n\r\n";

        try { svlc.write(request); } catch(...) { //broken pipe
            svlc.close();
            svlc.connect(host, sport, boost::posix_time::seconds(50));
            svlc.write(request);
        }
        size_t n1 = svlc.read(resp, boost::posix_time::seconds(50));
        if (resp) {
            size_t n = resp->find("\r\n\r\n");
            if (n != std::string::npos) *resp = resp->substr(n + 4);
        }
        return n1;
    }
    catch (std::exception& e) {
        WLOG << "servl_http_get(" << path << ")-> Exception: " << e.what();
        if (resp) resp->clear();
    }
    return -1;
}
#endif // 0

void http_url_parse(const std::string &url, std::string &host, std::string &port, std::string &path)
{
    int p1=6,p2=8,p3;
    if (url.compare(0, 4, "http")) {
        throw std::runtime_error(std::string("http_url_parse(")+url+")-> not http...");
    }
    while (p1 < url.size() && url[p1] == '/') p1++; p2=p1+1;
    while (p2 < url.size() && url[p2] != ':' && url[p2] != '/') p2++;
    p3 = p2; while (p3 < url.size() && url[p3] != '/') p3++;
    host = url.substr(p1,p2-p1);
    port = (p2==p3)?"http":url.substr(p2+1,p3-p2-1);
    path = url.substr(p3);
}

int http_client_get(const std::string &url, std::string *resp)
{
    try  {
        std::string host, port, path;
        http_url_parse(url, host, port, path);
        svl_priv svlc;
        svlc.connect(host, port, boost::posix_time::seconds(20));

        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "GET " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << host << "\r\n";
//        request_stream << "Accept: */*\r\n";
//        request_stream << "Connection: keep-alive\r\n\r\n";
        request_stream << "\r\n";

        try { svlc.write(request); } catch(...) { //broken pipe
            svlc.close();
            svlc.connect(host, port, boost::posix_time::seconds(20));
            svlc.write(request);
        }
        return svlc.read(resp, boost::posix_time::seconds(50));
/*        size_t n1 = svlc.read(resp, boost::posix_time::seconds(50));
        if (resp) {
            size_t n = resp->find("\r\n\r\n");
            if (n != std::string::npos) *resp = resp->substr(n + 4);
        }
        return 1; */
    }
    catch (std::exception& e) {
        WLOG << "http_client_get(" << url << ")-> Exception: " << e.what();
        if (resp) resp->clear();
    }
    return -1;
}
int http_client_post(const std::string &url, const std::string &data, std::string *resp)
{
    try  {
        std::string host, port, path;
        http_url_parse(url, host, port, path);
        svl_priv svlc;
        svlc.connect(host, port, boost::posix_time::seconds(20));

        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "POST " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << host << "\r\n";
//        request_stream << "Accept: */*\r\n";
        request_stream << "Content-Length: " << data.size() << "\r\n";
//        request_stream << "Connection: keep-alive\r\n\r\n";
        request_stream << "\r\n";

        request_stream << data;

        try { svlc.write(request); } catch(...) { //broken pipe
            svlc.close();
            svlc.connect(host, port, boost::posix_time::seconds(20));
            svlc.write(request);
        }
        return svlc.read(resp, boost::posix_time::seconds(150));
/*        svlc.read(resp, boost::posix_time::seconds(50));
        if (resp) {
            size_t n = resp->find("\r\n\r\n");
            if (n != std::string::npos) *resp = resp->substr(n + 4);
        }
        return 1; */
    }
    catch (std::exception& e) {
        WLOG << "http_client_post(" << url << ")-> Exception: " << e.what();
        if (resp) resp->clear();
    }
    return -1;
}

int http_client_get_file(const std::string &url, const std::string &fname)
{
    try  {
        std::string host, port, path;
        http_url_parse(url, host, port, path);
        svl_priv svlc;
        svlc.connect(host, port, boost::posix_time::seconds(50));

        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "GET " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << host << "\r\n";
//        request_stream << "Accept: */*\r\n";
//        request_stream << "Connection: keep-alive\r\n\r\n";
        request_stream << "\r\n";

        try { svlc.write(request); } catch(...) { //broken pipe
            svlc.close();
            svlc.connect(host, port, boost::posix_time::seconds(50));
            svlc.write(request);
        }
        std::string resp;
//        svlc.read(&resp, boost::posix_time::seconds(50));
/*        size_t n = resp.find("\r\n\r\n");
ELOG << "http_client_get_file(" << url << ", " << fname << ")->resp.size(): " << resp.size() << " /n: " << n;
ELOG << "http_client_get_file(" << url << ", " << fname << ")->resp:\n" << resp; */
        if (svlc.read(&resp, boost::posix_time::seconds(50)) > 0 && resp.size() && svlc.get_http_code() == 200) {
            ofstream_utf8 ofs(fname.c_str(), ofstream_utf8::binary);
            ofs.write(&(resp[0]), resp.size());
            return resp.size();
        } else {
            ELOG << "http_client_get_file(" << url << ")-> " << svlc.get_http_code() << " / len: " << resp.size();
            return -2;
        }
    }
    catch (std::exception& e) {
        WLOG << "http_client_get_file(" << url << ")-> Exception: " << e.what();
    }
    return -1;
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_http_client)

BOOST_AUTO_TEST_CASE (http_client_tests)
{
    std::string fname = "test_file.png";
    BOOST_CHECK( http_client_get_file("http://176.9.47.233:8080/asf-logo.png", fname) == 17811 );

    boost::filesystem::remove(fname);
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
