cmake_minimum_required(VERSION 3.16)
project(route_control)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_executable(route_control
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TitleBarWidgets.h
    src/ServerRegion.h
    src/RegionCache.h
    src/RegionCache.cpp
    src/PingScheduler.h
    src/PingScheduler.cpp
    src/FirewallController.h
    src/FirewallController.cpp
    src/RegionTableModel.h
    src/RegionTableModel.cpp
)

target_link_libraries(route_control Qt6::Core Qt6::Widgets Qt6::Network)

if(WIN32)
    target_link_libraries(route_control iphlpapi ole32 oleaut32)
endif()

if(MINGW)
    target_link_options(route_control PRIVATE -mwindows)
endif()
