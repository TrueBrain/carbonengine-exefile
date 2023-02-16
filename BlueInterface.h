#include <BlueExposure.h>
#include <Blue.h>
#include <IBlueOS.h>
#include <IBluePaths.h>


class BlueInterface
{
public:
	~BlueInterface();

	bool LoadBlue( const std::wstring& buildFlavor );

	IBlueOS* GetBeOS() const;
	IBluePaths* GetBluePaths() const;
	void SetCrashReporter( ICrashReporter* crashReporter ) const;
	void LogFuncChannel( CcpLogChannel_t& logObject, CCP::LogType type, unsigned long userData, const char* format, ... ) const;
	void ModuleStartup() const;
	void InitializeSocketLogger() const;
	bool InitializeResourceLoading() const;
	bool InitializePaths( const std::wstring& initialPath ) const;

	using BlueGetBeOSRoutine = IBlueOS*( __cdecl* )();
	using BlueGetBluePathsRoutine = IBluePaths*(__cdecl*)();
	using BlueSetCrashReporterRoutine = void( __cdecl* )( ICrashReporter* );
	using BlueLogFuncChannelRoutine = void( __cdecl* )( CcpLogChannel_t&, CCP::LogType, unsigned long, const char*, va_list );
	using BlueModuleStartupRoutine = void( __cdecl* )();
	using BlueInitializeSocketLoggerRoutine = void( __cdecl* )();
	using BlueInitializeResourceLoadingRoutine = bool( __cdecl* )();
	using BlueInitializePathsRoutine = bool( __cdecl* )( const std::wstring& );

private:
	BlueGetBeOSRoutine m_blueGetBeOSRoutine = nullptr;
	BlueGetBluePathsRoutine m_blueGetBluePathsRoutine = nullptr;
	BlueSetCrashReporterRoutine m_blueSetCrashReporterRoutine = nullptr;
	BlueLogFuncChannelRoutine m_blueLogFuncChannelRoutine = nullptr;
	BlueModuleStartupRoutine m_blueModuleStartupRoutine = nullptr;
	BlueInitializeSocketLoggerRoutine m_blueInitializeSocketLoggerRoutine = nullptr;
	BlueInitializeResourceLoadingRoutine m_blueInitializeResourceLoadingRoutine = nullptr;
	BlueInitializePathsRoutine m_blueInitializePathsRoutine = nullptr;

	void *m_module = nullptr;
};
