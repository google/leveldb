// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Prevent Windows headers from defining min/max macros and instead
// use STL.
#ifndef NOMINMAX
#define NOMINMAX
#endif  // ifndef NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/env_windows_test_helper.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/windows_logger.h"

#if defined(DeleteFile)
#undef DeleteFile
#endif  // defined(DeleteFile)

namespace leveldb {

namespace {

constexpr const size_t kWritableFileBufferSize = 65536;

// Up to 1000 mmaps for 64-bit binaries; none for 32-bit.
constexpr int kDefaultMmapLimit = sizeof(void*) >= 8 ? 1000 : 0;

// Modified by EnvWindowsTestHelper::SetReadOnlyMMapLimit().
int g_mmap_limit = kDefaultMmapLimit;

std::string GetWindowsErrorMessage(DWORD error_code) {
  std::string message;
  char* error_text = nullptr;
  // Use MBCS version of FormatMessage to match return value.
  size_t error_text_size = ::FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&error_text), 0, nullptr);
  if (!error_text) {
    return message;
  }
  message.assign(error_text, error_text_size);
  ::LocalFree(error_text);
  return message;
}

Status WindowsError(const std::string& context, DWORD error_code) {
  if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND)
    return Status::NotFound(context, GetWindowsErrorMessage(error_code));
  return Status::IOError(context, GetWindowsErrorMessage(error_code));
}

class ScopedHandle {
 public:
  ScopedHandle(HANDLE handle) : handle_(handle) {}
  ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.Release()) {}
  ~ScopedHandle() { Close(); }

  ScopedHandle& operator=(ScopedHandle&& rhs) noexcept {
    if (this != &rhs) handle_ = rhs.Release();
    return *this;
  }

  bool Close() {
    if (!is_valid()) {
      return true;
    }
    HANDLE h = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return ::CloseHandle(h);
  }

  bool is_valid() const {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }

  HANDLE get() const { return handle_; }

  HANDLE Release() {
    HANDLE h = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return h;
  }

 private:
  HANDLE handle_;
};

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

class WindowsSequentialFile : public SequentialFile {
 public:
  WindowsSequentialFile(std::string fname, ScopedHandle file)
      : filename_(fname), file_(std::move(file)) {}
  ~WindowsSequentialFile() override {}

  Status Read(size_t n, Slice* result, char* scratch) override {
    Status s;
    DWORD bytes_read;
    // DWORD is 32-bit, but size_t could technically be larger. However leveldb
    // files are limited to leveldb::Options::max_file_size which is clamped to
    // 1<<30 or 1 GiB.
    assert(n <= std::numeric_limits<DWORD>::max());
    if (!::ReadFile(file_.get(), scratch, static_cast<DWORD>(n), &bytes_read,
                    nullptr)) {
      s = WindowsError(filename_, ::GetLastError());
    } else {
      *result = Slice(scratch, bytes_read);
    }
    return s;
  }

  Status Skip(uint64_t n) override {
    LARGE_INTEGER distance;
    distance.QuadPart = n;
    if (!::SetFilePointerEx(file_.get(), distance, nullptr, FILE_CURRENT)) {
      return WindowsError(filename_, ::GetLastError());
    }
    return Status::OK();
  }

 private:
  std::string filename_;
  ScopedHandle file_;
};

class WindowsRandomAccessFile : public RandomAccessFile {
 public:
  WindowsRandomAccessFile(std::string fname, ScopedHandle handle)
      : filename_(fname), handle_(std::move(handle)) {}

  ~WindowsRandomAccessFile() override = default;

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    DWORD bytes_read = 0;
    OVERLAPPED overlapped = {0};

    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.Offset = static_cast<DWORD>(offset);
    if (!::ReadFile(handle_.get(), scratch, static_cast<DWORD>(n), &bytes_read,
                    &overlapped)) {
      DWORD error_code = ::GetLastError();
      if (error_code != ERROR_HANDLE_EOF) {
        *result = Slice(scratch, 0);
        return Status::IOError(filename_, GetWindowsErrorMessage(error_code));
      }
    }

    *result = Slice(scratch, bytes_read);
    return Status::OK();
  }

 private:
  std::string filename_;
  ScopedHandle handle_;
};

class WindowsMmapReadableFile : public RandomAccessFile {
 public:
  // base[0,length-1] contains the mmapped contents of the file.
  WindowsMmapReadableFile(std::string fname, void* base, size_t length,
                          Limiter* limiter)
      : filename_(std::move(fname)),
        mmapped_region_(base),
        length_(length),
        limiter_(limiter) {}

  ~WindowsMmapReadableFile() override {
    ::UnmapViewOfFile(mmapped_region_);
    limiter_->Release();
  }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    Status s;
    if (offset + n > length_) {
      *result = Slice();
      s = WindowsError(filename_, ERROR_INVALID_PARAMETER);
    } else {
      *result = Slice(reinterpret_cast<char*>(mmapped_region_) + offset, n);
    }
    return s;
  }

 private:
  std::string filename_;
  void* mmapped_region_;
  size_t length_;
  Limiter* limiter_;
};

class WindowsWritableFile : public WritableFile {
 public:
  WindowsWritableFile(std::string fname, ScopedHandle handle)
      : filename_(std::move(fname)), handle_(std::move(handle)), pos_(0) {}

  ~WindowsWritableFile() override = default;

  Status Append(const Slice& data) override {
    size_t n = data.size();
    const char* p = data.data();

    // Fit as much as possible into buffer.
    size_t copy = std::min(n, kWritableFileBufferSize - pos_);
    memcpy(buf_ + pos_, p, copy);
    p += copy;
    n -= copy;
    pos_ += copy;
    if (n == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status s = FlushBuffered();
    if (!s.ok()) {
      return s;
    }

    // Small writes go to buffer, large writes are written directly.
    if (n < kWritableFileBufferSize) {
      memcpy(buf_, p, n);
      pos_ = n;
      return Status::OK();
    }
    return WriteRaw(p, n);
  }

  Status Close() override {
    Status result = FlushBuffered();
    if (!handle_.Close() && result.ok()) {
      result = WindowsError(filename_, ::GetLastError());
    }
    return result;
  }

  Status Flush() override { return FlushBuffered(); }

  Status Sync() override {
    // On Windows no need to sync parent directory. It's metadata will be
    // updated via the creation of the new file, without an explicit sync.
    return FlushBuffered();
  }

 private:
  Status FlushBuffered() {
    Status s = WriteRaw(buf_, pos_);
    pos_ = 0;
    return s;
  }

  Status WriteRaw(const char* p, size_t n) {
    DWORD bytes_written;
    if (!::WriteFile(handle_.get(), p, static_cast<DWORD>(n), &bytes_written,
                     nullptr)) {
      return Status::IOError(filename_,
                             GetWindowsErrorMessage(::GetLastError()));
    }
    return Status::OK();
  }

  // buf_[0, pos_-1] contains data to be written to handle_.
  const std::string filename_;
  ScopedHandle handle_;
  char buf_[kWritableFileBufferSize];
  size_t pos_;
};

// Lock or unlock the entire file as specified by |lock|. Returns true
// when successful, false upon failure. Caller should call ::GetLastError()
// to determine cause of failure
bool LockOrUnlock(HANDLE handle, bool lock) {
  if (lock) {
    return ::LockFile(handle,
                      /*dwFileOffsetLow=*/0, /*dwFileOffsetHigh=*/0,
                      /*nNumberOfBytesToLockLow=*/MAXDWORD,
                      /*nNumberOfBytesToLockHigh=*/MAXDWORD);
  } else {
    return ::UnlockFile(handle,
                        /*dwFileOffsetLow=*/0, /*dwFileOffsetHigh=*/0,
                        /*nNumberOfBytesToLockLow=*/MAXDWORD,
                        /*nNumberOfBytesToLockHigh=*/MAXDWORD);
  }
}

class WindowsFileLock : public FileLock {
 public:
  WindowsFileLock(ScopedHandle handle, std::string name)
      : handle_(std::move(handle)), name_(std::move(name)) {}

  ScopedHandle& handle() { return handle_; }
  const std::string& name() const { return name_; }

 private:
  ScopedHandle handle_;
  std::string name_;
};

class WindowsEnv : public Env {
 public:
  WindowsEnv();
  ~WindowsEnv() override {
    static char msg[] = "Destroying Env::Default()\n";
    fwrite(msg, 1, sizeof(msg), stderr);
    abort();
  }

  Status NewSequentialFile(const std::string& fname,
                           SequentialFile** result) override {
    *result = nullptr;
    DWORD desired_access = GENERIC_READ;
    DWORD share_mode = FILE_SHARE_READ;
    ScopedHandle handle =
        ::CreateFileA(fname.c_str(), desired_access, share_mode, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!handle.is_valid()) {
      return WindowsError(fname, ::GetLastError());
    }
    *result = new WindowsSequentialFile(fname, std::move(handle));
    return Status::OK();
  }

  Status NewRandomAccessFile(const std::string& fname,
                             RandomAccessFile** result) override {
    *result = nullptr;
    DWORD desired_access = GENERIC_READ;
    DWORD share_mode = FILE_SHARE_READ;
    DWORD file_flags = FILE_ATTRIBUTE_READONLY;

    ScopedHandle handle =
        ::CreateFileA(fname.c_str(), desired_access, share_mode, nullptr,
                      OPEN_EXISTING, file_flags, nullptr);
    if (!handle.is_valid()) {
      return WindowsError(fname, ::GetLastError());
    }
    if (!mmap_limiter_.Acquire()) {
      *result = new WindowsRandomAccessFile(fname, std::move(handle));
      return Status::OK();
    }

    LARGE_INTEGER file_size;
    if (!::GetFileSizeEx(handle.get(), &file_size)) {
      return WindowsError(fname, ::GetLastError());
    }

    ScopedHandle mapping =
        ::CreateFileMappingA(handle.get(),
                             /*security attributes=*/nullptr, PAGE_READONLY,
                             /*dwMaximumSizeHigh=*/0,
                             /*dwMaximumSizeLow=*/0, nullptr);
    if (mapping.is_valid()) {
      void* base = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
      if (base) {
        *result = new WindowsMmapReadableFile(
            fname, base, static_cast<size_t>(file_size.QuadPart),
            &mmap_limiter_);
        return Status::OK();
      }
    }
    Status s = WindowsError(fname, ::GetLastError());

    if (!s.ok()) {
      mmap_limiter_.Release();
    }
    return s;
  }

  Status NewWritableFile(const std::string& fname,
                         WritableFile** result) override {
    DWORD desired_access = GENERIC_WRITE;
    DWORD share_mode = 0;

    ScopedHandle handle =
        ::CreateFileA(fname.c_str(), desired_access, share_mode, nullptr,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!handle.is_valid()) {
      *result = nullptr;
      return WindowsError(fname, ::GetLastError());
    }

    *result = new WindowsWritableFile(fname, std::move(handle));
    return Status::OK();
  }

  Status NewAppendableFile(const std::string& fname,
                           WritableFile** result) override {
    ScopedHandle handle =
        ::CreateFileA(fname.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!handle.is_valid()) {
      *result = nullptr;
      return WindowsError(fname, ::GetLastError());
    }

    *result = new WindowsWritableFile(fname, std::move(handle));
    return Status::OK();
  }

  bool FileExists(const std::string& fname) override {
    return GetFileAttributesA(fname.c_str()) != INVALID_FILE_ATTRIBUTES;
  }

  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* result) override {
    const std::string find_pattern = dir + "\\*";
    WIN32_FIND_DATAA find_data;
    HANDLE dir_handle = ::FindFirstFileA(find_pattern.c_str(), &find_data);
    if (dir_handle == INVALID_HANDLE_VALUE) {
      DWORD last_error = ::GetLastError();
      if (last_error == ERROR_FILE_NOT_FOUND) {
        return Status::OK();
      }
      return WindowsError(dir, last_error);
    }
    do {
      char base_name[_MAX_FNAME];
      char ext[_MAX_EXT];

      if (!_splitpath_s(find_data.cFileName, nullptr, 0, nullptr, 0, base_name,
                        ARRAYSIZE(base_name), ext, ARRAYSIZE(ext))) {
        result->emplace_back(std::string(base_name) + ext);
      }
    } while (::FindNextFileA(dir_handle, &find_data));
    DWORD last_error = ::GetLastError();
    ::FindClose(dir_handle);
    if (last_error != ERROR_NO_MORE_FILES) {
      return WindowsError(dir, last_error);
    }
    return Status::OK();
  }

  Status DeleteFile(const std::string& fname) override {
    if (!::DeleteFileA(fname.c_str())) {
      return WindowsError(fname, ::GetLastError());
    }
    return Status::OK();
  }

  Status CreateDir(const std::string& name) override {
    if (!::CreateDirectoryA(name.c_str(), nullptr)) {
      return WindowsError(name, ::GetLastError());
    }
    return Status::OK();
  }

  Status DeleteDir(const std::string& name) override {
    if (!::RemoveDirectoryA(name.c_str())) {
      return WindowsError(name, ::GetLastError());
    }
    return Status::OK();
  }

  Status GetFileSize(const std::string& fname, uint64_t* size) override {
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!::GetFileAttributesExA(fname.c_str(), GetFileExInfoStandard, &attrs)) {
      return WindowsError(fname, ::GetLastError());
    }
    ULARGE_INTEGER file_size;
    file_size.HighPart = attrs.nFileSizeHigh;
    file_size.LowPart = attrs.nFileSizeLow;
    *size = file_size.QuadPart;
    return Status::OK();
  }

  Status RenameFile(const std::string& src,
                    const std::string& target) override {
    // Try a simple move first.  It will only succeed when |to_path| doesn't
    // already exist.
    if (::MoveFileA(src.c_str(), target.c_str())) {
      return Status::OK();
    }
    DWORD move_error = ::GetLastError();

    // Try the full-blown replace if the move fails, as ReplaceFile will only
    // succeed when |to_path| does exist. When writing to a network share, we
    // may not be able to change the ACLs. Ignore ACL errors then
    // (REPLACEFILE_IGNORE_MERGE_ERRORS).
    if (::ReplaceFileA(target.c_str(), src.c_str(), nullptr,
                       REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
      return Status::OK();
    }
    DWORD replace_error = ::GetLastError();
    // In the case of FILE_ERROR_NOT_FOUND from ReplaceFile, it is likely
    // that |to_path| does not exist. In this case, the more relevant error
    // comes from the call to MoveFile.
    if (replace_error == ERROR_FILE_NOT_FOUND ||
        replace_error == ERROR_PATH_NOT_FOUND) {
      return WindowsError(src, move_error);
    } else {
      return WindowsError(src, replace_error);
    }
  }

  Status LockFile(const std::string& fname, FileLock** lock) override {
    *lock = nullptr;
    Status result;
    ScopedHandle handle = ::CreateFileA(
        fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        /*lpSecurityAttributes=*/nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (!handle.is_valid()) {
      result = WindowsError(fname, ::GetLastError());
    } else if (!LockOrUnlock(handle.get(), true)) {
      result = WindowsError("lock " + fname, ::GetLastError());
    } else {
      *lock = new WindowsFileLock(std::move(handle), std::move(fname));
    }
    return result;
  }

  Status UnlockFile(FileLock* lock) override {
    std::unique_ptr<WindowsFileLock> my_lock(
        reinterpret_cast<WindowsFileLock*>(lock));
    Status result;
    if (!LockOrUnlock(my_lock->handle().get(), false)) {
      result = WindowsError("unlock", ::GetLastError());
    }
    return result;
  }

  void Schedule(void (*function)(void*), void* arg) override;

  void StartThread(void (*function)(void* arg), void* arg) override {
    std::thread t(function, arg);
    t.detach();
  }

  Status GetTestDirectory(std::string* result) override {
    const char* env = getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
      *result = env;
      return Status::OK();
    }

    char tmp_path[MAX_PATH];
    if (!GetTempPathA(ARRAYSIZE(tmp_path), tmp_path)) {
      return WindowsError("GetTempPath", ::GetLastError());
    }
    std::stringstream ss;
    ss << tmp_path << "leveldbtest-" << std::this_thread::get_id();
    *result = ss.str();

    // Directory may already exist
    CreateDir(*result);
    return Status::OK();
  }

  Status NewLogger(const std::string& filename, Logger** result) override {
    std::FILE* fp = std::fopen(filename.c_str(), "w");
    if (fp == nullptr) {
      *result = nullptr;
      return WindowsError("NewLogger", ::GetLastError());
    } else {
      *result = new WindowsLogger(fp);
      return Status::OK();
    }
  }

  uint64_t NowMicros() override {
    // GetSystemTimeAsFileTime typically has a resolution of 10-20 msec.
    // TODO(cmumford): Switch to GetSystemTimePreciseAsFileTime which is
    // available in Windows 8 and later.
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    // Each tick represents a 100-nanosecond intervals since January 1, 1601
    // (UTC).
    uint64_t num_ticks =
        (static_cast<uint64_t>(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
    return num_ticks / 10;
  }

  void SleepForMicroseconds(int micros) override {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }

 private:
  // BGThread() is the body of the background thread
  void BGThread();

  std::mutex mu_;
  std::condition_variable bgsignal_;
  bool started_bgthread_;

  // Entry per Schedule() call
  struct BGItem {
    void* arg;
    void (*function)(void*);
  };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;

  Limiter mmap_limiter_;
};

// Return the maximum number of concurrent mmaps.
int MaxMmaps() {
  if (g_mmap_limit >= 0) {
    return g_mmap_limit;
  }
  // Up to 1000 mmaps for 64-bit binaries; none for smaller pointer sizes.
  g_mmap_limit = sizeof(void*) >= 8 ? 1000 : 0;
  return g_mmap_limit;
}

WindowsEnv::WindowsEnv()
    : started_bgthread_(false), mmap_limiter_(MaxMmaps()) {}

void WindowsEnv::Schedule(void (*function)(void*), void* arg) {
  std::lock_guard<std::mutex> guard(mu_);

  // Start background thread if necessary
  if (!started_bgthread_) {
    started_bgthread_ = true;
    std::thread t(&WindowsEnv::BGThread, this);
    t.detach();
  }

  // If the queue is currently empty, the background thread may currently be
  // waiting.
  if (queue_.empty()) {
    bgsignal_.notify_one();
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;
}

void WindowsEnv::BGThread() {
  while (true) {
    // Wait until there is an item that is ready to run
    std::unique_lock<std::mutex> lk(mu_);
    bgsignal_.wait(lk, [this] { return !queue_.empty(); });

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    lk.unlock();
    (*function)(arg);
  }
}

}  // namespace

static std::once_flag once;
static Env* default_env;
static void InitDefaultEnv() { default_env = new WindowsEnv(); }

void EnvWindowsTestHelper::SetReadOnlyMMapLimit(int limit) {
  assert(default_env == nullptr);
  g_mmap_limit = limit;
}

Env* Env::Default() {
  std::call_once(once, InitDefaultEnv);
  return default_env;
}

}  // namespace leveldb
