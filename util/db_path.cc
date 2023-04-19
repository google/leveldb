#include "leveldb/db_path.h"

namespace leveldb {
namespace path {

bool DbPath::IsDirectorySeparator(const char c) {
  return (c == DbPath::kDirectorySeparator || c == DbPath::kAltDirecttorySeparator);
}

// Windows

bool WindowsDbPath::IsAbsolute() const {
  return path_.size() >= 3 && IsValidDriveChar(path_[0]) &&
         path_[1] == DbPath::kVolumeSeparatorChar;
};

bool WindowsDbPath::IsRelative() const {
  if (path_.size() < 2) {
    return true;
  }

  if (IsDirectorySeparator(path_[0])) {
    if (path_[1] != '?') {
      return !IsDirectorySeparator(path_[1]);
    }
    return false;
  }
  if (path_.size() >= 3 && path_[1] == DbPath::kVolumeSeparatorChar &&
      IsDirectorySeparator(path_[2])) {
    return IsValidDriveChar(path_[0]);
  }
  return true;
};

void WindowsDbPath::Normalize() {
  auto out = path_.begin();

  for (const char c : path_) {
    if (!IsDirectorySeparator(c)) {
      *(out++) = c;
    } 
    else if (out == path_.begin() || !IsDirectorySeparator(*std::prev(out))) {
      *(out++) = kDirectorySeparator;
    } 
    else {
      continue;
    }
  }

  path_.erase(out, path_.end());
}

bool WindowsDbPath::IsValidDriveChar(const char c) {
  const char drive_char = std::toupper(c);
  return drive_char >= 'A' && drive_char <= 'Z';
}

// Windows path

DbPath* PathFactory::Create(const std::string& path) {
#ifdef LEVELDB_PLATFORM_WINDOWS
  return new WindowsDbPath(path);
#elif LEVELDB_PLATFORM_POSIX
  return nullptr;
#endif
  return nullptr;
}

}
}


