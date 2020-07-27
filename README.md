## kbot - IRC Bot Framework ![CI](https://github.com/kkdwivedi/kbot/workflows/CI/badge.svg?branch=master)

kbot is a simple IRC bot aiming to be highly parallel/scalable, while supporting some features like
resource management for plugins, zero downtime restarts, and the ability to serve multiple networks
and channels at once. It is written in C++20 and only supports Linux.


Contact: Kumar Kartikeya Dwivedi <memxor@gmail.com>


LICENSE: MIT


Dependencies:
  * Google Test (Tests)
  * GNU make, g++/clang++ with C++20 support
  * libcurl
  * pthreads
