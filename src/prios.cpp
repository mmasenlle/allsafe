
#include <boost/lexical_cast.hpp>
#include "prios.h"
#include "log_sched.h"

//#define WIN32
#ifdef WIN32

#include <windows.h>
#pragma comment (lib, "AdvApi32.lib")

int prios_init()
{
    TLOG << "prios_init()";
    TOKEN_PRIVILEGES tp;
    LUID luid;
    int r = 0;
    if ( !LookupPrivilegeValue(
            NULL, // lookup privilege on local system
            "SeDebugPrivilege", // privilege to lookup
            &luid ) ) {// receives LUID of privilege
        r = GetLastError();
        ELOG << "prios_init()->LookupPrivilegeValue(): " << r;// << "-" << SysErrorMessage(r);
        return r;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Enable the privilege or disable all privileges.
    HANDLE currProc = GetCurrentProcess();
    HANDLE procToken;
    if (!OpenProcessToken(currProc, TOKEN_ADJUST_PRIVILEGES, &procToken)) {
        r = GetLastError();
        ELOG << "prios_init()->OpenProcessToken(): " << r;// << "-" << SysErrorMessage(r);
    }
    else if ( !AdjustTokenPrivileges(procToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES),
                (PTOKEN_PRIVILEGES) NULL, (PDWORD) NULL) ) {
        r = GetLastError();
        ELOG << "prios_init()->AdjustTokenPrivileges(): " << r;// << "-" << SysErrorMessage(r);
    }
    CloseHandle(procToken);
    CloseHandle(currProc);
    return r;
}

static int prios_map_priority(int prio)
{
    if (prio <= 0)
        return PROCESS_MODE_BACKGROUND_BEGIN;
    else if (prio > 1) {
        switch (prio) {
        case 5: return THREAD_PRIORITY_ABOVE_NORMAL;
        }
    }
    return PROCESS_MODE_BACKGROUND_END;
}

int prios_set_child_prio(int pid, int prio)
{
    if (prio < 0) {
    std::string s = "wmic process where ProcessID=\"";
        s += boost::lexical_cast<std::string>(pid);
        s += "\" CALL setpriority 64 > NUL 2> NUL";
    return system(s.c_str());
    }
    return 0;

    int r = 0;
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
//    if (!SetPriorityClass(GetCurrentProcess(), prios_map_priority(prio))) {
    if (!SetPriorityClass(hProcess, prios_map_priority(prio))) {
        r = GetLastError();
        WLOG << "prios_set_child_prio(" << pid << "," << prio << "): " << r;// << "-" << SysErrorMessage(r);
    }
    CloseHandle(hProcess);
    return r;
}
int prios_set_thread_prio(int prio)
{
//    if (!SetThreadPriority(GetCurrentThread(), prios_map_priority(prio))) {
    if (!SetThreadPriority(GetCurrentThread(),
                (prio <= 0) ? THREAD_MODE_BACKGROUND_BEGIN : THREAD_MODE_BACKGROUND_END)) {
        int r = GetLastError();
        WLOG << "prios_set_thread_prio(" << prio << "): " << r;// << "-" << SysErrorMessage(r);
        return r;
    }
    return 0;
}

#else
int prios_init() { return 0; };
int prios_set_child_prio(int pid, int prio) { return 0; };
int prios_set_thread_prio(int prio) { return 0; };
#endif
