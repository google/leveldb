#ifndef STORAGE_LEVELDB_INCLUDE_DB_PATH_H_
#define STORAGE_LEVELDB_INCLUDE_DB_PATH_H_

#include <cstdint>
#include <string>

#include "leveldb/export.h"
#include "leveldb/status.h"

namespace leveldb {

namespace path {

class DbPath {
public:
  // Constants
  static const char kDirectorySeparator = '\\';
  static const char kAltDirecttorySeparator = '/';
  static const char kVolumeSeparatorChar = ':';

  virtual ~DbPath() {}

  const std::string& Name() const { return path_; }
  const char* CName() const { return path_.c_str(); }

  static bool IsDirectorySeparator(const char c);

  virtual bool IsAbsolute() const = 0;
  virtual bool IsRelative() const = 0;

  inline size_t Size() const { return path_.size(); }
  inline bool IsEmpty() const { return path_.empty(); }


protected:
  DbPath() : path_("") {}
  DbPath(const std::string& path) : path_(path) {}

  std::string path_;

  virtual void Normalize() = 0;
};

class WindowsDbPath : public DbPath {
public:
  explicit WindowsDbPath(const std::string& path) : DbPath(path) {
    Normalize();
  }
  ~WindowsDbPath() {}

  static bool IsValidDriveChar(const char c);

  bool IsAbsolute() const override;
  bool IsRelative() const override;

protected:
  void Normalize() override;
};


// Factory
class PathFactory {
public:
  PathFactory() = delete;
  ~PathFactory() = delete;

  static DbPath* Create(const std::string& path);
};

} // namespace path
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_DB_PATH_H_
