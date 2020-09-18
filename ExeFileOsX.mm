#include "StdAfx.h"
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#include "ExeFile.h"
#include "CcpCore/include/CcpCrash.h"
#include <fcntl.h>

namespace
{
    
FILE* logfile = nullptr;
    
CCPAssertResult CcpReportHookExit( int severity, const char* message )
{
    fprintf( stderr, "%s", message );
    CCP_LOGERR( "%s", message );
    BeOS->Terminate(1);

    return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookCrash( int severity, const char* message )
{
    fprintf(stderr, "%s", message);
    CCP_LOGERR( "%s", message);
    CcpCrashOnPurpose();
    abort();

    return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookAbort( int severity, const char* message )
{
    fprintf(stderr, "%s", message);
    CCP_LOGERR( "%s", message);
    abort();

    return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookHandle( int severity, const char* message )
{
    fprintf(stderr, "%s", message);
    CCP_LOGERR( "%s", message);
    return CCP_ASSERT_RESULT_NONE;
}

}

void SilenceAssert(int level)
{
    CcpAssertHook ccpHook;
    if (level == 0) {
        ccpHook = CcpReportHookHandle;
    } else if (level == 1) {
        ccpHook = CcpReportHookAbort;
    } else if (level == 2) {
        ccpHook = CcpReportHookCrash;
    } else if (level == 3) {
        ccpHook = CcpReportHookExit;
    } else {
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
    BlueModuleStartup();
}

void SetProcessAffinity( int affinity )
{
}

bool CreateDirectoryRec(const wchar_t *dir)
{
    if( !mkdir( CW2A( dir ), ACCESSPERMS ) )
    {
        return true;
    }
    if( errno == EEXIST )
    {
        return true;
    }
    //find the last separator
    std::wstring full(dir);
    std::wstring::size_type s = full.find_last_of(L"/\\");
    if (s != std::wstring::npos)
    {
        std::wstring pre = full.substr(0,s);
        if (!CreateDirectoryRec(pre.c_str()))
        {
            return false;
        }
        return !mkdir( CW2A( dir ), ACCESSPERMS );
    }
    return false;
}

std::wstring GetLogsFolder()
{
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSURL* url = [fileManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSLocalDomainMask appropriateForURL:nil create:false error:nil];
    if( url )
    {
        return std::wstring( CA2W( [[url path] UTF8String] ) ) + L"/CCP/EVE";
    }
    return L"";
}

void OpenLogFile()
{
    std::wstring logs = GetLogsFolder();
    if (!logs.size())
    {
        return; //ok, no logs file.
    }
    CreateDirectoryRec(logs.c_str());
    std::wstring filename = logs+L"history.txt";
    int fd = open( CW2A( filename.c_str() ), O_APPEND | O_CREAT | O_WRONLY , S_IREAD | S_IWRITE );
    if (fd == -1)
    {
        return;
    }
    logfile = fdopen(fd,  "a");
    if (!logfile)
    {
        return;
    }
}

void LogToLogfile( bool startup, const char* reason )
{
}

int g_originalArgc;
char** g_originalArgv;

int main( int argc, char* argv[])
{
    g_originalArgc = argc;
    g_originalArgv = argv;
    
    return Main();
}

#endif