#pragma once
#ifndef CommandArguments_H
#define CommandArguments_H

enum ConsoleMode 
{
	console_mode_off,		//deprecated: Neither attach nor create.
	console_mode_inherit,   //attach to whatever you have access to
	console_mode_create,	//create a new console.
};

struct CommandArguments
{
	// Search paths - add an entry with /path:key=value
	std::vector<std::wstring> searchPaths;
	ConsoleMode consoleMode; // console_mode_create
	bool uploadMinidump; // = true
	int affinity; // = -1
	int pyOptimize;// = 1 or = -1 for _DEBUG
	std::wstring redirectStdErr;
	std::wstring redirectStdOut;
	int assertLevel;
	bool asService;

	CommandArguments()
		:consoleMode( console_mode_create ),
		uploadMinidump( true ),
		affinity( -1 ),
#ifdef _DEBUG
		pyOptimize( -1 ),
#else
		pyOptimize( 1 ),
#endif
		assertLevel( -1 ),
		asService( false )
	{
	}
};

typedef std::vector<std::wstring> CommandLine;

void GetCommandLine( CommandLine& commandLine );
void GetCommandLine( const char** argv, int argc, CommandLine& commandLine );

void DumpCommandLineToDebugger( const CommandLine& commandLine );
void ParseCommandLine( const CommandLine& commandLine, CommandArguments& commandArguments );

#endif