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

  const std::string& ToString() const { return path_; }
  const char* ToCString() const { return path_.c_str(); }

  const char operator[](size_t i) const { return path_[i]; }

  static bool IsDirectorySeparator(const char c);

  virtual bool IsAbsolute() const = 0;
  virtual bool IsRelative() const = 0;
  virtual size_t RootLength() const = 0;

  inline size_t Size() const { return path_.size(); }
  inline bool IsEmpty() const { return path_.empty(); }
  inline const std::string Substring(size_t from, size_t to) const {
    return path_.substr(from, to);
  }
  inline const std::string GetRootDirectory() const {
    return path_.substr(0, RootLength());
  }

protected:
  DbPath() : path_("") {}
  DbPath(const std::string& path) : path_(path) {}

  virtual void Normalize() = 0;

  bool StartsWith(const std::string& value, bool ignore_case = false) const;

  std::string path_;
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
  size_t RootLength() const override;

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
