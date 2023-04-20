#include "leveldb/db_path.h"

namespace leveldb {
namespace path {

bool DbPath::IsDirectorySeparator(const char c) {
  return (c == DbPath::kDirectorySeparator || c == DbPath::kAltDirecttorySeparator);
}

bool DbPath::StartsWith(const std::string& value, bool ignore_case) {
  if (value.size() > path_.size()) {
    return false;
  }
  if (ignore_case) {
    auto ignore_case_cmp_func = [](char a, char b) { return std::tolower(a) == std::tolower(b); };
    return std::equal(value.begin(), value.end(), path_.begin(), ignore_case_cmp_func);
  }
  return std::equal(value.begin(), value.end(), path_.begin());
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

size_t WindowsDbPath::RootLength() {
  size_t path_length = path_.size();
  size_t root_length = 0;
  size_t volume_separator_length = 2;
  size_t unc_root_length = 2;

  bool extended_syntax = StartsWith(kExtendedPathPrefix);
  bool extended_unc_syntax = StartsWith(kUncExtendedPathPrefix);

  if (extended_syntax) {
    if (extended_unc_syntax) {
      unc_root_length = std::strlen(kUncExtendedPathPrefix);
    } 
    else {
      volume_separator_length += std::strlen(kExtendedPathPrefix);
    }
  }

  if ((!extended_syntax || extended_unc_syntax) && path_length != 0 && IsDirectorySeparator(path_[0])) {
    root_length = 1;

    if (extended_unc_syntax || (path_length > 1 && IsDirectorySeparator(path_[1]))) {
      root_length = unc_root_length;
      int n = 2; // maximum separators to skip
      while (root_length < path_length && (!IsDirectorySeparator(path_[root_length]) || --n > 0)) {
        ++root_length;
      }
    }
  } 
  else if (path_length >= volume_separator_length && path_[volume_separator_length - 1] == kVolumeSeparatorChar) {
    root_length = volume_separator_length;
    if (path_length >= volume_separator_length && IsDirectorySeparator(path_[volume_separator_length])) {
      ++root_length;
    }
  }

  return root_length;
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


