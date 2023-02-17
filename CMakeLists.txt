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


get_target_property(BLUE_INCLUDE_DIRECTORY
        Blue
        INTERFACE_INCLUDE_DIRECTORIES)

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

set_target_properties(Exefile PROPERTIES OUTPUT_NAME exefile)

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


target_include_directories(Exefile
    PRIVATE
        ${BLUE_INCLUDE_DIRECTORY}
)

set_target_properties(Exefile PROPERTIES OUTPUT_NAME "exefile")

# Provide an install target iff this is the top level project only
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # Install rule to ensure that our runtime and linker files are in the expected, platform-specific folders
    install(
            TARGETS Exefile
            EXPORT ExefileTargets
            CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES}
            ARCHIVE DESTINATION lib/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
            RUNTIME DESTINATION bin/${CCP_PLATFORM}/${CCP_ARCHITECTURE}/${CCP_TOOLSET}/
    )
    # Install rule for available public headers
    install(FILES ${PUBLIC_HEADER_FILES} DESTINATION include)

    configure_ccp_vendor_config_file(
            TARGET Exefile
            DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/ExefileConfig.cmake"
            DEPENDENCIES ""
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ExefileConfig.cmake DESTINATION .)
endif()
