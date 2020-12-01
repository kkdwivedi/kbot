#!/bin/bash

if [[ "$1" == "release" ]]; then
  cmake -DCMAKE_BUILD_TYPE=Release -Wno-dev . && CXX=clang++ make release
elif [[ "$1" == "debug" ]]; then
  cmake -DCMAKE_BUILD_TYPE=Debug -Wno-dev . && CXX=clang++ make VERBOSE=1 debug
elif [[ "$1" == "dirty" ]]; then
  git clean -dfxn
elif [[ "$1" == "clean" ]]; then
  git clean -dfx
fi
