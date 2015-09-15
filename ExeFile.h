#include "CommandArguments.h"

void SilenceAssert( int level );
void SetWorkingDirectory( const wchar_t* directory );
void ShowConsoleWindow( ConsoleMode mode );
void PreStartupTest();
void SetProcessAffinity( int affinity );
void LogToLogfile( bool startup, const char* reason );
bool CreateDirectoryRec(const wchar_t *dir);
int Main();

extern ICrashReporter* g_crashReporter;
