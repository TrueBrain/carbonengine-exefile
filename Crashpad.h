#if CRASH_REPORTS_ENABLED

// TODO: Replace experimental with regular filesystem
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

#if __APPLE__
std::string GetAppdataFolder();
#elif _WIN32
std::wstring GetAppdataFolder();
#endif
bool InitializeCrashpad();

extern ICrashReporter* g_crashReporter;

#endif
