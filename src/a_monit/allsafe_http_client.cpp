

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

extern boost::log::sources::logger lg;

namespace {

class svl_priv
{
public:
    svl_priv() : io_service_(), socket_(io_service_), deadline_(io_service_),
      ec_(boost::asio::error::would_block), resp_(NULL)
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
        n_ -= length;

        if (n_ > 0) {
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

static const char content_length[] = "Content-Length: ";
static const char headers_end[] = "\r\n\r\n";
        int n = 0, n1 = 0;
        if (length > 20) {
            int i = 10;
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
                        break;
                    }
                }
            }
        }
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

    return n_;
  }

  void write(boost::asio::streambuf &request)
  {
      boost::asio::write(socket_, request);
  }
    void close() { socket_.close(); }

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
  size_t n_;
};
}


int http_client_get(const std::string &host, int port, const std::string &path, std::string *resp)
{
    try  {
        std::string sport = boost::lexical_cast<std::string>(port);
        svl_priv svlc;
        svlc.connect(host, sport, boost::posix_time::seconds(50));

        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "GET " << path << " HTTP/1.0\r\n";
        request_stream << "Host: " << host << "\r\n\r\n";
//        request_stream << "Accept: */*\r\n";
//        request_stream << "Connection: keep-alive\r\n\r\n";

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
        return 1;
    }
    catch (std::exception& e) {
        BOOST_LOG(lg) << "servl_http_get(" << path << ")-> Exception: " << e.what();
        if (resp) resp->clear();
    }
    return -1;
}
