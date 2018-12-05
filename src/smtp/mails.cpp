#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#include "dict.h"
#include "log_sched.h"
#include "quickmail_cpp.h"

extern "C" int minigzip_main(int argc, char *argv[]);

static size_t mails_port = 25;
static std::string mails_from;
static std::string mails_to;
static std::string mails_server;
static std::string mails_user;
static std::string mails_pass;


static std::vector<std::string> mails_vectorize(const std::string &s)
{
    std::vector<std::string> vs;
    boost::char_separator<char> sep(";, ");
    boost::tokenizer<boost::char_separator<char> > tok(s, sep);
    BOOST_FOREACH(const std::string &_s, tok) {
        vs.push_back(_s);
    }
    return vs;
}

void mails_init()
{
    dict_set("mails.from", &mails_from);
    dict_set("mails.to", &mails_to);
    dict_set("mails.server", &mails_server);
    dict_set("mails.port", &mails_port);
    dict_set("mails.user", &mails_user);
    dict_set_enc("mails.pass", &mails_pass);

    quickmail_t::init();
}

extern const std::string &get_styles();
extern std::string sdump(int op);
void mails_send_test()
{
    quickmail_t mail(mails_from, "main test e-mail");
    std::vector<std::string> tos = mails_vectorize(mails_to);
    if (tos.size()) mail.add_to(tos[0]);
    for (int i=1;i<tos.size();i++) mail.add_cc(tos[i]);
    //mail.add_cc("mmasenn@gmail.com");
    //mail.add_bcc("mmasenlle002@ikasle.ehu.es");
    mail.add_header("Importance: Low");
    mail.add_header("X-Priority: 5");
    mail.add_header("X-MSMail-Priority: Low");
    std::string body = "This is a <b>test</b> e-mail from <u>main</u>.<br/>\n";
    body += "<style>" + get_styles() + "</style>";
    body += sdump(10);
    mail.add_body_mem("text/html", body);
    mail.add_attachment("text/xml", "dirs.xml");
#ifdef DEBUG
    //char *argv[6] = { "minizip", "-o", "-j", "main_log.zip", "main.log", 0 };
    char *argv[2] = { "minigzip", "main.log" };
    int r = minigzip_main(2, argv);
    if (r != 0) {
        WLOG << "mails_send_test()->minigzip(main.log): " << r;
    } else {
        //boost::this_thread::sleep(boost::posix_time::seconds(5));
        mail.add_attachment("application/x-gzip", "main.log.gz");
    }
//    mail.add_attachment("application/x-sqlite3", "deleted.db");
#endif // DEBUG

    mail.send(mails_server, mails_port, mails_user, mails_pass);
}

static void mails_insert_styles(std::string &html)
{
    std::size_t found = html.find("</head>");
    if (found != std::string::npos)
        html.insert(found, std::string("<style>") + get_styles() + "</style>");
}

extern std::string wserver_serve(const std::string &uri);
void mails_send_web(std::string command)
{
    try {
        DLOG << "mails_send_web(" << command << ")";
        quickmail_t mail(mails_from, "main (" + command + ")");
        //mail.add_to(mails_to);
        std::vector<std::string> tos = mails_vectorize(mails_to);
        if (tos.size()) mail.add_to(tos[0]);
        for (int i=1;i<tos.size();i++) mail.add_cc(tos[i]);
        std::string body = wserver_serve(command);
        //body += "<style>" + get_styles() + "</style>"; // email does not resolve links
        mails_insert_styles(body);
        mail.add_body_mem("text/html", body);
        mail.send(mails_server, mails_port, mails_user, mails_pass);
    } catch (std::exception& e) {
        ELOG << "mails_send_web(" << command << ")-> Exception: " << e.what();
    }
}

