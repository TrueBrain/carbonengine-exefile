#include "StdAfx.h"

#if CRASH_REPORTS_ENABLED

#include "Crashpad.h"
#include "CommandArguments.h"

#include "client/annotation.h"
#include "client/crashpad_client.h"
#include "client/crash_report_database.h"
#include "client/settings.h"

crashpad::Settings* s_settings;
std::unique_ptr<crashpad::CrashReportDatabase> s_database;

#if _WIN32

#include <Shlobj.h>

std::wstring GetAppdataFolder()
{
	wchar_t path[MAX_PATH];
	return SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path ) ) ? path : L"";
}

auto executablePath = "eve_crashmon.exe";

#elif __APPLE__

auto executablePath = "eve_crashmon";

#endif

bool InitializeCrashpad()
{
	auto appdata = GetAppdataFolder();
	if( appdata.empty() )
	{
		return false;
	}

	fs::path logsFolder;
	try
	{
		logsFolder = fs::path( appdata ).append( "CCP" ).append( "EVE" ).append( "Crashes" );
		fs::create_directories( logsFolder );
	}
	catch( const fs::filesystem_error& e)
	{
		fprintf( stderr, "Filesystem error when initializing crashpad path: %s", e.what() );
		return false;
	}
	catch (const std::exception& e)
	{
		fprintf( stderr, "Unexpected exception during crashpad path initialization: %s", e.what() );
	}
	catch (...)
	{
		fprintf( stderr, "Unhandled exception when initializing crashpad path" );
		return false;
	}

	base::FilePath handler( fs::path( CcpExecutablePath() ).parent_path().append( executablePath ) );
	base::FilePath reportsDir( logsFolder );
	base::FilePath metricsDir( reportsDir );
	std::string url = "https://sentry.io/api/1434648/minidump/?sentry_key=0b0785270cff40ab88073f3429a81eb4";
	std::map<std::string, std::string> annotations;
	std::vector<std::string> arguments = { "--no-rate-limit" };

	s_database = crashpad::CrashReportDatabase::Initialize( reportsDir );
	if( !s_database )
	{
		return false;
	}

	// Enable automated crash uploads
	s_settings = s_database->GetSettings();
	if( !s_settings )
	{
		return false;
	}
	s_settings->SetUploadsEnabled( true );

	return crashpad::CrashpadClient().StartHandler( handler, reportsDir, metricsDir, url, annotations, arguments, true, false );
}

// Data for an annotation isn't allowed to move around after created, so we use this wrapper class
class AnnotationData
{
public:
	AnnotationData( const wchar_t* name, const wchar_t* value )
	{
		memset( m_name, 0, sizeof( m_name ) );
		memset( m_value, 0, sizeof( m_value ) );
		
		std::string n( name, name + wcslen( name ) );
		strncpy_s( m_name, sizeof( m_name ), n.c_str(), n.length() );
		
		m_annotation = std::make_unique<crashpad::Annotation>( crashpad::Annotation::Type::kString, m_name, m_value );
		SetValue( value );
	}
	
	AnnotationData( const AnnotationData& ) = delete;
	AnnotationData& operator=( const AnnotationData& ) = delete;
	
	~AnnotationData()
	{
		m_annotation->Clear();
	}
	
	void SetValue( const wchar_t* value )
	{
		memset( m_value, 0, strlen( m_value ) );
		
		std::string v( value, value + wcslen( value ) );
		strncpy_s( m_value, sizeof( m_value ), v.c_str(), v.length() );
		
		m_annotation->SetSize( static_cast<crashpad::Annotation::ValueSizeType>( v.length() ) );
	}
	
private:
	char m_name[crashpad::Annotation::kNameMaxLength];
	char m_value[crashpad::Annotation::kValueMaxSize];
	std::unique_ptr<crashpad::Annotation> m_annotation;
};

// This interface class is given to Blue to allow it to configure some crashpad settings
struct BreakpadCrashInterface: public ICrashReporter
{
	// Sets key/value pairs (used in crashpad http post parameters)
	void SetCrashKeyValueW( const wchar_t* key, const wchar_t* val ) override
	{
		if( !s_settings )
		{
			return;
		}

		if( auto found = m_annotations.find( key ); found != m_annotations.end() )
		{
			found->second->SetValue( val );
		}
		else
		{
			m_annotations.emplace( key, std::make_unique<AnnotationData>( key, val ) );
		}
	}

	// Turn the breakpad upload on or off
	void EnableCrashReporting( bool enable ) override
	{
		g_commandArguments.uploadMinidump = enable;
		if( s_settings )
		{
			s_settings->SetUploadsEnabled( enable );
		}
	}

	bool IsCrashReportingEnabled() override
	{
		return g_commandArguments.uploadMinidump;
	}

	void SetBuildNumber( unsigned b ) override
	{
	}

	void SetUserId( int id ) override
	{
	}

	void SetSessionId( int64_t id ) override
	{
	}
	
	void SetSessionFileDescriptor( int fd ) override
	{
	}

	void ProduceImmediateDump() override
	{
	}

private:
	std::map<std::wstring, std::unique_ptr<AnnotationData>> m_annotations;
};

static BreakpadCrashInterface s_breakpadCrashInterface;
ICrashReporter* g_crashReporter = &s_breakpadCrashInterface;

#endif