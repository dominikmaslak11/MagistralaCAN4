# CMake toolchain file for cross-compiling MagistralaCAN4 for Windows
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=../mingw-toolchain.cmake -DQT6_DIR=/path/to/Qt/6.10.2/mingw_64/lib/cmake ..

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Mingw-w64 cross compiler
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Override Qt6 path (set via -DQT6_DIR from command line)
# set(QT6_DIR "/run/media/nz2xzhkzfeewkgbu/0C1A2332389DB1AF/Qt6/6.10.2/mingw_64/lib/cmake/Qt6")
