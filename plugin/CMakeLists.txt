include(${PROJECT_SOURCE_DIR}/config.cmake)

find_path(HDFS_H NAMES hdfs.h)
if(NOT HDFS_H)
    message(FATAL_ERROR "hdfs.h not found.")
endif()
include_directories(${HDFS_H})

find_library(HDFS_LIB NAMES hdfs)
if(NOT HDFS_LIB)
    message(FATAL_ERROR "hdfs not found.")
endif()

find_library(JSIG_LIB NAMES jsig)
if(NOT JSIG_LIB)
    message(FATAL_ERROR "jsig not found.")
endif()

find_library(JVM_LIB NAMES jvm)
if(NOT JVM_LIB)
    message(FATAL_ERROR "jvm not found.")
endif()

add_library(hdfstool SHARED hdfstool)
target_link_libraries(hdfstool ${JSIG_LIB} ${JVM_LIB} ${HDFS_LIB})
install(TARGETS hdfstool LIBRARY DESTINATION lib)

