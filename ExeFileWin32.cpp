#include "StdAfx.h"
#ifdef _WIN32
#include "ExeFile.h"
#include "CcpCore/include/CcpCrash.h"
#include <shlobj.h> //for SHGetFolderPath
#include <fcntl.h>

#if BREAKPAD_ENABLED

#include "client/windows/handler/exception_handler.h"
#include "client/windows/sender/crash_report_sender.h"
#include <WinInet.h>



extern CommandArguments g_commandArguments;

// The build number used for minidumps
unsigned int g_buildno = 999999;

FILE* g_sessionFile = nullptr;
unsigned int g_userId = 0;
int64_t g_sessionId = 0;
bool g_quit = false;


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
		g_commandArguments.uploadMinidump = enable;
	}

	virtual bool IsCrashReportingEnabled()
	{
		return g_commandArguments.uploadMinidump;
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
ICrashReporter* g_crashReporter = &s_breakpadCrashInterface;


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
	int sentryResult = -1;

	if( s_breakpadMinidumpUploader && g_commandArguments.uploadMinidump )
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
			CCP_LOGWARN( "Upload Crash Dump for b%d, %S, RESULT_SUCCEEDED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_THROTTLED:
			CCP_LOGWARN( "Upload Crash Dump for b%d, %S, RESULT_THROTTLED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		}

		if (s_breakpadMinidumpUploadHeaders.count(L"sentry") > 0) { // Inject minidump ID into sentry JSON
			std::wstring sentryJson = s_breakpadMinidumpUploadHeaders[L"sentry"];
			s_breakpadMinidumpUploadHeaders[L"sentry"] = sentryJson.replace(sentryJson.find(L"UUIDPLACEHOLDER"), 15, minidump_id); 
		} else {
			std::wstring manualJson;
			manualJson = manualJson + L"{\"tags\":{\"minidump_id\":\"" + minidump_id + L"\"}}";
			s_breakpadMinidumpUploadHeaders[L"sentry"] = manualJson;
		}

		CCP_LOGWARN ( "%S", s_breakpadMinidumpUploadHeaders[L"sentry"].c_str() );

		ReportResult sentryRes = s_breakpadMinidumpUploader->SendCrashReport(L"https://sentry.io/api/1434648/minidump/?sentry_key=0b0785270cff40ab88073f3429a81eb4", s_breakpadMinidumpUploadHeaders, fullFilePath, &s_breakpadCrashUploaderResult );
		sentryResult = (int)sentryRes;
		switch( sentryRes )
		{
		case RESULT_FAILED:
			CCP_LOGERR( "Upload Sentry minidump for b%d, %S, RESULT_FAILED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_REJECTED:
			CCP_LOGERR( "Upload Sentry minidump for b%d, %S, RESULT_REJECTED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_SUCCEEDED:
			// Actually a LOG_NOTICE
			CCP_LOGWARN( "Upload Sentry minidump for b%d, %S, RESULT_SUCCEEDED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
			break;
		case RESULT_THROTTLED:
			CCP_LOGWARN( "Upload Sentry minidump for b%d, %S, RESULT_THROTTLED: %S", g_buildno, fullFilePath, s_breakpadCrashUploaderResult.c_str());
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

			CCP_LOG( "Writing to session file (%d, %I64d, %I64d, %d, %S, %d %d)", g_userId, g_sessionId, timeStamp, g_buildno, minidump_id, resultCode, sentryResult);

			fprintf( g_sessionFile, "- crashed\n- %d\n- %I64d\n- %I64d\n- %d\n- %S\n- %d\n - %d\n", g_userId, g_sessionId, timeStamp, g_buildno, minidump_id, resultCode, sentryResult);
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

namespace
{

FILE *logfile = 0;
DWORD g_mainThreadId;
bool g_quit = false;

	
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

void OpenLogFile()
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

//Note, we might want to change this to produce always english
//messages...
std::string FmtError(DWORD err = 0)
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

void myInvalidParameterHandler(const wchar_t* expression,
   const wchar_t* function, 
   const wchar_t* file, 
   unsigned int line, 
   uintptr_t pReserved)
{
	CCP_LOGERR( "ExeFile CRT invalid parameter handler: %s:%d in %s",
		file?(const char*)CW2A(file):"<none>", line, function?(const char*)CW2A(function):"<none>");
	CCP_LOGERR( "Expression: %s", expression?(const char*)CW2A(expression):"<none>");
}

void __cdecl PureCallHandler()
{
    CcpCrashOnPurpose();
	// Crash the process instead of showing the default MS "pure virtual call" dialog
	volatile int* crashPointer = nullptr;
	*crashPointer = 42;
}

BOOL WINAPI OnConsoleCtrl(DWORD type)
{
	// let's quit no matter the type
	g_quit = true;
	LogToLogfile(false, "User closed the console.");
	PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
	return TRUE;
}

}

void LogToLogfile(bool startup, const char* reason)
{
	const char *prefix = startup?"STARTUP":"SHUTDOWN";
	CCP_LOGERR( "Exefile %s %s", prefix, reason );
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
	}
}

void PreStartupTest()
{
	SetConsoleCtrlHandler( OnConsoleCtrl, TRUE );

	if(!IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE))
	{
		MessageBox( NULL, 
					"SSE2 support is needed to run this game.\nClick \'OK\' to continue but the game will probably shut down.", 
					"SSE2 needed",
					MB_ICONEXCLAMATION | MB_OK );
	}
}

void SetProcessAffinity( int affinity )
{
	int mask = affinity;
	HANDLE me = GetCurrentProcess();
	HMODULE kernel32 = LoadLibrary( "kernel32" );

	typedef WINBASEAPI BOOL WINAPI maskfn( IN HANDLE process, IN DWORD_PTR mask );
	maskfn* proc = (maskfn*)GetProcAddress( kernel32, "SetProcessAffinityMask" );
		
	if( me && kernel32 && proc && proc( me, mask ) )
	{
		CCP_LOG( "Process affinity mask is %d", mask );
	}
	else
	{
		CCP_LOGERR(
			"Cannot set affinity mask, mask=%d, me=%p, kernel32=%p, proc=%p, err=%d:\"%s\"",
			mask, me, kernel32, proc, GetLastError(), FmtError().c_str()
			);
	}

	if( kernel32 )
	{
		FreeLibrary( kernel32 );
	}
}

void SetWorkingDirectory( const wchar_t* directory )
{
	SetCurrentDirectoryW( directory );
}



int APIENTRY WinMain( HINSTANCE, HINSTANCE, LPSTR, int )
{
	//NOTE! This is only local to Exefile as long as we don't use DLL RCT
	_set_invalid_parameter_handler(myInvalidParameterHandler);

	int retcode = 0;

	_set_purecall_handler( &PureCallHandler );

	g_mainThreadId = GetCurrentThreadId();

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
#endif
	
	retcode = Main();


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





// The silencing of asserts.  _ASSERT is redirected to a handler func
// that outputs the assert and then calls abort()
// retular assert() also alls abort().
// We then modify abort behavior to not output a dialogue box.
// We also set a the report mode for _ASSERT (not used, because the
// report hook gets there first) so that other parts of the application
// can see if we have modified the destination for asserts at all.
// This allows CCP_ASSERT to be silenced too.

static int ReportHook_exit( int reportType, char *message, int *returnValue )
{
	if (reportType == _CRT_ASSERT) {
		fprintf(stderr, "%s", message);
		CCP_LOGERR( "%s", message);
		BeOS->Terminate(1);
	}
	return FALSE;
}

static int ReportHook_crash( int reportType, char *message, int *returnValue )
{
	if (reportType == _CRT_ASSERT) {
		fprintf(stderr, "%s", message);
		CCP_LOGERR( "%s", message);
        CcpCrashOnPurpose();
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

CCPAssertResult CcpReportHookExit( int severity, const char* message )
{
	int returnValue = 0;
	ReportHook_exit( _CRT_ASSERT, const_cast<char*>( message ), &returnValue );
	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookCrash( int severity, const char* message )
{
	int returnValue = 0;
	ReportHook_crash( _CRT_ASSERT, const_cast<char*>( message ), &returnValue );
	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookAbort( int severity, const char* message )
{
	int returnValue = 0;
	ReportHook_abort( _CRT_ASSERT, const_cast<char*>( message ), &returnValue );
	return CCP_ASSERT_RESULT_NONE;
}

CCPAssertResult CcpReportHookHandle( int severity, const char* message )
{
	int returnValue = 0;
	ReportHook_handle( _CRT_ASSERT, const_cast<char*>( message ), &returnValue );
	return CCP_ASSERT_RESULT_NONE;
}

void SilenceAssert(int level)
{
	int (*hook)(int, char*, int*);
	CcpAssertHook ccpHook;
	if (level == 0) {
		hook = ReportHook_handle;
		ccpHook = CcpReportHookHandle;
	} else if (level == 1) {
		hook = ReportHook_abort;
		ccpHook = CcpReportHookAbort;
	} else if (level == 2) {
		hook = ReportHook_crash;
		ccpHook = CcpReportHookCrash;
	} else if (level == 3) {
		hook = ReportHook_exit;
		ccpHook = CcpReportHookExit;
	} else {
		return;
	}
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, hook);
	CcpAssertSetReportHook( ccpHook );

	// Set abort behaviour to REPORTFAULT only (disable the message which typcially
	// is a dialogue box.
	int flags = _CALL_REPORTFAULT;
	_set_abort_behavior(flags, _WRITE_ABORT_MSG |_CALL_REPORTFAULT);
}

#endif
