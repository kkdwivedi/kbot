cmake_minimum_required(VERSION 3.5)

project(kbot)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -Wall -Wextra -Wno-gnu-zero-variadic-macro-arguments -fsanitize=address,undefined")

include_directories(src)
include_directories(plugins)
include_directories(src/staging)

add_executable(kbot src/main.cc src/Database.cc src/Server.cc src/Manager.cc src/Epoll.cc src/IRC.cc src/UserCommand.cc)
add_library(version SHARED plugins/Version.cc src/IRC.cc src/Server.cc src/Database.cc src/UserCommand.cc)
add_executable(test_irc_message src/tests/test_irc_message.cc src/IRC.cc)
add_executable(test_stack_ptr src/tests/test_stack_ptr.cc)

find_package(absl REQUIRED)
find_package(fmt REQUIRED)

target_link_libraries(kbot PUBLIC absl::flat_hash_map fmt)
target_link_libraries(kbot PUBLIC glog pthread dl sqlite3)

target_link_libraries(version PUBLIC absl::flat_hash_set)
target_link_libraries(version PUBLIC glog pthread dl)

target_link_libraries(test_irc_message PUBLIC gtest glog fmt)
target_link_libraries(test_stack_ptr PUBLIC gtest)

add_custom_target(plugins)
add_dependencies(plugins version)

add_custom_target(tests)
add_dependencies(tests test_irc_message test_stack_ptr)

add_custom_target(debug)
add_dependencies(debug kbot plugins tests)
add_custom_target(release)
add_dependencies(release kbot plugins)

enable_testing()
add_test(NAME TestIRCMessage COMMAND test_irc_message)
add_test(NAME TestUtilStackPtr COMMAND test_stack_ptr)
