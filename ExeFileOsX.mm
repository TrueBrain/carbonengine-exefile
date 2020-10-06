#include "StdAfx.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#include "ExeFile.h"
#include "Crashpad.h"
#include "CcpCore/include/CcpCrash.h"
#include <fcntl.h>

#if CRASH_REPORTS_ENABLED

std::string GetAppdataFolder()
{
	NSArray *paths = NSSearchPathForDirectoriesInDomains( NSApplicationSupportDirectory, NSUserDomainMask, YES );
	return paths.count && paths.firstObject ? [paths.firstObject UTF8String] : "";
}

#endif

namespace
{
CCPAssertResult CcpReportHookExit( int severity, const char* message )
{
	fprintf( stderr, "%s", message );
	CCP_LOGERR( "%s", message );
	BeOS->Terminate( 1 );

	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookCrash( int severity, const char* message )
{
	fprintf( stderr, "%s", message );
	CCP_LOGERR( "%s", message );
	CcpCrashOnPurpose();
	abort();

	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookAbort( int severity, const char* message )
{
	fprintf( stderr, "%s", message );
	CCP_LOGERR( "%s", message );
	abort();

	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookHandle( int severity, const char* message )
{
	fprintf( stderr, "%s", message );
	CCP_LOGERR( "%s", message );
	return CCP_ASSERT_RESULT_NONE;
}

}

void SilenceAssert(int level)
{
	CcpAssertHook ccpHook;
	if( level == 0 )
	{
		ccpHook = CcpReportHookHandle;
	}
	else if( level == 1 )
	{
		ccpHook = CcpReportHookAbort;
	}
	else if( level == 2 )
	{
		ccpHook = CcpReportHookCrash;
	}
	else if( level == 3 )
	{
		ccpHook = CcpReportHookExit;
	}
	else
	{
		return;
	}
	CcpAssertSetReportHook( ccpHook );
}

void SetWorkingDirectory( const wchar_t* directory )
{
	chdir( CW2A( directory ) );
}

void ShowConsoleWindow( ConsoleMode mode )
{
}

void PreStartupTest()
{
}

void SetProcessAffinity( int affinity )
{
}

int g_originalArgc;
char** g_originalArgv;

int main( int argc, char* argv[] )
{
	g_originalArgc = argc;
	g_originalArgv = argv;
	return Main();
}

#endif
