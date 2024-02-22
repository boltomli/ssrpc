# Sound Stretch RPC (ssrpc)

## rpc

Follow [srpc](https://github.com/sogou/srpc). Service defined in `sound.proto`.

## audio

Follow [audiofile](https://github.com/adamstark/AudioFile)

## stretch

Follow [soundtouch](https://codeberg.org/soundtouch/soundtouch)

## dependencies

```
mkdir build && cd build
conan install .. --profile ../profile.txt --build missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
