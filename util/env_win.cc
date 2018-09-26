// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <windows.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <type_traits>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "leveldb/status.h"
#include "port/mman.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/posix_logger.h"
#include "util/env_posix_test_helper.h"

static ssize_t fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  DWORD err;
  if (h == INVALID_HANDLE_VALUE) {
    errno = EBADF;
    return -1;
  }
  if (!FlushFileBuffers(h)) {
    /* Translate some Windows errors into rough approximations of Unix
     * errors.  MSDN is useless as usual - in this case it doesn't
     * document the full range of errors.
     */
    err = GetLastError();
    switch (err) {
      case ERROR_ACCESS_DENIED:
        /* For a read-only handle, fsync should succeed, even though we have
        no way to sync the access-time changes.  */
        return 0;
      /* eg. Trying to fsync a tty. */
      case ERROR_INVALID_HANDLE:
        errno = EINVAL;
        break;
      default:
        errno = EIO;
    }
    return -1;
  }
  return 0;
}

static ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  if (lseek(fd, offset, SEEK_SET) != offset)
    return -1;
  return read(fd, buf, count);
}

// HAVE_FDATASYNC is defined in the auto-generated port_config.h, which is
// included by port_stdcxx.h.
#if !HAVE_FDATASYNC
#define fdatasync fsync
#endif  // !HAVE_FDATASYNC

namespace leveldb {

namespace {

static int open_read_only_file_limit = -1;
static int mmap_limit = -1;

constexpr const size_t kWritableFileBufferSize = 65536;

static Status PosixError(const std::string& context, int err_number) {
  if (err_number == ENOENT) {
    return Status::NotFound(context, strerror(err_number));
  } else {
    return Status::IOError(context, strerror(err_number));
  }
}

// Helper class to limit resource usage to avoid exhaustion.
// Currently used to limit read-only file descriptors and mmap file usage
// so that we do not run out of file descriptors or virtual memory, or run into
// kernel performance problems for very large databases.
class Limiter {
 public:
  // Limit maximum number of resources to |max_acquires|.
  Limiter(int max_acquires) : acquires_allowed_(max_acquires) {}

  Limiter(const Limiter&) = delete;
  Limiter operator=(const Limiter&) = delete;

  // If another resource is available, acquire it and return true.
  // Else return false.
  bool Acquire() {
    int old_acquires_allowed =
        acquires_allowed_.fetch_sub(1, std::memory_order_relaxed);

    if (old_acquires_allowed > 0)
      return true;

    acquires_allowed_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // Release a resource acquired by a previous call to Acquire() that returned
  // true.
  void Release() {
    acquires_allowed_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  // The number of available resources.
  //
  // This is a counter and is not tied to the invariants of any other class, so
  // it can be operated on safely using std::memory_order_relaxed.
  std::atomic<int> acquires_allowed_;
};

class PosixSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  int fd_;

 public:
  PosixSequentialFile(const std::string& fname, int fd)
      : filename_(fname), fd_(fd) {}
  virtual ~PosixSequentialFile() { close(fd_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    Status s;
    while (true) {
      ssize_t r = read(fd_, scratch, n);
      if (r < 0) {
        if (errno == EINTR) {
          continue;  // Retry
        }
        s = PosixError(filename_, errno);
        break;
      }
      *result = Slice(scratch, r);
      break;
    }
    return s;
  }

  virtual Status Skip(uint64_t n) {
    if (lseek(fd_, n, SEEK_CUR) == static_cast<off_t>(-1)) {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }
};

// pread() based random-access
class PosixRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  bool temporary_fd_;  // If true, fd_ is -1 and we open on every read.
  int fd_;
  Limiter* limiter_;

 public:
  PosixRandomAccessFile(const std::string& fname, int fd, Limiter* limiter)
      : filename_(fname), fd_(fd), limiter_(limiter) {
    temporary_fd_ = !limiter->Acquire();
    if (temporary_fd_) {
      // Open file on every access.
      close(fd_);
      fd_ = -1;
    }
  }

  virtual ~PosixRandomAccessFile() {
    if (!temporary_fd_) {
      close(fd_);
      limiter_->Release();
    }
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    int fd = fd_;
    if (temporary_fd_) {
      fd = open(filename_.c_str(), O_RDONLY | O_BINARY);
      if (fd < 0) {
        return PosixError(filename_, errno);
      }
    }

    Status s;
    ssize_t r = pread(fd, scratch, n, static_cast<off_t>(offset));
    *result = Slice(scratch, (r < 0) ? 0 : r);
    if (r < 0) {
      // An error: return a non-ok status
      s = PosixError(filename_, errno);
    }
    if (temporary_fd_) {
      // Close the temporary file descriptor opened earlier.
      close(fd);
    }
    return s;
  }
};

// mmap() based random-access
class PosixMmapReadableFile: public RandomAccessFile {
 private:
  std::string filename_;
  void* mmapped_region_;
  size_t length_;
  Limiter* limiter_;

 public:
  // base[0,length-1] contains the mmapped contents of the file.
  PosixMmapReadableFile(const std::string& fname, void* base, size_t length,
                        Limiter* limiter)
      : filename_(fname), mmapped_region_(base), length_(length),
        limiter_(limiter) {
  }

  virtual ~PosixMmapReadableFile() {
    munmap(mmapped_region_, length_);
    limiter_->Release();
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status s;
    if (offset + n > length_) {
      *result = Slice();
      s = PosixError(filename_, EINVAL);
    } else {
      *result = Slice(reinterpret_cast<char*>(mmapped_region_) + offset, n);
    }
    return s;
  }
};

class PosixWritableFile final : public WritableFile {
 public:
  PosixWritableFile(std::string filename, int fd)
      : pos_(0), fd_(fd), is_manifest_(IsManifest(filename)),
        filename_(std::move(filename)), dirname_(Dirname(filename_)) {}

  ~PosixWritableFile() override {
    if (fd_ >= 0) {
      // Ignoring any potential errors
      Close();
    }
  }

  Status Append(const Slice& data) override {
    size_t write_size = data.size();
    const char* write_data = data.data();

    // Fit as much as possible into buffer.
    size_t copy_size = std::min(write_size, kWritableFileBufferSize - pos_);
    std::memcpy(buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;
    if (write_size == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    // Small writes go to buffer, large writes are written directly.
    if (write_size < kWritableFileBufferSize) {
      std::memcpy(buf_, write_data, write_size);
      pos_ = write_size;
      return Status::OK();
    }
    return WriteUnbuffered(write_data, write_size);
  }

  Status Close() override {
    Status status = FlushBuffer();
    const int close_result = ::close(fd_);
    if (close_result < 0 && status.ok()) {
      status = PosixError(filename_, errno);
    }
    fd_ = -1;
    return status;
  }

  Status Flush() override {
    return FlushBuffer();
  }

  Status Sync() override {
    // Ensure new files referred to by the manifest are in the filesystem.
    //
    // This needs to happen before the manifest file is flushed to disk, to
    // avoid crashing in a state where the manifest refers to files that are not
    // yet on disk.
    Status status = SyncDirIfManifest();
    if (!status.ok()) {
      return status;
    }

    status = FlushBuffer();
    if (status.ok() && ::fdatasync(fd_) != 0) {
      status = PosixError(filename_, errno);
    }
    return status;
  }

 private:
  Status FlushBuffer() {
    Status status = WriteUnbuffered(buf_, pos_);
    pos_ = 0;
    return status;
  }

  Status WriteUnbuffered(const char* data, size_t size) {
    while (size > 0) {
      ssize_t write_result = ::write(fd_, data, size);
      if (write_result < 0) {
        if (errno == EINTR) {
          continue;  // Retry
        }
        return PosixError(filename_, errno);
      }
      data += write_result;
      size -= write_result;
    }
    return Status::OK();
  }

  Status SyncDirIfManifest() {
    Status status;
    if (!is_manifest_) {
      return status;
    }

    /*int fd = ::open(dirname_.c_str(), O_RDONLY | O_BINARY);
    if (fd < 0) {
      status = PosixError(dirname_, errno);
    } else {
      if (::fsync(fd) < 0) {
        status = PosixError(dirname_, errno);
      }
      ::close(fd);
    }*/
    DIR* d = opendir(dirname_.c_str());
    if (d == nullptr) {
      return PosixError(dirname_, errno);
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      std::string path = dirname_ + "/" + entry->d_name;
      int fd = ::open(path.c_str(), O_RDONLY);
      if (fd < 0) {
        status = PosixError(dirname_, errno);
      } else {
        if (::fsync(fd) < 0) {
          status = PosixError(dirname_, errno);
        }
        ::close(fd);
      }
    }
    closedir(d);

    return status;
  }

  // Returns the directory name in a path pointing to a file.
  //
  // Returns "." if the path does not contain any directory separator.
  static std::string Dirname(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return std::string(".");
    }
    // The filename component should not contain a path separator. If it does,
    // the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return filename.substr(0, separator_pos);
  }

  // Extracts the file name from a path pointing to a file.
  //
  // The returned Slice points to |filename|'s data buffer, so it is only valid
  // while |filename| is alive and unchanged.
  static Slice Basename(const std::string& filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
      return Slice(filename);
    }
    // The filename component should not contain a path separator. If it does,
    // the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return Slice(filename.data() + separator_pos + 1,
                 filename.length() - separator_pos - 1);
  }

  // True if the given file is a manifest file.
  static bool IsManifest(const std::string& filename) {
    return Basename(filename).starts_with("MANIFEST");
  }

  // buf_[0, pos_ - 1] contains data to be written to fd_.
  char buf_[kWritableFileBufferSize];
  size_t pos_;
  int fd_;

  const bool is_manifest_;  // True if the file's name starts with MANIFEST.
  const std::string filename_;
  const std::string dirname_;  // The directory of filename_.
};

static int LockOrUnlock(int fd, bool lock) {
  /*errno = 0;
  struct flock f;
  memset(&f, 0, sizeof(f));
  f.l_type = (lock ? F_WRLCK : F_UNLCK);
  f.l_whence = SEEK_SET;
  f.l_start = 0;
  f.l_len = 0;        // Lock/unlock entire file
  return fcntl(fd, F_SETLK, &f);*/
  return 0;
}

class PosixFileLock : public FileLock {
 public:
  int fd_;
  std::string name_;
};

// Set of locked files.  We keep a separate set instead of just
// relying on fcntrl(F_SETLK) since fcntl(F_SETLK) does not provide
// any protection against multiple uses from the same process.
class PosixLockTable {
 private:
  port::Mutex mu_;
  std::set<std::string> locked_files_ GUARDED_BY(mu_);
 public:
  bool Insert(const std::string& fname) LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    bool succeeded = locked_files_.insert(fname).second;
    mu_.Unlock();
    return succeeded;
  }
  void Remove(const std::string& fname) LOCKS_EXCLUDED(mu_) {
    mu_.Lock();
    locked_files_.erase(fname);
    mu_.Unlock();
  }
};

class PosixEnv : public Env {
 public:
  PosixEnv();
  virtual ~PosixEnv() {
    char msg[] = "Destroying Env::Default()\n";
    fwrite(msg, 1, sizeof(msg), stderr);
    abort();
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    int fd = open(fname.c_str(), O_RDONLY | O_BINARY);
    if (fd < 0) {
      *result = nullptr;
      return PosixError(fname, errno);
    } else {
      *result = new PosixSequentialFile(fname, fd);
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) {
    *result = nullptr;
    Status s;
    int fd = open(fname.c_str(), O_RDONLY | O_BINARY);
    if (fd < 0) {
      s = PosixError(fname, errno);
    } else if (mmap_limit_.Acquire()) {
      uint64_t size;
      s = GetFileSize(fname, &size);
      if (s.ok()) {
        void* base = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (base != MAP_FAILED) {
          *result = new PosixMmapReadableFile(fname, base, size, &mmap_limit_);
        } else {
          s = PosixError(fname, errno);
        }
      }
      close(fd);
      if (!s.ok()) {
        mmap_limit_.Release();
      }
    } else {
      *result = new PosixRandomAccessFile(fname, fd, &fd_limit_);
    }
    return s;
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) {
    Status s;
    int fd = open(fname.c_str(), O_TRUNC | O_WRONLY | O_BINARY | O_CREAT, 0644);
    if (fd < 0) {
      *result = nullptr;
      s = PosixError(fname, errno);
    } else {
      *result = new PosixWritableFile(fname, fd);
    }
    return s;
  }

  virtual Status NewAppendableFile(const std::string& fname,
                                   WritableFile** result) {
    Status s;
    int fd = open(fname.c_str(), O_APPEND | O_WRONLY | O_BINARY | O_CREAT, 0644);
    if (fd < 0) {
      *result = nullptr;
      s = PosixError(fname, errno);
    } else {
      *result = new PosixWritableFile(fname, fd);
    }
    return s;
  }

  virtual bool FileExists(const std::string& fname) {
    return access(fname.c_str(), F_OK) == 0;
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) {
    result->clear();
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
      return PosixError(dir, errno);
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      result->push_back(entry->d_name);
    }
    closedir(d);
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    Status result;
    if (unlink(fname.c_str()) != 0) {
      result = PosixError(fname, errno);
    }
    return result;
  }

  virtual Status CreateDir(const std::string& name) {
    Status result;
    if (mkdir(name.c_str()) != 0) {
      result = PosixError(name, errno);
    }
    return result;
  }

  virtual Status DeleteDir(const std::string& name) {
    Status result;
    if (rmdir(name.c_str()) != 0) {
      result = PosixError(name, errno);
    }
    return result;
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      *size = 0;
      s = PosixError(fname, errno);
    } else {
      *size = sbuf.st_size;
    }
    return s;
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    Status result;
    WINBOOL res = MoveFileExA(src.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
    if (!res) {
      result = PosixError(src, errno);
    }
    return result;
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = nullptr;
    Status result;
    int fd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
      result = PosixError(fname, errno);
    } else if (!locks_.Insert(fname)) {
      close(fd);
      result = Status::IOError("lock " + fname, "already held by process");
    } else if (LockOrUnlock(fd, true) == -1) {
      result = PosixError("lock " + fname, errno);
      close(fd);
      locks_.Remove(fname);
    } else {
      PosixFileLock* my_lock = new PosixFileLock;
      my_lock->fd_ = fd;
      my_lock->name_ = fname;
      *lock = my_lock;
    }
    return result;
  }

  virtual Status UnlockFile(FileLock* lock) {
    PosixFileLock* my_lock = reinterpret_cast<PosixFileLock*>(lock);
    Status result;
    if (LockOrUnlock(my_lock->fd_, false) == -1) {
      result = PosixError("unlock", errno);
    }
    locks_.Remove(my_lock->name_);
    close(my_lock->fd_);
    delete my_lock;
    return result;
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual Status GetTestDirectory(std::string* result) {
    const char* env = getenv("TEMP");
    if (env && env[0] != '\0') {
      *result = env;
    } else {
      char buf[MAX_PATH];
      DWORD res = GetTempPath(MAX_PATH, buf);
      if (!res) {
        return PosixError("Can't get test directory", errno);
      }
      *result = buf;
    }
    // Directory may already exist
    CreateDir(*result);
    return Status::OK();
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    FILE* f = fopen(fname.c_str(), "w");
    if (f == nullptr) {
      *result = nullptr;
      return PosixError(fname, errno);
    } else {
      *result = new PosixLogger(f);
      return Status::OK();
    }
  }

  virtual uint64_t NowMicros() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
  }

  virtual void SleepForMicroseconds(int micros) {
    usleep(micros);
  }

 private:
  void BackgroundThreadMain();

  static void BackgroundThreadEntryPoint(PosixEnv* env) {
    env->BackgroundThreadMain();
  }

  // Stores the work item data in a Schedule() call.
  //
  // Instances are constructed on the thread calling Schedule() and used on the
  // background thread.
  //
  // This structure is thread-safe beacuse it is immutable.
  struct BackgroundWorkItem {
    explicit BackgroundWorkItem(void (*function)(void* arg), void* arg)
        : function(function), arg(arg) {}

    void (* const function)(void*);
    void* const arg;
  };


  port::Mutex background_work_mutex_;
  port::CondVar background_work_cv_ GUARDED_BY(background_work_mutex_);
  bool started_background_thread_ GUARDED_BY(background_work_mutex_);

  std::queue<BackgroundWorkItem> background_work_queue_
      GUARDED_BY(background_work_mutex_);

  PosixLockTable locks_;
  Limiter mmap_limit_;
  Limiter fd_limit_;
};

// Return the maximum number of concurrent mmaps.
static int MaxMmaps() {
  if (mmap_limit >= 0) {
    return mmap_limit;
  }
  // Up to 1000 mmaps for 64-bit binaries; none for smaller pointer sizes.
  mmap_limit = sizeof(void*) >= 8 ? 1000 : 0;
  return mmap_limit;
}

// Return the maximum number of read-only files to keep open.
static intptr_t MaxOpenFiles() {
  if (open_read_only_file_limit >= 0) {
    return open_read_only_file_limit;
  }
  open_read_only_file_limit = 50;
  return open_read_only_file_limit;
}

PosixEnv::PosixEnv()
    : background_work_cv_(&background_work_mutex_),
      started_background_thread_(false),
      mmap_limit_(MaxMmaps()),
      fd_limit_(MaxOpenFiles()) {
}

void PosixEnv::Schedule(
    void (*background_work_function)(void* background_work_arg),
    void* background_work_arg) {
  background_work_mutex_.Lock();

  // Start the background thread, if we haven't done so already.
  if (!started_background_thread_) {
    started_background_thread_ = true;
    std::thread background_thread(PosixEnv::BackgroundThreadEntryPoint, this);
    background_thread.detach();
  }

  // If the queue is empty, the background thread may be waiting for work.
  if (background_work_queue_.empty()) {
    background_work_cv_.Signal();
  }

  background_work_queue_.emplace(background_work_function, background_work_arg);
  background_work_mutex_.Unlock();
}

void PosixEnv::BackgroundThreadMain() {
  while (true) {
    background_work_mutex_.Lock();

    // Wait until there is work to be done.
    while (background_work_queue_.empty()) {
      background_work_cv_.Wait();
    }

    assert(!background_work_queue_.empty());
    auto background_work_function =
        background_work_queue_.front().function;
    void* background_work_arg = background_work_queue_.front().arg;
    background_work_queue_.pop();

    background_work_mutex_.Unlock();
    background_work_function(background_work_arg);
  }
}

// Wraps an Env instance whose destructor is never created.
//
// Intended usage:
//   using PlatformSingletonEnv = SingletonEnv<PlatformEnv>;
//   void ConfigurePosixEnv(int param) {
//     PlatformSingletonEnv::AssertEnvNotInitialized();
//     // set global configuration flags.
//   }
//   Env* Env::Default() {
//     static PlatformSingletonEnv default_env;
//     return default_env.env();
//   }
template<typename EnvType>
class SingletonEnv {
 public:
  SingletonEnv() {
#if !defined(NDEBUG)
    env_initialized_.store(true, std::memory_order::memory_order_relaxed);
#endif  // !defined(NDEBUG)
    static_assert(sizeof(env_storage_) >= sizeof(EnvType),
                  "env_storage_ will not fit the Env");
    static_assert(alignof(decltype(env_storage_)) >= alignof(EnvType),
                  "env_storage_ does not meet the Env's alignment needs");
    new (&env_storage_) EnvType();
  }
  ~SingletonEnv() = default;

  SingletonEnv(const SingletonEnv&) = delete;
  SingletonEnv& operator=(const SingletonEnv&) = delete;

  Env* env() { return reinterpret_cast<Env*>(&env_storage_); }

  static void AssertEnvNotInitialized() {
#if !defined(NDEBUG)
    assert(!env_initialized_.load(std::memory_order::memory_order_relaxed));
#endif  // !defined(NDEBUG)
  }

 private:
  typename std::aligned_storage<sizeof(EnvType), alignof(EnvType)>::type
      env_storage_;
#if !defined(NDEBUG)
  static std::atomic<bool> env_initialized_;
#endif  // !defined(NDEBUG)
};

#if !defined(NDEBUG)
template<typename EnvType>
std::atomic<bool> SingletonEnv<EnvType>::env_initialized_;
#endif  // !defined(NDEBUG)

using PosixDefaultEnv = SingletonEnv<PosixEnv>;

}  // namespace

void PosixEnv::StartThread(void (*thread_main)(void* thread_main_arg),
                           void* thread_main_arg) {
  std::thread new_thread(thread_main, thread_main_arg);
  new_thread.detach();
}

void EnvPosixTestHelper::SetReadOnlyFDLimit(int limit) {
  PosixDefaultEnv::AssertEnvNotInitialized();
  open_read_only_file_limit = limit;
}

void EnvPosixTestHelper::SetReadOnlyMMapLimit(int limit) {
  PosixDefaultEnv::AssertEnvNotInitialized();
  mmap_limit = limit;
}

Env* Env::Default() {
  static PosixDefaultEnv env_container;
  return env_container.env();
}

}  // namespace leveldb
