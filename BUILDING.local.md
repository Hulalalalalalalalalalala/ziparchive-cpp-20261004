# Reproducible Ubuntu build

Install build-essential, CMake, pkg-config, libssl-dev, zlib1g-dev and libzip-dev. Use /usr/bin/g++ and /usr/bin/gcc; a compiler wrapper with a private sysroot cannot see system development headers.

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DLIBZIPPP_CMAKE_CONFIG_MODE=ON -DLIBZIPPP_BUILD_TESTS=ON -DLIBZIPPP_ENABLE_ENCRYPTION=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

