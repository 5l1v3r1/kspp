mkdir tmp && cd tmp

wget -O google-test.tar.gz "https://github.com/google/googletest/archive/release-1.8.1.tar.gz" && \
mkdir -p google-test && \
tar \
  --extract \
  --file google-test.tar.gz \
  --directory google-test \
  --strip-components 1 && \
cd google-test && \
mkdir build && cd build && \
cmake  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON .. && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd ../.. && \
rm google-test.tar.gz && \
rm -rf google-test

wget -O google-benchmark.tar.gz "https://github.com/google/benchmark/archive/v1.4.1.tar.gz" && \
mkdir -p google-benchmark && \
tar \
  --extract \
  --file google-benchmark.tar.gz \
  --directory google-benchmark \
  --strip-components 1 && \
cd google-benchmark && \
mkdir build && cd build && \
cmake  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON .. && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd ../.. && \
rm google-benchmark.tar.gz && \
rm -rf google-benchmark

wget -O rapidjson.tar.gz "https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz" && \
mkdir -p rapidjson && \
tar \
   --extract \
   --file rapidjson.tar.gz \
   --directory rapidjson \
   --strip-components 1 && \
cd rapidjson && \
mkdir build && \
cd build && \
cmake -DRAPIDJSON_BUILD_EXAMPLES=OFF -DRAPIDJSON_BUILD_DOC=OFF -DRAPIDJSON_BUILD_TESTS=OFF  -DBUILD_SHARED_LIBS=ON .. && \
sudo make install && \
sudo rm -rf /usr/local/share/doc/RapidJSON && \
cd ../.. && \
rm rapidjson.tar.gz && \
rm -rf rapidjson

wget -O rocksdb.tar.gz "https://github.com/facebook/rocksdb/archive/v5.14.3.tar.gz" && \
mkdir -p rocksdb && \
tar \
    --extract \
    --file rocksdb.tar.gz \
    --directory rocksdb \
    --strip-components 1 && \
cd rocksdb && \
export USE_RTTI=1 && \
make -j "$(getconf _NPROCESSORS_ONLN)" shared_lib && \
sudo cp -r include/* /usr/local/include/ && \
sudo cp librocksdb.so /usr/local/lib/ && \
cd .. && \
rm rocksdb.tar.gz && \
rm -rf rocksdb

#moved to prebuilt package
#wget -O freetds-patched.tar.gz "ftp://ftp.freetds.org/pub/freetds/stable/freetds-patched.tar.gz" && \
#mkdir -p freetds && \
#tar \
#  --extract \
#  --file freetds-patched.tar.gz \
#  --directory freetds \
#  --strip-components 1 && \
#cd freetds && \
#./configure --prefix=/usr/local && \
#make -j "$(getconf _NPROCESSORS_ONLN)" && \
#sudo make install && \
#sudo rm -rf /usr/local/share/doc/freetds && \
#cd .. && \
#rm freetds-patched.tar.gz && \
#rm -rf freetds

wget -O avro.tar.gz "https://github.com/apache/avro/archive/release-1.8.2.tar.gz" && \
mkdir -p avro && \
tar \
  --extract \
  --file avro.tar.gz \
  --directory avro \
  --strip-components 1 && \
cd avro/lang/c++/ && \
mkdir build && \
cd build && \
cmake -DCMAKE_BUILD_TYPE=Release .. -DBUILD_SHARED_LIBS=ON && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd ../../../..
rm avro.tar.gz && \
rm -rf arvo

wget -O civetweb.tar.gz "https://github.com/civetweb/civetweb/archive/v1.11.tar.gz" && \
mkdir -p civetweb && \
tar \
  --extract \
  --file civetweb.tar.gz \
  --directory civetweb \
  --strip-components 1 && \
cd civetweb && \
mkdir build_xx && cd build_xx && \
cmake  -DCMAKE_BUILD_TYPE=Release -DCIVETWEB_ENABLE_CXX=ON -DCIVETWEB_ENABLE_SERVER_EXECUTABLE=OFF -DCIVETWEB_BUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON .. && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd ../.. && \
rm civetweb.tar.gz && \
rm -rf civetweb

wget -O cpr.tar.gz "https://github.com/whoshuu/cpr/archive/1.3.0.tar.gz" && \
mkdir -p cpr && \
tar \
  --extract \
  --file cpr.tar.gz \
  --directory cpr \
  --strip-components 1 && \
cd cpr && \
mkdir build && cd build && \
cmake  -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_CURL=ON -DBUILD_CPR_TESTS=OFF -DBUILD_SHARED_LIBS=ON .. && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo cp lib/libcpr.so /usr/local/lib/libcpr.so && \
sudo mkdir -p /usr/local/include/cpr && \
sudo cp -r ../include/cpr/* /usr/local/include/cpr && \
cd ../.. && \
rm cpr.tar.gz && \
rm -rf cpr

#awaiting patch in org project (not possible to build from tar.gz)
#wget -O prometheus-cpp.tar.gz "https://github.com/jupp0r/prometheus-cpp/archive/v0.4.2.tar.gz" && \
#mkdir -p prometheus-cpp && \
#tar \
#  --extract \
#  --file prometheus-cpp.tar.gz \
#  --directory prometheus-cpp \
#  --strip-components 1 && \
#cd prometheus-cpp && \
#mkdir -p 3rdparty/civetweb/include && \
#cp /usr/local/include/civetweb.h 3rdparty/civetweb/include/civetweb.h && \
#mkdir -p 3rdparty/cpr/include/cpr && \
#cp /usr/local/include/cpr/* 3rdparty/cpr/include/cpr && \
#mkdir build && cd build && \
#cmake  -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF ..
#make -j "$(getconf _NPROCESSORS_ONLN)" && \
#sudo make install && \
#cd .. && \
#rm prometheus-cpp.tar.gz && \
#rm -rf prometheus-cpp

wget -O prometheus-cpp.tar.gz "https://github.com/bitbouncer/prometheus-cpp/archive/master.tar.gz" && \
mkdir -p prometheus-cpp && \
tar \
  --extract \
  --file prometheus-cpp.tar.gz \
  --directory prometheus-cpp \
  --strip-components 1 && \
cd prometheus-cpp && \
mkdir build && cd build && \
cmake  -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DBUILD_SHARED_LIBS=ON .. && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd ../.. && \
rm prometheus-cpp.tar.gz && \
rm -rf prometheus-cpp

wget -O librdkafka.tar.gz "https://github.com/edenhill/librdkafka/archive/v0.11.6-RC2.tar.gz" && \
mkdir -p librdkafka && \
tar \
  --extract \
  --file librdkafka.tar.gz \
  --directory librdkafka \
  --strip-components 1 && \
cd librdkafka && \
./configure --prefix=/usr/local && \
make -j "$(getconf _NPROCESSORS_ONLN)" && \
sudo make install && \
cd .. && \
rm librdkafka.tar.gz && \
rm -rf librdkafka

cd ..



