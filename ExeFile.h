// For MiniDump support
//#define __out_ecount(LEN) __out
#include <dbghelp.h>


class ExeFile
{
public:

	ExeFile();
	~ExeFile();

	int Run();
	
	std::wstring GetFilename(const wchar_t* name, const wchar_t* ext);
	DWORD mMainThreadId;

	void ShowBlueErr();

	static BOOL WINAPI OnConsoleCtrl(DWORD type);
	
	MINIDUMP_TYPE mDumpFlags;
	bool mPopupOnErrors;
	bool mHaveConsole;
	
	bool WriteMinidump(EXCEPTION_POINTERS *info);
	bool CreateException();
};

int MinidumpFilter(int code, EXCEPTION_POINTERS *info);