#! /bin/bash

set -o errexit

cd "$(dirname "$0")"
PACKAGE_DIR=$(pwd)

export SCMPF_MODULE_VERSION="1.0.0"
export HADOOP_HOME=/noah/thirdparty/hadoop
export JRE_HOME=/noah/thirdparty/jre6

BOOST_DIR=/usr/local/boost
PROTOBUF_DIR=/usr/local/protobuf
LIBEVENT_DIR=/usr/local/libevent
SNAPPY_DIR=/usr/local/snappy
THRIFT_DIR=/usr/local/thrift
LOG4CPP_DIR=/usr/local/log4cpp
LIBTORRENT_DIR=/usr/local/libtorrent

for libdir in ${BOOST_DIR} ${PROTOBUF_DIR} ${LIBEVENT_DIR} ${LIBTORRENT_DIR} ${THRIFT_DIR} ${SNAPPY_DIR} ${LOG4CPP_DIR}
do
    CMAKE_INCLUDE_PATH=${CMAKE_INCLUDE_PATH}:${libdir}/include
    CMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}:${libdir}/lib
done

export CMAKE_INCLUDE_PATH=${CMAKE_INCLUDE_PATH}:${HADOOP_HOME}/libhdfs:${BOOST_DIR}/include/boost/tr1:/usr/local/include:/usr/include
export CMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}:${HADOOP_HOME}/libhdfs:${JRE_HOME}/lib/amd64/server:/usr/local/lib:/usr/lib64:/usr/lib
export PATH=$PROTOBUF_DIR/bin:$THRIFT_DIR/bin:$PATH

BUILD=${PACKAGE_DIR}/build
OUTPUT=${PACKAGE_DIR}/output
DEBUG_OUTPUT=${OUTPUT}/bbts.debug
GINGKO_OUTPUT=${OUTPUT}/bbts

rm -fr ${OUTPUT}
mkdir -p ${DEBUG_OUTPUT} ${BUILD}

# 生成thrift&proto代码
cd src/proto
protoc --cpp_out=. *.proto
thrift --gen cpp -out . TorrentProvider.thrift
thrift --gen cpp -out . transfer_server.thrift
thrift --gen cpp -out . stat.thrift
thrift --gen cpp -out . tracker.thrift
thrift --gen cpp -out . announce.thrift
cd -

# 编译
make -C libtorrent
cd ${BUILD} && cmake .. -DCMAKE_INSTALL_PREFIX=${DEBUG_OUTPUT}/ && make VERBOSE=1 && make install && cd -

chmod 0755 ${DEBUG_OUTPUT}/NOAH/*

TAR="tar --owner=0 --group=0 --mode=-s -zcpf"
GINGKO_PACKAGE="bin conf NOAH"

#处理上线的包
cp -r ${DEBUG_OUTPUT} ${GINGKO_OUTPUT}
objcopy --strip-unneeded ${GINGKO_OUTPUT}/bin/gko3
[ -f ${GINGKO_OUTPUT}/lib/libhdfstool.so ] && (
    objcopy --strip-unneeded ${GINGKO_OUTPUT}/lib/libhdfstool.so
    GINGKO_PACKAGE="$GINGKO_PACKAGE lib"
)

if [ -n "${SCMPF_MODULE_VERSION}" ]; then 
    cd ${DEBUG_OUTPUT} && \
    ${TAR} "${OUTPUT}/bbts-agent-${SCMPF_MODULE_VERSION}.debug.tgz" ${GINGKO_PACKAGE}
    rm -fr ${DEBUG_OUTPUT}
fi

cd ${GINGKO_OUTPUT} && \
find . -type f -exec md5sum {} \; > md5sums && mv md5sums NOAH  

if [ -n "${SCMPF_MODULE_VERSION}" ]; then
    cd ${GINGKO_OUTPUT} && ${TAR} "${OUTPUT}/bbts-agent-${SCMPF_MODULE_VERSION}.tgz" ${GINGKO_PACKAGE}
    rm -fr ${GINGKO_OUTPUT}
fi
