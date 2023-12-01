#include "StdAfx.h"
#include "BlueInterface.h"

#if __APPLE__
#include <dlfcn.h>
#endif


BlueInterface::~BlueInterface()
{
	if( m_module )
	{
#if __APPLE__
		dlclose( m_module );
#elif _WIN32
		FreeLibrary( static_cast<HMODULE>( m_module ) );
#else
#error Unsupported platform
#endif
		// Make invalid access obvious
		memset( this, 0, sizeof( *this ) );
	}
}

bool BlueInterface::LoadBlue( const std::wstring& buildFlavor )
{
	if( m_module )
	{
		return true;
	}

	std::wstring name = L"blue" + ( buildFlavor.empty() ? L"" : L"_" + buildFlavor );

#if __APPLE__
	std::string str = std::string( std::begin( name ), std::end( name ) ) + ".so";
	m_module = dlopen( str.c_str(), RTLD_LAZY );
#elif _WIN32
	m_module = LoadLibraryW( name.c_str() );
#else
#error Unsupported platform
#endif

	if( !m_module )
	{
		return false;
	}

#if __APPLE__
#define LoadBlueRoutine( name ) if( !( m_blue##name##Routine = reinterpret_cast<Blue##name##Routine>( dlsym( m_module, CCP_STRINGIZE( Blue##name ) ) ) ) ) return false
#elif _WIN32
#define LoadBlueRoutine( name ) if( !( m_blue##name##Routine = reinterpret_cast<Blue##name##Routine>( GetProcAddress( static_cast<HMODULE>( m_module ), CCP_STRINGIZE( Blue##name ) ) ) ) ) return false
#else
#error Unsupported platform
#endif
	LoadBlueRoutine( GetBeOS );
	LoadBlueRoutine( GetBluePaths );
	LoadBlueRoutine( SetCrashReporter );
	LoadBlueRoutine( LogFuncChannel );
	LoadBlueRoutine( ModuleStartup );
	LoadBlueRoutine( InitializeSocketLogger );
	LoadBlueRoutine( InitializeResourceLoading );
	LoadBlueRoutine( InitializePaths );
	LoadBlueRoutine( ShowMessageBox );
#undef LoadBlueRoutine

	return true;
}

IBlueOS* BlueInterface::GetBeOS() const
{
	return m_blueGetBeOSRoutine();
}

IBluePaths* BlueInterface::GetBluePaths() const
{
	return m_blueGetBluePathsRoutine();
}

void BlueInterface::SetCrashReporter( ICrashReporter* crashReporter ) const
{
	m_blueSetCrashReporterRoutine( crashReporter );
}

void BlueInterface::LogFuncChannel( CcpLogChannel_t& logObject, CCP::LogType type, unsigned long userData, const char* format, ... ) const
{
	va_list args;
	va_start( args, format );
	m_blueLogFuncChannelRoutine( logObject, type, userData, format, args );
	va_end( args );
}

void BlueInterface::ModuleStartup() const
{
	m_blueModuleStartupRoutine();
}

void BlueInterface::InitializeSocketLogger() const
{
	m_blueInitializeSocketLoggerRoutine();
}

bool BlueInterface::InitializeResourceLoading() const
{
	return m_blueInitializeResourceLoadingRoutine();
}

bool BlueInterface::InitializePaths( const std::wstring& initialPath ) const
{
	return m_blueInitializePathsRoutine( initialPath );
}

void BlueInterface::ShowMessageBox( const BlueErrorMessage& windowTitle, const BlueErrorMessage& message ) const
{
	m_blueShowMessageBoxRoutine( windowTitle, message );
}
