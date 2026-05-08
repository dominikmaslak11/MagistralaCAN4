cmake_minimum_required(VERSION 3.16)
project(MagistralaCAN4 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts WebSockets)
find_package(XCB REQUIRED)
find_package(Lua REQUIRED)
find_package(OpenCL REQUIRED)

set(SOURCES
    src/core/CanDashboard.cpp
    src/core/Logger.cpp
    src/core/OfflineAnalyzer.cpp
    src/core/DbcParser.cpp
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
    src/core/WebSocketServer.cpp
    src/core/J1939Parser.cpp
    src/core/J1939Widget.cpp
    src/core/DbcEditorWidget.cpp
    src/core/CanNodeSimulator.cpp
    src/core/CanNodeSimWidget.cpp
    src/core/RemoteCanClient.cpp
    src/core/RemoteCanWidget.cpp
    src/core/UdsParser.cpp
    src/core/UdsWidget.cpp
    src/core/CanGaugeWidget.cpp
    src/core/LogComparatorWidget.cpp
    src/core/ObdParser.cpp
    src/core/ObdWidget.cpp
    src/core/CanRecorder.cpp
    src/gui/MainWindow.cpp
)

set(HEADERS
    src/core/CanDashboard.h
    src/core/Logger.h
    src/core/OfflineAnalyzer.h
    src/core/DbcParser.h
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
    src/core/WebSocketServer.h
    src/core/J1939Frame.h
    src/core/J1939Parser.h
    src/core/J1939Widget.h
    src/core/DbcEditorWidget.h
    src/core/CanNodeSimulator.h
    src/core/CanNodeSimWidget.h
    src/core/RemoteCanClient.h
    src/core/RemoteCanWidget.h
    src/core/CanGaugeWidget.h
    src/core/LogComparatorWidget.h
    src/core/ObdFrame.h
    src/core/ObdWidget.h
    src/core/CanRecorder.h
    src/core/UdsFrame.h
    src/core/UdsParser.h
    src/core/UdsWidget.h
    src/gui/MainWindow.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
    ${OpenCL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${XCB_LIBRARIES}
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    Qt6::Charts
    Qt6::WebSockets
    ${LUA_LIBRARIES}
    ${OpenCL_LIBRARIES}
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_SOCKETCAN)
endif()
