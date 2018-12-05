
#include <time.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include "fstream_utf8.h"
#include "sshBackend.h"
#include "dict.h"
#include "sftp_sched.h"
#include "props_sched.h"
#include "log_sched.h"
#include "web_utils.h"
#include "mails.h"

static time_t ptasks_tt = 0;
static inline void get_time() { if (!ptasks_tt) {ptasks_tt = time(0);}}

enum { PROG_TASK_MUS, PROG_TASK_SYS, PROG_TASK_GET, PROG_TASK_PUT, PROG_TASK_BATCH, PROG_TASK_MAIL, PROG_TASK_LAST };

extern std::string wserver_serve(const std::string &uri);

static void task_get(const std::string &cmd)
{
#ifdef COMP1
    sftp_session_t sses;
    std::size_t n = cmd.find(" ");
    if (n != std::string::npos) {
        props::init_session(&sses);
        sftp_sread(&sses, cmd.substr(0, n), cmd.substr(n+1, n-cmd.size()-1));
        if (sses.opened) sftp_end_session(&sses);
    }
#endif // COMP1
}
static void task_put(const std::string &cmd)
{
#ifdef COMP1
    sftp_session_t sses;
    std::size_t n = cmd.find(" ");
    if (n != std::string::npos) {
        props::init_session(&sses);
        sftp_swrite(&sses, cmd.substr(0, n), cmd.substr(n+1, n-cmd.size()-1));
        if (sses.opened) sftp_end_session(&sses);
    }
#endif // COMP1
}
void task_batch(const std::string &fname);

struct prog_task_t
{
    time_t go_time; // <=0 inmediate
    size_t period_secs; // <=0 only once
    int type; // main, system,...

    std::string name;
    std::string command;

    volatile bool running;

    prog_task_t() : go_time(0), period_secs(0), type(PROG_TASK_MUS), running(false) {};

    bool is_time()
    {
        get_time();
        if (ptasks_tt > go_time) {
            if (period_secs > 0) while (go_time < ptasks_tt) go_time += period_secs; //go_time = ptasks_tt + period_secs;
            return true;
        }
        return false;
    }
    void remove_task();
    void run_task()
    {
        ILOG << "About to run_task(" << name << ", " << type << ")";
        switch (type) {
        case PROG_TASK_MUS: {
            wserver_serve(command);
        } break;
        case PROG_TASK_SYS: {
            int r = system(command.c_str());
			ILOG << "system(" << command << "): " << r;
        } break;
        case PROG_TASK_GET: {
            task_get(command);
        } break;
        case PROG_TASK_PUT: {
            task_put(command);
        } break;
        case PROG_TASK_BATCH: {
            task_batch(command);
        } break;
        case PROG_TASK_MAIL: {
            mails_send_web(command);
        } break;
        }
        if (period_secs <= 0) remove_task();
        running = false;
    }
    void run()
    {
        if (!running && is_time()) {
            running = true;
            boost::thread th(boost::bind(&prog_task_t::run_task, this));
            th.detach();
        }
    }
};

void task_batch(const std::string &fname)
{
    try {
        DLOG << "task_batch(" << fname << ") INIT";
        ifstream_utf8 ifs(fname.c_str());
        if (ifs) {
            std::string line;
            while (std::getline(ifs, line)) {
                if (line.size() > 2) {
                    int type = line[0] - '0';
                    if (type >= 0 && type < PROG_TASK_LAST) {
                        prog_task_t ptask1;
                        ptask1.type = type;
                        ptask1.command = line.substr(2);
                        ptask1.run_task();
                    }
                }
            }
        }
    } catch (std::exception &e) {
        WLOG << "task_batch(" << fname << ")->Exception: " << e.what();
    }
}

static boost::mutex mtx_ptasks;
static std::map<std::string, prog_task_t> ptasks;

void prog_task_t::remove_task()
{
    boost::mutex::scoped_lock scoped_lock(mtx_ptasks);
    ptasks.erase(name);
}

static size_t ptasks_iter = 0;
static bool ptasks_running = false;
static size_t ptasks_iter_n = 100; // 0 means disabled
#ifdef COMP1
static std::string ptasks_todo_path = "control/todo.txt";
void ptasks_get_todo()
{
    DLOG << "ptasks_get_todo()";
    sftp_session_t sses;
    std::string todofile = props::get().confd_path + "todo.txt";
    props::init_session(&sses);
    if (sftp_sread(&sses, "control/todo.txt", todofile) == 0) {
        std::string mv_cmd = "mv " + ptasks_todo_path + " " + ptasks_todo_path + ".1";
        ssh_exec(&sses, mv_cmd);
        task_batch(todofile);
    }
    if (sses.opened) sftp_end_session(&sses);
    ptasks_running = false;
}
#endif
int ptasks_run()
{
    ptasks_tt = 0;
    boost::mutex::scoped_lock scoped_lock(mtx_ptasks);
    for (std::map<std::string, prog_task_t>::iterator i = ptasks.begin(), n = ptasks.end(); i != n; ++i) {
        i->second.run();
    }
#ifdef COMP1
    if (ptasks_iter_n && !ptasks_running && ++ptasks_iter % ptasks_iter_n == 0) {
        if (!dict_get("props.hostip").empty())  {
            ptasks_running = true;
            boost::thread th(ptasks_get_todo);
            th.detach();
        }
    }
#endif
    return 0;
}

static void add_ptask(const std::string &n, const std::string &c, int t, time_t gt, size_t pt)
{
    prog_task_t ptask;
    ptask.name = n;
    ptask.command = c;
    ptask.type = t;
    ptask.go_time = gt;
    if (gt > 0) {
        time_t now = time(0);
        time_t day00 = (now / (24*3600)) * (24*3600);
        if (gt < (5000*24*3600)) ptask.go_time = day00 + gt; // initialization from 00:00 of today
        if (pt > 0) while (ptask.go_time < now) ptask.go_time += pt;
        if (ptask.go_time + 100 < now) {
            WLOG << "add_ptask("<<n<<","<<gt<<","<<pt<<")->timed out";
            return;
        }
    }
    ptask.period_secs = pt;
    boost::mutex::scoped_lock scoped_lock(mtx_ptasks);
    ptasks[n] = ptask;
}

#define PTASKS_FILENAME "ptasks.xml"
void ptasks_save(const char *fname)
{
    TLOG << "ptasks_save(): " << ptasks.size();
    try {
        ofstream_utf8 ofs(fname);
        if (ofs) {
            ofs << "<?xml version=\"1.0\"?>\n<prog_tasks>\n";
            for (std::map<std::string, prog_task_t>::iterator i = ptasks.begin(), n = ptasks.end(); i != n; ++i) {
                ofs << "\t<ptask name=\"" << i->second.name << "\" type=\"" << i->second.type << "\">\n";
                ofs << "\t\t<cmd>" << i->second.command << "</cmd>\n";
                ofs << "\t\t<go_secs>" << i->second.go_time << "</go_secs>\n";
                ofs << "\t\t<per_secs>" << i->second.period_secs << "</per_secs>\n";
                ofs << "\t</ptask>\n";
            }
            ofs << "</prog_tasks>\n";
        }
    } catch (std::exception &e) {
        ELOG << "ptasks_save()->Exception '" << e.what() << "' saving file '" <<fname<< "'";
    }
    TLOG << "ptasks_save() END";
}

int ptasks_load(const char *fname)
{
    TLOG << "ptasks_load() INIT";
    try {
        ifstream_utf8 ifs(fname);
        if (ifs) {
            using boost::property_tree::ptree;
            ptree pt;  read_xml(ifs, pt);
            BOOST_FOREACH( ptree::value_type const& v, pt.get_child("prog_tasks") ) {
            try { if( v.first == "ptask" ) {
                add_ptask(v.second.get<std::string>("<xmlattr>.name"),
                       v.second.get<std::string>("cmd"), v.second.get<int>("<xmlattr>.type"),
                       v.second.get<time_t>("go_secs"), v.second.get<size_t>("per_secs"));
            }} catch (std::exception &e) {
                WLOG << "ptasks_load()->Exception '" << e.what() << "' parsing file '" <<fname<< "'";
            }}
        }
    } catch (std::exception &e) {
        ELOG << "ptasks_load()->Exception '" << e.what() << "' loading file '" <<fname<< "'";
    }
    TLOG << "ptasks_load(): " << ptasks.size();
    return 0;
}

#define EDIT_ICON +web_icon("<i class=\"fa fa-edit\" style=\"font-size:18px;color:blue\" title=\"edit\"></i>", "edit")+
#define REMOVE_ICON +web_icon("<i class=\"fa fa-close\" style=\"font-size:18px;color:red\" title=\"remove\"></i>", "remove")+

extern const std::string &wserver_confirm();
static std::string ptasks_main()
{
    std::string s = wserver_pages_head();
    s += "<body><h2>Tareas programadas</h2>\n";
    s += "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
        "<a href=/ptasks?save=1 class=ml>save</a><a href=/ptasks?load=1 class=ml>load</a><a href=/ptasks?edit=1 class=ml>nueva</a><br/><br/>\n";
    s += "<table>";
    for (std::map<std::string, prog_task_t>::iterator i = ptasks.begin(), n = ptasks.end(); i != n; ++i) {
        s += "<tr><th>" + i->first;
        s += "</th><td>&nbsp;&nbsp;<a href=\"ptasks?edit=" + i->first + "\">" EDIT_ICON "</a></td>";
        s += "<td>&nbsp;&nbsp;<a href=# onclick='go_if(\"Delete ?\",\"ptasks?remove=" + i->first + "\")'>" REMOVE_ICON "</a></td>"
            "</tr>\n";
    }
    s += "</table><br/>\n"; //"<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/ptasks?edit=1 class=ml>nueva</a>\n";
    s += wserver_confirm() + "</body></html>";
    return s;
}

static const std::string prog_task_names[] = { "main", "system", "get", "put", "batch", "mail", "" };
static std::string ptasks_edit(const std::string &n)
{
    prog_task_t ptask;
    std::string s = wserver_pages_head();
    if (n.size() > 1 && ptasks.find(n) != ptasks.end()) {
        ptask = ptasks[n];
        s += "<body><h2>" + n + "</h2>\n";
    } else s += "<body><h2>NEW TASK</h2>\n";
//TODO: select for type and human readable times
    s += "<form action=ptasks method=get><table class=\"new_ptask\">"
        "<tr><th>Name:</th><td><input type=text name=ptask size=60 value=\"" + ptask.name + "\"></td></tr>\n";
    s += "<tr><th>Command:</th><td><input type=text name=con size=60 value='" + ptask.command + "'></td></tr>\n";
    s += "<tr><th>Type:</th><td><select name=t>";
    for (int i = 0; i < PROG_TASK_LAST; i++) {
        s += "<option value=" + boost::lexical_cast<std::string>(i);
        if (ptask.type == i) s += " selected";
        s += ">" + prog_task_names[i] + "</option>";
    }
    s += "</select></td></tr>\n";
    s += "<tr><th>Init (d h:m:s):</th><td><input type=text name=gt size=16 value=\"" + boost::lexical_cast<std::string>(ptask.go_time) + "\"></td></tr>\n";
    s += "<tr><th>Period (h:m:s):</th><td><input type=text name=pt size=16 value=\"" + props::from_seconds(ptask.period_secs) + "\"></td></tr>\n";
    s += "<tr><td>"; s += ptask.running ? "running" : "&nbsp;";
    s += "</td><td><input type=submit value=SET></td></tr></table></form>\n";
    s += "<br/><br/><br/>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/ptasks class=ml>cancel</a>\n"
        "</body></html>";
    return s;
}

std::string ptasks_page(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 1) {
        if (vuri.size() > 2) {
            if (vuri[1] == "load") {
                ptasks_load((props::get().confd_path + PTASKS_FILENAME).c_str());
                return web_srefresh("ptasks");
            }
            if (vuri[1] == "save") {
                ptasks_save((props::get().confd_path + PTASKS_FILENAME).c_str());
                return web_srefresh("ptasks");
            }
            if (vuri[1] == "remove") {
                std::string s = web_url_decode(vuri[2]);
                boost::mutex::scoped_lock scoped_lock(mtx_ptasks);
                ptasks.erase(s);
                return web_srefresh("ptasks");
            }
            if (vuri[1] == "edit") {
                return ptasks_edit(web_url_decode(vuri[2]));
            }
            if (vuri.size() > 10 && vuri[1] == "ptask") {
                try {
                    add_ptask(web_url_decode(vuri[2]),web_url_decode(vuri[4]),boost::lexical_cast<int>(vuri[6]),
                           props::to_seconds(web_url_decode(vuri[8])), props::to_seconds(web_url_decode(vuri[10])));
                } catch (std::exception &e) {
                    WLOG << "ptasks_page->add_ptask()->Exception: " << e.what();
                }
                return web_srefresh("ptasks");
            }
        }
        std::string s = vuri[0];
        for (int i = 1; i < vuri.size(); i++) {
            s += " //// " + vuri[i];
        }
        return s;
    }

    return ptasks_main();
}

int ptasks_init()
{
    dict_set("ptasks.ptasks_iter_n", &ptasks_iter_n);
#ifdef COMP1
    dict_set("ptasks.ptasks_todo_path", &ptasks_todo_path);
#endif

    ptasks_load((props::get().confd_path + PTASKS_FILENAME).c_str());

    return 0;
}

std::string ptasks_dump()
{
    std::stringstream ss;
    ss << "<li><b>ptasks_dump()</b></li>\n";
    for (std::map<std::string, prog_task_t>::iterator i = ptasks.begin(), n = ptasks.end(); i != n; ++i) {
        ss << "<li>" << i->first << " " << (i->second.running ? "running":"") << "</li>\n<li>_ "
            << i->second.go_time << " " << i->second.period_secs
            << " " << i->second.type << "</li>\n";
    }
    return ss.str();
}

#ifndef NDEBUG
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_ptasks)

#ifdef COMP1
BOOST_AUTO_TEST_CASE (ptasks_tests_1)
{
    props::init();
    dict_setv("props.hostip", "192.168.11.153");
    dict_setv("props.usern", "manu");
    dict_setv("props.password", "manu");
    SshBackend::init();
    dict_setv("sshBackend.path", "");
    dict_setv("sshBackend.host", "192.168.11.153");
    dict_setv("sshBackend.user", "manu");
    dict_setv("sshBackend.pass", "manu");
    dict_setv("sshBackend.port", 22);

    SshBackend sshb;
    std::string dbase = "ptasks_tests_1__erasemeifiexist_/";
    std::string dname = dbase + "dir1/ptasks_tests_1/";
    BOOST_CHECK( sshb.mkdir(dname) == 0 );

    std::string fname = "main_ptasks_tests_1.txt";
    {ofstream_utf8 ofs(fname.c_str()); ofs << "1234567890";}
    ptasks.clear();
    add_ptask("ptasks_tests_1_1", fname + " " + dname + "/" + fname, PROG_TASK_PUT, time(0)-1, 0);
    BOOST_CHECK( ptasks.size() == 1 );
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(5));
    BOOST_CHECK( ptasks.size() == 0 );
    add_ptask("ptasks_tests_1_2", dname + "/" + fname + " " + fname + ".1", PROG_TASK_GET, time(0)-1, 0);
    BOOST_CHECK( ptasks.size() == 1 );
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(5));
    BOOST_CHECK( ptasks.size() == 0 );
    std::string resp;
    {ifstream_utf8 ifs((fname+".1").c_str()); ifs >> resp;}
    BOOST_CHECK( resp == "1234567890" );

    BOOST_CHECK( sshb.rmdir(dbase) == 0 );
    boost::system::error_code ec;
    boost::filesystem::remove(fname, ec);
    boost::filesystem::remove(fname + ".1", ec);
}
BOOST_AUTO_TEST_CASE (ptasks_tests_2)
{
    boost::system::error_code ec;
    std::string fname = "main_ptasks_tests_2.txt";
    boost::filesystem::remove(fname, ec);
    boost::filesystem::remove("main_ptasks_tests_3.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_4.txt", ec);
    size_t testvar1 = 8680;
    dict_set("testvar1", &testvar1);
    BOOST_CHECK( dict_get("testvar1") == "8680" );
    {ofstream_utf8 ofs(fname.c_str());
        ofs << "0 /dict?set=testvar1&val=7117" << std::endl;
        ofs << "# nothing" << std::endl;
#ifdef WIN32
        ofs << "1 echo 1 copy main_ptasks_tests_3.txt main_ptasks_tests_4.txt >main_ptasks_tests_3.txt" << std::endl;
#else
    ofs << "1 echo \"1 cp main_ptasks_tests_3.txt main_ptasks_tests_4.txt\" >main_ptasks_tests_3.txt" << std::endl;
#endif // WIN32
        ofs << "nothing" << std::endl;
        ofs << "4 main_ptasks_tests_3.txt" << std::endl;
    }
    ptasks.clear();
    add_ptask("ptasks_tests_2", fname, PROG_TASK_BATCH, time(0)-1, 0);
    BOOST_CHECK( ptasks.size() == 1 );
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(5));
    BOOST_CHECK( ptasks.size() == 0 );

    BOOST_CHECK( boost::filesystem::is_regular_file("main_ptasks_tests_3.txt") );
    BOOST_CHECK( boost::filesystem::is_regular_file("main_ptasks_tests_4.txt") );
    BOOST_CHECK( dict_get("testvar1") == "7117" );

    boost::filesystem::remove(fname, ec);
    boost::filesystem::remove("main_ptasks_tests_3.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_4.txt", ec);
}
BOOST_AUTO_TEST_CASE (ptasks_tests_3)
{
    boost::system::error_code ec;
    boost::filesystem::remove("todo_ptasks_test.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_5.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_6.txt", ec);
    props::init();
    dict_setv("props.hostip", "192.168.11.153");
    dict_setv("props.usern", "manu");
    dict_setv("props.password", "manu");
    SshBackend::init();
    dict_setv("sshBackend.path", "");
    dict_setv("sshBackend.host", "192.168.11.153");
    dict_setv("sshBackend.user", "manu");
    dict_setv("sshBackend.pass", "manu");
    dict_setv("sshBackend.port", 22);

    size_t testvar2 = 8680;
    dict_set("testvar2", &testvar2);
    BOOST_CHECK( dict_get("testvar2") == "8680" );
    {ofstream_utf8 ofs("todo_ptasks_test.txt");
        ofs << "0 /dict?set=testvar2&val=7227" << std::endl;
        ofs << "1 echo hola >main_ptasks_tests_5.txt" << std::endl;
        ofs << "# nothing" << std::endl;
        ofs << "2 " << ptasks_todo_path << ".1 main_ptasks_tests_6.txt" << std::endl;
    }
    SshBackend sshb;
    BOOST_CHECK( sshb.mkdir("control") == 0 );
    task_put("todo_ptasks_test.txt " + ptasks_todo_path);

    ptasks_get_todo();
    BOOST_CHECK( boost::filesystem::is_regular_file("main_ptasks_tests_5.txt") );
    BOOST_CHECK( dict_get("testvar2") == "7227" );
    BOOST_CHECK( boost::filesystem::file_size("main_ptasks_tests_6.txt") == boost::filesystem::file_size("todo_ptasks_test.txt"));

    boost::filesystem::remove("todo_ptasks_test.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_5.txt", ec);
    boost::filesystem::remove("main_ptasks_tests_6.txt", ec);
}
#endif
BOOST_AUTO_TEST_CASE (ptasks_tests)
{
    ptasks.clear();
    log_trace_level = 2;
    boost::system::error_code ec;
    boost::filesystem::remove("ptasts_autotest_1.txt", ec);
    add_ptask("ptasks_autotest_1", "echo hola >ptasts_autotest_1.txt", PROG_TASK_SYS, time(0)+1, 0);
    BOOST_CHECK( ptasks.size() == 1 );
    ptasks_run();
    BOOST_CHECK( ptasks.size() == 1 );
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( ptasks.size() == 0 );
    BOOST_CHECK( boost::filesystem::is_regular_file("ptasts_autotest_1.txt") );
    boost::filesystem::remove("ptasts_autotest_1.txt");
    add_ptask("ptasks_autotest_2", "echo hola >ptasts_autotest_2.txt", PROG_TASK_SYS, time(0)+1, 10);
    BOOST_CHECK( ptasks.size() == 1 );
    boost::this_thread::sleep(boost::posix_time::seconds(3));
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( boost::filesystem::is_regular_file("ptasts_autotest_2.txt") );
    add_ptask("ptasks_autotest_3", "/ptasks?remove=ptasks_autotest_2", PROG_TASK_MUS, time(0)-1, 0);
    BOOST_CHECK( ptasks.size() == 2 );
    ptasks_run();
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    BOOST_CHECK( ptasks.size() == 0 );
    boost::filesystem::remove("ptasts_autotest_2.txt");

    time_t t1 = time(0)+17;
    std::string pn = "ptasks_autotest_0";
    add_ptask(pn, "echo hola", PROG_TASK_SYS, t1, 47);
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == t1 );
    BOOST_CHECK( ptasks[pn].name == pn );
    BOOST_CHECK( ptasks[pn].type == PROG_TASK_SYS );
    BOOST_CHECK( ptasks[pn].command == "echo hola" );
    BOOST_CHECK( ptasks[pn].period_secs == 47 );

    time_t day00 = ((time(0) / (24*3600)) * (24*3600));
    add_ptask(pn, "echo hola", PROG_TASK_SYS, (15*3600), 0);
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(15*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == 0 );
    add_ptask(pn, "echo hola", PROG_TASK_SYS, 1, 24*3600);
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(24*3600)+1) );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );
//std::cout << ptasks_dump() << std::endl << (day00+(24*3600)+1) << std::endl;
    add_ptask(pn, "echo hola", PROG_TASK_SYS, (26*3600), (7*24*3600));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(26*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == (7*24*3600) );
    add_ptask(pn, "echo hola", PROG_TASK_SYS, day00-(12*3600), (24*3600));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(12*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );
    add_ptask(pn, "echo hola", PROG_TASK_SYS, day00-(60*3600), (24*3600));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(12*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );

    add_ptask(pn, "echo hola", PROG_TASK_SYS, props::to_seconds("15:0:0"), 0);
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(15*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == 0 );
    add_ptask(pn, "echo hola", PROG_TASK_SYS, props::to_seconds("1:1"), props::to_seconds("1 0.0.0"));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(24*3600)+61) );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );
//std::cout << ptasks_dump() << std::endl << (day00+(24*3600)+61) << std::endl;
    add_ptask(pn, "echo hola", PROG_TASK_SYS, props::to_seconds("1-2:00:00"), props::to_seconds("7 0 0 0"));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(26*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == (7*24*3600) );
    add_ptask(pn, "echo hola", PROG_TASK_SYS, day00-(12*3600), props::to_seconds("01:00:00:00"));
    BOOST_CHECK( ptasks.size() == 1 );
    BOOST_CHECK( ptasks[pn].go_time == (day00+(12*3600)) );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );

    ptasks[pn].go_time = (day00-(12*3600));
    add_ptask("ptasks_autotest_3", "/ptasks?remove=ptasks_autotest_2", PROG_TASK_MUS, props::to_seconds("21:0:0"), 0);
    add_ptask("ptasks_autotest_4", "echo hola >ptasts_autotest_4.txt", PROG_TASK_SYS, t1-10000, 0);
    t1 = t1 + props::to_seconds("4 21:59:11");
    add_ptask("ptasks_autotest_2", "echo hola >ptasts_autotest_2.txt", PROG_TASK_SYS, t1, 0);
    add_ptask("ptasks_autotest_5", "/ptasks?remove=ptasks_autotest_3", PROG_TASK_MUS, props::to_seconds("01:30:0"), props::to_seconds("21:30:0"));
    ptasks_save("ptasks_autotests.xml");
    ptasks.clear();
    ptasks_load("ptasks_autotests.xml");
    BOOST_CHECK( ptasks.size() == 4 );
    BOOST_CHECK( ptasks[pn].go_time == day00+(12*3600) );
    BOOST_CHECK( ptasks[pn].name == pn );
    BOOST_CHECK( ptasks[pn].type == PROG_TASK_SYS );
    BOOST_CHECK( ptasks[pn].command == "echo hola" );
    BOOST_CHECK( ptasks[pn].period_secs == (24*3600) );
    BOOST_CHECK( ptasks["ptasks_autotest_3"].go_time == day00+(21*3600) );
    BOOST_CHECK( ptasks["ptasks_autotest_3"].name == "ptasks_autotest_3" );
    BOOST_CHECK( ptasks["ptasks_autotest_3"].type == PROG_TASK_MUS );
    BOOST_CHECK( ptasks["ptasks_autotest_3"].command == "/ptasks?remove=ptasks_autotest_2" );
    BOOST_CHECK( ptasks["ptasks_autotest_3"].period_secs == (0) );
    BOOST_CHECK( ptasks["ptasks_autotest_2"].go_time == t1 );
    BOOST_CHECK( ptasks["ptasks_autotest_2"].name == "ptasks_autotest_2" );
    BOOST_CHECK( ptasks["ptasks_autotest_2"].type == PROG_TASK_SYS );
    BOOST_CHECK( ptasks["ptasks_autotest_2"].command == "echo hola >ptasts_autotest_2.txt" );
    BOOST_CHECK( ptasks["ptasks_autotest_2"].period_secs == (0) );
    BOOST_CHECK( ptasks["ptasks_autotest_5"].go_time == day00+(23*3600) );
    BOOST_CHECK( ptasks["ptasks_autotest_5"].name == "ptasks_autotest_5" );
    BOOST_CHECK( ptasks["ptasks_autotest_5"].type == PROG_TASK_MUS );
    BOOST_CHECK( ptasks["ptasks_autotest_5"].command == "/ptasks?remove=ptasks_autotest_3" );
    BOOST_CHECK( ptasks["ptasks_autotest_5"].period_secs == ((21*3600+1800)) );
    ptasks.clear();
    boost::filesystem::remove("ptasks_autotests.xml");
}

BOOST_AUTO_TEST_SUITE_END( )

#endif

