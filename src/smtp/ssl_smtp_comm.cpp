
#ifdef WITH_OPENSSL

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio/ssl.hpp>
#include "log_sched.h"
#include "ssl_smtp_comm.h"

//namespace {
class comm_priv_ssl
{
public:
  comm_priv_ssl(int pr)
    : prot(pr),
    io_service_(),
    ctx_(prot==prot_tls1 ? boost::asio::ssl::context::tlsv1 : boost::asio::ssl::context::sslv23),
        ssl_sock_(io_service_, ctx_),
      deadline_(io_service_),
      ec_(boost::asio::error::would_block),
      resp_(NULL)
  {
    // No deadline is required until the first socket operation is started. We
    // set the deadline to positive infinity so that the actor takes no action
    // until a specific deadline is set.
    deadline_.expires_at(boost::posix_time::pos_infin);


    ctx_.set_default_verify_paths();

    // Start the persistent actor that checks for deadline expiry.
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
        ssl_sock_.lowest_layer().close();
        return;
      } else if (ec)
        throw boost::system::system_error(ec_); // Some other error.
//WLOG << "read_handler(" << ec << ", " << length << "): '" << std::string(sbuf_, length) << "'";
        resp_->append(sbuf_, length);
        n_ -= length;
//WLOG << "read_handler_content(" << n_ << ", " << *resp_ << ")";
        if (n_ > 0) {
            ec_ = boost::asio::error::would_block;
            ssl_sock_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&comm_priv_ssl::read_handler_content, this, _1, _2));
        }
  }
  void read_handler(const boost::system::error_code& ec, size_t length)
  {
      ec_ = ec;
      if (ec == boost::asio::error::eof) {
        ssl_sock_.lowest_layer().close();
        return;
      } else if (ec)
        throw boost::system::system_error(ec_); // Some other error.
//WLOG << "read_handler(" << ec << ", " << length << "): '" << std::string(sbuf_, length) << "'";
    resp_->assign(sbuf_, length);
/*
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
//WLOG << "read_handler1: "<< std::string(sbuf_ + j, length - j);
                    if (strncmp(sbuf_ + j, headers_end, sizeof(headers_end) - 1) == 0) {
                        int n2 = j + sizeof(headers_end) - 1, n3 = length - n2;
                        n_ = n1 - n3;
                        resp_->assign(sbuf_ + n2, n3);
                        n = n_;
//WLOG << "read_handler2(" << n << ", " << n1 << ", " << n2 << ", " << n3 << ", " << resp_->size() << ")";
                        break;
                    }
                }
            }
        }
//WLOG << "read_handler3(" << n << ", " << n1 << ", " << *resp_ << ")";
        if (n > 0 || n1 == 0) {
            ec_ = boost::asio::error::would_block;
            socket_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&comm_priv_ssl::read_handler_content, this, _1, _2));
        }
*/
  }

  bool verify_certificate(bool preverified,
      boost::asio::ssl::verify_context& ctx)
  {
    // The verify callback can be used to check whether the certificate that is
    // being presented is valid for the peer. For example, RFC 2818 describes
    // the steps involved in doing this for HTTPS. Consult the OpenSSL
    // documentation for more details. Note that the callback is called once
    // for each certificate in the certificate chain, starting from the root
    // certificate authority.

    // In this example we will simply print the certificate's subject name.
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    ILOG << "verify_certificate() -> Verifying " << subject_name;

    //return preverified;
    return true;
  }

  void connect(const std::string& host, const std::string& service,
//    void connect(const std::string& host, const size_t service,
      boost::posix_time::time_duration timeout)
  {
      if (!ssl_sock_.lowest_layer().is_open()) {
        TLOG << "connect(" << host << ", " << service << ") about to connect";

    // Resolve the host name and service to a list of endpoints.
    boost::asio::ip::tcp::resolver::query query(host, service);
    boost::asio::ip::tcp::resolver::iterator iter = boost::asio::ip::tcp::resolver(io_service_).resolve(query);

    deadline_.expires_from_now(timeout);

    boost::asio::async_connect(ssl_sock_.lowest_layer(), iter, boost::bind(&comm_priv_ssl::connect_handler, this, _1));
    ec_ = boost::asio::error::would_block;
    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec_ == boost::asio::error::would_block);

    if (ec_ || !ssl_sock_.lowest_layer().is_open())
      throw boost::system::system_error(
          ec_ ? ec_ : boost::asio::error::operation_aborted);

    if (prot != prot_plain) {
    ssl_sock_.set_verify_mode(boost::asio::ssl::verify_peer);
    //ssl_sock_.set_verify_callback(boost::asio::ssl::rfc2818_verification(host));
    ssl_sock_.set_verify_callback(boost::bind(&comm_priv_ssl::verify_certificate, this, _1, _2));
    ssl_sock_.handshake(boost::asio::ssl::stream_base::client);
    }

      } else {
        TLOG << "connect(" << host << ", " << service << ") already connected";
      }
  }

  void read(std::string *resp, boost::posix_time::time_duration timeout)
  {
      resp_ = resp;

    deadline_.expires_from_now(timeout);

    ec_ = boost::asio::error::would_block;
    ssl_sock_.async_read_some(boost::asio::buffer(sbuf_),
                boost::bind(&comm_priv_ssl::read_handler, this, _1, _2));

    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec_ == boost::asio::error::would_block);

    if (ec_ && ec_ != boost::asio::error::eof)
      throw boost::system::system_error(ec_);
  }

  void write(boost::asio::streambuf &request)
  {
      boost::asio::write(ssl_sock_, request);
  }
    void close() { ssl_sock_.lowest_layer().close(); }

private:
  void check_deadline()
  {

    if (deadline_.expires_at() <= boost::asio::deadline_timer::traits_type::now())
    {
      boost::system::error_code ignored_ec;
      ssl_sock_.lowest_layer().close(ignored_ec);

      // There is no longer an active deadline. The expiry is set to positive
      // infinity so that the actor takes no action until a new deadline is set.
      deadline_.expires_at(boost::posix_time::pos_infin);

        ec_ = boost::asio::error::timed_out;
    }

    // Put the actor back to sleep.
    deadline_.async_wait(boost::bind(&comm_priv_ssl::check_deadline, this));
  }

  int prot;
  boost::asio::io_service io_service_;
  boost::asio::ssl::context ctx_;
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_sock_;
  //boost::asio::ip::tcp::socket socket_;
  boost::asio::deadline_timer deadline_;
  boost::system::error_code ec_;
  std::string *resp_;
  char sbuf_[1500];
  size_t n_;
};
//}

ssl_smtp_comm::ssl_smtp_comm(int p, const std::string &s, int pr)
 : comm(NULL), port(p), prot(pr), server(s) { }

ssl_smtp_comm::~ssl_smtp_comm()
{
    delete comm;
}

int ssl_smtp_comm::command(const std::string &cmd)
{
    try {
        if (!comm) {
            comm = new comm_priv_ssl(prot);
            comm->connect(server, boost::lexical_cast<std::string>(port), boost::posix_time::seconds(50));
        }
        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << cmd << "\r\n";

        std::string resp;
        comm->write(request);
        comm->read(&resp, boost::posix_time::seconds(50));

        TLOG << "ssl_smtp_comm::command(" << cmd << "): " << resp;

        int ret = 999;
        if (resp.size() > 4) {
            ret = boost::lexical_cast<int>(resp.substr(0,3));
        }
        if (ret >= 400) {
            WLOG << "ssl_smtp_comm::command(" << cmd << ")->ret: " << ret << "-" << resp;
        }

        return ret;

    } catch (std::exception& e) {
        ELOG << "ssl_smtp_comm::command(" << cmd << ")-> Exception: " << e.what();
        delete comm;
        comm = NULL;
    }
    return 999;
}

int ssl_smtp_comm::send(const std::string &data)
{
    try {
        if (!comm) {
            ELOG << "ssl_smtp_comm::send()->Not connected";
            return -1;
        }
        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << data;

        comm->write(request);

        return data.length();

    } catch (std::exception& e) {
        ELOG << "ssl_smtp_comm::send(" << data.length() << ")-> Exception: " << e.what();
        delete comm;
        comm = NULL;
    }
    return -1;
}

#endif
