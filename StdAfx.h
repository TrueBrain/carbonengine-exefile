// stdafx.h : include file for standard system include files,
//      or project specific include files that are used frequently,
//      but are changed infrequently
//#define STRICT
//#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#ifdef _WIN32

#include <windows.h>

#ifdef max
#undef max
#endif

#endif

#include "BlueExposure/include/BlueExposure.h"
#include <blue/Include/Blue.h>
#include <blue/Include/IBlueOS.h>

#ifdef __APPLE__
#ifdef toupper
#undef toupper
#endif
#ifdef tolower
#undef tolower
#endif
#ifdef isalnum
#undef isalnum
#endif
#ifdef isalpha
#undef isalpha
#endif
#ifdef islower
#undef islower
#endif
#ifdef isspace
#undef isspace
#endif
#ifdef isupper
#undef isupper
#endif
#endif