#include "StdAfx.h"
#include "ExeFile.h"
#include "Crashpad.h"
#include "BlueInterface.h"

#include <CCPLog.h>

#include <errno.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <wchar.h>

#include <fstream>

#include "CommandArguments.h"

const char* g_moduleName = "ExeFile";

#ifdef _WIN32
#include <signal.h>
#include <pythread.h>
#include <windows.h>
#endif

void ShowBlueErr( const BlueInterface& blue )
{
	if( blue.GetBeOS()->GetError( 0 ) )
	{
		char* err = NULL;
		blue.GetBeOS()->FormatError( &err );
		blue.GetBeOS()->SetError( BEFLUSH ); //output to logger
		blue.GetBeOS()->SetError( BECLEAR ); //clear it
	}
}

bool SetBlueSearchPaths( const BlueInterface& blue, const std::vector<std::wstring>& searchPaths )
{
	for( const std::wstring & s : searchPaths )
	{
		size_t pos = s.find_first_of( L'=');
		if( pos == std::wstring::npos )
		{
			blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_WARN, 0, "Invalid path specification: %S", s.c_str() );
			continue;
		}

		std::wstring keyW = s.substr( 0, pos );
		std::wstring valueW = s.substr( pos + 1 );

		if( !BeIsSuccess( blue.GetBluePaths()->SetSearchPathW( CW2A( keyW.c_str() ), valueW.c_str() ) ) )
		{
			return false;
		}
	}

	return true;
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

int Main(const CommandLine& commandLine)
{
    DumpCommandLineToDebugger( commandLine );
    CommandArguments commandArguments = GetCommandArguments( commandLine );

	BlueInterface blue;
	bool defaultedBlueFlavor = false;
	std::wstring flavor = commandArguments.buildFlavor;
	if( flavor == L"release" )
	{
		flavor = L"";
	}

	const std::vector<std::wstring> validBuildFlavors
	{
		L"", // Release
		L"internal",
		L"trinitydev",
		L"debug",
	};

	if( std::find( std::begin( validBuildFlavors ), std::end( validBuildFlavors ), flavor ) == std::end( validBuildFlavors ) )
	{
		flavor = L"";
		defaultedBlueFlavor = true;
	}

	if( !blue.LoadBlue( flavor ) )
	{
		fprintf( stderr, "Failed to load Blue flavor: '%S', trying default\n", flavor.c_str() );

		if( flavor.empty() || !blue.LoadBlue( L"" ) )
		{
			fprintf( stderr, "Failed to load release Blue flavor\n" );
			fflush( stderr );
			return 5;
		}

		defaultedBlueFlavor = true;
	}

#if !_DEBUG
	auto crashReporter = GetCrashReporter();
	if( commandArguments.uploadMinidump )
	{
		crashReporter->InitializeCrashpad();
	}
	// Tell Blue about our crash interface so that it can change settings
	blue.SetCrashReporter( crashReporter );
#endif

	blue.ModuleStartup();
	blue.InitializeSocketLogger();

	if( defaultedBlueFlavor )
	{
		blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_ERR, 0, "Could not load Blue flavor '%S', defaulted to release flavor.", commandArguments.buildFlavor.c_str() );
	}
	else
	{
		blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_NOTICE, 0, "Loaded Blue flavor '%S'", flavor.c_str() );
	}

#ifdef _WIN32
	HANDLE sdcHandle = 0;
	if (commandArguments.asService == true)
	{
		sdcHandle = CreateThread(NULL, 0, ServiceEntrypoint, NULL, 0, NULL);
		if (sdcHandle == NULL)
		{
			blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_ERR, 0, "Failed to set up windows service ctrl." );
			return 0;
		}
	}
#endif

	//Initialize console and redirect stdoutput
	ShowConsoleWindow( commandArguments.consoleMode );
	if( commandArguments.redirectStdErr.size() )
	{
		RedirectOutput( stderr, commandArguments.redirectStdErr.c_str() );
	}
	if( commandArguments.redirectStdOut.size() )
	{
		RedirectOutput( stdout, commandArguments.redirectStdOut.c_str() );
	}

	PreStartupTest();

	blue.GetBeOS()->SetStartupArgs( commandLine );

	bool interpreterMode = blue.GetBeOS()->HasStartupArg( L"py" );

	std::wstring defaultPath = CcpGetCurrentWorkingDirectory();
	if( interpreterMode )
	{
		// Python interpreter mode must not assume that the current working directory contains
		// the expected relative paths passed from the varios *.args files.
		// Instead, we're constructing a path relative to /the/path/to/exefile.exe, e.g.:
		// c:/p4/eve/server/bin/x64/exefile.exe
		// -> c:/p4/eve/server/bin/x64/exefile.exe/../../../
		// -> c:/p4/eve/server
		defaultPath = CcpGetAbsolutePath(CcpExecutablePath() + L"/../../..");
	}
	blue.InitializePaths( defaultPath );
	if( !SetBlueSearchPaths( blue, commandArguments.searchPaths ) )
	{
		blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_ERR, 0, "Error setting search paths. You might have a circular reference." );
		ShowBlueErr( blue );
		return 6; // Search path argument error
	}
	blue.GetBluePaths()->LogPaths();
	blue.InitializeResourceLoading();

	if( commandArguments.affinity != -1 )
	{
		SetProcessAffinity( commandArguments.affinity );
	}

	if( !blue.GetBeOS()->Startup( commandArguments.pyOptimize, interpreterMode ? IGNORE_MANIFEST : VERIFY_MANIFEST ) )
	{
		ShowBlueErr( blue );
		return 3; // Blue startup error code
	}

	// Now, enter stackless and continue running from there.  This allows stackless to initialize
	// the main tasklet.
	if( !blue.GetBeOS()->RunStackless() )
	{
		ShowBlueErr( blue );
		return 4;
	}

#ifdef _WIN32
	blue.LogFuncChannel( CCP::GetModuleChannel(), CCP::LOGTYPE_NOTICE, 0, "Windows Service Stop Event Triggered" );
	SetEvent(g_ServiceStopEvent);
	WaitForSingleObject(sdcHandle, 5000);
	CloseHandle(sdcHandle);
#endif

	// Normally the RunStackless function will terminate the app before even
	// reaching here.  But just in case that doesn't happen we call Terminate,
	// it's the only way to be sure :)
	blue.GetBeOS()->Terminate();

	// Stop the compiler from complaining - we won't get here
	return 0;
}
