#if !_DEBUG


class AnnotationData;
namespace crashpad
{
	class Settings;
	class CrashReportDatabase;
	class CrashpadClient;
}

// This interface class is given to Blue to allow it to configure some crashpad settings
struct CrashpadCrashInterface : public ICrashReporter
{
	bool InitializeCrashpad();

	// Sets key/value pairs (used in crashpad http post parameters)
	void SetCrashKeyValue( const char* key, const char* val ) override;

	// Turn the crashpad upload on or off
	void EnableCrashReporting( bool enable ) override;

	bool IsCrashReportingEnabled() override;

	// Only exists on windows
	void ProduceImmediateDump() override;

private:
	std::map<std::string, std::unique_ptr<AnnotationData>> m_annotations;
	crashpad::Settings* m_settings = nullptr;
	std::unique_ptr<crashpad::CrashReportDatabase> m_database;
	std::unique_ptr<crashpad::CrashpadClient> m_client;
	bool m_enabled = true;
};

CrashpadCrashInterface* GetCrashReporter();

#endif
