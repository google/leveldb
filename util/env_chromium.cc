// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <deque>
#include <errno.h>
#include <stdio.h>
#include "base/at_exit.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/platform_file.h"
#include "base/process_util.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"
#include "base/task.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "util/logging.h"

#if defined(OS_WIN)
#include <io.h>
#include "base/win/win_util.h"
#endif

#if defined(OS_MACOSX) || defined(OS_WIN)
// The following are glibc-specific
namespace {

size_t fread_unlocked(void *ptr, size_t size, size_t n, FILE *file) {
  return fread(ptr, size, n, file);
}

size_t fwrite_unlocked(const void *ptr, size_t size, size_t n, FILE *file) {
  return fwrite(ptr, size, n, file);
}

int fflush_unlocked(FILE *file) {
  return fflush(file);
}

int fdatasync(int fildes) {
#if defined(OS_WIN)
  return _commit(fildes);
#else
  return fsync(fildes);
#endif
}

}
#endif

namespace leveldb {

namespace {

class Thread;

static const ::FilePath::CharType kLevelDBTestDirectoryPrefix[]
    = FILE_PATH_LITERAL("leveldb-test-");

::FilePath CreateFilePath(const std::string& file_path) {
#if defined(OS_WIN)
  return FilePath(UTF8ToUTF16(file_path));
#else
  return FilePath(file_path);
#endif
}

std::string FilePathToString(const ::FilePath& file_path) {
#if defined(OS_WIN)
  return UTF16ToUTF8(file_path.value());
#else
  return file_path.value();
#endif
}

// TODO(jorlow): This should be moved into Chromium's base.
const char* PlatformFileErrorString(const ::base::PlatformFileError& error) {
  switch (error) {
    case ::base::PLATFORM_FILE_ERROR_FAILED:
      return "Opening file failed.";
    case ::base::PLATFORM_FILE_ERROR_IN_USE:
      return "File currently in use.";
    case ::base::PLATFORM_FILE_ERROR_EXISTS:
      return "File already exists.";
    case ::base::PLATFORM_FILE_ERROR_NOT_FOUND:
      return "File not found.";
    case ::base::PLATFORM_FILE_ERROR_ACCESS_DENIED:
      return "Access denied.";
    case ::base::PLATFORM_FILE_ERROR_TOO_MANY_OPENED:
      return "Too many files open.";
    case ::base::PLATFORM_FILE_ERROR_NO_MEMORY:
      return "Out of memory.";
    case ::base::PLATFORM_FILE_ERROR_NO_SPACE:
      return "No space left on drive.";
    case ::base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY:
      return "Not a directory.";
    case ::base::PLATFORM_FILE_ERROR_INVALID_OPERATION:
      return "Invalid operation.";
    case ::base::PLATFORM_FILE_ERROR_SECURITY:
      return "Security error.";
    case ::base::PLATFORM_FILE_ERROR_ABORT:
      return "File operation aborted.";
    case ::base::PLATFORM_FILE_ERROR_NOT_A_FILE:
      return "The supplied path was not a file.";
    case ::base::PLATFORM_FILE_ERROR_NOT_EMPTY:
      return "The file was not empty.";
  }
  NOTIMPLEMENTED();
  return "Unknown error.";
}

class ChromiumSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  FILE* file_;

 public:
  ChromiumSequentialFile(const std::string& fname, FILE* f)
      : filename_(fname), file_(f) { }
  virtual ~ChromiumSequentialFile() { fclose(file_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    Status s;
    size_t r = fread_unlocked(scratch, 1, n, file_);
    *result = Slice(scratch, r);
    if (r < n) {
      if (feof(file_)) {
        // We leave status as ok if we hit the end of the file
      } else {
        // A partial read with an error: return a non-ok status
        s = Status::IOError(filename_, strerror(errno));
      }
    }
    return s;
  }

  virtual Status Skip(uint64_t n) {
    if (fseek(file_, n, SEEK_CUR)) {
      return Status::IOError(filename_, strerror(errno));
    }
    return Status::OK();
  }
};

class ChromiumRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  ::base::PlatformFile file_;

 public:
  ChromiumRandomAccessFile(const std::string& fname, ::base::PlatformFile file)
      : filename_(fname), file_(file) { }
  virtual ~ChromiumRandomAccessFile() { ::base::ClosePlatformFile(file_); }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status s;
    int r = ::base::ReadPlatformFile(file_, offset, scratch, n);
    *result = Slice(scratch, (r < 0) ? 0 : r);
    if (r < 0) {
      // An error: return a non-ok status
      s = Status::IOError(filename_, "Could not preform read");
    }
    return s;
  }
};

class ChromiumWritableFile : public WritableFile {
 private:
  std::string filename_;
  FILE* file_;

 public:
  ChromiumWritableFile(const std::string& fname, FILE* f)
      : filename_(fname), file_(f) { }

  ~ChromiumWritableFile() {
    if (file_ != NULL) {
      // Ignoring any potential errors
      fclose(file_);
    }
  }

  virtual Status Append(const Slice& data) {
    size_t r = fwrite_unlocked(data.data(), 1, data.size(), file_);
    Status result;
    if (r != data.size()) {
      result = Status::IOError(filename_, strerror(errno));
    }
    return result;
  }

  virtual Status Close() {
    Status result;
    if (fclose(file_) != 0) {
      result = Status::IOError(filename_, strerror(errno));
    }
    file_ = NULL;
    return result;
  }

  virtual Status Flush() {
    Status result;
    if (fflush_unlocked(file_) != 0) {
      result = Status::IOError(filename_, strerror(errno));
    }
    return result;
  }

  virtual Status Sync() {
    Status result;
    if ((fflush_unlocked(file_) != 0) ||
        (fdatasync(fileno(file_)) != 0)) {
      result = Status::IOError(filename_, strerror(errno));
    }
    return result;
  }
};

class ChromiumFileLock : public FileLock {
 public:
  ::base::PlatformFile file_;
};

class ChromiumEnv : public Env {
 public:
  ChromiumEnv();
  virtual ~ChromiumEnv() {
    fprintf(stderr, "Destroying Env::Default()\n");
    exit(1);
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    FILE* f = fopen(fname.c_str(), "rb");
    if (f == NULL) {
      *result = NULL;
      return Status::IOError(fname, strerror(errno));
    } else {
      *result = new ChromiumSequentialFile(fname, f);
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) {
    int flags = ::base::PLATFORM_FILE_READ | ::base::PLATFORM_FILE_OPEN;
    bool created;
    ::base::PlatformFileError error_code;
    ::base::PlatformFile file = ::base::CreatePlatformFile(
        CreateFilePath(fname), flags, &created, &error_code);
    if (error_code != ::base::PLATFORM_FILE_OK) {
      *result = NULL;
      return Status::IOError(fname, PlatformFileErrorString(error_code));
    }
    *result = new ChromiumRandomAccessFile(fname, file);
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) {
    *result = NULL;
    FILE* f = fopen(fname.c_str(), "wb");
    if (f == NULL) {
      return Status::IOError(fname, strerror(errno));
    } else {
      *result = new ChromiumWritableFile(fname, f);
      return Status::OK();
    }
  }

  virtual bool FileExists(const std::string& fname) {
    return ::file_util::PathExists(CreateFilePath(fname));
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) {
    result->clear();
    ::file_util::FileEnumerator iter(
        CreateFilePath(dir), false, ::file_util::FileEnumerator::FILES);
    ::FilePath current = iter.Next();
    while (!current.empty()) {
      result->push_back(FilePathToString(current.BaseName()));
      current = iter.Next();
    }
    // TODO(jorlow): Unfortunately, the FileEnumerator swallows errors, so
    //               we'll always return OK. Maybe manually check for error
    //               conditions like the file not existing?
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    Status result;
    // TODO(jorlow): Should we assert this is a file?
    if (!::file_util::Delete(CreateFilePath(fname), false)) {
      result = Status::IOError(fname, "Could not delete file.");
    }
    return result;
  };

  virtual Status CreateDir(const std::string& name) {
    Status result;
    if (!::file_util::CreateDirectory(CreateFilePath(name))) {
      result = Status::IOError(name, "Could not create directory.");
    }
    return result;
  };

  virtual Status DeleteDir(const std::string& name) {
    Status result;
    // TODO(jorlow): Should we assert this is a directory?
    if (!::file_util::Delete(CreateFilePath(name), false)) {
      result = Status::IOError(name, "Could not delete directory.");
    }
    return result;
  };

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
    Status s;
    int64_t signed_size;
    if (!::file_util::GetFileSize(CreateFilePath(fname), &signed_size)) {
      *size = 0;
      s = Status::IOError(fname, "Could not determine file size.");
    } else {
      *size = static_cast<uint64_t>(signed_size);
    }
    return s;
  }

  virtual Status RenameFile(const std::string& src, const std::string& dst) {
    Status result;
    if (!::file_util::ReplaceFile(CreateFilePath(src), CreateFilePath(dst))) {
      result = Status::IOError(src, "Could not rename file.");
    }
    return result;
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;
    Status result;
    int flags = ::base::PLATFORM_FILE_OPEN_ALWAYS |
                ::base::PLATFORM_FILE_READ |
                ::base::PLATFORM_FILE_WRITE |
                ::base::PLATFORM_FILE_EXCLUSIVE_READ |
                ::base::PLATFORM_FILE_EXCLUSIVE_WRITE;
    bool created;
    ::base::PlatformFileError error_code;
    ::base::PlatformFile file = ::base::CreatePlatformFile(
        CreateFilePath(fname), flags, &created, &error_code);
    if (error_code != ::base::PLATFORM_FILE_OK) {
      result = Status::IOError(fname, PlatformFileErrorString(error_code));
    } else {
      ChromiumFileLock* my_lock = new ChromiumFileLock;
      my_lock->file_ = file;
      *lock = my_lock;
    }
    return result;
  }

  virtual Status UnlockFile(FileLock* lock) {
    ChromiumFileLock* my_lock = reinterpret_cast<ChromiumFileLock*>(lock);
    Status result;
    if (!::base::ClosePlatformFile(my_lock->file_)) {
      result = Status::IOError("Could not close lock file.");
    }
    delete my_lock;
    return result;
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual std::string UserIdentifier() {
#if defined(OS_WIN)
    std::wstring user_sid;
    bool ret = ::base::win::GetUserSidString(&user_sid);
    DCHECK(ret);
    return UTF16ToUTF8(user_sid);
#else
    char buf[100];
    snprintf(buf, sizeof(buf), "%d", int(geteuid()));
    return buf;
#endif
  }

  virtual Status GetTestDirectory(std::string* path) {
    mu_.Acquire();
    if (test_directory_.empty()) {
      if (!::file_util::CreateNewTempDirectory(kLevelDBTestDirectoryPrefix,
                                               &test_directory_)) {
        mu_.Release();
        return Status::IOError("Could not create temp directory.");
      }
    }
    *path = FilePathToString(test_directory_);
    mu_.Release();
    return Status::OK();
  }

  virtual void Logv(WritableFile* info_log, const char* format, va_list ap) {
    // TODO(jorlow): We may want to just use Chromium's built in logging.

    uint64_t thread_id = 0;
    // Coppied from base/logging.cc.
#if defined(OS_WIN)
    thread_id = GetCurrentThreadId();
#elif defined(OS_MACOSX)
    thread_id = mach_thread_self();
#elif defined(OS_LINUX)
    thread_id = syscall(__NR_gettid);
#elif defined(OS_FREEBSD) || defined(OS_NACL)
    // TODO(BSD): find a better thread ID
    pthread_t tid = pthread_self();
    memcpy(&thread_id, &tid, min(sizeof(r), sizeof(tid)));
#endif

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

      ::base::Time::Exploded t;
      ::base::Time::Now().LocalExplode(&t);
      p += snprintf(p, limit - p,
                    "%04d/%02d/%02d-%02d:%02d:%02d.%06d %llx ",
                    t.year,
                    t.month,
                    t.day_of_month,
                    t.hour,
                    t.minute,
                    t.second,
                    static_cast<int>(t.millisecond) * 1000,
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
      info_log->Append(Slice(base, p - base));
      info_log->Flush();
      if (base != buffer) {
        delete[] base;
      }
      break;
    }
  }

  virtual int AppendLocalTimeToBuffer(char* buffer, size_t size) {
    ::base::Time::Exploded t;
    ::base::Time::Now().LocalExplode(&t);
    return snprintf(buffer, size,
                    "%04d/%02d/%02d-%02d:%02d:%02d.%06d",
                    t.year,
                    t.month,
                    t.day_of_month,
                    t.hour,
                    t.minute,
                    t.second,
                    static_cast<int>(t.millisecond) * 1000);
  }

  virtual uint64_t NowMicros() {
    return ::base::TimeTicks::HighResNow().ToInternalValue();
  }

  virtual void SleepForMicroseconds(int micros) {
    // Round up to the next millisecond.
    ::base::PlatformThread::Sleep((micros + 999) / 1000);
  }

 private:
  // BGThread() is the body of the background thread
  void BGThread();
  static void BGThreadWrapper(void* arg) {
    reinterpret_cast<ChromiumEnv*>(arg)->BGThread();
  }

  FilePath test_directory_;

  size_t page_size_;
  ::base::Lock mu_;
  ::base::ConditionVariable bgsignal_;
  bool started_bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;
};

ChromiumEnv::ChromiumEnv()
    : page_size_(::base::SysInfo::VMAllocationGranularity()),
      bgsignal_(&mu_),
      started_bgthread_(false) {
#if defined(OS_MACOSX)
  ::base::EnableTerminationOnHeapCorruption();
  ::base::EnableTerminationOnOutOfMemory();
#endif  // OS_MACOSX
}

class Thread : public ::base::PlatformThread::Delegate {
 public:
  Thread(void (*function)(void* arg), void* arg)
      : function_(function), arg_(arg) {
    ::base::PlatformThreadHandle handle;
    bool success = ::base::PlatformThread::Create(0, this, &handle);
    DCHECK(success);
  }
  virtual ~Thread() {}
  virtual void ThreadMain() {
    (*function_)(arg_);
    delete this;
  }

 private:
  void (*function_)(void* arg);
  void* arg_;
};

void ChromiumEnv::Schedule(void (*function)(void*), void* arg) {
  mu_.Acquire();

  // Start background thread if necessary
  if (!started_bgthread_) {
    started_bgthread_ = true;
    StartThread(&ChromiumEnv::BGThreadWrapper, this);
  }

  // If the queue is currently empty, the background thread may currently be
  // waiting.
  if (queue_.empty()) {
    bgsignal_.Signal();
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;

  mu_.Release();
}

void ChromiumEnv::BGThread() {
  while (true) {
    // Wait until there is an item that is ready to run
    mu_.Acquire();
    while (queue_.empty()) {
      bgsignal_.Wait();
    }

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    mu_.Release();
    (*function)(arg);
  }
}

void ChromiumEnv::StartThread(void (*function)(void* arg), void* arg) {
  new Thread(function, arg); // Will self-delete.
}

::base::LazyInstance<ChromiumEnv, ::base::LeakyLazyInstanceTraits<ChromiumEnv> >
    default_env(::base::LINKER_INITIALIZED);

}

Env* Env::Default() {
  return default_env.Pointer();
}

}
