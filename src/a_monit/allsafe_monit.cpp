
#include <time.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include "fstream_utf8.h"
#include "allsafe_dict.h"
#include "allsafe_http_client.h"

boost::log::sources::logger lg;

struct proc_child_adapter_t {
    boost::process::child pc;
    proc_child_adapter_t(const boost::process::child &c) : pc(c) {};
    ~proc_child_adapter_t() { try { pc.terminate(); } catch(...) {} };
};

static size_t lport = 8680;
static std::string allsafe_cmd = "allsafe.exe";

static proc_child_adapter_t *child = NULL;

static std::string allsafe_update_dir = "update";
static void allsafe_update()
{
    try {
        size_t n = 0;
        if (boost::filesystem::exists(allsafe_update_dir)) {
            boost::filesystem::directory_iterator it(allsafe_update_dir),itEnd;
            for (; it != itEnd; it++) {
                if (boost::filesystem::is_regular_file(it->path())) {
                    BOOST_LOG(lg) << "Updating("<<it->path()<<") ...";
                    boost::filesystem::copy_file(it->path(), it->path().filename(),
                                boost::filesystem::copy_option::overwrite_if_exists);
                    n++;
                }
            }
            BOOST_LOG(lg) << n << " files updated, removing '"<<allsafe_update_dir<<"' ...";
            boost::filesystem::remove_all(allsafe_update_dir);
        }
    } catch(std::exception &e) {
        BOOST_LOG(lg) << "Applying update("<<allsafe_update_dir<<"): " << e.what();
    }
}

void start_allsafe()
{
    BOOST_LOG(lg) << "start("<<allsafe_cmd<<")";
    allsafe_update();

    std::vector<std::string> args;
    args.push_back(allsafe_cmd);
    try {
        boost::process::context ctx;
        ctx.environment = boost::process::self::get_environment();
        child = new proc_child_adapter_t(boost::process::launch(args[0], args, ctx));
    } catch(std::exception &e) {
        BOOST_LOG(lg) << "Exception starting '"<<allsafe_cmd<<"': " << e.what();
    }
}
void kill_allsafe()
{
    BOOST_LOG(lg) << "kill("<<allsafe_cmd<<")";
    if (child) {
        delete child;
        child = NULL;
    }
}

static size_t fails = 0;
static size_t monit_period = 50;
static size_t max_fails = 3;
static volatile bool monit_run = false;
static volatile bool monit_stopped = false;
void monit_loop()
{
    time_t lt2 = time(0);
    start_allsafe();
    while (monit_run) {
        monit_stopped = false;
        time_t now = time(0);
        if (lt2 + monit_period < now || lt2 > now) {
            lt2 = now;
            if (http_client_get("localhost", lport, "/status") < 0) {
                fails = fails + 1;
                BOOST_LOG(lg) << "Ping failed " << fails << " times";
                if (fails > max_fails) {
                    kill_allsafe();
                    start_allsafe();
                    fails = 0;
                }
            } else {
                fails = 0;
            }
        }
        boost::this_thread::sleep(boost::posix_time::seconds(monit_period + 1));
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
        BOOST_LOG(lg) << "Stopping " << allsafe_cmd;
        http_client_get("localhost", lport, "/stop");

    boost::this_thread::sleep(boost::posix_time::seconds(2));
    kill_allsafe();

        service_status.dwWin32ExitCode = 0;
        service_status.dwWaitHint = 0;
        service_status.dwCheckPoint = 0;
        service_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(service_handle, &service_status);
      break;
   }
}

static std::string allsafe_service_id = "allsafe_monit";
void WINAPI serviceStartCallback(DWORD argc, char **argv)
{
   service_handle = RegisterServiceCtrlHandler(allsafe_service_id.c_str(), serviceControlCallback);
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

static std::string allsafe_service_desc = "AllSafe Service";
static std::string allsafe_m_logfile = "allsafe_m_%N.log";
//static std::string allsafe_m_work_path;

int main(int argc, char *argv[])
{
    dict_set("wserver.http_port", &lport);
    dict_set("allsafe_m.allsafe_cmd", &allsafe_cmd);
    dict_set("allsafe_m.logfile", &allsafe_m_logfile);
    dict_set("allsafe_m.max_fails", &max_fails);
    dict_set("allsafe_m.monit_period", &monit_period);
    dict_set("allsafe_m.service_id", &allsafe_service_id);
    dict_set("allsafe_m.service_desc", &allsafe_service_desc);
    dict_set("allsafe_m.update_dir", &allsafe_update_dir);

    boost::filesystem::current_path(boost::filesystem::system_complete(argv[0]).branch_path());

    dict_load("dict0.xml");
    dict_load("dict.xml");
    dict_load("dict_m.xml");

    boost::log::add_file_log(boost::log::keywords::file_name = allsafe_m_logfile,
            boost::log::keywords::rotation_size = 4 * 1024 * 1024,
            boost::log::keywords::max_size = 20 * 1024 * 1024,
            boost::log::keywords::auto_flush = true,
            boost::log::keywords::open_mode = ( std::ios::out | std::ios::app),
            boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    boost::log::add_common_attributes();

    BOOST_LOG(lg) << "ALLSAFE MONIT INIT (" << boost::filesystem::current_path() << " / " << allsafe_cmd << ")";
#ifdef WIN32
    if (argc == 1) {
    SERVICE_TABLE_ENTRY dispatchTable[] = {
         { (char *)allsafe_service_id.c_str(), (LPSERVICE_MAIN_FUNCTION)serviceStartCallback },
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
        std::cout << argv[0] << "                  -  windows service" << std::endl;
        std::cout << argv[0] << " help             -  show this help" << std::endl;
        std::cout << argv[0] << " no_service       -  run as standard process" << std::endl;
        std::cout << argv[0] << " install          -  install windows service" << std::endl;
        return 0;
    } else if (std::string(argv[1]) == "install") {
        SC_HANDLE mainService, serviceManager;
        serviceManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
          if (!serviceManager) {
             std::cerr << "Open Service Manager failed" << std::endl;
             return -1;
          }
        mainService = CreateService(serviceManager,
                                    allsafe_service_id.c_str(),                       /* Our service name */
                                    allsafe_service_desc.c_str(), /* Display name */
                                    SERVICE_ALL_ACCESS,
                                    SERVICE_WIN32_OWN_PROCESS,
                                    SERVICE_AUTO_START,
                                    SERVICE_ERROR_NORMAL,
                                    boost::filesystem::system_complete(argv[0]).string().c_str(),        /* Command string to start the service */
                                    NULL,
                                    NULL,
                                    NULL,            /* Services to start before us */
                                    NULL,                           /* Use default SYSTEM account */
                                    NULL);
      if (!mainService) {
         CloseServiceHandle(serviceManager);
             std::cerr << "CreateService failed" << std::endl;

         return -1;
      }
      CloseServiceHandle(serviceManager);
      CloseServiceHandle(mainService);
        return 0;
    }

#endif // WIN32
    monit_run = true;
    monit_loop();
}

