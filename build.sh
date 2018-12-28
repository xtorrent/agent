#! /bin/bash

set -o errexit

cd "$(dirname "$0")"
PACKAGE_DIR=$(pwd)

export SCMPF_MODULE_VERSION="1.0.0"

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

export CMAKE_INCLUDE_PATH=${CMAKE_INCLUDE_PATH}:${HADOOP_HOME}/include:${BOOST_DIR}/include/boost/tr1:/usr/local/include:/usr/include
export CMAKE_LIBRARY_PATH=${CMAKE_LIBRARY_PATH}:${HADOOP_HOME}/lib/native:${JRE_HOME}/lib/amd64/server:/usr/local/lib:/usr/lib64:/usr/lib
export PATH=$PROTOBUF_DIR/bin:$THRIFT_DIR/bin:$PATH

BUILD=${PACKAGE_DIR}/build
OUTPUT=${PACKAGE_DIR}/output
GKO3_AGENT=${OUTPUT}/gko3-agent

rm -fr ${OUTPUT}
mkdir -p ${GKO3_AGENT} ${BUILD}
mkdir -p ${GKO3_AGENT}/log

echo "Generate verion and timestamp ..."
echo $(date -d  today +%Y%m%d%H%M%S) > ${GKO3_AGENT}/version


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
cd ${BUILD} && cmake .. -DCMAKE_INSTALL_PREFIX=${GKO3_AGENT}/ && make VERBOSE=1 && make install && cd - || exit $?

# 下载 hdfs 依赖的 jars Hadoop 2.7.7, 如果改变版本, 同时改动 jars/pom.xml 文件
cd ${PACKAGE_DIR}
export PATH=/usr/local/apache-maven-3.2.5/bin/:$PATH
mvn -f jars/pom.xml process-sources

chmod 0755 ${GKO3_AGENT}/NOAH/*
if [ -e ${HADOOP_HOME} ]
then
    cp ${HADOOP_HOME}/lib/native/libhdfs.so.0.0.0 ${GKO3_AGENT}/lib
fi

TAR="tar --owner=0 --group=0 --mode=-s -zcpf"

# 打包含 DEBUG 信息的编译文件
cd ${GKO3_AGENT}
if [ -e md5sums ]
then
    rm md5sums
fi
find . -type f -exec md5sum {} \; > md5sums
cd -
cd ${OUTPUT}
if [ -n "${SCMPF_MODULE_VERSION}" ]; then 
    ${TAR} "gko3-agent-${SCMPF_MODULE_VERSION}.debug.tgz" gko3-agent/
fi
cd -

# 处理上线的包
objcopy --strip-unneeded ${GKO3_AGENT}/bin/gko3
[ -f ${GKO3_AGENT}/lib/libhdfstool.so ] && (
    objcopy --strip-unneeded ${GKO3_AGENT}/lib/libhdfstool.so
)

# 打包上线包
cd ${GKO3_AGENT}
if [ -e md5sums ]
then
    rm md5sums
fi
find . -type f -exec md5sum {} \; > md5sums
cd -
cd ${OUTPUT}
if [ -n "${SCMPF_MODULE_VERSION}" ]; then
    ${TAR} "gko3-agent-${SCMPF_MODULE_VERSION}.tgz" gko3-agent/
fi
cd -

rm -fr ${GKO3_AGENT}
