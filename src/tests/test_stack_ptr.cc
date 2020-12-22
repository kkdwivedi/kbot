#include <gtest/gtest.h>

#include <Util.hh>
#include <type_traits>
using namespace kbot;

#define __TEST_TRAITS(type)                                \
  ({                                                       \
    static_assert(std::is_default_constructible_v<type>);  \
    static_assert(!std::is_copy_constructible_v<type>);    \
    static_assert(!std::is_copy_assignable_v<type>);       \
    static_assert(!std::is_move_constructible_v<type>);    \
    static_assert(!std::is_move_assignable_v<type>);       \
    static_assert(std::is_trivially_destructible_v<type>); \
  })

#define TEST_TRAITS(type) __TEST_TRAITS(StackPtr<type>)

TEST(StackPtr, Traits) {
  TEST_TRAITS(int);
  TEST_TRAITS(int[]);
}

TEST(StackPtr, WithDeleter) {}

TEST(StackPtr, NonArray1) {
  auto s = make_stack_ptr<int>(1);
  ASSERT_EQ(*s, 1);
  *s = 2;
  ASSERT_EQ(*s, 2);
  ASSERT_EQ(&(*s), s.get());
  ASSERT_TRUE(s);
}

TEST(StackPtr, NonArray2) {
  struct S {
    char buf[4];
    int i;
  };
  auto p = make_stack_ptr<S>(S{{'a', 'b', 'c', 'd'}, 4});
  ASSERT_EQ(p->buf[0], 'a');
  ASSERT_EQ(p->buf[1], 'b');
  ASSERT_EQ(p->buf[2], 'c');
  ASSERT_EQ(p->buf[3], 'd');
  ASSERT_EQ(p->i, 4);
  (*p).i = 5;
  ASSERT_EQ(p->i, 5);
  ASSERT_EQ(&(*p), p.get());
  ASSERT_TRUE(p);
}

TEST(StackPtr, Array1) {
  auto p = make_stack_ptr<int[]>(2);
  auto s = p.get();
  s[0] = 1;
  s[1] = 2;
  ASSERT_EQ(p[0], 1);
  ASSERT_EQ(p[1], 2);
  ASSERT_TRUE(p);
}

int main() {
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
