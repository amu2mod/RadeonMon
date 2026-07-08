cmake_minimum_required(VERSION 3.25)

project(radeonmon LANGUAGES CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/.git/HEAD
)

execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_executable(radeonmon WIN32
    src/main.cpp
    src/adlx.cpp
    third_party/AMD/ADLX-1.5/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
    third_party/AMD/ADLX-1.5/SDK/platform/Windows/WinAPIs.cpp
    src/ryzen_sdk.cpp
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/radeonmon/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/version.hpp
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
            -manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
            -outputresource:"$<TARGET_FILE:radeonmon>\;1"
        COMMENT "Embedding application manifest"
    )
endif()

target_link_libraries(radeonmon PRIVATE
    user32
)