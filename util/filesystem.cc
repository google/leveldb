#include "leveldb/filesystem.h"

namespace leveldb {
namespace filesystem {

bool IsDirectorySeparator(const char c) {
  return (c == Path::kDirectorySeparator || c == Path::kAltDirecttorySeparator);
}

Path* PathFactory::Create(const std::string& path)
{
  #ifdef LEVELDB_PLATFORM_WINDOWS
    return new WindowsFilePath(path);
  #elif LEVELDB_PLATFORM_POSIX
    return nullptr;
  #endif
    return nullptr;
}

#ifdef LEVELDB_PLATFORM_WINDOWS

bool WindowsFilePath::IsAbsolute() const {
  return path_.size() >= 3 && IsValidDriveChar(path_[0]) &&
         path_[1] == Path::kVolumeSeparatorChar;
};

bool WindowsFilePath::IsRelative() const {
  if (path_.size() < 2) {
    return true;
  }

  if (IsDirectorySeparator(path_[0])) {
    if (path_[1] != '?') {
      return !IsDirectorySeparator(path_[1]);
    }
    return false;
  }
  if (path_.size() >= 3 && path_[1] == Path::kVolumeSeparatorChar &&
      IsDirectorySeparator(path_[2])) {
    return IsValidDriveChar(path_[0]);
  }

  return true;
};

void WindowsFilePath::Normalize() {
  auto out = path_.begin();

  for (const char c : path_) {
    if (!IsDirectorySeparator(c)) {
      *(out++) = c;
    } else if (out == path_.begin() || IsDirectorySeparator(*std::prev(out))) {
      *(out++) = Path::kDirectorySeparator;
    } else {
      continue;
    }
  }

  path_.erase(out, path_.end());
}

bool WindowsFilePath::CreateDirectories() { return true; }

bool WindowsFilePath::CreateDirectory() { return true; }

#endif

}
}


