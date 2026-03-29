#ifdef _WIN32
#include <windows.h>
#include <string>
#include "config.h"
#include "sync_loop.h"
#include "logger.h"
#include "platform.h"

static SERVICE_STATUS        g_status{};
static SERVICE_STATUS_HANDLE g_handle  = nullptr;
static SyncLoop*             g_loop    = nullptr;
static std::string           g_config_path;

static void set_service_state(DWORD state, DWORD exit_code = NO_ERROR) {
    g_status.dwCurrentState  = state;
    g_status.dwWin32ExitCode = exit_code;
    SetServiceStatus(g_handle, &g_status);
}

void WINAPI ServiceHandler(DWORD control) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            set_service_state(SERVICE_STOP_PENDING);
            if (g_loop) g_loop->stop();
            break;
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD /*argc*/, LPTSTR* /*argv*/) {
    make_dirs("C:\\ProgramData\\second-brain");
    log_enable_file("C:\\ProgramData\\second-brain\\service.log");
    log_info("ServiceMain started");

    g_handle = RegisterServiceCtrlHandlerA("second-brain", ServiceHandler);
    if (!g_handle) {
        log_error("RegisterServiceCtrlHandlerA failed");
        return;
    }

    g_status.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_status.dwCheckPoint       = 0;
    g_status.dwWaitHint         = 0;
    set_service_state(SERVICE_RUNNING);
    log_info("Reported SERVICE_RUNNING");

    try {
        std::string path = g_config_path.empty() ? get_config_path() : g_config_path;
        SyncLoop loop(path);
        g_loop = &loop;
        loop.run();  // blocks until stop() is called
        g_loop = nullptr;
    } catch (const std::exception& ex) {
        log_error(std::string("Service exception: ") + ex.what());
    }

    set_service_state(SERVICE_STOPPED);
}

void run_as_service(const std::string& config_path) {
    g_config_path = config_path;
    SERVICE_TABLE_ENTRYA dispatch_table[] = {
        {const_cast<char*>("second-brain"), ServiceMain},
        {nullptr, nullptr}
    };
    StartServiceCtrlDispatcherA(dispatch_table);
}

bool service_install(const std::string& exe_path) {
    // Resolve the config path now, in the installing user's context.
    // It gets baked into the service binary path so the service can find it
    // even when running under the LocalSystem account.
    std::string config_path;
    try {
        config_path = get_config_path();
    } catch (const std::exception& ex) {
        log_error(std::string("Cannot determine config path: ") + ex.what());
        return false;
    }
    // Quote both paths to handle spaces; pass config path as --service argument.
    std::string binary_path = "\"" + exe_path + "\" --service \"" + config_path + "\"";

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        log_error("OpenSCManager failed. Run as Administrator.");
        return false;
    }
    SC_HANDLE svc = CreateServiceA(
        scm,
        "second-brain",
        "Second Brain Sync",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binary_path.c_str(),
        nullptr, nullptr, nullptr,
        nullptr,   // LocalSystem account
        nullptr
    );
    if (!svc) {
        DWORD err = GetLastError();
        log_error("CreateService failed (error " + std::to_string(err) + ").");
        CloseServiceHandle(scm);
        return false;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool service_uninstall() {
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        log_error("OpenSCManager failed. Run as Administrator.");
        return false;
    }
    SC_HANDLE svc = OpenServiceA(scm, "second-brain", SERVICE_STOP | DELETE);
    if (!svc) {
        log_error("Service 'second-brain' not found or access denied.");
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    bool ok = (DeleteService(svc) != 0);
    if (!ok) log_error("DeleteService failed.");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

#endif // _WIN32
