#include "leveldb/db_path.h"

namespace leveldb {
namespace path {

#ifdef LEVELDB_PLATFORM_WINDOWS

const char* kExtendedPathPrefix = "\\\\?\\";
const char* kUncExtendedPathPrefix = "\\\\?\\UNC\\";

bool IsValidDriveChar(const char c) {
  const char drive_char = std::toupper(c);
  return drive_char >= 'A' && drive_char <= 'Z';
}

#elif LEVELDB_PLATFORM_POSIX

#endif

bool IsDirectorySeparator(const char c) {
  return (c == kDirectorySeparator || c == kAltDirecttorySeparator);
} 

const std::string& GetRootDirectory(const std::string& path) {
  return path.substr(0, RootLength(path));
}

bool IsAbsolute(const std::string& path) {
#ifdef LEVELDB_PLATFORM_WINDOWS
  return path.size() >= 3 && IsValidDriveChar(path[0]) &&
         path[1] == kVolumeSeparatorChar;
#endif
}

bool IsRelative(const std::string& path) {
  if (path.size() < 2) {
    return true;
  }

  if (IsDirectorySeparator(path[0])) {
    if (path[1] != '?') {
      return !IsDirectorySeparator(path[1]);
    }
    return false;
  }
  if (path.size() >= 3 && path[1] == kVolumeSeparatorChar &&
      IsDirectorySeparator(path[2])) {
      #ifdef LEVELDB_PLATFORM_WINDOWS
          return IsValidDriveChar(path[0]);
      #elif LEVELDB_PLATFORM_POSIX

      #endif
  }
  return true;
}

size_t RootLength(const std::string& path) {
  size_t path_length = path.size();
  size_t root_length = 0;
  size_t volume_separator_length = 2;
  size_t unc_root_length = 2;

  bool extended_syntax = StartsWith(path, std::string(kExtendedPathPrefix));
  bool extended_unc_syntax = StartsWith(path, std::string(kUncExtendedPathPrefix));

  if (extended_syntax) {
    if (extended_unc_syntax) {
      unc_root_length = std::strlen(kUncExtendedPathPrefix);
    } else {
      volume_separator_length += std::strlen(kExtendedPathPrefix);
    }
  }

  if ((!extended_syntax || extended_unc_syntax) && path_length != 0 &&
      IsDirectorySeparator(path[0])) {
    root_length = 1;

    if (extended_unc_syntax ||
        (path_length > 1 && IsDirectorySeparator(path[1]))) {
      root_length = unc_root_length;
      int n = 2;  // maximum separators to skip
      while (root_length < path_length &&
             (!IsDirectorySeparator(path[root_length]) || --n > 0)) {
        ++root_length;
      }
    }
  } else if (path_length >= volume_separator_length &&
             path[volume_separator_length - 1] == kVolumeSeparatorChar) {
    root_length = volume_separator_length;
    if (path_length >= volume_separator_length &&
        IsDirectorySeparator(path[volume_separator_length])) {
      ++root_length;
    }
  }

  return root_length;
}

bool StartsWith(const std::string& path, const std::string& search,
                bool ignore_case) {
  if (search.size() > path.size()) {
    return false;
  }
  if (ignore_case) {
    auto ignore_case_cmp_func = [](char a, char b) {
      return std::tolower(a) == std::tolower(b);
    };
    return std::equal(search.begin(), search.end(), path.begin(),
                      ignore_case_cmp_func);
  }
  return std::equal(search.begin(), search.end(), path.begin());
}

std::string Normalize(const std::string& path) {
  std::string out;
  auto path_it = path.begin();

  for (const char c : path) {
    if (!IsDirectorySeparator(c)) {
      out += c;
      path_it++;
    } else if (path_it == path.begin() ||
               !IsDirectorySeparator(*std::prev(path_it))) {
      out += kDirectorySeparator;
      path_it++;
    } else {
      continue;
    }
  }

  return out;
}

}  // namespace path
}  // namespace leveldb