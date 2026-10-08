#ifndef STORAGE_LEVELDB_INCLUDE_DB_PATH_H_
#define STORAGE_LEVELDB_INCLUDE_DB_PATH_H_

#include <cstdint>
#include <string>

namespace leveldb {

namespace path {

const char kDirectorySeparator = '\\';
const char kAltDirecttorySeparator = '/';
const char kVolumeSeparatorChar = ':';

bool IsAbsolute(const std::string& path);

bool IsRelative(const std::string& path);

size_t RootLength(const std::string& path);

const std::string& GetRootDirectory(const std::string& path);

std::string Normalize(const std::string& path);

bool IsDirectorySeparator(const char c);

static bool IsValidDriveChar(const char c);

static bool StartsWith(const std::string& path, const std::string& search, bool ignore_case = false);

} // namespace path
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_INCLUDE_DB_PATH_H_
