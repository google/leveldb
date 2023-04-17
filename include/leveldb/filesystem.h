#ifndef STORAGE_LEVELDB_INCLUDE_FILE_SYSTEM_H_
#define STORAGE_LEVELDB_INCLUDE_FILE_SYSTEM_H_

#include <cstdint>
#include <string>

#include "leveldb/export.h"

namespace leveldb {

namespace filesystem {

bool IsDirectorySeparator(const char c);

class Path {
public:
  // Constants
  static const char kDirectorySeparator = '\\';
  static const char kAltDirecttorySeparator = '/';
  static const char kVolumeSeparatorChar = ':';

  Path() : isDir_{false}, path_("") {}
  Path(const std::string& path) : path_(path) {
    isDir_ = !IsEmpty() && IsDirectorySeparator(path_[Size() - 1]);
  }
  virtual ~Path() {}

  const std::string& ToString() const { return path_; }
  const char* ToCString() const { return path_.c_str(); }

  virtual bool IsAbsolute() const = 0;
  virtual bool IsRelative() const = 0;

  virtual bool CreateDirectories() = 0;
  virtual bool CreateDirectory() = 0;

  inline size_t Size() const { return path_.size(); }
  inline bool IsEmpty() const { return path_.empty(); }
  inline bool IsDirectory() const { return isDir_; }

  // Utility functions

  inline bool HasExtension() { 
    if (!IsEmpty()) {
      std::string::reverse_iterator& path_iter = path_.rbegin();

      while (path_iter != path_.rend()) {
        char c = *path_iter;

        if (c == '.') {
          return true;
        }
        if (IsDirectorySeparator(c)) {
          break;
        }
      }
    }

    return false;
  }

protected:
  bool isDir_;
  std::string path_;

  virtual void Normalize() = 0;
};

#ifdef LEVELDB_PLATFORM_WINDOWS

class WindowsFilePath : public Path {
public:
  explicit WindowsFilePath(const std::string& path) : Path(path) {
    Normalize();
  }

  ~WindowsFilePath() {}

  bool IsAbsolute() const override;
  bool IsRelative() const override;

  bool CreateDirectories() override;
  bool CreateDirectory() override;

protected:
  void Normalize() override;
};

inline bool IsValidDriveChar(const char c) {
  const char drive_char = std::toupper(c);
  return drive_char >= 'A' && drive_char <= 'Z';
}

#endif

// Factory
class PathFactory {
 public:
  static Path* Create(const std::string& path);

 private:
  PathFactory() {}
  ~PathFactory() {}
};

} // namespace filesystem
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_FILE_SYSTEM_H_
