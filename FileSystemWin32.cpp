// Copyright © 2026 CCP ehf.
#include "StdAfx.h"
#if _WIN32

#include "FileSystem.h"
#include <shlobj.h> //for SHGetFolderPath

std::wstring GetAppdataFolder()
{
	wchar_t path[MAX_PATH];
	return SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path ) ) ? path : L"";
}

bool CreateDirectoryRec(const wchar_t* dir)
{
    if( CreateDirectoryW(dir, NULL) )
    {
        return true;
    }
    DWORD err = GetLastError();
    if( err == ERROR_ALREADY_EXISTS )
    {
        return true;
    }
    //find the last separator
    std::wstring full(dir);
    std::wstring::size_type s = full.find_last_of(L"/\\");
    if( s != std::wstring::npos )
    {
        std::wstring pre = full.substr(0, s);
        if( !CreateDirectoryRec(pre.c_str()) )
        {
            return false;
        }
        return !!CreateDirectoryW(dir, NULL);
    }
    return false;
}

const std::wstring& GetLogsFolder()
{
    static std::wstring result;
    if( !result.size() )
    {
        wchar_t path[MAX_PATH];
        HRESULT hr = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
        if( SUCCEEDED(hr) )
        {
            result = std::wstring(path) + L"\\CCP\\EVE\\";
            CreateDirectoryRec(result.c_str());
        }
    }
    return result;
}
#endif