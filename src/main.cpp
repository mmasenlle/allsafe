#include <iostream>
#include <boost/locale.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include "mails.h"
#include "old_files_data.h"
#include "props_sched.h"
#include "log_sched.h"
#include "cc_rules.h"
#include "web_utils.h"
#include "prios.h"
#include "persistence.h"
#include "dict.h"
#include "dirs.h"

extern void resched();
extern void sched_init();
extern void sched_end();
//extern void sched_save_data();
extern void save_data();
extern void journal_init();
extern int file_watcher_init();
extern int mod_files_init();
extern int mod_files_start();
extern int sftp_init();
extern void sftp_end();
#ifdef COMP1
extern int mssql_init();
extern int oracle_init();
#endif
extern int ptasks_init();

#ifdef WIN32
#include <Windows.h>
extern void win_service_init();
LONG WINAPI DefaultCrashHandler(EXCEPTION_POINTERS * /*ExceptionInfo*/)
{
//    FLOG << "DefaultCrashHandler(): " << GetExceptionCode();
    FLOG << "DefaultCrashHandler() !!!!!!!!!!!!!!!!!!!!!!!";
    return EXCEPTION_CONTINUE_EXECUTION; //EXCEPTION_EXECUTE_HANDLER; //EXCEPTION_CONTINUE_SEARCH;
}
void myInvalidParameterHandler(const wchar_t* expression,
   const wchar_t* function,
   const wchar_t* file,
   unsigned int line,
   uintptr_t pReserved)
{
   wprintf(L"Invalid parameter detected in function %s."
            L" File: %s Line: %d\n", function, file, line);
   wprintf(L"Expression: %s\n", expression);

   FLOG << "myInvalidParameterHandler() !!!!!!!!!!!!!!!!!!!!!!!";
}
#else
#include <signal.h>
#endif // WIN32

extern bool pausa;
extern int nthreads;
extern const char *main_version;

#define MODULE_NAME "allsafe"

#ifndef NDEBUG
#define BOOST_TEST_NO_MAIN
#define BOOST_TEST_NO_LIB
#define BOOST_TEST_MODULE main_test_module
#include <boost/test/included/unit_test.hpp>
#endif // NDEBUG

BOOST_LOG_ATTRIBUTE_KEYWORD(a_channel, "Channel", std::string);

int main(int argc, char *argv[])
{
#ifdef WIN32
#ifdef NDEBUG
    SetUnhandledExceptionFilter(DefaultCrashHandler);
    _set_invalid_parameter_handler(myInvalidParameterHandler);
#endif
    boost::filesystem::path::imbue(boost::locale::generator().generate(""));
#endif // WIN32
#ifndef NDEBUG
    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        log_trace_level = 3;
        return boost::unit_test::unit_test_main( init_unit_test_suite, argc, argv );
    }
#endif // NDEBUG
    {  int requested_verbose = -1;
    boost::program_options::options_description desc("Options");
    desc.add_options()
      ("addr,a", boost::program_options::value<std::string>(), "http server address (127.0.0.1)")
      ("backend,b", boost::program_options::value<std::string>(), "backend name (local,ssh)")
      ("dict,d", boost::program_options::value<std::string>(), "dictionary file to load")
      ("help,h", "print help messages")
      ("logfile,l", boost::program_options::value<std::string>(), "specify log file (stdout)")
      ("nthreads,n", boost::program_options::value<std::vector<int> >()->multitoken(), "number of threadas (<n>[ <max>[ <min>]])")
      ("pause,p", "start in pause mode")
#ifdef COMP1
      ("user,u", boost::program_options::value<std::string>(), "epsilon backup user")
#endif
      ("verbose,v", boost::program_options::value<int>(&requested_verbose), "verbosity level")
      ("version,V", "print version")
      ("port,w", boost::program_options::value<int>(), "http server port (8680)");
//    boost::log::add_console_log(std::cout, boost::log::keywords::auto_flush = true,
//            boost::log::keywords::format = "[%TimeStamp%] %Message%");
////            boost::log::keywords::format = "[%TimeStamp% %ThreadID%] %Message%");
//    boost::log::add_common_attributes();
    boost::program_options::variables_map vm;
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(),
                vm); // throws on error
        if ( vm.count("help")  ) {
            std::cout << desc << std::endl;
            return 0;
        }
        if ( vm.count("version")  ) {
            std::cout << MODULE_NAME " version: " << main_version << "" << std::endl;
            return 0;
        }
        boost::program_options::notify(vm);
    } catch(boost::program_options::error& e) {
        ELOG << "Program options error: " << e.what();
    }
    if ( vm.count("logfile") && vm["logfile"].as<std::string>() == "stdout" ) {
        boost::log::add_console_log(std::cout, boost::log::keywords::auto_flush = true,
            boost::log::keywords::format = "[%TimeStamp%] %Message%");
    } else {
        std::string log_fname = MODULE_NAME "_%N.log";
        if ( vm.count("logfile") ) log_fname = vm["logfile"].as<std::string>() + "_%N.log";
#ifdef COMP1
        if ( vm.count("user") )
            log_fname = "Recursos/EPSILON/" + vm["user"].as<std::string>() + "/logs/musched_%N.log";
#endif
        boost::log::add_file_log(boost::log::keywords::file_name = log_fname,
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::max_size = 60 * 1024 * 1024,
            boost::log::keywords::auto_flush = true,
            boost::log::keywords::open_mode = ( std::ios::out | std::ios::app),
            boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    }
    boost::log::add_common_attributes();
    if (requested_verbose >= 0) log_trace_level = requested_verbose;
    ILOG << "Starting " MODULE_NAME " (" << main_version << ")";
    dict_load("dict0.xml");
    if (requested_verbose >= 0) log_trace_level = requested_verbose;
#ifdef COMP1
    if ( vm.count("user") ) props::set_user(vm["user"].as<std::string>());
    if ( dict_get("allsafe.mode").size() && (dict_get("allsafe.mode") != "0") ) {
#endif
        props::init();
        dict_load();
        dirs_load();
#ifdef COMP1
    } else {
        props::load_fluxu(argv[0]);
        props::load_conf();
    }
#endif // COMP1
    if ( vm.count("dict") ) dict_load(vm["dict"].as<std::string>());
    if (requested_verbose >= 0) { log_trace_level = requested_verbose; dict_setv("wserver.log_trace_level", requested_verbose); }
    if ( vm.count("pause") ) pausa = 1;
    if ( vm.count("addr") ) dict_setv("wserver.http_addr", vm["addr"].as<std::string>());
    if ( vm.count("backend") ) dict_setv("props.backend", vm["backend"].as<std::string>());
    if ( vm.count("port") ) dict_setv("wserver.http_port", (size_t)vm["port"].as<int>());
    if ( vm.count("nthreads") ) {
        std::vector<int> vn = vm["nthreads"].as<std::vector<int> >();
        if (vn.size() > 0) nthreads = vn[0];
        if (vn.size() > 1) dict_setv("sched.max_threads", (size_t)vn[1]);
        if (vn.size() > 2) dict_setv("sched.min_threads", (size_t)vn[2]);
    }}    try {
#ifndef WIN32
        signal(SIGPIPE, SIG_IGN);
#endif
    props::update();
    {
    boost::log::add_file_log(
        boost::log::keywords::file_name = props::get().confd_path + "logs/files_ok_%Y-%m-%d.log",
        boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
        boost::log::keywords::max_size = 100 * 1024 * 1024,
        boost::log::keywords::auto_flush = true,
        boost::log::keywords::open_mode = ( std::ios::out | std::ios::app),
        boost::log::keywords::filter = a_channel == "lg_ok",
        boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    boost::log::add_file_log(
        boost::log::keywords::file_name = props::get().confd_path + "logs/files_err_%Y-%m-%d.log",
        boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
        boost::log::keywords::max_size = 100 * 1024 * 1024,
        boost::log::keywords::auto_flush = true,
        boost::log::keywords::open_mode = ( std::ios::out | std::ios::app),
        boost::log::keywords::filter = a_channel == "lg_err",
        boost::log::keywords::format = "[%TimeStamp%]: %Message%");
    }
    ccrules_load();
#ifdef COMP1
    mssql_init();
    oracle_init();
#endif
    ptasks_init();
    prios_init();
    mails_init();
    persistence_init();
    ofd_init();
    sftp_init();
    sched_init();
    wserver_init();
    file_watcher_init();
    mod_files_init();
#ifdef WIN32
    //journal_init();
#endif // WIN32
    mod_files_start();
    resched();
    sched_end();
    save_data();
    sftp_end();
    ofd_end();
    persistence_end();
        ILOG << argv[0] << " normal ending";
    } catch (std::exception &e) {
        FLOG << "Exception '" << e.what() << "' in main thread";
        return -1;
    } catch (...) {
        FLOG << "Exception no STD in main thread";
        return -11;
    }
    return 0;
}
