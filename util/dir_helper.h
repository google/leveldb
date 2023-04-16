#ifndef STORAGE_LEVELDB_UTIL_DIR_HELPER_H_
#define STORAGE_LEVELDB_UTIL_DIR_HELPER_H_

#include <cstdint>
#include <string>

namespace leveldb {

inline bool IsAbsolute(const std::string& dirpath) {
  return (
      dirpath.size() >= 3 &&
      (std::toupper(dirpath[0]) >= 'A' && std::toupper(dirpath[0]) <= 'Z') &&
      dirpath[1] == ':');
}

inline bool IsDirectorySeparator(const char c) {
  return (c == '/' || c == '\\');
}

inline std::string GetNormalizedDirectoryPath(const std::string& dirpath) {
  std::string path = dirpath;
  size_t remove_pos = 0;

  if (!IsAbsolute(path)) {
    remove_pos = path.find_first_not_of(".\\/");
    if (remove_pos != std::string::npos) {
      path.erase(0, remove_pos);
    }
  }
  if (IsDirectorySeparator(path[path.size() - 1])) {
    remove_pos = path.find_last_not_of(".\\/");
    if (remove_pos != std::string::npos) {
      path.erase(remove_pos + 1);
    }
  }
  return path;
}

}  // namespace leveldb

#endif