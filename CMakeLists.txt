cmake_minimum_required(VERSION 3.24)
project(Zex VERSION 0.5.0.0 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent Core5Compat Svg Test)
if(MSVC)
  add_compile_options(/utf-8 /MP /permissive- /Zc:__cplusplus)
endif()

file(GLOB SCINTILLA_SOURCES CONFIGURE_DEPENDS vendor/scintilla/src/*.cxx)
add_library(scintilla STATIC ${SCINTILLA_SOURCES}
  vendor/scintilla/qt/ScintillaEditBase/PlatQt.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaQt.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.cpp
  vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.h)
target_include_directories(scintilla PUBLIC vendor/scintilla/include vendor/scintilla/src vendor/scintilla/qt/ScintillaEditBase)
target_compile_definitions(scintilla PUBLIC SCINTILLA_QT=1 EXPORT_IMPORT_API=)
target_link_libraries(scintilla PUBLIC Qt6::Widgets Qt6::Core5Compat)

file(GLOB LEXILLA_SOURCES CONFIGURE_DEPENDS vendor/lexilla/lexlib/*.cxx vendor/lexilla/lexers/*.cxx)
add_library(lexilla STATIC ${LEXILLA_SOURCES} vendor/lexilla/src/Lexilla.cxx)
set_target_properties(lexilla PROPERTIES AUTOMOC OFF)
target_include_directories(lexilla PUBLIC vendor/lexilla/include PRIVATE vendor/lexilla/lexlib vendor/scintilla/include)
target_compile_definitions(lexilla PRIVATE LEXILLA_NO_EXPORT)

add_library(zex_core STATIC src/fileloader.cpp src/editor.cpp src/theme.cpp src/mainwindow.cpp
  src/fileicons.cpp src/appearance.cpp src/appearancedialog.cpp src/session.cpp src/filereceiver.cpp src/shellintegration.cpp src/integrationdialog.cpp assets/file_icons.qrc assets/ui_resources.qrc)
target_include_directories(zex_core PUBLIC src)
target_link_libraries(zex_core PUBLIC scintilla lexilla Qt6::Concurrent Qt6::Svg dwmapi advapi32 shell32)
target_compile_definitions(zex_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(zex_app SHARED src/app.cpp assets/resources.qrc)
target_link_libraries(zex_app PRIVATE zex_core)
set_target_properties(zex_app PROPERTIES PREFIX "" OUTPUT_NAME "zex_app")

add_executable(Zex WIN32 src/launcher.cpp src/instance.cpp assets/version.rc)
set_target_properties(Zex PROPERTIES AUTOMOC OFF AUTORCC OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(Zex PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Zex PRIVATE shell32 advapi32)

enable_testing()
add_executable(zex_open_probe WIN32 tests/open_probe.cpp)
set_target_properties(zex_open_probe PROPERTIES AUTOMOC OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(zex_open_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(zex_open_probe PRIVATE shell32)
add_executable(zex_tests tests/test_zex.cpp)
add_dependencies(zex_tests zex_open_probe)
target_link_libraries(zex_tests PRIVATE zex_core Qt6::Test)
add_test(NAME zex_tests COMMAND zex_tests)
