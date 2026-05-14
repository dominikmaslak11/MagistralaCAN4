cmake_minimum_required(VERSION 3.16)
project(MagistralaCAN4 VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Google Test (FetchContent) ──
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)
enable_testing()

# ── Qt6 (wspólne) ──
# Statyczne Qt6 tylko lokalnie (nie na CI — tam Qt6 jest dynamiczne z /ucrt64)
if(WIN32 OR MINGW)
    if(NOT DEFINED ENV{CI} AND EXISTS "C:/msys64/ucrt64/qt6-static")
        set(CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static" CACHE PATH "Static Qt6 prefix (MSYS2 UCRT64)")
    endif()
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts WebSockets OpenGLWidgets SerialPort Sql Network)

# ── zstd (kompresja nagrań) ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)

# ── Platform-specific dependencies ──
if(UNIX AND NOT APPLE)
    find_package(XCB REQUIRED)
    find_package(Lua REQUIRED)
    find_package(OpenCL REQUIRED)
    set(HAS_LUA ON)
    set(HAS_OPENCL ON)
    set(HAS_XCB ON)
elseif(WIN32 OR MINGW)
    find_package(Lua)
    find_package(OpenCL)
    set(HAS_LUA ${Lua_FOUND})
    set(HAS_OPENCL ${OpenCL_FOUND})
    set(HAS_XCB OFF)
endif()

# ── Source files ──
set(SOURCES
    src/core/CanDashboard.cpp
    src/core/Logger.cpp
    src/core/OfflineAnalyzer.cpp
    src/core/DbcParser.cpp
    main.cpp
    src/core/CanSniffer.cpp
    src/core/SocketCanDriver.cpp
    src/core/PcanDriver.cpp
    src/core/CanFrameModel.cpp
    src/core/FrameDetailWidget.cpp
    src/core/AssociativeLearner.cpp
    src/core/CandidateModel.cpp
    src/core/LearningEngine.cpp
    src/core/LearningEngine_filters.cpp
    src/core/LearningEngine_clustering.cpp
    src/core/LearningEngine_correlations.cpp
    src/core/LearningEngine_models.cpp
    src/core/LearningEngine_persistence.cpp
    src/core/LuaScriptEngine.cpp
    src/core/GpuCompute.cpp
    src/core/GpuCorrelator.cpp
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
    src/core/CanTpLayer.cpp
    src/core/SomeIpParser.cpp
    src/core/SomeIpWidget.cpp
    src/core/SignalPlotterWidget.cpp
    src/core/DoIpParser.cpp
    src/core/DoIpWidget.cpp
    src/core/DbcAutoGenerator.cpp
    src/core/DbcAutoWidget.cpp
    src/core/BusLoadAnalyzer.cpp
    src/core/CanBusLoadWidget.cpp
    src/core/CanFrameSenderWidget.cpp
    src/core/CanGateway.cpp
    src/core/CanGatewayWidget.cpp
    src/core/UdsSequenceRunner.cpp
    src/core/UdsSequenceWidget.cpp
    src/core/CanFilterExpr.cpp
    src/core/LinParser.cpp
    src/core/LinWidget.cpp
    src/core/CanIdStats.cpp
    src/core/CanIdStatsWidget.cpp
    src/core/Kwp2000Parser.cpp
    src/core/Kwp2000Widget.cpp
    src/core/XcpParser.cpp
    src/core/XcpWidget.cpp
    src/core/CanReplayFilter.cpp
    src/core/CanReplayFilterWidget.cpp
    src/core/CanSignalMonitor.cpp
    src/core/CanSignalMonitorWidget.cpp
    src/core/CanPeriodicSender.cpp
    src/core/CanPeriodicSenderWidget.cpp
    src/core/CanObservationDb.cpp
    src/core/CanObservationDbWidget.cpp
    src/core/CanAlertEngine.cpp
    src/core/CanAlertWidget.cpp
    src/core/PcapExporter.cpp
    src/core/CanTimelineModel.cpp
    src/core/CanProtocolTimelineWidget.cpp
    src/core/CanByteHeatmapModel.cpp
    src/core/CanByteHeatmapWidget.cpp
    src/core/CanGaugeWidget.cpp
    src/core/ArxmlParser.cpp
    src/core/CanModuleProfiler.cpp
    src/core/CanModuleProfilerWidget.cpp
    src/core/CanPrototypeExporter.cpp
    src/core/CanPrototypeExporterWidget.cpp
    src/core/LogComparatorWidget.cpp
    src/core/CanOpenParser.cpp
    src/core/CanOpenWidget.cpp
    src/core/ObdParser.cpp
    src/core/ObdWidget.cpp
    src/core/CanRecorder.cpp
    src/core/MqttBridge.cpp
    src/core/Mdf4Writer.cpp
    src/core/PluginLoader.cpp
    src/core/HttpRestServer.cpp
    src/core/SlCanDriver.cpp
    src/core/CanStatsPanel.cpp
    src/core/CanFilterProxy.cpp
    src/core/CanPlayer.cpp
    src/core/CanFrameDiff.cpp
    src/core/CanBitAnalyzer.cpp
    src/core/CanIntervalAnalyzer.cpp
    src/core/CanSignalStatistics.cpp
    src/core/CanChecksumHelper.cpp
    src/core/CanCounterValidator.cpp
    src/core/CanPayloadSearch.cpp
    src/core/CanSignalEncoder.cpp
    src/core/CanBusErrorAnalyzer.cpp
    src/gui/MainWindow.cpp
    resources.qrc
)

if(HAS_XCB)
    list(APPEND SOURCES src/core/CanInterfaceEnumerator.cpp)
    list(APPEND SOURCES src/core/CanExporter.cpp)
endif()

set(HEADERS
    src/core/CanDashboard.h
    src/core/Logger.h
    src/core/OfflineAnalyzer.h
    src/core/DbcParser.h
    src/core/CanFrame.h
    src/core/ICanDriver.h
    src/core/CanSniffer.h
    src/core/SocketCanDriver.h
    src/core/PcanDriver.h
    src/core/SlCanDriver.h
    src/core/CanFrameModel.h
    src/core/FrameDetailWidget.h
    src/core/AssociativeLearner.h
    src/core/CandidateModel.h
    src/core/WebSocketServer.h
    src/core/J1939Frame.h
    src/core/J1939Parser.h
    src/core/LearningEngine.h
    src/core/J1939Widget.h
    src/core/DbcEditorWidget.h
    src/core/CanNodeSimulator.h
    src/core/CanNodeSimWidget.h
    src/core/RemoteCanClient.h
    src/core/RemoteCanWidget.h
    src/core/CanGaugeWidget.h
    src/core/LogComparatorWidget.h
    src/core/CanOpenFrame.h
    src/core/CanOpenWidget.h
    src/core/ObdFrame.h
    src/core/ObdWidget.h
    src/core/CanRecorder.h
    src/core/MqttBridge.h
    src/core/Mdf4Writer.h
    src/core/ProtocolPlugin.h
    src/core/PluginLoader.h
    src/core/HttpRestServer.h
    src/core/CanStatsPanel.h
    src/core/CanFilterProxy.h
    src/core/CanPlayer.h
    src/core/DataHighlightDelegate.h
    src/core/RingBuffer.h
    src/core/UdsFrame.h
    src/core/UdsParser.h
    src/core/UdsWidget.h
    src/gui/MainWindow.h
    src/gui/HeatmapBar.h
)

if(HAS_XCB)
    list(APPEND HEADERS src/core/CanInterfaceEnumerator.h)
    list(APPEND HEADERS src/core/CanExporter.h)
endif()

# ── Executable ──
if(WIN32 OR MINGW)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# ── Precompiled header (STL + Qt — 46 translation units) ──
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# ── Link libraries ──
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    Qt6::Charts
    Qt6::WebSockets
    Qt6::OpenGLWidgets
    Qt6::SerialPort
    Qt6::Sql
    Qt6::Network
    $<$<BOOL:${ZSTD_FOUND}>:PkgConfig::ZSTD>
)

if(HAS_XCB)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR} ${OpenCL_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${XCB_LIBRARIES}
        ${LUA_LIBRARIES}
        ${OpenCL_LIBRARIES}
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LUA HAS_OPENCL HAS_XCB)
endif()

if(HAS_LUA)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LUA)
endif()

if(HAS_OPENCL)
    target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCL_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCL_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_OPENCL)
endif()

if(WIN32 OR MINGW)
    target_compile_definitions(${PROJECT_NAME} PRIVATE Q_OS_WIN)
endif()

# ── Windows specifics ──
if(WIN32 OR MINGW)
    # Windows resource file (icon + version info)
    target_sources(${PROJECT_NAME} PRIVATE app.rc)
    # Statyczne linkowanie MinGW runtime — tylko lokalnie ze statycznym Qt6
    if(NOT DEFINED ENV{CI})
        target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    # ── Post-build: sygnalizacja gotowości ──
    # Binarka i DLL-e pozostają w build_native/ (jeden katalog wyjściowy)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Build complete: $<TARGET_FILE:${PROJECT_NAME}>"
    )
endif()

# ═══════════════════════════════════════════════════════════════
# ── Testy jednostkowe (Google Test + QTest) ──
# ═══════════════════════════════════════════════════════════════
find_package(Qt6 REQUIRED COMPONENTS Test Network)

set(TEST_SOURCES
    # Test files
    tests/test_canframe.cpp
    tests/test_ringbuffer.cpp
    tests/test_dbcparser.cpp
    tests/test_canframemodel.cpp
    tests/test_learningengine.cpp
    tests/test_cantplayer.cpp
    tests/test_someipparser.cpp
    tests/test_doipparser.cpp
    tests/test_dbcautogenerator.cpp
    tests/test_busloadanalyzer.cpp
    tests/test_cangateway.cpp
    tests/test_udssequencerunner.cpp
    tests/test_canfilterexpr.cpp
    tests/test_linparser.cpp
    tests/test_canidstats.cpp
    tests/test_kwp2000parser.cpp
    tests/test_xcpparser.cpp
    tests/test_canreplayfilter.cpp
    tests/test_cansignalmonitor.cpp
    tests/test_canperiodicsender.cpp
    tests/test_canobservationdb.cpp
    tests/test_canalerengine.cpp
    tests/test_pcapexporter.cpp
    tests/test_cantimelinemodel.cpp
    tests/test_httprestserver.cpp
    tests/test_canbyteheatmapmodel.cpp
    tests/test_arxmlparser.cpp
    tests/test_cangaugewidget.cpp
    tests/test_canmoduleprofiler.cpp
    tests/test_canprototypeexporter.cpp
    tests/test_remote_can.cpp
    tests/test_remote_can_integration.cpp
    tests/test_mqttbridge.cpp
    tests/test_obdparser.cpp
    tests/test_canopenparser.cpp
    tests/test_cannodesimulator.cpp
    tests/test_canexporter.cpp
    tests/test_udsparser.cpp
    tests/test_canrecorder.cpp
    tests/test_mdf4writer.cpp
    tests/test_canfilterproxy.cpp
    tests/test_canplayer.cpp
    tests/test_pluginloader.cpp
    tests/test_logger.cpp
    tests/test_canframediff.cpp
    tests/test_j1939parser.cpp
    tests/test_candidatemodel.cpp
    tests/test_cansniffer.cpp
    tests/test_canbitanalyzer.cpp
    tests/test_canintervalanalyzer.cpp
    tests/test_cansignalstatistics.cpp
    tests/test_canchecksumhelper.cpp
    tests/test_cancountervalidator.cpp
    tests/test_canpayloadsearch.cpp
    tests/test_cansignalencoder.cpp
    tests/test_canbuserroranalyzer.cpp
    # Sources under test (no hardware/driver deps)
    src/core/DbcParser.cpp
    src/core/CanFrameModel.cpp
    src/core/J1939Parser.cpp
    src/core/LearningEngine.cpp
    src/core/LearningEngine_filters.cpp
    src/core/LearningEngine_clustering.cpp
    src/core/LearningEngine_correlations.cpp
    src/core/LearningEngine_models.cpp
    src/core/LearningEngine_persistence.cpp
    src/core/GpuCompute.cpp
    src/core/CanTpLayer.cpp
    src/core/SomeIpParser.cpp
    src/core/DoIpParser.cpp
    src/core/DbcAutoGenerator.cpp
    src/core/BusLoadAnalyzer.cpp
    src/core/CanGateway.cpp
    src/core/UdsSequenceRunner.cpp
    src/core/CanSniffer.cpp
    src/core/CanFilterExpr.cpp
    src/core/LinParser.cpp
    src/core/CanIdStats.cpp
    src/core/Kwp2000Parser.cpp
    src/core/XcpParser.cpp
    src/core/CanReplayFilter.cpp
    src/core/CanSignalMonitor.cpp
    src/core/CanPeriodicSender.cpp
    src/core/CanObservationDb.cpp
    src/core/CanAlertEngine.cpp
    src/core/PcapExporter.cpp
    src/core/CanTimelineModel.cpp
    src/core/HttpRestServer.cpp
    src/core/CanByteHeatmapModel.cpp
    src/core/ArxmlParser.cpp
    src/core/CanGaugeWidget.cpp
    src/core/CanModuleProfiler.cpp
    src/core/CanPrototypeExporter.cpp
    src/core/WebSocketServer.cpp
    src/core/RemoteCanClient.cpp
    src/core/MqttBridge.cpp
    src/core/CanOpenParser.cpp
    src/core/ObdParser.cpp
    src/core/CanNodeSimulator.cpp
    src/core/CanExporter.cpp
    src/core/CanPlayer.cpp
    src/core/UdsParser.cpp
    src/core/CanRecorder.cpp
    src/core/Mdf4Writer.cpp
    src/core/CanFilterProxy.cpp
    src/core/PluginLoader.cpp
    src/core/Logger.cpp
    src/core/CanFrameDiff.cpp
    src/core/CandidateModel.cpp
    src/core/CanBitAnalyzer.cpp
    src/core/CanIntervalAnalyzer.cpp
    src/core/CanSignalStatistics.cpp
    src/core/CanChecksumHelper.cpp
    src/core/CanCounterValidator.cpp
    src/core/CanPayloadSearch.cpp
    src/core/CanSignalEncoder.cpp
    src/core/CanBusErrorAnalyzer.cpp
)

add_executable(${PROJECT_NAME}_tests ${TEST_SOURCES})
target_include_directories(${PROJECT_NAME}_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    gtest_main
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    Qt6::Sql
    Qt6::Network
    Qt6::WebSockets
    $<$<BOOL:${ZSTD_FOUND}>:PkgConfig::ZSTD>
)
target_compile_definitions(${PROJECT_NAME}_tests PRIVATE UNIT_TEST)

if(WIN32 OR MINGW)
    target_compile_definitions(${PROJECT_NAME}_tests PRIVATE Q_OS_WIN)
    if(NOT DEFINED ENV{CI})
        target_link_options(${PROJECT_NAME}_tests PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)