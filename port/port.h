// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_PORT_PORT_H_
#define STORAGE_LEVELDB_PORT_PORT_H_

#include <string.h>

// Include the appropriate platform specific file below.  If you are
// porting to a new platform, see "port_example.h" for documentation
// of what the new port_<platform>.h file must provide.
#if defined(LEVELDB_PLATFORM_POSIX) || defined(LEVELDB_PLATFORM_WINDOWS)
  #include "port/port_stdcxx.h"
  #if defined(LEVELDB_PLATFORM_WINDOWS)
    #ifdef LEVELDB_WINDOWS_UTF8_FILENAMES
      #define LD_FN(a) leveldb::utf8_to_utf16(a)
      #define FN_TO_LD(a) leveldb::utf16_to_utf8(a)
      #define LD_DeleteFile DeleteFileW
      #define LD_CreateFile CreateFileW
      #define LD_CreateFileMapping CreateFileMappingW
      #define LD_GetFileAttributesEx GetFileAttributesExW
      #define LD_GetFileAttributes GetFileAttributesW
      #define LD_FindFirstFile FindFirstFileW
      #define LD_FindNextFile FindNextFileW
      #define LD_WIN32_FIND_DATA WIN32_FIND_DATAW
      #define LD_CreateDirectory CreateDirectoryW
      #define LD_RemoveDirectory RemoveDirectoryW
      #define LD_MoveFile MoveFileW
      #define LD_ReplaceFile ReplaceFileW
      #define LD_SPLITPATH_S _wsplitpath_s
      #define LD_CHAR WCHAR
    #else
      #define LD_FN(a) a
      #define FN_TO_LD(a) a
      #define LD_DeleteFile DeleteFileA
      #define LD_CreateFile CreateFileA
      #define LD_CreateFileMapping CreateFileMappingA
      #define LD_GetFileAttributesEx GetFileAttributesExA
      #define LD_GetFileAttributes GetFileAttributesA
      #define LD_FindFirstFile FindFirstFileA
      #define LD_CreateDirectory CreateDirectoryA
      #define LD_FindNextFile FindNextFileA
      #define LD_WIN32_FIND_DATA WIN32_FIND_DATAA
      #define LD_RemoveDirectory RemoveDirectoryA
      #define LD_MoveFile MoveFileA
      #define LD_ReplaceFile ReplaceFileA
      #define LD_SPLITPATH_S _splitpath_s
      #define LD_CHAR CHAR
    #endif /* LEVELDB_WINDOWS_UTF8_FILENAMES */
  #endif /* LEVELDB_PLATFORM_WINDOWS */
#elif defined(LEVELDB_PLATFORM_CHROMIUM)
  #include "port/port_chromium.h"
#endif

#endif  // STORAGE_LEVELDB_PORT_PORT_H_
