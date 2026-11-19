cmake_minimum_required(VERSION 3.21)
project(MCDebugger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)

set(SOURCES
    src/main.cpp
    src/AppSettings.h
    src/SecureStore.h
    src/SecureStore.cpp
    src/LogCleaner.h
    src/LogCleaner.cpp
    src/Downloader.h
    src/Downloader.cpp
    src/AiClient.h
    src/AiClient.cpp
    src/LlamaManager.h
    src/LlamaManager.cpp
    src/Theme.h
    src/Theme.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/resources.qrc
)

qt_add_executable(MCDebugger WIN32 ${SOURCES})
target_link_libraries(MCDebugger PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(MCDebugger PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

if(WIN32)
    target_link_libraries(MCDebugger PRIVATE crypt32)
    target_compile_definitions(MCDebugger PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
    target_compile_options(MCDebugger PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(MCDebugger PRIVATE -Wall -Wextra)
endif()
