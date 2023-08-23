# Sound Stretch RPC (ssrpc)

## srpc

Follow official [doc](https://github.com/sogou/srpc)

## dependencies

```
mkdir build && cd build
conan install .. --profile ../profile.txt --build missing
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
```
