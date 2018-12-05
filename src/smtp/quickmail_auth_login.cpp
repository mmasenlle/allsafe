
#include <sstream>
#include <time.h>
#include <boost/locale.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/filesystem.hpp>
#include "fstream_utf8.h"
#include <boost/scoped_ptr.hpp>
#include "quickmail_cpp.h"
#include "basic_smtp_comm.h"
#include "tls_smtp_comm.h"
#include "ssl_smtp_comm.h"
#include "log_sched.h"


std::string randomize_zeros (const char *p)
{
    std::string s;
    while (*p) {
        if (*p == '0')
            s.push_back('0' + rand() % 10);
        else
            s.push_back(*p);
        p++;
    }
    return s;
}

void quickmail_t::init ()
{
    srand(time(NULL));
}

quickmail_t::quickmail_t(const std::string &f, const std::string &s)
{
    from = f;
    subject = s;
}

void quickmail_t::add_to (const std::string & email)
{
    to.push_back(email);
}

void quickmail_t::add_cc (const std::string & email)
{
    cc.push_back(email);
}

void quickmail_t::add_bcc (const std::string & email)
{
    bcc.push_back(email);
}

void quickmail_t::add_header (const std::string & headerline)
{
    header += headerline + "\r\n";
}

void quickmail_t::add_body_mem(const std::string &mime, const std::string &body)
{
    bodylist.push_back(std::make_pair(mime, body));
}

void quickmail_t::add_attachment(const std::string &mime, const std::string &fname)
{
    attachmentlist.push_back(std::make_pair(mime, fname));
}

extern const char *main_version;
static boost::local_time::time_zone_ptr const utc_time_zone(new boost::local_time::posix_time_zone("GMT"));
//#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>
static std::string encode64_b(const std::vector<unsigned char> &val) {
    std::string tmp = std::string(boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::vector<unsigned char>::const_iterator, 6, 8> >(val.begin()), boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::vector<unsigned char>::const_iterator, 6, 8> >(val.end()));
    return tmp.append((3 - val.size() % 3) % 3, '=');
}
static std::string encode64(const std::string &val) {
    std::string tmp = std::string(boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::string::const_iterator, 6, 8> >(val.begin()), boost::archive::iterators::base64_from_binary<boost::archive::iterators::transform_width<std::string::const_iterator, 6, 8> >(val.end()));
    return tmp.append((3 - val.size() % 3) % 3, '=');
}

std::string quickmail_t::get_data ()
{
    std::stringstream ss;
    ss.imbue(std::locale(std::locale::classic(), new boost::local_time::local_time_facet("%a, %d %b %Y %H:%M:%S %z")));
    boost::local_time::local_date_time now(boost::posix_time::second_clock::universal_time(), utc_time_zone);

    ss << "User-Agent: main v" << main_version << "\r\n";
    ss << "Date: " << now << "\r\n";
    ss << "From: <" << from << ">\r\n";
    for (int i = 0; i < to.size(); i++)
        ss << "To: " << to[0] << "\r\n";
    for (int i = 0; i < cc.size(); i++)
        ss << "Cc: " << cc[0] << "\r\n";
    ss << "Subject: " << subject << "\r\n";
    ss << header;
    mime_boundary_part = randomize_zeros("=PART=SEPARATOR=_0000_0000_0000_0000_0000_0000_=");
    ss << "Content-Type: multipart/mixed; boundary=\"" << mime_boundary_part << "\"\r\n\r\n";
    for (int i = 0; i < bodylist.size(); i++) {
        ss << "\r\n--" << mime_boundary_part << "\r\nContent-Type: " << bodylist[i].first
            << "\r\nContent-Transfer-Encoding: 8bit\r\nContent-Disposition: inline\r\n\r\n";
        ss << bodylist[i].second;
    }
    for (int i = 0; i < attachmentlist.size(); i++) {
        try {
            size_t fsize = boost::filesystem::file_size(attachmentlist[i].second);
            std::vector<unsigned char> fdata(fsize);
            ifstream_utf8 ifs(attachmentlist[i].second.c_str(), ifstream_utf8::binary);
            ifs.read((char*)&fdata.front(), fdata.size());
            if (ifs.gcount() != fdata.size()) {
                WLOG << "quickmail_t::get_data ("<<attachmentlist[i].second<<")->ifs.gcount() != fdata.size(): " << ifs.gcount() << "/" << fdata.size();
                continue;
            }
            ss << "\r\n--" << mime_boundary_part << "\r\nContent-Type: " << attachmentlist[i].first
                << "; Name=\"" << attachmentlist[i].second
                << "\"\r\nContent-Disposition: attachment; filename=\"" << attachmentlist[i].second
                << "\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"
                << encode64_b(fdata);
        } catch (std::exception &e) {
            WLOG << "quickmail_t::get_data("<<attachmentlist[i].second<<")->" << e.what();
        }
    }
    ss << "\r\n--" << mime_boundary_part << "--\r\n";
    return ss.str();
}

void quickmail_t::send(const std::string &svr, const size_t prot_port, const std::string &usr, const std::string &pass)
{
    boost::scoped_ptr<smtp_comm> comm;
    const size_t port = prot_port % 100000; // real port
#ifdef WITH_OPENSSL
    if (prot_port > 400000) {
        comm.reset(new ssl_smtp_comm(port, svr, prot_plain));
    } else
    if (prot_port > 300000) {
        comm.reset(new ssl_smtp_comm(port, svr, prot_ssl));
    } else
    if (prot_port > 200000) {
        comm.reset(new ssl_smtp_comm(port, svr));
    } else
#endif
#ifdef WIN32
    if (prot_port > 100000) {
        comm.reset(new tls_smtp_comm(port, svr));
    } else
#endif
    {
        comm.reset(new basic_smtp_comm(port, svr));
    }
    if (!comm) {
        ELOG << "quickmail_t::send()->bad protocol";
        return;
    }

    if (comm->command("EHLO localhost") >= 400) {
        ELOG << "SMTP EHLO/HELO returned error";
        return;
    }
    int statuscode = comm->command("AUTH LOGIN");
    if (statuscode < 400) {
        statuscode = comm->command(encode64(usr));
        if (statuscode < 400) {
            statuscode = comm->command(encode64(pass));
        }
    }
    if (statuscode >= 400) {
        ELOG << "SMTP authentication failed";
        return;
		}
    if (comm->command(std::string("MAIL FROM:<") + from + ">") >= 400) {
        ELOG << "SMTP server did not accept sender";
        return;
    }
    std::vector<std::string> *tos[] = { &to, &cc, &bcc, NULL };
    for (int j = 0; tos[j]; j++) {
    for (int i = 0; i < tos[j]->size(); i++) {
        if (comm->command(std::string("RCPT TO:<") + tos[j]->at(i) + ">") >= 400)
			WLOG << "SMTP server did not accept sender (" << tos[j]->at(i) << ")";
    }}
    if (comm->command("DATA") >= 400) {
        ELOG << "SMTP DATA returned error";
        return;
    }
    comm->send(get_data());

    if (comm->command("\r\n.") >= 400) {
        ELOG << "SMTP error after sending message data";
    }
    comm->command("QUIT");
}
