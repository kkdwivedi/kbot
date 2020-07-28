#include <gtest/gtest.h>
#include <stdexcept>
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

TEST(IRCMessage, BadMessage1)
{
  ASSERT_THROW(kbot::IRCMessage(""), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage("@url="), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage("@url"), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage(":source_no_command"), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage(":source command_no_parameters"), std::runtime_error);
  ASSERT_NO_THROW(kbot::IRCMessage("command pa ra me te rs"));
  ASSERT_NO_THROW(kbot::IRCMessage(":source command pa ra me te rs"));
  ASSERT_NO_THROW(kbot::IRCMessage("@key=val;key= :source command pa ra me te rs"));
}

TEST(IRCMessage, UserRecord1)
{
  const kbot::IRCMessage m(":dan!~d@localhost/foo command param");
  auto u = m.get_user();
  ASSERT_EQ(u.nickname, "dan");
  ASSERT_EQ(u.username, "~d");
  ASSERT_EQ(u.hostname, "localhost/foo");
  ASSERT_THROW(kbot::IRCMessage(":source. command param").get_user(), std::runtime_error);
  const kbot::IRCMessage m1(":dan!~d command param");
  ASSERT_EQ(m1.get_user().nickname, "dan");
  ASSERT_EQ(m1.get_user().username, "~d");
  ASSERT_EQ(m1.get_user().hostname, "");
  const kbot::IRCMessage m2(":dan! command param");
  ASSERT_EQ(m2.get_user().nickname, "dan");
  ASSERT_EQ(m2.get_user().username, "");
  ASSERT_EQ(m2.get_user().hostname, "");
  ASSERT_EQ(kbot::IRCMessage(":dan command param").get_user().username, m2.get_user().username);
  ASSERT_EQ(kbot::IRCMessage(":dan!~d@ command param").get_user().hostname, m2.get_user().hostname);
}

int main()
{
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
