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
	bool InitializePaths( const std::wstring& initialPath ) const;
	void ShutdownSocketLogger() const;
	void InstallPythonMemoryHooks() const;
	PyObject* LoadPythonExtension( const char* name ) const;
	void ShowInvalidOSVersionError() const;

	using BlueGetBeOSRoutine = IBlueOS*( __cdecl* )();
	using BlueGetBluePathsRoutine = IBluePaths*(__cdecl*)();
	using BlueSetCrashReporterRoutine = void( __cdecl* )( ICrashReporter* );
	using BlueLogFuncChannelRoutine = void( __cdecl* )( CcpLogChannel_t&, CCP::LogType, unsigned long, const char*, va_list );
	using BlueModuleStartupRoutine = void( __cdecl* )();
	using BlueInitializeSocketLoggerRoutine = void( __cdecl* )();
	using BlueInitializePathsRoutine = bool( __cdecl* )( const std::wstring& );
	using BlueShutdownSocketLoggerRoutine = void( __cdecl* )();
	using BlueInstallPythonMemoryHooksRoutine = void(__cdecl* )();
	using BlueLoadPythonExtensionRoutine = PyObject*(__cdecl*) (const char*);
	using BlueShowInvalidOSVersionErrorRoutine = void( __cdecl* )();

private:
	BlueGetBeOSRoutine m_blueGetBeOSRoutine = nullptr;
	BlueGetBluePathsRoutine m_blueGetBluePathsRoutine = nullptr;
	BlueSetCrashReporterRoutine m_blueSetCrashReporterRoutine = nullptr;
	BlueLogFuncChannelRoutine m_blueLogFuncChannelRoutine = nullptr;
	BlueModuleStartupRoutine m_blueModuleStartupRoutine = nullptr;
	BlueInitializeSocketLoggerRoutine m_blueInitializeSocketLoggerRoutine = nullptr;
	BlueInitializePathsRoutine m_blueInitializePathsRoutine = nullptr;
	BlueShutdownSocketLoggerRoutine m_blueShutdownSocketLoggerRoutine = nullptr;
	BlueInstallPythonMemoryHooksRoutine m_blueInstallPythonMemoryHooksRoutine = nullptr;
	BlueLoadPythonExtensionRoutine m_blueLoadPythonExtensionRoutine = nullptr;
	BlueShowInvalidOSVersionErrorRoutine m_blueShowInvalidOSVersionErrorRoutine = nullptr;

	void *m_module = nullptr;
};
