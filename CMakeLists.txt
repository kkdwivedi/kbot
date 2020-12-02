cmake_minimum_required(VERSION 3.5)

project(kbot)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -Wall -Wextra -Wno-gnu-zero-variadic-macro-arguments -fsanitize=address,undefined")

add_executable(kbot src/epoll.cc src/irc.cc src/loop.cc src/UserCommand.cc src/server.cc src/main.cc)
add_library(version SHARED plugins/Version.cc src/irc.cc src/server.cc src/UserCommand.cc)
add_executable(test_irc_message src/tests/test_irc_message.cc)


find_package(absl REQUIRED)

target_link_libraries(kbot absl::flat_hash_map)
target_link_libraries(kbot glog pthread dl)
target_include_directories(kbot PUBLIC "src/")

target_link_libraries(version absl::flat_hash_set)
target_link_libraries(version glog pthread dl)
target_include_directories(version PUBLIC "src/")

target_link_libraries(test_irc_message gtest glog)
target_include_directories(test_irc_message PUBLIC "src/")

add_custom_target(plugins)
add_dependencies(plugins version)

add_custom_target(tests)
add_dependencies(tests test_irc_message)

add_custom_target(debug)
add_dependencies(debug kbot plugins tests)
add_custom_target(release)
add_dependencies(release kbot plugins)

enable_testing()
add_test(NAME TestIRCMessage COMMAND test_irc_message)
