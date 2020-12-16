#include <gtest/gtest.h>

#include <IRC.hh>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

TEST(IRCMessage, FullParsing1) {
  const kbot::IRCMessage m("@url=;netsplit=tur,ty :dan!d@localhost PRIVMSG #chan :hey what's up!");
  std::cout << m;
  ASSERT_EQ(m.GetTags(), "url=;netsplit=tur,ty");
  ASSERT_EQ(m.GetSource(), "dan!d@localhost");
  ASSERT_EQ(m.GetCommand(), "PRIVMSG");
  auto &vec = m.GetParameters();
  ASSERT_EQ(vec.size(), 4);
  ASSERT_EQ(vec[0], "#chan");
  ASSERT_EQ(vec[1], ":hey");
  ASSERT_EQ(vec[2], "what's");
  ASSERT_EQ(vec[3], "up!");
}

TEST(IRCMessage, TagParsing1) {
  const kbot::IRCMessage m("@url=;netsplit=tur,ty :dan!d@localhost PRIVMSG #chan :Hey what's up!");
  auto &tag_kv = m.GetTagKV();
  ASSERT_EQ(tag_kv.size(), 2);
  ASSERT_EQ(tag_kv[0].first, "url");
  ASSERT_EQ(tag_kv[0].second, "");
  ASSERT_EQ(tag_kv[1].first, "netsplit");
  ASSERT_EQ(tag_kv[1].second, "tur,ty");
}

TEST(IRCMessage, ParametersSpaces1) {
  const kbot::IRCMessage m(":source command 1 2 3 4 ");
  ASSERT_EQ(m.GetParameters().size(), 4);
  ASSERT_EQ(m.GetParameters().at(0), "1"sv);
  ASSERT_EQ(m.GetParameters().at(1), "2"sv);
  ASSERT_EQ(m.GetParameters().at(2), "3"sv);
  ASSERT_EQ(m.GetParameters().at(3), "4"sv);
  ASSERT_THROW((void)m.GetParameters().at(4), std::out_of_range);
}

TEST(IRCMessage, BadMessage1) {
  ASSERT_THROW(kbot::IRCMessage(""), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage("@url="), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage("@url"), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage(":source_no_command"), std::runtime_error);
  ASSERT_THROW(kbot::IRCMessage(":source command_no_parameters"), std::runtime_error);
  ASSERT_NO_THROW(kbot::IRCMessage("command pa ra me te rs"));
  ASSERT_NO_THROW(kbot::IRCMessage(":source command pa ra me te rs"));
  ASSERT_NO_THROW(kbot::IRCMessage("@key=val;key= :source command pa ra me te rs"));
}

TEST(IRCMessage, UserRecord1) {
  const kbot::IRCMessagePrivMsg m(kbot::IRCMessage(":dan!~d@localhost/foo command param"));
  auto u = m.GetUser();
  ASSERT_EQ(u.nickname, "dan");
  ASSERT_EQ(u.username, "~d");
  ASSERT_EQ(u.hostname, "localhost/foo");
  ASSERT_FALSE(kbot::Message::IsPrivMsgMessage(kbot::IRCMessage(":source. command param")));
  const kbot::IRCMessagePrivMsg m1(kbot::IRCMessage(":dan!~d command param"));
  ASSERT_EQ(m1.GetUser().nickname, "dan");
  ASSERT_EQ(m1.GetUser().username, "~d");
  ASSERT_EQ(m1.GetUser().hostname, "");
  const kbot::IRCMessagePrivMsg m2(kbot::IRCMessage(":dan! command param"));
  ASSERT_EQ(m2.GetUser().nickname, "dan");
  ASSERT_EQ(m2.GetUser().username, "");
  ASSERT_EQ(m2.GetUser().hostname, "");
  ASSERT_EQ(kbot::IRCMessagePrivMsg(kbot::IRCMessage(":dan!~d@ command param")).GetUser().hostname,
            m2.GetUser().hostname);
}

int main() {
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}
