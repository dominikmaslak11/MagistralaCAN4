cmake_minimum_required(VERSION 3.16)
project(MagistralaCAN4 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts)
find_package(Lua REQUIRED)
find_package(OpenCL REQUIRED)

set(SOURCES
    main.cpp
    src/core/CanSniffer.cpp
    src/core/CanFrameModel.cpp
    src/core/FrameDetailWidget.cpp
    src/core/AssociativeLearner.cpp
    src/core/LuaScriptEngine.cpp
    src/core/CanExporter.cpp
    src/core/CanInterfaceEnumerator.cpp
    src/core/GpuCorrelator.cpp
    src/core/CandidateModel.cpp
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
    src/core/CanInterfaceEnumerator.h
    src/core/GpuCorrelator.h
    src/core/CandidateModel.h
    src/gui/MainWindow.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
    ${OpenCL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    Qt6::Charts
    ${LUA_LIBRARIES}
    ${OpenCL_LIBRARIES}
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_SOCKETCAN)
endif()
