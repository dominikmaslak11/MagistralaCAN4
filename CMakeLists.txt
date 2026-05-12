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
# Na Windows wymuszamy statycznego Qt6 (brak zależności od DLL)
if(WIN32 OR MINGW)
    set(CMAKE_PREFIX_PATH "C:/msys64/ucrt64/qt6-static" CACHE PATH "Static Qt6 prefix (MSYS2 UCRT64)")
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent Charts WebSockets OpenGLWidgets SerialPort)

# ── zstd (kompresja nagrań) ──
if(WIN32 OR MINGW)
    find_package(PkgConfig)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
else()
    find_package(zstd)
endif()

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
    src/core/CanGaugeWidget.cpp
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
    # Statyczne linkowanie MinGW runtime (brak zależności od libgcc_s_seh-1.dll itp.)
    target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)

    # ── Post-build: sygnalizacja gotowości ──
    # Binarka i DLL-e pozostają w build_native/ (jeden katalog wyjściowy)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Build complete: $<TARGET_FILE:${PROJECT_NAME}>"
    )
endif()

# ═══════════════════════════════════════════════════════════════
# ── Testy jednostkowe (Google Test + QTest) ──
# ═══════════════════════════════════════════════════════════════
find_package(Qt6 REQUIRED COMPONENTS Test)

set(TEST_SOURCES
    # Test files
    tests/test_canframe.cpp
    tests/test_ringbuffer.cpp
    tests/test_dbcparser.cpp
    tests/test_canframemodel.cpp
    tests/test_learningengine.cpp
    tests/test_cantplayer.cpp
    tests/test_someipparser.cpp
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
)

add_executable(${PROJECT_NAME}_tests ${TEST_SOURCES})
target_include_directories(${PROJECT_NAME}_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    gtest_main
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)
target_compile_definitions(${PROJECT_NAME}_tests PRIVATE UNIT_TEST)

if(WIN32 OR MINGW)
    target_compile_definitions(${PROJECT_NAME}_tests PRIVATE Q_OS_WIN)
    target_link_options(${PROJECT_NAME}_tests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)