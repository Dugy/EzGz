cmake_minimum_required(VERSION 3.10)

project(EzGz LANGUAGES CXX)

# Allow overriding the C++ standard via command line (default is C++20)
if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Use -std=c++XX instead of -std=gnu++XX

message(STATUS "Compiling for C++" ${CMAKE_CXX_STANDARD})

# Detect compiler type
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(STATUS "Detected AppleClang on macOS")
    else()
        message(STATUS "Detected Clang")
    endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    message(STATUS "Detected AppleClang on macOS")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "Detected GCC")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    message(STATUS "Detected MSVC on Windows")
else()
    message(STATUS "Using unknown compiler")
endif()

# Compiler-specific options
if (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    # Extra warnings for Clang/AppleClang
    set(WARNING_OPTIONS "-Wall" "-Wextra" "-Wpedantic")
    set(DEBUG_FLAGS "-g")
    # Suppress unknown attribute warning when compiling for C++17
    if (CMAKE_CXX_STANDARD STREQUAL "17")
        message(STATUS "Suppressing C++20 warnings for C++17")
        list(APPEND WARNING_OPTIONS "-Wno-c++20-extensions")
    endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # Extra warnings for MSVC
    set(WARNING_OPTIONS "/W4")
    set(DEBUG_FLAGS "/Zi")
    # Enable proper reporting of __cplusplus in MSVC
    add_compile_options(/Zc:__cplusplus)    
    # Suppress unknown attribute warning when compiling for C++17
    if (CMAKE_CXX_STANDARD STREQUAL "17")
        message(STATUS "Suppressing C++20 warnings for C++17")
        list(APPEND WARNING_OPTIONS "/wd5051")
    endif()
endif()

# Set output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Add executables
add_executable(ezgz_test ezgz_test.cpp)
add_executable(ezgz_decompress ezgz_decompress.cpp)

# Apply compiler options
if (WARNING_OPTIONS)
    target_compile_options(ezgz_test PRIVATE ${WARNING_OPTIONS} ${DEBUG_FLAGS})
    target_compile_options(ezgz_decompress PRIVATE ${WARNING_OPTIONS} ${DEBUG_FLAGS})
endif()

# Add threading support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(ezgz_test PRIVATE Threads::Threads)
target_link_libraries(ezgz_decompress PRIVATE Threads::Threads)
