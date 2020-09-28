#include "CommandArguments.h"

void SilenceAssert( int level );
void SetWorkingDirectory( const wchar_t* directory );
void ShowConsoleWindow( ConsoleMode mode );
void PreStartupTest();
void SetProcessAffinity( int affinity );
void LogToLogfile( bool startup, const char* reason );
bool CreateDirectoryRec( const wchar_t *dir );
std::string GetCrashDumpPath();
int Main();



#ifdef _WIN32
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
#endif
