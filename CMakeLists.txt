cmake_minimum_required(VERSION 3.16)
project(qtty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

option(QTTY_BUILD_TESTS    "Build qtty tests"    ON)
option(QTTY_BUILD_EXAMPLES "Build qtty examples" ON)
option(QTTY_BUILD_TOOLS    "Build qtty tools"    ON)

add_library(qtty STATIC
    src/core/cellbuffer.cpp
    src/grid/gridstyle.cpp
    src/render/cellpaint.cpp
    src/runtime/application.cpp
    src/backends/ansi/ansiruntime.cpp
)
add_library(qtty::qtty ALIAS qtty)
target_include_directories(qtty PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(qtty PUBLIC Qt6::Widgets)

if(QTTY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(QTTY_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if(QTTY_BUILD_EXAMPLES)
    add_subdirectory(examples/chat)
endif()

include(GNUInstallDirs)
install(TARGETS qtty EXPORT qttyTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/qtty DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT qttyTargets NAMESPACE qtty::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qtty)
