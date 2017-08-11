cmake_minimum_required(VERSION 2.6)
set(CMAKE_SUPPRESS_REGENERATION 1)
set(CMAKE_USE_RELATIVE_PATHS ON)
SET(CMAKE_BUILD_TYPE "Debug") 
project(gko3-agent)
include(config.cmake)
add_subdirectory(uri)
add_subdirectory(sqlite)
add_subdirectory(src)
add_subdirectory(plugin)

install(DIRECTORY conf NOAH
        DESTINATION .
        FILE_PERMISSIONS ${default_file_permissions}
        DIRECTORY_PERMISSIONS ${default_excute_permissions}
        PATTERN .svn EXCLUDE)
