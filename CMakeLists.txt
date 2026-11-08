cmake_minimum_required(VERSION 3.20)
project(AddonSQLite VERSION 1.0.0 LANGUAGES C CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SDK/CMakeLists.txt")
    message(FATAL_ERROR "Initialize Addon-SDK: git submodule update --init --recursive")
endif()
add_subdirectory(SDK)
include(SDK/cmake/Plugin.cmake)
find_package(Threads REQUIRED)

add_library(SQLiteEngine STATIC vendor/sqlite/sqlite3.c)
target_include_directories(SQLiteEngine PUBLIC vendor/sqlite)
target_compile_definitions(SQLiteEngine PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
set_target_properties(SQLiteEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(SQLiteEngine PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_library(DatabaseCore STATIC src/Database.cpp)
target_compile_features(DatabaseCore PUBLIC cxx_std_17)
target_include_directories(DatabaseCore PUBLIC src)
target_link_libraries(DatabaseCore PUBLIC SQLiteEngine Legacy::SDK)
set_target_properties(DatabaseCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

legacy_plugin(Addon-SQLite src/Main.cpp)
target_link_libraries(Addon-SQLite PRIVATE DatabaseCore)
