#!/bin/bash

if [[ "$1" == "release" ]]; then
  cmake -DCMAKE_BUILD_TYPE=Release -Wno-dev -G Ninja . && ninja -v release
elif [[ "$1" == "debug" ]]; then
  cmake -DCMAKE_BUILD_TYPE=Debug -Wno-dev -G Ninja . && ninja -v debug
elif [[ "$1" == "dirty" ]]; then
  git clean -dfxn -e 'compile_commands.json'
elif [[ "$1" == "clean" ]]; then
  git clean -dfx -e 'compile_commands.json'
else
  echo "Unknown command";
fi
