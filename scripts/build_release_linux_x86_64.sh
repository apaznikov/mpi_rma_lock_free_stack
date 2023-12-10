#!/bin/bash

cd ..

export CC=/usr/local/bin/mpicc
export CXX=/usr/local/bin/mpic++
export CXXFLAGS="-O3 -march=native -Wall -Wextra -pedantic"
export CFLAGS="-O3 -march=native -Wall -Wextra -pedantic"

BUILD_DIR=cmake-build-release
INSTALL_DIR=install-release
BUILD_JOBS=6

# shellcheck disable=SC2115
rm -rf ${INSTALL_DIR}/*

cmake -G "Unix Makefiles" \
      -S . \
      -B ${BUILD_DIR} \
      -D CMAKE_BUILD_TYPE=Release \
      -D BUILD_SHARED_LIBS=1

cmake --build ${BUILD_DIR} -j${BUILD_JOBS} --verbose
cmake --install ${BUILD_DIR} --prefix ${INSTALL_DIR}
