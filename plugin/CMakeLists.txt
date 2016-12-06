include(${PROJECT_SOURCE_DIR}/config.cmake)

find_path(HDFS_H NAMES hdfs.h)
find_library(HDFS_LIB NAMES hdfs)
find_library(JSIG_LIB NAMES jsig)
find_library(JVM_LIB NAMES jvm)

if (HDFS_H AND HDFS_LIB AND JSIG_LIB AND NOT JVM_LIB)
    add_library(hdfstool SHARED hdfstool)
    target_link_libraries(hdfstool ${JSIG_LIB} ${JVM_LIB} ${HDFS_LIB})
    install(TARGETS hdfstool LIBRARY DESTINATION lib)
else()
    message(STATUS "not found hdfs or jvm library, ignore and not support download from hdfs!")
endif()
