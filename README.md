kspp
=========

A high performance C++ kafka streams library (C++14)

It is intended to be run on mesos or kubernetes but works equally well standalone

Platforms: Windows / Linux / Mac

This is work-in-progress

## Ubuntu 16 x64:

Install build tools
```
sudo apt-get install -y automake autogen shtool libtool git wget cmake unzip build-essential libboost-all-dev g++ python-dev autotools-dev libicu-dev zlib1g-dev openssl libssl-dev libbz2-dev libsnappy-dev

```
Build
```

git clone https://github.com/facebook/rocksdb.git
cd rocksdb
git checkout v5.3.4
make static_lib
cd ..

git clone https://github.com/edenhill/librdkafka.git
cd librdkafka
git checkout v0.9.5
./configure
make
sudo make install
cd ..

git clone https://github.com/bitbouncer/kspp.git
cd kspp
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ..
```

## MacOS X

Install build tools (using Homebrew)
```
# Install Xcode
xcode-select --install
brew install cmake
brew install kafka
brew install snappy
brew install rocksdb
brew install boost
brew install boost-python
```

Check out source code
```
git clone https://github.com/bitbouncer/kspp.git
```

Run the build
```
./rebuild_macos.sh
```

## Windows x64:

Install build tools
```
- CMake (https://cmake.org/)
- Visual Studio 14 (https://www.visualstudio.com/downloads/)
- nasm (https://sourceforge.net/projects/nasm/)
- perl (http://www.activestate.com/activeperl)
```
Build
```
wget --no-check-certificate http://downloads.sourceforge.net/project/boost/boost/1.62.0/boost_1_62_0.zip
unzip boost_1_62_0.zip
rename boost_1_62_0 boost

git clone https://github.com/facebook/rocksdb.git
git clone https://github.com/madler/zlib.git
git clone https://github.com/lz4/lz4.git
git clone https://github.com/openssl/openssl.git
git clone https://github.com/edenhill/librdkafka.git
git clone https://github.com/bitbouncer/kspp.git

set VISUALSTUDIO_VERSION_MAJOR=14
call "C:\Program Files (x86)\Microsoft Visual Studio %VISUALSTUDIO_VERSION_MAJOR%.0\VC\vcvarsall.bat" amd64

cd openssl
git checkout OpenSSL_1_1_0e
perl Configure VC-WIN64A
nmake
#you need to be Administrator for the next step)
nmake install 
cd ..

cd rocksdb
git checkout v5.3.4
mkdir build & cd build
cmake -G "Visual Studio 14 Win64" ..
msbuild /maxcpucount:8 rocksdb.sln
msbuild /maxcpucount:8 rocksdb.sln /p:Configuration=Release
cd ../..

cd zlib
mkdir build & cd build
cmake -G "Visual Studio 14 Win64" ..
msbuild zlib.sln
msbuild zlib.sln /p:Configuration=Release
cd ../..

cd boost
call bootstrap.bat
.\b2.exe -toolset=msvc-%VisualStudioVersion% variant=release,debug link=static address-model=64 architecture=x86 --stagedir=stage\lib\x64 stage -s ZLIB_SOURCE=%CD%\..\zlib headers log_setup log date_time timer thread system program_options filesystem regex chrono
cd ..

cd librdkafka
git checkout v0.9.5
cd ..

mkdir include
mkdir include\librdkafka
xcopy /e /s librdkafka\src\*.h include\librdkafka
xcopy /e /s librdkafka\src-cpp\*.h include\librdkafka

cd kspp
call rebuild_windows_vs14.bat
cd ..

```

