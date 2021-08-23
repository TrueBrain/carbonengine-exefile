#include "windows.h"

#define EVEFILEDESC "CCP ExeFile\0"
#if CCP_DEPLOY
#define EVEINTFILENAME "ExeFile_deploy\0"
#define EVEFILENAME "ExeFile_deploy.exe\0"
#elif defined(NDEBUG)
#define EVEINTFILENAME "ExeFile\0"
#define EVEFILENAME "ExeFile.exe\0"
#else
#define EVEINTFILENAME "ExeFileD\0"
#define EVEFILENAME "ExeFileD.exe\0"
#endif
#define EVEFILETYPE VFT_APP

#include "autoversion.h"
