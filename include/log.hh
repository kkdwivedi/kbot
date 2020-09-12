#pragma once

#include <cstdio>
#include <type_traits>

namespace kbot {

namespace log {

inline constexpr bool debug_enabled = true;
inline bool runtime_debug_enabled = true;

#define LOG_INFO_CLR "\033[32m"
#define LOG_WARN_CLR "\033[33m"
#define LOG_ERROR_CLR "\033[31m"
#define LOG_DEBUG_CLR "\033[0m"

#define _log_impl(fmt, color, ...) fprintf(stderr, color fmt "\033[0m\n" __VA_OPT__(,) __VA_ARGS__)

#define log_info(fmt, ...) _log_impl(fmt, LOG_INFO_CLR __VA_OPT__(,) __VA_ARGS__)
#define log_warn(fmt, ...) _log_impl(fmt, LOG_WARN_CLR __VA_OPT__(,) __VA_ARGS__)
#define log_error(fmt, ...) _log_impl(fmt, LOG_ERROR_CLR __VA_OPT__(,) __VA_ARGS__)
#define log_debug(fmt, ...)						\
  do {									\
    if constexpr (kbot::log::debug_enabled) {				\
      if (kbot::log::runtime_debug_enabled)				\
        _log_impl(fmt, LOG_DEBUG_CLR __VA_OPT__(,) __VA_ARGS__);	\
    }								        \
  } while (0)
#define log_debug_id(id, fmt, ...)				\
  do {								\
    static_assert(std::is_same_v<decltype(id), size_t>,		\
		  "Log ID needs to be of type size_t!");	\
    log_debug("[%zu]: " fmt, (id) __VA_OPT__(,) __VA_ARGS__);	\
  } while (0)

} // namespace log

} // namespace kbot
