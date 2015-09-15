#include "stdafx.h"
#include "ExeFile.h"

// logging
#include <Logger/Logger.h>
#include <CcpCore/include/CCPLog.h>


#include <errno.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <wchar.h>

#include <fstream>

#include "blue/include/Blue.h"
#include "blue/include/IBlueOS.h"
#include "blue/include/IBluePaths.h"

#include "CommandArguments.h"

CommandArguments g_commandArguments;

const char* g_moduleName = "ExeFile";



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


int Main()
{
	BlueInitializeSocketLogger();

	LogToLogfile( true, "" );

	std::vector<std::wstring> commandLine;
	GetCommandLine( commandLine );
	DumpCommandLineToDebugger( commandLine );

	//where are we running?
	ParseCommandLine( commandLine, g_commandArguments );
	if( g_commandArguments.assertLevel >= 0 )
	{
		SilenceAssert( g_commandArguments.assertLevel );
	}
	if( !g_commandArguments.workingDirectory.empty() )
	{
		SetWorkingDirectory( g_commandArguments.workingDirectory.c_str() );
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

#if BREAKPAD_ENABLED
	// Tell Blue about our crash interface so that it can set options and settings
	BeCrashes = g_crashReporter;
#endif

	BlueInitializePaths();
	SetBlueSearchPaths( g_commandArguments.searchPaths );
	BePaths->LogPaths();
	BlueInitializeResourceLoading();
	
	if( g_commandArguments.affinity != -1 )
	{
		SetProcessAffinity( g_commandArguments.affinity );
	}

	if( !BeOS->Startup( 13/*IBlueOSType.mVersion*/, g_commandArguments.pyOptimize ) )
	{
		ShowBlueErr();
		return -1;
	}

	// Now, enter stackless and continue running from there.  This allows stackless to initialize
	// the main tasklet.
	if( !BeOS->RunStackless() )
	{
		ShowBlueErr();
	}

	// Normally the RunStackless function will terminate the app before even 
	// reaching here.  But just in case that doesn't happen we call Terminate,
	// it's the only way to be sure :)
	BeOS->Terminate();	

	// Stop the compiler from complaining - we won't get here
	return 0;
}
