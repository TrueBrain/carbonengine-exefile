#include "stdafx.h"
#include "ExeFile.h"

// logging
#include <Logger/Logger.h>
#include <CcpCore/include/CCPLog.h>


#include <windows.h>
#include <shellapi.h> // For ShellExecute
#include <shlobj.h> //for SHGetFolderPath
#include <crtdbg.h>
#include <errno.h>
#include <string>
#include <vector>
#include <atlbase.h>
#include <fcntl.h>
#include <wchar.h>
#include <fstream>

#include "blue/include/Blue.h"
#include "blue/include/IBlueOS.h"
#include "blue/include/IBluePaths.h"

const char* g_moduleName = "ExeFile";

static bool g_quit = false;
ExeFile g_exeObject;
static HINSTANCE g_instance = NULL;
std::wstring g_exeModuleName;

// Reserve memory used for error handling
static void * g_reserveMemory = 0;

// Does breakpad upload the minidump?
bool g_uploadMinidump = true;

// The build number used for minidumps
unsigned int g_buildno = 999999;

unsigned int g_userId = 0;
int64_t g_sessionId = 0;

FILE* g_sessionFile = nullptr;

#if BREAKPAD_ENABLED

#include "client/windows/handler/exception_handler.h"
#include "client/windows/sender/crash_report_sender.h"
#include <WinInet.h>

namespace google_breakpad 
{
	static wchar_t s_crashDumpPath[MAX_PATH];
	static ExceptionHandler* s_breakpadExceptionHandler = NULL;
	static CrashReportSender* s_breakpadMinidumpUploader = NULL;
	static std::map<std::wstring,std::wstring> s_breakpadMinidumpUploadHeaders;
	static std::wstring s_breakpadCrashUploaderResult;
}

// This interface class is given to Blue to allow it to configure some breakpad settings
struct BreakpadCrashInterface: public ICrashReporter
{
	// Sets key/value pairs (used in breakpad http post parameters)
	virtual void SetCrashKeyValueW( wchar_t* key, wchar_t* val )
	{
		google_breakpad::s_breakpadMinidumpUploadHeaders[key]=val;
	}

	// Turn the breakpad upload on or off
	virtual void EnableCrashReporting( bool enable )
	{
		g_uploadMinidump = enable;
	}

	virtual bool IsCrashReportingEnabled()
	{
		return g_uploadMinidump;
	}

	// This allows us to set the build number from python when boot.ini is read
	// It gets set as soon as possible, rather than when we crash (which was the older method)
	virtual void SetBuildNumber( unsigned b )
	{
		g_buildno = b;
	}

	virtual void SetSessionFileDescriptor( int fd )
	{
		g_sessionFile = _fdopen( fd, "a" );
		if( g_sessionFile )
		{
			CCP_LOG( "Session file set" );
		}
		else
		{
			CCP_LOGERR( "Failed to set session file" );
		}
	}

	virtual void SetUserId( int id )
	{
		g_userId = id;
	}

	virtual void SetSessionId( int64_t id )
	{
		g_sessionId = id;
	}

	virtual void ProduceImmediateDump()
	{
		if( google_breakpad::s_breakpadExceptionHandler )
		{
			google_breakpad::s_breakpadExceptionHandler->WriteMinidump();
		}
	}
};

static BreakpadCrashInterface s_breakpadCrashInterface;

#endif

// Command line arguments are gathered in here. Arguments starting with @
// are assumed to be referencing a file with further arguments, expanded
// into here.
std::vector<std::wstring> g_commandLineArguments;

// How is shutdown done?
// 0 = soft shutdown, 1 = firm shutdown, 2 = hard shutdown
// Defaults to 0, set with /hardkill=x
int g_hardkill = 0;     

// Should a minidump be produced on exceptions? Defaults to false,
// set to true with /minidump
bool g_minidump = false; //no minidumps

// Is ExeFile verbose?
bool g_verbose = false;

// Device name to use for LogServer - defaults to EVE.
// Set with /logDevice=<name>
std::wstring g_logDeviceName( L"EVE" );

// Search paths - add an entry with /path:key=value
std::vector<std::wstring> g_searchPaths;

// Old-style path for binpath
std::wstring g_binPath;

// Old-style path for rootpath
std::wstring g_rootPath;

// Old-style path for libpath
std::wstring g_libPath;

int g_affinity = -1;
bool g_jessica = false;

#ifndef _DEBUG
int g_pyoptimize = 1; //run as python with -O flag.
#else
int g_pyoptimize = -1; //use whatever default blue gives us
#endif

void Usage(bool ok);
std::wstring ToLower(const std::wstring &s);
std::wstring PathSubst(const std::wstring &s);
DWORD ParseCommandLine();
std::wstring GetModuleName();
std::wstring GetRootFolder();
std::wstring GetBinFolder();
const std::wstring &GetLogsFolder();
std::wstring GetResFolder();
bool CreateDirectoryRec(const wchar_t *dir);
#if _MSC_VER >= 1400
void myInvalidParameterHandler(const wchar_t* expression,
   const wchar_t* function, 
   const wchar_t* file, 
   unsigned int line, 
   uintptr_t pReserved);
_invalid_parameter_handler oldHandler=0;
#endif
std::string FmtError(DWORD err=0); //format windows error message


//--------------------------------------------------------------------
// --------------------------------------------------------------------------------
// Description:
//   Temporary copy of function from TransGaming.cpp
// --------------------------------------------------------------------------------
bool IsTransgaming()
{
	HMODULE hMod = GetModuleHandle("ntdll");
	typedef bool (*IsTransgaming) (void);
	IsTransgaming pFunc = (IsTransgaming)GetProcAddress (hMod, "IsTransgaming");

	return pFunc && pFunc();
}
// UncaughtExceptions
//--------------------------------------------------------------------
static LPTOP_LEVEL_EXCEPTION_FILTER prevFilter = NULL;
static void OpenLogFile();
static void LogToLogfile(bool startup, const char* reason);

FILE *logfile = 0;
static void OpenLogFile()
{
	std::wstring logs = GetLogsFolder();
	if (!logs.size())
	{
		return; //ok, no logs file.
	}
	CreateDirectoryRec(logs.c_str());
	std::wstring filename = logs+L"history.txt";
	int fd;
	errno_t err = _wsopen_s(&fd, filename.c_str(), _O_APPEND|_O_CREAT|_O_WRONLY , _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if (err || fd == -1)
	{
		return;
	}
	logfile = _wfdopen(fd,  L"a");
	if (!logfile)
	{
		return;
	}

	//make _Crt error messages also go to this file
	//Note, this is not global, as long as we don't use DLL CRT.
	int facility[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
	for( int i = 0; i < sizeof(facility)/sizeof(*facility); i++ )
	{
		int mode = _CrtSetReportMode(facility[i], _CRTDBG_REPORT_MODE) | _CRTDBG_MODE_FILE;
		_CrtSetReportMode(facility[i], mode);
		_CrtSetReportFile(facility[i], (HANDLE)_get_osfhandle(_fileno(logfile)));
	}
}	

static void LogToLogfile(bool startup, const char* reason)
{
	const char *prefix = startup?"STARTUP":"SHUTDOWN";
	CCP_LOGERR( "Exefile %s %s", prefix, reason );
	if (true)
	{
		if( !logfile )
		{
			OpenLogFile();
		}
		
		if( logfile != NULL )
		{
			SYSTEMTIME st;
			GetSystemTime(&st);
			fprintf(
				logfile,
				// 2003.04.29 10:44:14	STARTUP [8273]:	
				// 2003.04.29 10:46:20	SHUTDOWN [8273: Oh no!!!!
				"%.4d.%.2d.%.2d %.2d:%.2d:%.2d\t%s [%d]\t%s\n",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
				prefix, GetCurrentProcessId(), reason
				);

			fflush(logfile);
		}
	}
}

//--------------------------------------------------------------------
DWORD WINAPI KillThread(LPVOID)
{
	Sleep(6500);
	fprintf(stderr, "ExeFile.KillThread terminating process");
	CCP_LOGERR( " ExeFile.KillThread terminating process" );
	TerminateProcess(GetCurrentProcess(), 0);
	return 0;
}


//--------------------------------------------------------------------
// ShowConsoleWindow
// ExeFile is a WindowsApplication.  But we sometimes want a console,
// and in particular, we want to run the servers as regular console
// applications.  This function attemts to emulate the services
// normally provided by the system when starting a "console" app.
// When CreateProcess starts a non-console application, it doesn't
// heed the CREATE_NEW_CONSOLE flag.
// And DETACHED_PROCESS doesn't prevent the process from attaching
// to the parent process...  Therefore, to emulate a console app,
// one has to be reasonable.
//--------------------------------------------------------------------
enum ConsoleMode {
		console_mode_off,		//deprecated: Neither attach nor create.
		console_mode_inherit,   //attach to whatever you have access to
		console_mode_create,	//create a new console.
	};

void ShowConsoleWindow(ConsoleMode mode)
{
	if( mode == console_mode_off )
	{
		// Completely bypassing any mucking around with IO handles.
		// This tended to break Mac Remote Debugging.
		return;
	}

	//first, look at those std handles that we want to keep.  Allocating
	//or attaching to a console will trample these handles that we were given
	//by CreateProcess.  C stdio has already been initialized to use these
	//handles.
	DWORD hKeys[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
	HANDLE keep[3];
	for( int i = 0; i<3; i++ )
	{
		//We detect those file redirection handles by our ability to set
		//their inheritance flags.  Console handles can't do that.
		//(cmd.exe will pass on _its_ stdio handles, even if they are to 
		//a console, and they won't work for us).
		HANDLE h = GetStdHandle(hKeys[i]);
		if (h && SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
		{
			keep[i] = h;
		}
		else
		{
			keep[i] = 0;
		}
	}

	//Now, Attach to or Create a console, depending on our mode.
	BOOL console;
	switch (mode)
	{
		case console_mode_off:
			console = FALSE;
			break;
		case console_mode_inherit:
			{
				//dynamic loading of AttachConsole, which is supported only on XP and later
				console = FALSE;
				HMODULE h = GetModuleHandle("kernel32");
				if( h )
				{
					typedef BOOL (__stdcall *at)(DWORD);
					at ft = (at)GetProcAddress(h, "AttachConsole");
					if( ft )
					{
						console = (*ft)(-1);
					}
				}
				break;
			}
		case console_mode_create:
			console = AllocConsole();
			break;
	}

	if( console )
	{
		//if we want to keep any handles, reinstate those handles now,
		//else, reopen stdio to the new handles from console alloc/attach
		//(Maybe we should close those handles that we trample but lets
		//not worry about that)
		if( mode == console_mode_inherit )
		{
			for( int i = 0; i<3; ++i )
			{
				if( keep[i] )
				{
					SetStdHandle( hKeys[i], keep[i] );
				}
			}
		}

		//reopen stdio for those handles that we didn't want to keep from before
		FILE *si, *so, *se;
		if( mode == console_mode_create || !keep[0] )
		{
			freopen_s(&si, "CONIN$", "r", stdin);
		}
		if( mode == console_mode_create || !keep[1] )
		{
			freopen_s(&so, "CONOUT$", "w", stdout);
		}
		if( mode == console_mode_create || !keep[2] )
		{
			freopen_s(&se, "CONOUT$", "w", stderr);
		}

	}
	if( mode != console_mode_off )
	{
		//We are pretending to be a console app

		// Set the output channel of the CRT error handler to stderr
		// This will prevent abort() and assert() from putting up a
		// dialogue box. (default for win app is a dialog box)
		_set_error_mode(_OUT_TO_STDERR);

		// Change abort behaviour to not invoke DRWatson for a dump when
		// abort() is called.  Turn off the _CALL_REPORTFAULT flag.
		_set_abort_behavior(0, _CALL_REPORTFAULT);
		
#if 0
		// For _DEBUG mode, you can disable the "abort, retry ignore" dialoge box by
		// enabling this part following.  But since the cluster runs only release, this is
		// not really needed.
		const int chans[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
		for( int i = 0; i<3; i++) {
			int chan = chans[i];
			int mode = _CrtSetReportMode(chan, _CRTDBG_REPORT_MODE);
			if (mode & _CRTDBG_MODE_WNDW) {
				mode &= ~_CRTDBG_MODE_WNDW;
				mode |= _CRTDBG_MODE_FILE;
				_CrtSetReportMode(chan, mode);
				_CrtSetReportFile(chan, _CRTDBG_FILE_STDERR);
			}
		}
#endif
	}
}

//Use this to redirect standard error to a file.
//Note, that we don't like this much.  The user should use
//ExeFile.com in conjunction with shell redirection to do this.
bool RedirectOutput(FILE *which, const wchar_t *pattern)
{
	std::wstring tmp;
	const wchar_t *f = wcsstr(pattern, L"%p");
	if( f )
	{
		wchar_t buffer[16];
		DWORD pid = GetCurrentProcessId();
		_itow_s(pid, buffer, 10);
		tmp = std::wstring(pattern, (f-pattern));
		tmp += buffer;
		tmp += std::wstring(f+2);
		pattern = tmp.c_str();
	}
	FILE *os;
	//don"t use _wfreopen_s, because it uses no-sharing for the file
	//(one can't browse it until the process dies)
#pragma warning( suppress : 4996 )
	os = _wfreopen(pattern, L"a", which);
	if( os )
	{
		setvbuf(os, 0, _IONBF, 2);
		return true;
	}
	return false;
}

#if BREAKPAD_ENABLED 

bool BreakpadDumpCallback(const wchar_t* dump_path,
						 const wchar_t* minidump_id,
						 void* context,
						 EXCEPTION_POINTERS* exinfo,
						 MDRawAssertionInfo* assertion,
						 bool succeeded)
{
	using namespace google_breakpad;

	wchar_t fullFilePath[MAX_PATH];
	wcsncpy_s( fullFilePath, MAX_PATH, s_crashDumpPath, wcsnlen(s_crashDumpPath, MAX_PATH) );
	wcsncat_s( fullFilePath, MAX_PATH, minidump_id, wcsnlen(minidump_id, MAX_PATH) );
	wcsncat_s( fullFilePath, MAX_PATH, L".dmp", 4 );

	char reason[1024];
	reason[1023]='\0';
	int resultCode = -1;

	if( s_breakpadMinidumpUploader && g_uploadMinidump )
	{
		ReportResult res = s_breakpadMinidumpUploader->SendCrashReport(L"http://crashes.eveonline.com/UploadDump", s_breakpadMinidumpUploadHeaders, fullFilePath, &s_breakpadCrashUploaderResult );
		resultCode = (int)res;
		switch( res )
		{
		case RESULT_FAILED:
			CCP_LOGERR( "Upload Crash Dump for b%d, %S, RESULT_FAILED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_REJECTED:
			CCP_LOGERR( "Upload Crash Dump for b%d, %S, RESULT_REJECTED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_SUCCEEDED:
			// Actually a LOG_NOTICE
			CCP_LOG( "Upload Crash Dump for b%d, %S, RESULT_SUCCEEDED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_THROTTLED:
			CCP_LOGWARN( "Upload Crash Dump for b%d, %S, RESULT_THROTTLED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		}

		if( exinfo )
		{
			// We write a history of startup and shutdowns in history.txt
			_snprintf_s(
				reason, 
				_countof(reason), 
				_TRUNCATE, 
				"%s (0x%.8X) for b%d, minidump written %s in %S", 
				g_quit ? "Quit" : "Crashed", 
				exinfo->ExceptionRecord->ExceptionCode, 
				g_buildno,
				res == RESULT_SUCCEEDED ? "and uploaded" : "but not uploaded",
				fullFilePath );
		}
	}
	else
	{
		CCP_LOG( "Wrote Crash Dump for b%d, %S", g_buildno, fullFilePath );

		// We write a history of startup and shutdowns in history.txt
		_snprintf_s(
			reason, 
			_countof(reason), 
			_TRUNCATE, 
			"%s (0x%.8X) for b%d, minidump written in %S", 
			g_quit ? "Quit" : "Crashed",
			exinfo->ExceptionRecord->ExceptionCode,
			g_buildno,
			fullFilePath );
	}

	if( exinfo )
	{
		// Freeze dumps don't have any exception info
		if( g_sessionFile )
		{
			Be::Time timeStamp = BeOS->GetActualTime();

			CCP_LOG( "Writing to session file (%d, %I64d, %I64d, %d, %S, %d)", g_userId, g_sessionId, timeStamp, g_buildno, minidump_id, resultCode );

			fprintf( g_sessionFile, "- crashed\n- %d\n- %I64d\n- %I64d\n- %d\n- %S\n- %d\n", g_userId, g_sessionId, timeStamp, g_buildno, minidump_id, resultCode );
			fflush( g_sessionFile );
		}
		else
		{
			CCP_LOG( "No session file" );
		}

		LogToLogfile(false, reason);
	}

	// Report it as unhandled, to allow debuggers to catch it
	return false;
}
#endif
//--------------------------------------------------------------------
// WinMain
//--------------------------------------------------------------------
int APIENTRY WinMain_Guarded(HINSTANCE hInstance, HINSTANCE hi2, LPSTR cmdline, int arg);
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hi2, LPSTR cmdline, int arg)
{
#if _MSC_VER >= 1400
	//NOTE! This is only local to Exefile as long as we don't use DLL RCT
	oldHandler = _set_invalid_parameter_handler(myInvalidParameterHandler);
#endif
	int retcode = 0;

#if BREAKPAD_ENABLED
	{
		using namespace google_breakpad;

        char computerName [MAX_COMPUTERNAME_LENGTH + 1];
        DWORD sizeComputerName = sizeof ( computerName );


		// Get the name of the domain for the computer
		// We generally want this so that we can treat machines that are on the CCP network specially
		std::string ccpDomainTest;
		ccpDomainTest.resize( 128 );
		DWORD envResultSize = GetEnvironmentVariable( "USERDNSDOMAIN", &ccpDomainTest[0], 128 );
		if( envResultSize != 0 )
		{
			if( !ccpDomainTest.empty() && strncmp( ccpDomainTest.c_str(), "CCP.AD.LOCAL", 12 ) == 0 )
			{
			    s_breakpadMinidumpUploadHeaders[L"domain"] = CA2W(ccpDomainTest.c_str());
    
		        // Get the name of the computer
                GetComputerName(computerName, &sizeComputerName);
                s_breakpadMinidumpUploadHeaders[L"computerName"] = CA2W(computerName);
			}
		}

		// This code is duplicated from 'GetLogsFolder'
		HRESULT hr = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, s_crashDumpPath);
		if( SUCCEEDED(hr) )
		{
			wcsncat_s(s_crashDumpPath, L"\\CCP\\EVE\\", 12 );
			CreateDirectoryRec( s_crashDumpPath );
		}

		// The crash history file tracks which dumps have been uploaded to us for throttling and retrying
		wchar_t crashHistoryFilePath[MAX_PATH];
		wcsncpy_s( crashHistoryFilePath, MAX_PATH, s_crashDumpPath, wcsnlen(s_crashDumpPath, MAX_PATH) );
		wcsncat_s( crashHistoryFilePath, MAX_PATH, L"crash_history.crs", 17 );

		DWORD miniDumpType = MiniDumpWithIndirectlyReferencedMemory;

		wchar_t* found = wcsstr( GetCommandLineW(), L"/fulldump" );
		if( found )
		{
			miniDumpType |= MiniDumpWithPrivateReadWriteMemory | MiniDumpWithDataSegs;
		}

		// Register Breakpad's handler
		s_breakpadExceptionHandler = new ExceptionHandler(
			s_crashDumpPath,
			NULL,
			&BreakpadDumpCallback,
			NULL,
			// Don't catch CRT Invalid Parameter issues, since we're not using debug CRTs
			// Assertion info is therefore NULL, and the handler doesn't like this
			ExceptionHandler::HANDLER_EXCEPTION | ExceptionHandler::HANDLER_PURECALL,
			// http://www.debuginfo.com/articles/effminidumps.html
			// Add small blocks of captured memory around pointers that are referenced in the stack
			(MINIDUMP_TYPE)miniDumpType,
			NULL,
			NULL );

		s_breakpadMinidumpUploader = new CrashReportSender( crashHistoryFilePath );
	}
	
	GetLogsFolder();
	retcode = WinMain_Guarded(hInstance, hi2, cmdline, arg);

#else

	//could use  SetUnhandledExceptionFilter(TopLevelFilter);
	__try
	{
		GetLogsFolder();
		// allocate and commit virtual memory that we can release when errors occur.
		g_reserveMemory = VirtualAlloc(0, 16*1024*2014, MEM_COMMIT, PAGE_NOACCESS);
		retcode = WinMain_Guarded(hInstance, hi2, cmdline, arg);
	}
	__except(MinidumpFilter(_exception_code(), (EXCEPTION_POINTERS*)_exception_info()))
	{
		//it wrote the minidump.
		TerminateProcess(GetCurrentProcess(), 0);
	}
#endif



	// If the game was launched from Media Center, find the window and restore
	// (possibly the game wasn't launched from MCE even though it's running, in
	// that case it's restored anyway)
	HWND hwndMCE = FindWindow(_T("eHome Render Window"), NULL);
	if( hwndMCE )
	{
		ShowWindow(hwndMCE, SW_RESTORE);
	}

	return retcode;
}

#ifndef CCP_DEPLOY

#if _WIN64
	const wchar_t* CORE_BIN_PATH = L"\\..\\..\\carbon\\autobuild\\x64";
#else
	const wchar_t* CORE_BIN_PATH = L"\\..\\..\\carbon\\autobuild\\win32";
#endif

bool NeedToUpdate( std::wstring &binFolder, std::wstring &exeName )
{
	FILETIME coreVersion;
	FILETIME eveVersion;
	HANDLE coreFile;
	HANDLE eveFile;

	// if we fail on opening any of the files, we won't proceed with update
	// so we return false.
	std::wstring exeFileAppPath = binFolder + exeName;
	std::wstring exeFileCorePath = binFolder + exeName.insert( 0, CORE_BIN_PATH );
	eveFile = CreateFileW( exeFileAppPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if (eveFile == INVALID_HANDLE_VALUE)
	{
		CCP_LOGERR( "CreateFile failed with error: %s", FmtError(GetLastError()).c_str());
		return false;
	}
	coreFile = CreateFileW( exeFileCorePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if (coreFile == INVALID_HANDLE_VALUE)
	{
		CCP_LOGERR( "CreateFile failed with error: %s", FmtError(GetLastError()).c_str());
		return false;
	}
	
	if (!GetFileTime(eveFile, NULL, NULL, &eveVersion))
	{
		CCP_LOGERR( "GetFileTime failed with error: %s", FmtError(GetLastError()).c_str());
		return false;	
	}
	if (!GetFileTime(coreFile, NULL, NULL, &coreVersion))
	{
		CCP_LOGERR( "GetFileTime failed with error: %s", FmtError(GetLastError()).c_str());
		return false;	
	}
	
	CloseHandle(eveFile);
	CloseHandle(coreFile);

	return (CompareFileTime(&coreVersion, &eveVersion) == 1) ? true : false;
}
#endif

void PreStartupTest()
{
	if(!IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE))
	{
		MessageBox( NULL, 
					"SSE2 support is needed to run this game.\nClick \'OK\' to continue but the game will probably shut down.", 
					"SSE2 needed",
					MB_ICONEXCLAMATION | MB_OK );
	}
}

int APIENTRY WinMain_Guarded(HINSTANCE hInstance, HINSTANCE, LPSTR cmdline, int)
{
	g_instance = hInstance;
	LogToLogfile(true, "");

	//where are we running?
	g_exeModuleName = GetModuleName();
	DWORD fail = ParseCommandLine();
	if( fail )
	{
		return fail;
	}


	// Pre startup tests
	PreStartupTest();

	BeOS->SetStartupArgs( g_commandLineArguments );

#if BREAKPAD_ENABLED
	// Tell Blue about our crash interface so that it can set options and settings
	BeCrashes = &s_breakpadCrashInterface;
#endif

	BlueInitializePaths();

	for( std::vector<std::wstring>::const_iterator it = g_searchPaths.begin(); it != g_searchPaths.end(); ++it )
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

	BePaths->LogPaths();

	if (g_affinity!=-1)
	{
		int mask = g_affinity;
		HANDLE me = GetCurrentProcess();
		HMODULE kernel32 = LoadLibrary("kernel32");

		typedef WINBASEAPI BOOL WINAPI maskfn(IN HANDLE process, IN DWORD_PTR mask);
		maskfn* proc = (maskfn*)GetProcAddress(kernel32, "SetProcessAffinityMask");
		
		if (me && kernel32 && proc && proc(me, mask))
		{
			CCP_LOG( "Process affinity mask is %d", mask);
		}
		else
		{
			CCP_LOGERR(
				"Cannot set affinity mask, mask=%d, me=%p, kernel32=%p, proc=%p, err=%d:\"%s\"",
				mask, me, kernel32, proc, GetLastError(), FmtError().c_str()
				);
		}

		if (kernel32)
			FreeLibrary(kernel32);
	}

	return g_exeObject.Run();
}


//--------------------------------------------------------------------
// ExeFile constructor
//--------------------------------------------------------------------
ExeFile::ExeFile()
{
	mDumpFlags = MiniDumpNormal;
	mPopupOnErrors = true;
	mMainThreadId = GetCurrentThreadId();
}


//--------------------------------------------------------------------
// ExeFile destructor
//--------------------------------------------------------------------
ExeFile::~ExeFile()
{
	// shouldn't do anything here
}


//--------------------------------------------------------------------
// ExeFile::Run
//--------------------------------------------------------------------
int ExeFile::Run()
{	
	SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);
	if (!BeOS->Startup(13/*IBlueOSType.mVersion*/, g_pyoptimize))
	{
		ShowBlueErr();
		return -1;
	}

	//Minidump behaviour
	mDumpFlags = (MINIDUMP_TYPE)atol(BeOS->KeyVal("dumpflags", "0"));
	mPopupOnErrors = false; // simpler that way

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


//--------------------------------------------------------------------
// ExeFile::OnConsoleCtrl
//--------------------------------------------------------------------
BOOL WINAPI ExeFile::OnConsoleCtrl(DWORD type)
{
	// let's quit no matter the type
	g_quit = true;
	LogToLogfile(false, "User closed the console.");
	PostThreadMessage(g_exeObject.mMainThreadId, WM_QUIT, 0, 0);
	return TRUE;
}


//--------------------------------------------------------------------
// ExeFile::ShowBlueErr
//--------------------------------------------------------------------
void ExeFile::ShowBlueErr()
{
	if (BeOS->GetError(0))
	{
		char* err = NULL;
		BeOS->FormatError(&err);
		BeOS->SetError(BEFLUSH); //output to logger
		BeOS->SetError(BECLEAR); //clear it
	}
}


//--------------------------------------------------------------------
// ExeFile::GetFilename
//--------------------------------------------------------------------
// Creates a filename in the 'logs' folder using current time
// and process ID.
//--------------------------------------------------------------------
std::wstring ExeFile::GetFilename(const wchar_t* name, const wchar_t* ext)
{
	// Create path to logfolder and create the folder just in case
	std::wstring logs = GetLogsFolder();
	CreateDirectoryRec(logs.c_str());

	// Generate filename using current time and process id
	SYSTEMTIME st;
	GetSystemTime(&st);
	wchar_t tmp[100];

#if _MSC_VER >= 1400
	swprintf(tmp, sizeof(tmp)/sizeof(*tmp),
#else
	swprintf(tmp,
#endif
		L"#%s b%d %.4d.%.2d.%.2d %.2d.%.2d.%.2d.%s", 
		name, g_buildno,
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
		ext
	);

	return logs+tmp;
}


//--------------------------------------------------------------------
// ExeFile::WriteMinidump
// Minidump uses exception info, which is only availible when the exception
// is being filtered.  For stack overflow, we are in a pickle since the tight
// stack means we can't run anything sensible.  So we have to start a new
// thread to do the dump.
//--------------------------------------------------------------------


// Dynamically determine at runtime if we want to handle minidumps ourselves
bool MiniDumpEnabled()
{
	if (BeOS)
	{
		g_buildno = BeOS->GetInfo()->mBuildno;
		return g_minidump || BeOS->GetInfo()->mMiniDump;
	}
	return g_minidump;
}

int MinidumpFilter(int code, EXCEPTION_POINTERS *info)
{
	//we are called in the filter, we must not raise exceptions!
	__try
	{
		// Only handle this if we are configured to
		if (!MiniDumpEnabled())
			return EXCEPTION_CONTINUE_SEARCH; // No, let Windows handle this.

		if (g_reserveMemory)
		{
			VirtualFree(g_reserveMemory, 0, MEM_RELEASE);
			g_reserveMemory = 0;
		}
		bool success;
		// Previously we would start a thread here in case we got a STATUS_STACK_OVERFLOW
		// because then it could start afresh with new stack space.  But it turns out
		// that it often deadlocks because the exception may be raised in a context where
		// the main thread has the memory allocation lock, and the just started thread
		// will hang waiting for that lock while the main thread waits for the worker thread.
		// So, no cigar.
		if (code == STATUS_STACK_OVERFLOW)
		{
			//we are in a stack overflow situation, this thread cannot do much.
			CCP_LOGERR( "got stack overflow, minidumping may fail.");
		}
		success = g_exeObject.WriteMinidump(info);
		return success  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) //wow, got an exception in our filter!  Deal with it.
	{
		__try {
			CCP_LOGERR( "exception writing dump file, giving up.");
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			;
		}
		return EXCEPTION_CONTINUE_SEARCH;  //we couldn't handle the exception in the filter
		//Here we could attach a debugger
		//MessageBox();
	}
}


bool ExeFile::WriteMinidump(EXCEPTION_POINTERS *info)
{
	 if (!info)
	 {
		// Generate exception to get proper context in dump
		return CreateException();
	 }

	const std::wstring dumpfile = GetFilename(L"crash", L"dmp");
	CCP_LOGERR( "%s 0x%.8X, minidump being written in %S",
		g_quit?"Quit":"Crashed", info->ExceptionRecord->ExceptionCode, dumpfile.c_str());
	
	// Load dbghelp.dll.  Could link with it perhaps...
	std::wstring dbghelpname = GetBinFolder()+L"DBGHELP.DLL";
	HMODULE dbghelp;
	dbghelp = LoadLibraryW(dbghelpname.c_str());
	if (dbghelp)
	{
		CCP_LOG( "Loaded %S", dbghelpname.c_str());
	}
	else
	{
		CCP_LOGWARN( "Failed to load %S, err=%d:\"%s\"", dbghelpname.c_str(), GetLastError(), FmtError().c_str());
		dbghelp = LoadLibrary("DBGHELP.DLL");
		if (dbghelp)
		{
			CCP_LOG( "Loaded default DBGHELP.DLL");
		}
		else
		{
			CCP_LOGERR( "Cannot create minidump, no DBGHELP.DLL, err=%d:\"%s\"", GetLastError(), FmtError().c_str());
			goto ERR;
		}
	}

	typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(
		HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
		CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
		CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
		CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam
		);
	MINIDUMPWRITEDUMP dump = (MINIDUMPWRITEDUMP)GetProcAddress(dbghelp, "MiniDumpWriteDump" );
	if (dump == NULL)
	{
		// Baah, useless version of dbghelp.dll
		DWORD err = GetLastError();
		FreeLibrary(dbghelp);
		CCP_LOGERR( "Couldn't load MiniDumpWriteDump, err=%d:\"%s\"", err, FmtError(err).c_str());
		goto ERR;
	}

	//Create file for the dump
	const int ATTEMPTS = 5;
	HANDLE file = INVALID_HANDLE_VALUE;
	for (int i = 0; i < ATTEMPTS; i++)
	{
		file = CreateFileW(dumpfile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			break;
		}
	}
	if (file == INVALID_HANDLE_VALUE)
	{
		DWORD err = GetLastError();
		FreeLibrary(dbghelp);
		CCP_LOGERR( "Couldn't create dump file %S, err=%d:\"%s\"", dumpfile.c_str(), err, FmtError(err).c_str());
		goto ERR;
	}

	//create dump info
	MINIDUMP_EXCEPTION_INFORMATION exInfo;
	exInfo.ThreadId = ::GetCurrentThreadId();
	exInfo.ExceptionPointers = info;
	exInfo.ClientPointers = FALSE;

	MINIDUMP_TYPE type = mDumpFlags;
	//See http://www.debuginfo.com/articles/effminidumps.html
	//needs latest debugging tools sdk from windows.
	type = (MINIDUMP_TYPE) (type | MiniDumpWithIndirectlyReferencedMemory);
	if (mHaveConsole)
	{
		type = (MINIDUMP_TYPE)( type | MiniDumpWithHandleData);
	}

	// write the dump
	BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), file,
		type, &exInfo, NULL, NULL);
	FreeLibrary(dbghelp);
	CloseHandle(file);

	if (!ok)
	{
		CCP_LOGERR( "Couldn't write dump file.");
		goto ERR;
	}

	CCP_LOGERR( "MiniDump generated: %S", dumpfile.c_str());
	
	if (mPopupOnErrors)
	{
		char buff[MAX_PATH + 200];
		sprintf_s(
			buff,  
			"MiniDump generated in %S\nDo you want to take a look at this?", dumpfile.c_str()
			);

		int ret = 
			MessageBox(NULL, buff, "I had a minor issue :/", MB_ICONQUESTION | MB_YESNO);

		if (ret == IDYES)
		{
			HINSTANCE h = ShellExecuteW(NULL, NULL, dumpfile.c_str(), NULL, NULL, SW_SHOWNORMAL);
			if ((intptr_t)h<=32)
				ShellExecuteA(NULL, NULL, CW2A(dumpfile.c_str()), NULL, NULL, SW_SHOWNORMAL);
		}
	}

	bool wrote = true;
OK:
	char reason[1024];
	reason[1023]='\0';
	_snprintf_s(reason, _countof(reason), _TRUNCATE, "%s 0x%.8X, minidump %s written in %S", 
		g_quit?"Quit":"Crashed", info->ExceptionRecord->ExceptionCode, wrote?"":"not", dumpfile.c_str());
	LogToLogfile(false, reason);

	return wrote;
ERR:
	wrote = false;
	goto OK;
}

bool ExeFile::CreateException()
{
	// Generate exception to get proper context in dump
	__try
	{
		__try 
		{
			RaiseException(EXCEPTION_BREAKPOINT, 0, 0, NULL);
		} 
		__except(MinidumpFilter(_exception_code(), (EXCEPTION_POINTERS*)_exception_info()) )
		{
			return true;  //successfully wrote minidump
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		;
	}
	return false;
} 


#if _MSC_VER < 1400
int __cdecl _purecall(
		void
		)
{
	// Whatever..
	TerminateProcess(GetCurrentProcess(), 0);
	return -3;
}
#endif

void myInvalidParameterHandler(const wchar_t* expression,
   const wchar_t* function, 
   const wchar_t* file, 
   unsigned int line, 
   uintptr_t pReserved)
{
	CCP_LOGERR( "ExeFile CRT invalid parameter handler: %s:%d in %s",
		file?CW2A(file):"<none>", line, function?CW2A(function):"<none>");
	CCP_LOGERR( "Expression: %s", expression?CW2A(expression):"<none>");
}


//Command line parsing and stuff
std::vector<std::wstring> SplitCommandLine(const wchar_t *line)
{
	int nArgs;
	LPWSTR *argv = CommandLineToArgvW(line, &nArgs);
	std::vector<std::wstring> res;
	for(int i = 0; i<nArgs; i++)
	{
		res.push_back(argv[i]);
	}
	LocalFree(argv);
	return res;
}


bool GetEnvironmentVar(std::wstring *val, const wchar_t *name)
{
	wchar_t f;
	DWORD size = GetEnvironmentVariableW(name, &f, 0);
	if (!size)
	{
		return false; // not set
	}
	if (!val)
	{
		return true; //just test for existance
	}
	std::vector<wchar_t> buf(size);
	DWORD size2 = GetEnvironmentVariableW(name, &buf[0], size);
	if(size2 > 0 && size2+1 <= size)
	{
		*val = &buf[0];
		return true;
	}
	return false;
}


std::wstring GetEnvironmentArgs()
{
	std::wstring result;
	GetEnvironmentVar(&result, L"EXEFILE_ARGS");
	return result;
}

std::wstring trimLeft(const std::wstring& str)
{
	int start = 0;
	for( std::wstring::const_iterator i = str.begin(); i != str.end(); ++i)
	{
		if( *i != L' ' && *i != L'\t' )
			break;
		++start;
	}

	return str.substr(start);
}

void ExpandFileContents( const std::wstring& filename, std::vector<std::wstring>& argv )
{
	std::ifstream is;
	is.open( filename.c_str() );
	if( !is.good() )
	{
		CCP_LOGERR( "File name '%S' provided as startup argument to executable cannot be read", filename.c_str() );
		return;
	}

	while( is.good() )
	{
		std::string a;
		is >> a;

		std::wstring wa = trimLeft( std::wstring( CA2W( a.c_str() ) ) );
		
		if( wa.length() > 0 )
		{
			if( wa[0] == L'@' )
			{
				std::wstring filename = wa.substr(1);
				ExpandFileContents( filename, argv );
			} 
			else 
			{
				argv.push_back( wa );
			}
		}
	}
}


// The silencing of asserts.  _ASSERT is redirected to a handler func
// that outputs the assert and then calls abort()
// retular assert() also alls abort().
// We then modify abort behavior to not output a dialogue box.
// We also set a the report mode for _ASSERT (not used, because the
// report hook gets there first) so that other parts of the application
// can see if we have modified the destination for asserts at all.
// This allows CCP_ASSERT to be silenced too.

static int ReportHook_crash( int reportType, char *message, int *returnValue )
{
	if (reportType == _CRT_ASSERT) {
		fprintf(stderr, "%s", message);
		CCP_LOGERR( "%s", message);
		*(int*)0 = 1; //cause segmentation failure
		abort();
	}
	return FALSE;
}

static int ReportHook_abort( int reportType, char *message, int *returnValue )
{
	if (reportType == _CRT_ASSERT) {
		fprintf(stderr, "%s", message);
		CCP_LOGERR( "%s", message);
		abort();
	}
	return FALSE;
}

static int ReportHook_handle( int reportType, char *message, int *returnValue )
{
	if (reportType == _CRT_ASSERT) {
		fprintf(stderr, "%s", message);
		CCP_LOGERR( "%s", message);
		return TRUE;
	}
	return FALSE;
}

void SilenceAssert(int level)
{
	int (*hook)(int, char*, int*);
	if (level == 0) {
		hook = ReportHook_handle;
	} else if (level == 1) {
		hook = ReportHook_abort;
	} else if (level == 2) {
		hook = ReportHook_crash;
	} else {
		return;
	}
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, hook);
	CcpAssertSetReportHook( hook );

	// Set abort behaviour to REPORTFAULT only (disable the message which typcially
	// is a dialogue box.
	int flags = _CALL_REPORTFAULT;
	_set_abort_behavior(flags, _WRITE_ABORT_MSG |_CALL_REPORTFAULT);
}

DWORD ParseCommandLine()
{
	std::vector<std::wstring> tmpArgv;

	//construct vector of arguments
	tmpArgv = SplitCommandLine(GetCommandLineW());
	if( tmpArgv.size() < 2 )
	{
		//use env var if nothing on the cmd line
		std::wstring envargs = GetEnvironmentArgs();
		if( envargs.size() )
		{
			// don't call SplitCommandLine with an empty envargs since
			// it will cause the executable path to be used instead
			std::vector<std::wstring> tmp = SplitCommandLine(envargs.c_str());
			tmpArgv.insert(tmpArgv.end(), tmp.begin(), tmp.end());
		}
	}
	if( !tmpArgv.size() )
	{
		return GetLastError();
	}

	size_t i;
	for( i = 0; i<tmpArgv.size(); i++)
	{
		if( tmpArgv[i][0] == L'@' )
		{
			std::wstring filename = tmpArgv[i].substr(1);
			ExpandFileContents( filename, g_commandLineArguments );

		}
		else
		{
			g_commandLineArguments.push_back(tmpArgv[i]);
		}
	}

	for(i = 0; i<g_commandLineArguments.size(); i++)
	{
		OutputDebugStringW(g_commandLineArguments[i].c_str());
		OutputDebugStringW(L"\n");
	}

	//Initial value for the console mode
	//Legacy mode: we don't want to mess up all those .start scripts that
	//excpect a new console to pop up
	ConsoleMode showConsole = console_mode_create;

	//Environment var way of turning on our spiffy new _inherit mode.
	//This is used by ExeFile.com
	if (GetEnvironmentVar(0, L"EXEFILE_INHERIT"))
	{
		showConsole = console_mode_inherit;
	}
	
	// Special case: If started witout arguments, it behaves as though eve.exe had
	// started it.  This is to support "pinning to taskbar" on windows 7.
	// Later we want better taskbar support.
	if (g_commandLineArguments.size() < 2)
	{
		showConsole = console_mode_off;
	}

	std::wstring redirect_stderr;
	std::wstring redirect_stdout;

	for(i = 1; i< g_commandLineArguments.size(); i++) {
		const std::wstring &arg = g_commandLineArguments[i];
		const std::wstring larg = ToLower(arg);
		if (larg.find(L"/?")==0 || larg.find(L"help")==0) {
			Usage(true);
		} else if (larg.find(L"/root=")==0) {
			g_rootPath = arg.substr(6);
			std::wstring path = L"root=";
			path += g_rootPath;
			g_searchPaths.push_back( path );
		} else if (larg.find(L"/bin=")==0) {
			g_binPath = arg.substr(5);
			std::wstring path = L"bin=";
			path += g_binPath;
			g_searchPaths.push_back( path );
		} else if (larg.find(L"/lib=")==0) {
			g_libPath = arg.substr(5);
			std::wstring path = L"lib=";
			path += g_libPath;
			g_searchPaths.push_back( path );
		} else if (larg.find(L"/path:")==0 ) {
			// Gather up search paths - push them to BeOS once we've loaded Blue
			std::wstring path = arg.substr(6);
			g_searchPaths.push_back( path );
		} else if (larg == L"/verbose") {
			g_verbose = true;
		} else if (larg == L"/noverbose") {
			g_verbose = false;
		} else if (larg == L"/console") {
			showConsole = console_mode_create;
		} else if (larg == L"/noconsole") {
			showConsole = console_mode_off;
		} else if (larg == L"/inherit") {
			showConsole = console_mode_inherit;
		} else if (larg == L"/jessica") {
			g_jessica = true;
		} else if (larg == L"/minidump") {
			g_minidump = true;
		} else if (larg == L"/nobreakpadupload") {
			g_uploadMinidump = false;
		} else if (larg.find(L"/aflock=")==0) {
			g_affinity = _wtoi(arg.substr(8).c_str());
		} else if (larg.find(L"/pyoptimize=")==0) {
			g_pyoptimize = _wtoi(arg.substr(12).c_str());
		} else if (larg.find(L"/hardkill=") == 0) {
			g_hardkill = _wtoi(arg.substr(10).c_str());
		} else if (larg == L"/hardkill") {
			g_hardkill = 2;
		} else if (larg.find(L"/stderr=") == 0) {
			redirect_stderr = arg.substr(8);
		} else if (larg.find(L"/stdout=") == 0) {
			redirect_stdout = arg.substr(8);
		} else if( arg.find( L"/logDevice=" ) == 0 )
		{
			g_logDeviceName = arg.substr( 11 );
			break;
		} else if ( larg.find( L"/assert=") == 0) {
			int lvl = _wtoi(arg.substr(8).c_str());
			SilenceAssert(lvl);
		} else if( larg.find( L"/cwd=") == 0 ) {
			std::wstring cwd = arg.substr(5);
			SetCurrentDirectoryW( cwd.c_str() );
		}
	}
	//Initialize console and redirect stdoutput
	ShowConsoleWindow(showConsole);
	if( redirect_stderr.size() )
	{
		RedirectOutput(stderr, redirect_stderr.c_str());
	}
	if( redirect_stdout.size() )
	{
		RedirectOutput(stdout, redirect_stdout.c_str());
	}
	
	if( g_verbose )
	{
		fprintf(stdout, "%S starting up\n", g_commandLineArguments.size()>0?g_commandLineArguments[0].c_str() : L"ExeFile.exe");
		fprintf(stdout, "/root=%S\n", g_rootPath.c_str());
		fprintf(stdout, "/hardkill=%d\n", g_hardkill);
		fprintf(stdout, "/bin=%S\n", g_binPath.c_str());
		fprintf(stdout, "/lib=%S\n", g_libPath.c_str());
		fprintf(stdout, "/%sverbose\n", g_verbose?"":"no");
		fprintf(stdout, "/console_mode %d\n", showConsole);
		fprintf(stdout, "/aflock=%d\n", g_affinity);
		fprintf(stdout, "/jessica=%d\n", g_jessica);
		fprintf(stdout, "/pyoptimize=%d\n", g_pyoptimize);
		fprintf(stdout, "/minidump=%s\n", g_minidump?"on":"off");
	}

	return 0;
}

std::wstring ToLower(const std::wstring &s)
{
	std::wstring r=s;
	for (size_t i = 0; i< s.size(); i++) {
		wchar_t c = r[i];
		if (iswupper(c)) {
			wchar_t lc = towlower(c);
			if (iswlower(lc))
				c = lc;
		}
		r[i] = c;
	}
	return r;
}

std::wstring ReplaceKeyword( const std::wstring& in, const std::wstring& keyword, const std::wstring& replacement )
{
	std::wstring::size_type f = ToLower(in).find( keyword );
	std::wstring out;
	if( f != in.npos )
	{
		out = in.substr( 0, f ) + replacement + in.substr( f + keyword.size() );
	}
	else
	{
		out = in;
	}

	return out;
}

std::wstring PathSubst(const std::wstring &s)
{
	std::wstring ret;

	std::wstring paths = s;
	// split the string on ';' - producing individual paths and process each
	while( paths.empty() == false )
	{
		std::wstring p;
		std::wstring::size_type i = paths.find( L";" );
		// chop off - note that i == paths.npos is safe because of the clever design
		// of substr and erase. <halldor>
		p = paths.substr( 0, i );
		paths.erase( 0, i );
		paths.erase( 0, 1 ); // chop off the semicolon

		// p is now the current path to process

		// This is old functionality that we're keeping for backwards compatibility.
		// The new relative path support makes this stuff obsolete. <halldor>
		wchar_t* cwd = _wgetcwd( NULL, 0 );
		p = ReplaceKeyword( p, L"cwd:", cwd );
		free( cwd );
		p = ReplaceKeyword( p, L"root:", GetRootFolder() );
		// End of old oboslete-but-kept-for-backwards-compatibility functionality.

		// Let's normalize the path!		
		wchar_t absPath[ _MAX_PATH ];
		if( _wfullpath( absPath, p.c_str(), _MAX_PATH ) )
		{
			p = absPath;
		}

		// Finally stitch it onto our list of paths:
		if( ret.empty() == false )
		{
			ret += L";";
		}

		ret += p;
	}

	return ret;
}

void Usage(bool ok)
{
	fprintf(stderr, "Usage: exefile [flag ...]\n");
	fprintf(stderr, "flags: /?, /help: this text\n");
	fprintf(stderr, "       /root=<root path>\n");
	fprintf(stderr, "       /bin=<bin path>\n");
	fprintf(stderr, "       /lib=<bin path>\n");
	fprintf(stderr, "       /verbose , /noverbose\n");
	fprintf(stderr, "       /console , /noconsole, /inherit\n");
	fprintf(stderr, "       /aflock=<aff>\n");
	fprintf(stderr, "       /jessica\n");
	fprintf(stderr, "       /pyoptimize=<opt>\n");
	fprintf(stderr, "       /assert=<lvl> (3=dialogue box (default),  2=crash, 1=exit, 0=ignore)\n");
	fprintf(stderr, "       /stderr=<path with %p as pid>\n");
	fprintf(stderr, "       /stdout=<path with %p as pid>\n");
	fprintf(stderr, "       /noBreakpadUpload\n");
	fprintf(stderr, "       paths can start with $(cwd)\n");
	exit(ok?0:-1);
}

std::wstring GetModuleName()
{
	static std::wstring module;
	if( module.empty() )
	{
		//where are we running?
		std::vector<wchar_t> buf(MAX_PATH);
		while (GetModuleFileNameW(0, &buf[0], (int)buf.size()) == buf.size())
		{
			buf.resize(buf.size()*2);
		}

		module = &buf[0];
		if( module.empty() )
		{
			char tmp[MAX_PATH];
			GetModuleFileNameA(0, tmp, sizeof(tmp)); //win98 friendly
			module = CA2W(tmp);
		}
	}
	return module;
}

std::wstring GetRootFolder()
{
	std::wstring folder = BePaths->ResolvePathW( L"root:/" );

	if( folder.empty() )
	{
		//default root is directory above us
		folder = GetModuleName();
		folder = folder.substr(0, folder.rfind(L"\\"));
		folder = folder.substr(0, folder.rfind(L"\\"));
		folder += L"\\";
	}
	return folder;
}

std::wstring GetBinFolder()
{
	std::wstring folder = BePaths->ResolvePathW( L"bin:/" );
	if( folder.empty() )
	{
		//default is where we are located
		folder = GetModuleName().substr(0, GetModuleName().rfind(L"\\")) + L"\\";
	}
	return folder;
}

std::wstring GetResFolder()
{
	std::wstring folder = BePaths->ResolvePathW( L"res:/" );

	if( folder.empty() )
	{
		folder = GetRootFolder() + L"res\\";
	}
	return folder;
}
	
const std::wstring &GetLogsFolder()
{
	//don't rely on g_beOS for the logs folder.  We may be calling here before
	//g_beOS has decided if it is running in LUA mode or not.  Just use the
	//application private folder
	//have the string static, and allocated early, to make errorhandling safer
	static std::wstring result;
	if( !result.size() )
	{
		wchar_t path[MAX_PATH];
		HRESULT hr = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
		if( SUCCEEDED(hr) )
		{
			result = path;
			result += L"\\CCP\\EVE\\";
			CreateDirectoryRec(result.c_str());
		}
	}
	return result;
}

bool CreateDirectoryRec(const wchar_t *dir)
{
	if( CreateDirectoryW(dir, NULL) )
	{
		return true;
	}
	DWORD err = GetLastError();
	if (err == ERROR_ALREADY_EXISTS)
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
		return !!CreateDirectoryW(dir, NULL);
	}
	return false;
}


//Note, we might want to change this to produce always english
//messages...
std::string FmtError(DWORD err)
{
	DWORD olderr = GetLastError();
	if (!err)
		err = olderr;

	LPVOID lpMsgBuf;
	DWORD ok = FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		err,
		MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL );
	
	if (!ok && GetLastError() == ERROR_RESOURCE_LANG_NOT_FOUND)
	{
		//try again using default language
		ok = FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		err,
		0,
		(LPTSTR) &lpMsgBuf,
		0,
		NULL );
	}

	if (!ok)
	{
		SetLastError(olderr);
		return "";
	}

	std::string r((LPTSTR)lpMsgBuf);  
	LocalFree( lpMsgBuf );
	SetLastError(olderr);
	return r;
}
