cmake_minimum_required(VERSION 3.16)
project(MagistralaCAN4 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent)
find_package(Lua REQUIRED)

set(SOURCES
    main.cpp
    src/core/CanSniffer.cpp
    src/core/CanFrameModel.cpp
    src/core/FrameDetailWidget.cpp
    src/core/AssociativeLearner.cpp
    src/core/LuaScriptEngine.cpp
    src/core/CanExporter.cpp
    src/gui/MainWindow.cpp
)

set(HEADERS
    src/core/CanFrame.h
    src/core/CanSniffer.h
    src/core/CanFrameModel.h
    src/core/FrameDetailWidget.h
    src/core/AssociativeLearner.h
    src/core/LuaScriptEngine.h
    src/core/CanExporter.h
    src/gui/MainWindow.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    ${LUA_LIBRARIES}
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_SOCKETCAN)
endif()
