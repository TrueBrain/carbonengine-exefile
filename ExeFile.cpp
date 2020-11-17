#include "StdAfx.h"
#include "ExeFile.h"
#include "Crashpad.h"

// logging
#include <Logger/Logger.h>
#include <CcpCore/include/CCPLog.h>


#include <errno.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <wchar.h>

#include <fstream>

#include "blue/Include/IBluePaths.h"

#include "CommandArguments.h"

CommandArguments g_commandArguments;

const char* g_moduleName = "ExeFile";

#ifdef _WIN32
#include <signal.h>
#include <pythread.h>
#include <windows.h>
#endif

void ShowBlueErr()
{
	if (BeOS->GetError(0))
	{
		char* err = NULL;
		BeOS->FormatError(&err);
		BeOS->SetError(BEFLUSH); //output to logger
		BeOS->SetError(BECLEAR); //clear it
	}
}

void SetBlueSearchPaths( const std::vector<std::wstring>& searchPaths )
{
	for( std::vector<std::wstring>::const_iterator it = searchPaths.begin(); it != searchPaths.end(); ++it )
	{
		const std::wstring& s = *it;
		size_t pos = s.find_first_of( L'=');
		if( pos == std::wstring::npos )
		{
			CCP_LOGWARN( "Invalid path specification: %S", s.c_str() );
			continue;
		}

		std::wstring keyW = s.substr( 0, pos );
		std::wstring valueW = s.substr( pos + 1 );

		BePaths->SetSearchPathW( CW2A( keyW.c_str() ), valueW.c_str() );
	}
}


//Use this to redirect standard error to a file.
//Note, that we don't like this much.  The user should use
//ExeFile.com in conjunction with shell redirection to do this.
void RedirectOutput( FILE* which, const wchar_t* pattern )
{
	std::wstring tmp;
	const wchar_t *f = wcsstr( pattern, L"%p" );
	if( f )
	{
		tmp = std::wstring( pattern, ( f-pattern ) );
		tmp += std::to_wstring( uint64_t( CcpGetCurrentProcessId() ) );
		tmp += std::wstring( f + 2 );
		pattern = tmp.c_str();
	}
	FILE *os;
#ifdef _WIN32
	//don"t use _wfreopen_s, because it uses no-sharing for the file
	//(one can't browse it until the process dies)
#pragma warning( suppress : 4996 )
	os = _wfreopen(pattern, L"a", which);
#else
	os = freopen( CW2A( pattern ), "a", which );
#endif
	if( os )
	{
		setvbuf(os, 0, _IONBF, 2);
	}
}

#ifdef _WIN32

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T(
				"Eve Node Service: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

		raise(SIGINT); // Signal the main-thread by sending an interrupt to the process.
					   // I HOPE that the stackless system catches this SIGINT and properly shuts down the process

		break;

	default:
		break;
	}
}

bool ServiceSetup()
{
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL)
	{
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T(
				"Eve Node Service: ServiceMain: SetServiceStatus returned error"));
		}
		return FALSE;
	}
	ResetEvent(g_ServiceStopEvent);

	g_StatusHandle = RegisterServiceCtrlHandler("", ServiceCtrlHandler);
	if (g_StatusHandle == NULL)
	{
		return FALSE;
	}

	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"Eve Node Service: ServiceMain: SetServiceStatus returned error"));
	}

	return TRUE;
}

void ServiceStart()
{
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"Eve Node Service: ServiceMain: SetServiceStatus returned error"));
	}
}

void ServiceStop()
{
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"Eve Node Service: ServiceMain: SetServiceStatus returned error"));
	}
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
	ServiceSetup();
	ServiceStart();
	WaitForSingleObject(g_ServiceStopEvent, INFINITE);
	ServiceStop();
	CloseHandle(g_ServiceStopEvent);
}

DWORD WINAPI ServiceEntrypoint(LPVOID lpParam)
{

	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{ (LPSTR)"", (LPSERVICE_MAIN_FUNCTION)ServiceMain }
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}

#endif


int Main()
{
#if !_DEBUG
	auto crashReporter = GetCrashReporter();
	if( crashReporter->InitializeCrashpad() )
	{
		// Tell Blue about our crash interface so that it can set options and settings
		BeCrashes = crashReporter;
	}
#endif
	
	std::vector<std::wstring> commandLine;
	GetCommandLine( commandLine );
	DumpCommandLineToDebugger( commandLine );

	//where are we running?
	ParseCommandLine( commandLine, g_commandArguments );

#if !_DEBUG
	crashReporter->EnableCrashReporting( g_commandArguments.uploadMinidump );
#endif

	BlueModuleStartup();
	BlueInitializeSocketLogger();

#ifdef _WIN32
	HANDLE sdcHandle = 0;
	if (g_commandArguments.asService == true)
	{
		sdcHandle = CreateThread(NULL, 0, ServiceEntrypoint, NULL, 0, NULL);
		if (sdcHandle == NULL)
		{
			CCP_LOGERR("Failed to set up windows service ctrl.");
			return 0;
		}
	}
#endif

	if( g_commandArguments.assertLevel >= 0 )
	{
		SilenceAssert( g_commandArguments.assertLevel );
	}

	//Initialize console and redirect stdoutput
	ShowConsoleWindow( g_commandArguments.consoleMode );
	if( g_commandArguments.redirectStdErr.size() )
	{
		RedirectOutput( stderr, g_commandArguments.redirectStdErr.c_str() );
	}
	if( g_commandArguments.redirectStdOut.size() )
	{
		RedirectOutput( stdout, g_commandArguments.redirectStdOut.c_str() );
	}

	PreStartupTest();

	BeOS->SetStartupArgs( commandLine );
	
	std::wstring defaultPath = CcpGetCurrentWorkingDirectory();
	if (BeOS->HasStartupArg(L"py")) {
		// Python interpreter mode must not assume that the current working directory contains
		// the expected relative paths passed from the varios *.args files.
		// Instead, we're constructing a path relative to /the/path/to/exefile.exe, e.g.:
		// c:/p4/eve/server/bin/x64/exefile.exe 
		// -> c:/p4/eve/server/bin/x64/exefile.exe/../../../ 
		// -> c:/p4/eve/server
		defaultPath = CcpGetAbsolutePath(CcpExecutablePath() + L"/../../..");
	}
	BlueInitializePaths(defaultPath);
	SetBlueSearchPaths( g_commandArguments.searchPaths );
	BePaths->LogPaths();
	BlueInitializeResourceLoading();
	
	if( g_commandArguments.affinity != -1 )
	{
		SetProcessAffinity( g_commandArguments.affinity );
	}

	if( !BeOS->Startup( g_commandArguments.pyOptimize, VERIFY_MANIFEST ) )
	{
		ShowBlueErr();
		return 3; // Blue startup error code
	}
	
	// Now, enter stackless and continue running from there.  This allows stackless to initialize
	// the main tasklet.
	if( !BeOS->RunStackless() )
	{
		ShowBlueErr();
		return 4;
	}

#ifdef _WIN32
	CCP_LOGNOTICE("Windows Service Stop Event Triggered");
	SetEvent(g_ServiceStopEvent);
	WaitForSingleObject(sdcHandle, 5000);
	CloseHandle(sdcHandle);
#endif
	
	// Normally the RunStackless function will terminate the app before even 
	// reaching here.  But just in case that doesn't happen we call Terminate,
	// it's the only way to be sure :)
	BeOS->Terminate();	

	// Stop the compiler from complaining - we won't get here
	return 0;
}

