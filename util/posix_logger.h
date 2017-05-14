// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Logger implementation that can be shared by all environments
// where enough posix functionality is available.

#ifndef STORAGE_LEVELDB_UTIL_POSIX_LOGGER_H_
#define STORAGE_LEVELDB_UTIL_POSIX_LOGGER_H_

#include <algorithm>
#include <stdio.h>

#ifdef LEVELDB_PLATFORM_WINDOWS
// <sys/time.h> not available in WIN32
#include <winsock.h>
void _win32getlocaltime(tm *ptm, int *pms)
{
	// win native api to get local time
	SYSTEMTIME st;
	::GetLocalTime(&st);
	// convert
	ptm->tm_sec  = (int)st.wSecond;
	ptm->tm_min  = (int)st.wMinute;
	ptm->tm_hour = (int)st.wHour;
	ptm->tm_mday = (int)st.wDay;
	ptm->tm_mon  = (int)st.wMonth - 1;
	ptm->tm_year = (int)st.wYear - 1900;
	ptm->tm_wday = (int)st.wDayOfWeek;
	*pms	     = st.wMilliseconds;
}
#else//LEVELDB_PLATFORM_WINDOWS
#include <sys/time.h>
#endif
#include <time.h>
#include "leveldb/env.h"

namespace leveldb {

class PosixLogger : public Logger {
 private:
  FILE* file_;
  uint64_t (*gettid_)();  // Return the thread id for the current thread
 public:
  PosixLogger(FILE* f, uint64_t (*gettid)()) : file_(f), gettid_(gettid) { }
  virtual ~PosixLogger() {
    fclose(file_);
  }
  virtual void Logv(const char* format, va_list ap) {
    const uint64_t thread_id = (*gettid_)();

    // We try twice: the first time with a fixed-size stack allocated buffer,
    // and the second time with a much larger dynamically allocated buffer.
    char buffer[500];
    for (int iter = 0; iter < 2; iter++) {
      char* base;
      int bufsize;
      if (iter == 0) {
        bufsize = sizeof(buffer);
        base = buffer;
      } else {
        bufsize = 30000;
        base = new char[bufsize];
      }
      char* p = base;
      char* limit = base + bufsize;

	  // get local time in struct <t> + millisec in m
	  struct tm t;
	  int ms = 0;
#ifdef LEVELDB_PLATFORM_WINDOWS
	  _win32getlocaltime(&t,&ms);
#else
      struct timeval now_tv;
      gettimeofday(&now_tv, NULL);
      const time_t seconds = now_tv.tv_sec;
	  localtime_r(&seconds, &t);
	  ms = static_cast<int>(now_tv.tv_usec);
#endif


      p += snprintf(p, limit - p,
                    "%04d/%02d/%02d-%02d:%02d:%02d.%06d %llx ",
                    t.tm_year + 1900,
                    t.tm_mon + 1,
                    t.tm_mday,
                    t.tm_hour,
                    t.tm_min,
                    t.tm_sec,
                    ms,
                    static_cast<long long unsigned int>(thread_id));

      // Print the message
      if (p < limit) {
        va_list backup_ap;
        va_copy(backup_ap, ap);
        p += vsnprintf(p, limit - p, format, backup_ap);
        va_end(backup_ap);
      }

      // Truncate to available space if necessary
      if (p >= limit) {
        if (iter == 0) {
          continue;       // Try again with larger buffer
        } else {
          p = limit - 1;
        }
      }

      // Add newline if necessary
      if (p == base || p[-1] != '\n') {
        *p++ = '\n';
      }

      assert(p <= limit);
      fwrite(base, 1, p - base, file_);
      fflush(file_);
      if (base != buffer) {
        delete[] base;
      }
      break;
    }
  }
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_POSIX_LOGGER_H_
