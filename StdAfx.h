// stdafx.h : include file for standard system include files,
//      or project specific include files that are used frequently,
//      but are changed infrequently
#define STRICT
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers


// comment this out if you want python
//#define NOPYTHON

#if (_MSC_VER < 1400 && !_DLL)
#define NOSTDEXCEPT
#endif

#ifdef NOSTDEXCEPT
// Not using c++ exceptions
#define _HAS_EXCEPTIONS 0
#if _MSC_VER < 1400
#include <exception>
using std::exception;
#endif
#endif

#include <windows.h>
#include "BlueExposure/include/BlueExposure.h"
#include <blue/include/Blue.h>
#include <blue/include/IBlueOS.h>
