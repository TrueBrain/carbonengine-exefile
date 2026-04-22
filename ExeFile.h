// Copyright © 2026 CCP ehf.
#include "CommandArguments.h"

void ShowConsoleWindow( ConsoleMode mode );
void PreStartupTest();
void SetProcessAffinity( int affinity );
bool CreateDirectoryRec( const wchar_t *dir );
int Main( const CommandLine& commandLine, bool isSupportedOS );

#if _WIN32
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
#endif
