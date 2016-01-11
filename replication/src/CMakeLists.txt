cmake_minimum_required(VERSION 2.8.8)

set(DSN_ROOT "$ENV{DSN_ROOT}")
if((DSN_ROOT STREQUAL "") OR (NOT EXISTS "${DSN_ROOT}/"))
    message(FATAL_ERROR "Please make sure that DSN_ROOT is defined and does exists.")
endif()

include("${DSN_ROOT}/bin/dsn.cmake")

set(DSN_BUILD_RUNTIME FALSE)

set(MY_PROJ_NAME "rrdb")
project(${MY_PROJ_NAME} C CXX)

# Source files under CURRENT project directory will be automatically included.
# You can manually set MY_PROJ_SRC to include source files under other directories.
set(MY_PROJ_SRC "")

# Search mode for source files under CURRENT project directory?
# "GLOB_RECURSE" for recursive search
# "GLOB" for non-recursive search
set(MY_SRC_SEARCH_MODE "GLOB")

set(MY_PROJ_INC_PATH "../../include" "../ext/shmcounter/include")

set(MY_PROJ_LIBS 
    dsn.replication.global_checker 
    dsn.replication 
    dsn.replication.meta_server 
    dsn.replication.clientlib 
    dsn.failure_detector
    dsn.replication.zookeeper_provider 
    zookeeper_mt
    shmcounter
    )

if (UNIX)
	set(MY_PROJ_LIBS rocksdb ${MY_PROJ_LIBS} z bz2 snappy rt aio pthread)
else()
	set(MY_PROJ_LIBS rocksdblib ${MY_PROJ_LIBS} rpcrt4)
endif()

set(MY_PROJ_LIB_PATH "../.." "../ext/shmcounter/lib")

# Extra files that will be installed
set(MY_BINPLACES 
    "${CMAKE_CURRENT_SOURCE_DIR}/config.ini" "${CMAKE_CURRENT_SOURCE_DIR}/test.cmd"
    "${CMAKE_CURRENT_SOURCE_DIR}/vconfig.ini" "${CMAKE_CURRENT_SOURCE_DIR}/vtest.cmd"
    )

# Copy libdsn.core.so and set rpath, to make rrdb use dsn shared lib in local directory.
if (UNIX)
    SET(CMAKE_INSTALL_RPATH ".")
    SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(MY_BINPLACES ${MY_BINPLACES} "${DSN_ROOT}/lib/libdsn.core.so")
endif()

dsn_common_setup()
dsn_add_executable()
