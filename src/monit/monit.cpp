
#include <time.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include "fstream_utf8.h"
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include "monit_http_client.h"

boost::log::sources::logger lg;

static int lport = 8680;
struct proc_child_adapter_t {
    boost::process::child pc;
    proc_child_adapter_t(const boost::process::child &c) : pc(c) {};
    ~proc_child_adapter_t() { try { pc.terminate(); } catch(...) {} };
};
struct user_t {
    int port;
    int fails;
    proc_child_adapter_t *c;
    user_t() : fails(0), c(NULL) { port = lport++; };
};
static std::map<std::string, user_t> users;

#define USERS_LST_FILE "users.lst"

void start_musched(const std::string &user_name, user_t &user)
{
    BOOST_LOG(lg) << "start_musched(" << user_name << ", " << user.port << ")";
    std::vector<std::string> args;
    args.push_back("bin/musched.exe");
    args.push_back("-u"); args.push_back(user_name);
    args.push_back("-w"); args.push_back(boost::lexical_cast<std::string>(user.port));
    try {
        boost::process::context ctx;
        ctx.environment = boost::process::self::get_environment();
        user.c = new proc_child_adapter_t(boost::process::launch(args[0], args, ctx));

    } catch(std::exception &e) {
        BOOST_LOG(lg) << "Exception starting musched("<<user_name<<"): " << e.what();
    }
}
void kill_musched(user_t &user)
{
    BOOST_LOG(lg) << "kill_musched(" << user.port << ")";
    if (user.c) {
        delete user.c;
        user.c = NULL;
    }
}

static volatile bool monit_run = false;
static volatile bool monit_stopped = false;
void monit_loop()
{
    time_t lt1 = 0, lt2 = 0;
    while (monit_run) {
        monit_stopped = false;
        time_t now = time(0);
        if (lt1 + 900 < now || lt1 > now) {
            lt1 = now;
            try {
                ifstream_utf8 ifs(USERS_LST_FILE);
                if (!ifs) {
                    BOOST_LOG(lg) << "File '" USERS_LST_FILE "' not found";
                } else {
                    std::string user;
                    std::set<std::string> users1;
                    for (std::map<std::string, user_t>::iterator i = users.begin(), n = users.end(); i != n; ++i)
                        users1.insert(i->first);
                    while (std::getline(ifs, user)) {
                        bool is_continuous = false;
                        std::string l,p = "Recursos/EPSILON/" + user + "/Configuracion.txt";
                        { ifstream_utf8 cfs(p.c_str()); if (cfs) {
                            while (std::getline(cfs, l)) {
                                if (l == "clientBackupMethod=Continuous") {
                                    is_continuous = true;
                                    break;
                                }
                            }
                        }}
                        if (is_continuous) {
                            if (users.find(user) == users.end()) {
                                users[user];
                                BOOST_LOG(lg) << "New user found " << user;
                                start_musched(user, users[user]);
                            }
                            users1.erase(user);
                        }
                    }
                    for (std::set<std::string>::iterator i = users1.begin(), n = users1.end(); i != n; ++i) {
                        if (users.find(*i) != users.end()) {
                            BOOST_LOG(lg) << "Stopping user " << (*i);
                            http_client_get("localhost", users[(*i)].port, "/stop");
                            users.erase(*i);
                        }
                    }
                }
            } catch (std::exception &e) {
                BOOST_LOG(lg) << "Exception reading '" USERS_LST_FILE "': " << e.what();
            }
            lt2 = now; // start easy
        }
        if (lt2 + 90 < now || lt2 > now) {
            lt2 = now;
            for (std::map<std::string, user_t>::iterator i = users.begin(), n = users.end(); i != n; ++i) {
                if (http_client_get("localhost", i->second.port, "/home") < 10) {
                    i->second.fails = i->second.fails + 1;
                    BOOST_LOG(lg) << "Ping failed ("<<i->second.fails<<") for user " << i->first;
                    if (i->second.fails > 3) {
                        kill_musched(i->second);
                        start_musched(i->first, i->second);
                        i->second.fails = 0;
                    }
                } else {
                    i->second.fails = 0;
                }
            }
        }
        boost::this_thread::sleep(boost::posix_time::seconds(47));
    }

    monit_stopped = true;
}

#ifdef WIN32
SERVICE_STATUS_HANDLE  service_handle;
SERVICE_STATUS service_status;
void WINAPI serviceControlCallback(DWORD ctrlcode)
{
   switch(ctrlcode) {
   case SERVICE_CONTROL_STOP:
       BOOST_LOG(lg) << "SERVICE_CONTROL_STOP command received";
      service_status.dwCurrentState = SERVICE_STOP_PENDING;
      SetServiceStatus(service_handle, &service_status);
      monit_run = false;
    for (std::map<std::string, user_t>::iterator i = users.begin(), n = users.end(); i != n; ++i) {
        BOOST_LOG(lg) << "Stopping user " << i->first;
        http_client_get("localhost", i->second.port, "/stop");
    }
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    for (std::map<std::string, user_t>::iterator i = users.begin(), n = users.end(); i != n; ++i) {
        kill_musched(i->second);
    }
        service_status.dwWin32ExitCode = 0;
        service_status.dwWaitHint = 0;
        service_status.dwCheckPoint = 0;
        service_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(service_handle, &service_status);
      break;
   }
}
void WINAPI serviceStartCallback(DWORD argc, char **argv)
{
   service_handle = RegisterServiceCtrlHandler("musched_monit", serviceControlCallback);
   if (!service_handle) {
      BOOST_LOG(lg) << "Error starting service";
      return;
   }
   service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwServiceSpecificExitCode = 0;
    service_status.dwCurrentState = SERVICE_START_PENDING;
    service_status.dwControlsAccepted = 0;
    SetServiceStatus(service_handle, &service_status);
    monit_run = true;

    boost::this_thread::sleep(boost::posix_time::seconds(1));
    service_status.dwCurrentState = SERVICE_RUNNING;
    service_status.dwWin32ExitCode = 0;
    service_status.dwWaitHint = 0;
    service_status.dwCheckPoint = 0;
    service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(service_handle, &service_status);

    monit_loop();
   return;
}
#endif

int main(int argc, char *argv[])
{
    boost::filesystem::path musched_monit_path = boost::filesystem::system_complete(argv[0]);
    std::string wp = musched_monit_path.parent_path().parent_path().string();
    if (wp.compare(wp.size()-3, 3, "bin") == 0) wp = wp.substr(0, wp.size()-4);
    std::cout << "path: " << wp << " / " << argv[0] << std::endl;
    boost::filesystem::current_path(wp);

    std::string log_fname = "Recursos/EPSILON/logs/musched_monit_%N.log";
        boost::log::add_file_log(boost::log::keywords::file_name = log_fname,
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::max_size = 60 * 1024 * 1024,
            boost::log::keywords::auto_flush = true,
            boost::log::keywords::open_mode = ( std::ios::out | std::ios::app),
            boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    boost::log::add_common_attributes();

    BOOST_LOG(lg) << "MUSCHED MONIT INIT (" << boost::filesystem::current_path() << ")";
#ifdef WIN32
    if (argc == 1) {
    SERVICE_TABLE_ENTRY dispatchTable[] = {
         { (char *)"musched_monit", (LPSERVICE_MAIN_FUNCTION)serviceStartCallback },
         { (char * )NULL, NULL }
      };

      /*
       * Start the service control dispatcher
       */
      if (!StartServiceCtrlDispatcher(dispatchTable)) {
         BOOST_LOG(lg) << "ERROR: StartServiceCtrlDispatcher failed";
      }
      return 0;
    } else if (std::string(argv[1]) == "help") {
        std::cout << "musched_monit                          -  windows service" << std::endl;
        std::cout << "musched_monit help                     -  show this help" << std::endl;
        std::cout << "musched_monit no_service               -  run musched_monit as standard process" << std::endl;
        std::cout << "musched_monit install [<service id>]   -  install windows service" << std::endl;
        return 0;
    } else if (std::string(argv[1]) == "install") {
        SC_HANDLE muschedService, serviceManager;
        serviceManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
          if (!serviceManager) {
             std::cerr << "Open Service Manager failed" << std::endl;
             return -1;
          }
        muschedService = CreateService(serviceManager,
                                    argc > 2 ? argv[2] : "musched_monit",                       /* Our service name */
                                    argc > 3 ? argv[3] : "Epsilon Continuous Backup", /* Display name */
                                    SERVICE_ALL_ACCESS,
                                    SERVICE_WIN32_OWN_PROCESS,
                                    SERVICE_AUTO_START,
                                    SERVICE_ERROR_NORMAL,
                                    musched_monit_path.string().c_str(),        /* Command string to start the service */
                                    NULL,
                                    NULL,
                                    NULL,            /* Services to start before us */
                                    NULL,                           /* Use default SYSTEM account */
                                    NULL);
      if (!muschedService) {
         CloseServiceHandle(serviceManager);
             std::cerr << "CreateService failed" << std::endl;

         return -1;
      }
      CloseServiceHandle(serviceManager);
      CloseServiceHandle(muschedService);
        return 0;
    }

#endif // WIN32
    monit_run = true;
    monit_loop();
}

