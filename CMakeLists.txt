cmake_minimum_required(VERSION 3.25)

project(radeonmon LANGUAGES CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/.git/HEAD
)

set(APP_VERSION_RC "${VERSION_MAJOR},${VERSION_MINOR},${VERSION_PATCH},0")

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Git tag: ${GIT_TAG}")

if(NOT GIT_TAG)
    set(GIT_TAG "v0.0.0")
endif()

string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${GIT_TAG}")

if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Cannot parse version from git tag: ${GIT_TAG}")
endif()

set(VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VERSION_MINOR "${CMAKE_MATCH_2}")
set(VERSION_PATCH "${CMAKE_MATCH_3}")

set(APP_VERSION_STR
    "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
)

set(APP_VERSION_RC
    "${VERSION_MAJOR},${VERSION_MINOR},${VERSION_PATCH},0"
)

message(STATUS "Version string: ${APP_VERSION_STR}")
message(STATUS "Version resource: ${APP_VERSION_RC}")

set(WEB_FILES
    ${CMAKE_SOURCE_DIR}/web/index.html
    ${CMAKE_SOURCE_DIR}/web/styles.css
    ${CMAKE_SOURCE_DIR}/web/script.js
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/resources.rc
    PROPERTIES OBJECT_DEPENDS "${WEB_FILES}"
)

add_executable(radeonmon WIN32
    src/main.cpp
    src/adlx.cpp
    third_party/AMD/ADLX-1.5/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    third_party/AMD/ADLX-1.5/SDK/platform/Windows/WinAPIs.cpp
    src/ryzen_sdk.cpp
    src/ryzen.cpp
    src/networking.cpp
    src/webserver.cpp
    resources/resources.rc
    ${CMAKE_BINARY_DIR}/generated/version.rc
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/radeonmon/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/version.hpp
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/resources/version.rc.in
    ${CMAKE_BINARY_DIR}/generated/version.rc
    @ONLY
)

target_include_directories(radeonmon PRIVATE
    include
    third_party
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include

)

target_compile_features(radeonmon PRIVATE cxx_std_23)

target_compile_definitions(radeonmon PRIVATE
    UNICODE
    _UNICODE
)

target_link_libraries(radeonmon PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/AMD/RyzenMasterMonitoringSDK/lib/Platform.lib
)

target_link_options(radeonmon PRIVATE
    "/DELAYLOAD:Platform.dll"
)

target_link_libraries(radeonmon PRIVATE
    delayimp.lib
)

if(MSVC)
    target_compile_options(radeonmon PRIVATE
        /W4
        /permissive-
        /utf-8
    )

    set_property(TARGET radeonmon PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    add_custom_command(TARGET radeonmon POST_BUILD
        COMMAND mt.exe
            -manifest "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
            -outputresource:"$<TARGET_FILE:radeonmon>\;1"
        COMMENT "Embedding application manifest"
    )
endif()

target_link_libraries(radeonmon PRIVATE
    user32
)