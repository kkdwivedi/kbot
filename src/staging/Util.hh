#include <alloca.h>

#include <concepts>
#include <memory>
#include <type_traits>

namespace kbot {

template <class, class>
class StackPtr;

namespace detail {

// TODO: Use prospective destructors once they land in clang
template <class T, class Deleter>
struct StackPtrBase {
  StackPtrBase() = default;
  ~StackPtrBase() {
    static_assert(std::is_default_constructible_v<Deleter>);
    Deleter{}(static_cast<StackPtr<T, Deleter> *>(this)->ptr);
  }
};

template <class T>
struct StackPtrBase<T, void> {
  StackPtrBase() = default;
  ~StackPtrBase() = default;
};

}  // namespace detail

template <class T, class Deleter = void>
class StackPtr : detail::StackPtrBase<T, Deleter> {
  friend detail::StackPtrBase<T, Deleter>;
  mutable std::remove_extent_t<T> *ptr = nullptr;

 public:
  StackPtr() = default;
  StackPtr(std::nullptr_t) : StackPtr() {}
  StackPtr(std::remove_extent_t<T> *ptr) : ptr(ptr) {}
  StackPtr(const StackPtr &) = delete;
  StackPtr &operator=(const StackPtr &) = delete;
  StackPtr(StackPtr &&) = delete;
  StackPtr &operator=(StackPtr &&) = delete;
  ~StackPtr() = default;

  std::remove_extent_t<T> *get() const noexcept { return ptr; }
  explicit operator bool() const noexcept { return ptr != nullptr; }
  T &operator*() const requires(!std::is_array_v<T>) { return *ptr; }
  T *operator->() const noexcept requires(std::is_class_v<T>) { return ptr; }
  std::remove_extent_t<T> &operator[](size_t index) const requires(std::is_array_v<T>) {
    return ptr[index];
  }
};

namespace detail {

template <class T>
StackPtr<T> __make_stack_ptr(void *ptr, size_t size) {
  return StackPtr<T>(new (ptr) std::remove_extent_t<T>[size]());
}

template <class T, class... Args>
requires(!std::is_array_v<T>) StackPtr<T> __make_stack_ptr(void *ptr, Args &&...args) {
  return StackPtr<T>(new (ptr) T(std::forward<Args>(args)...));
}

}  // namespace detail

template <class T, class... Args>
inline __attribute__((always_inline)) auto make_stack_ptr(Args &&...args) {
  // This needs to use caller stack frame
  // TODO: use overload for size_t
  void *p;
  if constexpr (std::is_array_v<T>) {
    static_assert(sizeof...(args) == 1);
    p = alloca(sizeof(std::remove_extent_t<T>) * (args, ...));
  } else {
    p = alloca(sizeof(T));
  }
  return detail::__make_stack_ptr<T>(p, std::forward<Args>(args)...);
}

}  // namespace kbot
