#include <absl/container/flat_hash_map.h>
#include <glog/logging.h>
#include <sqlite3.h>

#include <Database.hh>
#include <IRC.hh>
#include <stdexcept>
#include <string_view>

namespace kbot {
namespace db {

Database::Database(std::string_view filename, int flags) {
  int r = sqlite3_open_v2(filename.data(), &handle, flags, nullptr);
  if (r != SQLITE_OK) {
    LOG(ERROR) << "Error for database " << filename << DB_ERRMSG(r);
    throw std::runtime_error("Failed to open database");
  }
}

Database::Database(Database &&d) { handle = std::exchange(d.handle, nullptr); }

Database &Database::operator=(Database &&d) {
  if (this != &d) {
    if (handle) {
      FinalizeDatabaseConnection();
      int r = sqlite3_close(handle);
      assert(r != SQLITE_BUSY);
    }
    handle = std::exchange(d.handle, nullptr);
  }
  return *this;
}

Database::~Database() {
  // All statements, blobs and backups should be finalized before destructor is called
  if (handle) {
    FinalizeDatabaseConnection();
    int r = sqlite3_close(handle);
    assert(r != SQLITE_BUSY);
  }
}

void Database::FinalizeDatabaseConnection() noexcept {}

}  // namespace db
}  // namespace kbot
