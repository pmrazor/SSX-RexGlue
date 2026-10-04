// ssx - crash diagnostics.
//
// Fatal CRT paths (uncaught C++ exception, abort(), invalid CRT parameter) end
// in __fastfail (0xC0000409) without leaving anything in the runtime log. These
// handlers append the reason and a module+offset stack trace to ssx_crash.txt
// next to the executable before the process dies.

#ifdef _WIN32

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <windows.h>

namespace {

void WriteCrashReport(const char* reason, const wchar_t* detail) {
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  wchar_t* slash = wcsrchr(exe_path, L'\\');
  if (slash)
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - exe_path), L"ssx_crash.txt");

  FILE* f = _wfopen(exe_path, L"a");
  if (!f)
    return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  fprintf(f, "=== %04d-%02d-%02d %02d:%02d:%02d.%03d thread %lu: %s\n", t.wYear, t.wMonth, t.wDay,
          t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), reason);
  if (detail)
    fprintf(f, "  detail: %ls\n", detail);

  void* frames[64];
  USHORT count = RtlCaptureStackBackTrace(1, 64, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(frames[i]), &module)) {
      GetModuleFileNameA(module, name, MAX_PATH);
    }
    const char* base_name = strrchr(name, '\\');
    fprintf(f, "  #%02u %s+0x%llx\n", i, base_name ? base_name + 1 : name,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) -
                                            reinterpret_cast<uintptr_t>(module)));
  }
  fclose(f);
}

void OnTerminate() {
  const char* what = "(no active exception)";
  try {
    if (auto e = std::current_exception())
      std::rethrow_exception(e);
  } catch (const std::exception& e) {
    what = e.what();
  } catch (...) {
    what = "(non-std exception)";
  }
  char reason[1024];
  snprintf(reason, sizeof(reason), "std::terminate: %s", what);
  WriteCrashReport(reason, nullptr);
  std::abort();
}

void OnAbortSignal(int) {
  WriteCrashReport("SIGABRT (abort called)", nullptr);
}

void OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t*,
                        unsigned int, uintptr_t) {
  wchar_t detail[512];
  swprintf_s(detail, L"%ls in %ls", expression ? expression : L"?", function ? function : L"?");
  WriteCrashReport("CRT invalid parameter", detail);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* rec = info->ExceptionRecord;
  HMODULE module = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(rec->ExceptionAddress), &module)) {
    GetModuleFileNameA(module, name, MAX_PATH);
  }
  const char* base_name = strrchr(name, '\\');
  wchar_t detail[512];
  swprintf_s(detail, L"code=0x%08lX at %hs+0x%llx, access=%llu addr=0x%llx, rsp=0x%llx",
             rec->ExceptionCode, base_name ? base_name + 1 : name,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(rec->ExceptionAddress) -
                                             reinterpret_cast<uintptr_t>(module)),
             rec->NumberParameters > 0 ? rec->ExceptionInformation[0] : 0ull,
             rec->NumberParameters > 1 ? rec->ExceptionInformation[1] : 0ull,
             info->ContextRecord->Rsp);
  WriteCrashReport("unhandled SEH exception", detail);
  return EXCEPTION_CONTINUE_SEARCH;
}

struct CrashHandlerInstaller {
  CrashHandlerInstaller() {
    SetUnhandledExceptionFilter(OnUnhandledException);
    std::set_terminate(OnTerminate);
    std::signal(SIGABRT, OnAbortSignal);
    _set_invalid_parameter_handler(OnInvalidParameter);
  }
} g_crash_handler_installer;

}  // namespace

#endif  // _WIN32
