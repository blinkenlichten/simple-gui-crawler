cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)


project(WebgrepGUI)
#sensible project options:

## Where to look for Qt5:
#set(QT5_SEARCH_PATH $ENV{HOMEPATH}/Qt/5.7 CACHE PATH "Qt5 search path")

## Enables experimental build with QtNetwork instead of cURL or NEON http libraries (a.t.m. it's buggy)
#option(USE_QTNETWORK "Experimental: Compile using QtNetwork async i/o" OFF)

set(DEPENDS_ROOT ${CMAKE_SOURCE_DIR}/3rdparty CACHE PATH "Directory where we search for cURL or NEON libraries & includes.")

if(WIN32)
	#-----this must be set by the developer using cmake arguments:------
	set(QT5_SEARCH_PATH $ENV{HOMEPATH}/Qt/5.7 CACHE PATH "Qt5 search path")
else()
	#-----this must be set by the developer using cmake arguments:-------
	set(QT5_SEARCH_PATH $ENV{HOME}/Qt/5.7 CACHE PATH "Qt5 search path")
endif()

find_package(Qt5Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)


file(GLOB src_ui *.cpp *.h *.hpp)


if(USE_QTNETWORK)
#compiling all sources into one executable
	file(GLOB src_wgrep webgrep/*.cpp webgrep/*.h) 
	file(GLOB src_wgrep_qt webgrep/http_imp/ch_ctx_qtnet.cpp webgrep/http_imp/ch_ctx_qtnet.h) 
	

	file(GLOB src_webgrep webgrep)
	add_executable(webgrepGUI ${src_ui} ${src_wgrep} ${src_wgrep_qt})
        if(DO_MEMADDR_SANITIZE)
            add_definitions("-fsanitize=address")
        target_link_libraries(webgrepGUI -lasan)
        endif()

        target_compile_features(webgrepGUI PUBLIC cxx_constexpr)
	target_link_libraries(webgrepGUI Qt5::Network Qt5::Widgets)
else()
#using libneon, compiling against static libwebgrep
	add_subdirectory(webgrep)
	add_executable(webgrepGUI ${src_ui})
        if(DO_MEMADDR_SANITIZE)
            add_definitions("-fsanitize=address")
            target_link_libraries(webgrepGUI -lasan)
        endif()
        target_compile_features(webgrepGUI PUBLIC cxx_constexpr)
	target_link_libraries(webgrepGUI Qt5::Widgets Qt5::Gui webgrep)
endif()

add_subdirectory(unit_tests/test_ThreadPool)

