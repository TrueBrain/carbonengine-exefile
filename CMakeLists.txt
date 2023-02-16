cmake_minimum_required(VERSION 3.16)

project(Exefile VERSION 1.0.0)

if(NOT DEFINED ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "Missing required environment variable CCP_EVE_PERFORCE_BRANCH_PATH")
elseif(NOT IS_ABSOLUTE $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH needs to be an absolute path")
elseif(NOT EXISTS $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH is pointing at a non-existing location $ENV{CCP_EVE_PERFORCE_BRANCH_PATH}")
elseif(NOT IS_DIRECTORY $ENV{CCP_EVE_PERFORCE_BRANCH_PATH})
    message(FATAL_ERROR "CCP_EVE_PERFORCE_BRANCH_PATH needs to be a directory")
else()
    message(STATUS "Using CCP_EVE_PERFORCE_BRANCH_PATH located at $ENV{CCP_EVE_PERFORCE_BRANCH_PATH}")
endif()

file(TO_CMAKE_PATH "$ENV{CCP_EVE_PERFORCE_BRANCH_PATH}" BRANCH_ROOT_DIR)
list(INSERT CMAKE_MODULE_PATH 0 ${BRANCH_ROOT_DIR}/cmake)

include(CcpGlobalSettings)


find_package(Crashpad CONFIG REQUIRED NO_CMAKE_PATH)
find_package(Blue CONFIG REQUIRED NO_CMAKE_PATH)

#get_property(<variable>
#             <GLOBAL             |
#              DIRECTORY [<dir>]  |
#              TARGET    <target> |
#              SOURCE    <source>
#                        [DIRECTORY <dir> | TARGET_DIRECTORY <target>] |
#              INSTALL   <file>   |
#              TEST      <test>   |
#              CACHE     <entry>  |
#              VARIABLE           >
#             PROPERTY <name>
#             [SET | DEFINED | BRIEF_DOCS | FULL_DOCS])
get_property(BLUE_INCLUDE_DIRECTORY
        INCLUDE_PATH_DIRECTORIES
        PROPERTY include)

add_executable(Exefile
    autoversion.h
    BlueInterface.cpp
    BlueInterface.h
    CommandArguments.cpp
    CommandArguments.h
    Crashpad.cpp
    Crashpad.h
    ExeFile.cpp
    ExeFile.h
    ExeFileOsX.mm
    ExeFileWin32.cpp
    FileSystem.h
    FileSystemOsX.mm
    FileSystemWin32.cpp
    resource.h
    StdAfx.cpp
    StdAfx.h
    version.h
    # Icon resource file on Windows, ignored on macOS
    ExeFile.rc
)

target_precompile_headers(Exefile PRIVATE StdAfx.h)

set_target_properties(Exefile PROPERTIES OUPTUT_NAME exefile)

if(WIN32)
    set_target_properties(Exefile PROPERTIES WIN32_EXECUTABLE True)
endif()

if(APPLE)
    enable_language(OBJCXX)
    target_link_libraries(Exefile PRIVATE "-framework Foundation")
    set_target_properties(
        Exefile 
        PROPERTIES 
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/.")
endif()

target_compile_definitions(Exefile PRIVATE BLUEBUILD_STATIC CRASH_REPORTS_ENABLED)

target_link_libraries(Exefile PUBLIC BlueExposure Crashpad::Crashpad)

target_include_directories(Blue PRIVATE ${BLUE_INCLUDE_DIRECTORY})

target_include_directories(Exefile
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_target_properties(Exefile PROPERTIES OUTPUT_NAME "exefile")
