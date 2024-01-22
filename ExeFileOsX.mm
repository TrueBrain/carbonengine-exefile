#include "StdAfx.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#include "ExeFile.h"
#include "Crashpad.h"
#include <CcpCrash.h>
#include <fcntl.h>

#if CCP_MEMORY_REPLACE_OPERATOR_NEW

void* operator new( size_t size )
{
	extern const char* g_moduleName;
	return CCP_MALLOC( g_moduleName, size );
}
void* operator new[]( size_t size )
{
	extern const char* g_moduleName;
	return CCP_MALLOC( g_moduleName, size );
}
void operator delete( void *p ) noexcept
{
	CCP_FREE( p );
}
void operator delete[]( void *p ) noexcept
{
	CCP_FREE( p );
}

#endif

void ShowConsoleWindow( ConsoleMode mode )
{
}

void PreStartupTest()
{
}

void SetProcessAffinity( int affinity )
{
}

bool IsSupportedOSVersion()
{
    if( @available( macOS 10.15, * ) )
    {
        return true;
    }
    else
    {
        return false;
    }
}

int main( int argc, char* argv[] )
{
	CommandLine commandLine = ParseCommandLine( argc, argv );
	return Main( commandLine, IsSupportedOSVersion() );
}

#endif
