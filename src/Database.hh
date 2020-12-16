#pragma once

#include <absl/container/flat_hash_map.h>
#include <sqlite3.h>

#include <IRC.hh>
#include <memory>
#include <string_view>

namespace kbot {
namespace db {

#define DB_ERRMSG(x) " " << sqlite3_errstr(x)

class Database {
  sqlite3 *handle = nullptr;

 public:
  enum {
    kMutex = SQLITE_OPEN_FULLMUTEX,
    kNoMutex = SQLITE_OPEN_NOMUTEX,
    kTransient = SQLITE_OPEN_MEMORY,
    kNoFollow = SQLITE_OPEN_NOFOLLOW,
    kSharedCache = SQLITE_OPEN_SHAREDCACHE,
    kPrivateCache = SQLITE_OPEN_PRIVATECACHE,
  };

  Database() = default;
  explicit Database(std::string_view filename, int flags = kMutex);
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  Database(Database &&);
  Database &operator=(Database &&);
  ~Database();

  void FinalizeDatabaseConnection() noexcept;
};

struct UserData {
  uint64_t cap_mask;
};

class UserDataCache {
  struct Node {
    std::unique_ptr<UserData> db_data_ptr;
    struct {
      uint64_t any_last_time;
      absl::flat_hash_map<std::string, uint64_t> command_last_time;
    } transient_data;

    void InitializeUserDataFromDatabase();
  };

 public:
  UserDataCache() = default;
  UserDataCache(const UserDataCache &) = delete;
  UserDataCache &operator=(const UserDataCache &) = delete;
  UserDataCache(UserDataCache &&) = delete;
  UserDataCache &operator=(UserDataCache &&) = delete;

  // Persistent
  uint64_t GetCapabilityMask(const IRCUser &u);
  UserData GetUserData();
  // Transient
  uint64_t GetCommandLastUseTime(std::string_view command);
  // Memory Management
  // Use to free up cache if unused for a long time
  void EvictResidentData();
};

}  // namespace db
}  // namespace kbot
