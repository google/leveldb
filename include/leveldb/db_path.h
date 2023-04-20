#ifndef STORAGE_LEVELDB_INCLUDE_DB_PATH_H_
#define STORAGE_LEVELDB_INCLUDE_DB_PATH_H_

#include <cstdint>
#include <string>

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

  virtual bool IsAbsolute() const = 0;
  virtual bool IsRelative() const = 0;
  virtual size_t RootLength() = 0;

  inline size_t Size() const { return path_.size(); }
  inline bool IsEmpty() const { return path_.empty(); }


protected:
  DbPath() : path_("") {}
  DbPath(const std::string& path) : path_(path) {}

  std::string path_;

  static bool IsDirectorySeparator(const char c);

  bool StartsWith(const std::string& value, bool ignore_case = false);

  virtual void Normalize() = 0;
};

// Win path
class WindowsDbPath : public DbPath {
public:
  explicit WindowsDbPath(const std::string& path) : DbPath(path) {
    Normalize();
  }
  ~WindowsDbPath() {}

  bool IsAbsolute() const override;
  bool IsRelative() const override;

  size_t RootLength() override;

protected:
  static bool IsValidDriveChar(const char c);

  void Normalize() override;

private:
  const char* kExtendedPathPrefix = "\\\\?\\";
  const char* kUncExtendedPathPrefix = "\\\\?\\UNC\\";
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
