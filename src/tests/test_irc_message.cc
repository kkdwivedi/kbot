#include <gtest/gtest.h>
#include <string_view>
#include <vector>

#include <irc.hh>

TEST(IRCMessage, FullParsing1)
{
  const kbot::IRCMessage m("@url=;netsplit=tur,ty :dan!d@localhost PRIVMSG #chan :hey what's up!");
  std::cout << m;
  ASSERT_EQ(m.get_tags(), "url=;netsplit=tur,ty");
  ASSERT_EQ(m.get_source(), "dan!d@localhost");
  ASSERT_EQ(m.get_command(), "PRIVMSG");
  auto& vec = m.get_parameters();
  ASSERT_EQ(vec.size(), 4);
  ASSERT_EQ(vec[0], "#chan");
  ASSERT_EQ(vec[1], ":hey");
  ASSERT_EQ(vec[2], "what's");
  ASSERT_EQ(vec[3], "up!");
}

TEST(IRCMessage, TagParsing1)
{
  const kbot::IRCMessage m("@url=;netsplit=tur,ty :dan!d@localhost PRIVMSG #chan :Hey what's up!");
  auto& tag_kv = m.get_tag_kv();
  ASSERT_EQ(tag_kv.size(), 2);
  ASSERT_EQ(tag_kv[0].first, "url");
  ASSERT_EQ(tag_kv[0].second, "");
  ASSERT_EQ(tag_kv[1].first, "netsplit");
  ASSERT_EQ(tag_kv[1].second, "tur,ty");
}

int main()
{
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
