cmake_minimum_required(VERSION 3.16)
project(ChronoKit VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# ----------------------------------------------------------------------------
# Locate Lua 5.4 (pkg-config -> find_package -> FetchContent fallback)
# ----------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUA QUIET lua5.4 lua-5.4 lua>=5.4)
endif()

if(NOT LUA_FOUND)
    find_package(Lua 5.4 QUIET)
    if(LUA_FOUND)
        set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
    endif()
endif()

if(NOT LUA_FOUND)
    message(STATUS "System Lua 5.4 not found; fetching Lua 5.4.7 from source...")
    include(FetchContent)
    FetchContent_Declare(
        lua54
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    )
    FetchContent_MakeAvailable(lua54)
    file(GLOB LUA_SRC_FILES "${lua54_SOURCE_DIR}/src/*.c")
    list(REMOVE_ITEM LUA_SRC_FILES "${lua54_SOURCE_DIR}/src/lua.c" "${lua54_SOURCE_DIR}/src/luac.c")
    add_library(lua_bundled STATIC ${LUA_SRC_FILES})
    target_include_directories(lua_bundled PUBLIC "${lua54_SOURCE_DIR}/src")
    if(UNIX)
        target_link_libraries(lua_bundled PUBLIC m dl)
    endif()
    set(LUA_LIBRARIES lua_bundled)
    set(LUA_INCLUDE_DIRS "${lua54_SOURCE_DIR}/src")
endif()

# ----------------------------------------------------------------------------
# Locate SQLite3 (find_package -> pkg-config -> FetchContent fallback)
# ----------------------------------------------------------------------------
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    set(SQLITE_LIBS SQLite::SQLite3)
else()
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 QUIET sqlite3)
    endif()
    if(SQLITE3_FOUND)
        set(SQLITE_LIBS ${SQLITE3_LIBRARIES})
        include_directories(${SQLITE3_INCLUDE_DIRS})
    else()
        message(STATUS "System SQLite3 not found; fetching SQLite amalgamation...")
        include(FetchContent)
        FetchContent_Declare(
            sqlite3_amalg
            URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
        )
        FetchContent_MakeAvailable(sqlite3_amalg)
        add_library(sqlite3_bundled STATIC "${sqlite3_amalg_SOURCE_DIR}/sqlite3.c")
        target_include_directories(sqlite3_bundled PUBLIC "${sqlite3_amalg_SOURCE_DIR}")
        set(SQLITE_LIBS sqlite3_bundled)
    endif()
endif()

# ----------------------------------------------------------------------------
# Core Composable Engine Library: libchronokit
# ----------------------------------------------------------------------------
add_library(chronokit STATIC
    src/core/json.cpp
    src/core/time_utils.cpp
    src/core/timeline_index.cpp
    src/storage/memory_storage.cpp
    src/storage/sqlite_storage.cpp
    src/bricks/builtin_bricks.cpp
    src/lua/lua_vm.cpp
    src/lua/lua_brick.cpp
    src/engine/appointment_engine.cpp
    src/server/http_server.cpp
)

target_include_directories(chronokit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIRS}
)

target_link_libraries(chronokit
    PUBLIC
        ${LUA_LIBRARIES}
        ${SQLITE_LIBS}
        Threads::Threads
)

if(WIN32)
    target_link_libraries(chronokit PUBLIC ws2_32)
endif()

# ----------------------------------------------------------------------------
# Executables & Automated Test Suite
# ----------------------------------------------------------------------------
add_executable(chronokit_cli apps/cli_demo.cpp)
target_link_libraries(chronokit_cli PRIVATE chronokit)

add_executable(chronokit_server apps/server_main.cpp)
target_link_libraries(chronokit_server PRIVATE chronokit)

enable_testing()
add_executable(chronokit_tests tests/test_chronokit.cpp)
target_link_libraries(chronokit_tests PRIVATE chronokit)
add_test(NAME ChronoKitAllTests COMMAND chronokit_tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
