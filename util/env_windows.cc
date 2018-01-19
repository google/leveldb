// Copyright (c) 2017 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <io.h>
#include <stdio.h>
#include <wchar.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <thread>
#include "leveldb/env.h"
#include "port/port.h"
#include "util/mutexlock.h"
#include "util/file_logger.h"
#include "util/env_windows_test_helper.h"

// Win32 defines the DeleteFile macro
// Include last, and immediately undefine macro.
#include <windows.h>
#ifdef DeleteFile
#undef DeleteFile
#endif

namespace leveldb {

namespace {

static int open_read_only_file_limit = -1;
static int mmap_limit = -1;
static port::Mutex pread_mutex;

static Status IOError(const std::string& context, int err_number) {
  return Status::IOError(context, strerror(err_number));
}

static Status WindowsError(const std::string & context, DWORD err_number)
{
  std::string err_mess;
  LPSTR text = NULL;
  if (!::FormatMessageA(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
    0,
    err_number,
    0,
    (LPTSTR)&text,
    32000,
    0
  )) {
    err_mess = "unknown error";
  } else {
    err_mess = text;
    ::LocalFree(text);
  }

  // both error codes map to ENOENT
  if (err_number == ERROR_FILE_NOT_FOUND ||
      err_number == ERROR_PATH_NOT_FOUND) {
    return Status::NotFound(context, err_mess);
  } else {
    return Status::IOError(context, err_mess);
  }
}


std::vector<char> Utf16ToUtf8(const std::wstring& wide)
{
  if (wide.empty()) {
    return std::vector<char>();
  }
  int size = WideCharToMultiByte(
    CP_UTF8,
    0,
    &wide[0],
    wide.size(),
    NULL,
    0,
    NULL,
    NULL
  );
  std::vector<char> buffer(size+1);
  buffer[size] = '\0';
  WideCharToMultiByte(CP_ACP, 0, &wide[0], wide.size(), &buffer[0], size, NULL, NULL);
  return buffer;
  }


std::vector<wchar_t> Utf8ToUtf16(const std::string& narrow)
{
  if (narrow.empty()) {
    return std::vector<wchar_t>();
  }
  int size = MultiByteToWideChar(
    CP_ACP,
    0,
    &narrow[0],
    narrow.size(),
    NULL,
    0
  );
  std::vector<wchar_t> buffer(size+1);
  buffer[size] = L'\0';
  MultiByteToWideChar(CP_UTF8, 0, &narrow[0], narrow.size(), &buffer[0], size);
  return buffer;
}

// Helper class to limit resource usage to avoid exhaustion.
// Currently used to limit read-only file descriptors and mmap file usage
// so that we do not end up running out of file descriptors, virtual memory,
// or running into kernel performance problems for very large databases.
class Limiter {
 public:
  // Limit maximum number of resources to |n|.
  Limiter(intptr_t n) {
    SetAllowed(n);
  }

  // If another resource is available, acquire it and return true.
  // Else return false.
  bool Acquire() {
    if (GetAllowed() <= 0) {
      return false;
    }
    MutexLock l(&mu_);
    intptr_t x = GetAllowed();
    if (x <= 0) {
      return false;
    } else {
      SetAllowed(x - 1);
      return true;
    }
  }

  // Release a resource acquired by a previous call to Acquire() that returned
  // true.
  void Release() {
    MutexLock l(&mu_);
    SetAllowed(GetAllowed() + 1);
  }

 private:
  port::Mutex mu_;
  port::AtomicPointer allowed_;

  intptr_t GetAllowed() const {
    return reinterpret_cast<intptr_t>(allowed_.Acquire_Load());
  }

  // REQUIRES: mu_ must be held
  void SetAllowed(intptr_t v) {
    allowed_.Release_Store(reinterpret_cast<void*>(v));
  }

  Limiter(const Limiter&);
  void operator=(const Limiter&);
};


class WindowsSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  HANDLE handle_;

 public:
  WindowsSequentialFile(const std::string& fname, HANDLE handle)
      : filename_(fname), handle_(handle) {}
  virtual ~WindowsSequentialFile() { CloseHandle(handle_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    DWORD r = 0;
    if (!::ReadFile(handle_, scratch, n, &r, NULL)) {
        return WindowsError(filename_, GetLastError());
    }
    *result = Slice(scratch, r);
    return Status::OK();
  }

  virtual Status Skip(uint64_t n) {
    LARGE_INTEGER bytes;
    bytes.QuadPart = n;
    if (!::SetFilePointerEx(handle_, bytes, NULL, FILE_BEGIN)) {
        return WindowsError(filename_, GetLastError());
    }
    return Status::OK();
  }
};


class WindowsRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  bool temporary_handle_;  // If true, we open on every read.
  HANDLE handle_;
  Limiter* limiter_;

 public:
  WindowsRandomAccessFile(const std::string& fname, HANDLE handle, Limiter* limiter)
      : filename_(fname), handle_(handle), limiter_(limiter) {
    temporary_handle_ = !limiter->Acquire();
    if (temporary_handle_) {
      // Open file on every access.
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

  virtual ~WindowsRandomAccessFile() {
    if (!temporary_handle_) {
      CloseHandle(handle_);
      limiter_->Release();
    }
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    HANDLE handle = handle_;
    if (temporary_handle_) {
      // Create temporary handle.
      handle = CreateFileW(
        Utf8ToUtf16(filename_).data(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_RANDOM_ACCESS,
        NULL
      );
      if (handle == INVALID_HANDLE_VALUE) {
        return WindowsError(filename_, GetLastError());
      }
    }

    pread_mutex.Lock();

    // Seek offset
    Status s = Status::OK();
    LARGE_INTEGER in, out;
    in.QuadPart = offset;
    if (!SetFilePointerEx(handle, in, &out, FILE_BEGIN)) {
      s = WindowsError(filename_, GetLastError());
    }

    // read from file
    DWORD r = 0;
    if (s.ok() && !::ReadFile(handle, scratch, n, &r, NULL)) {
      s = WindowsError(filename_, GetLastError());
    }

    pread_mutex.Unlock();

    *result = Slice(scratch, r);
    if (temporary_handle_) {
      // Close the temporary file descriptor opened earlier.
      CloseHandle(handle);
    }
    return s;
  }
};


class WindowsMmapReadableFile: public RandomAccessFile
{
 private:
  std::string filename_;
  void* mmapped_region_;
  size_t length_;
  Limiter* limiter_;
  HANDLE filemap_;

 public:
  // base[0,length-1] contains the mmapped contents of the file.
  WindowsMmapReadableFile(const std::string& fname, void* base, size_t length,
                          HANDLE filemap, Limiter* limiter)
      : filename_(fname), mmapped_region_(base), length_(length),
        limiter_(limiter), filemap_(filemap) {
  }

  virtual ~WindowsMmapReadableFile() {
    ::UnmapViewOfFile(mmapped_region_);
    CloseHandle(filemap_);
    limiter_->Release();
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status s;
    if (offset + n > length_) {
      *result = Slice();
      s = WindowsError(filename_, ERROR_INVALID_PARAMETER);
    } else {
      *result = Slice(reinterpret_cast<char*>(mmapped_region_) + offset, n);
    }
    return s;
  }
};


class WindowsWritableFile : public WritableFile {
 private:
  std::string filename_;
  HANDLE handle_;

 public:
  WindowsWritableFile(const std::string& fname, HANDLE handle)
      : filename_(fname), handle_(handle) { }

  virtual ~WindowsWritableFile() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      // Ignoring any potential errors
      CloseHandle(handle_);
    }
  }

  virtual Status Append(const Slice& data) {
    DWORD wrote = 0;
    if (!::WriteFile(handle_, data.data(), data.size(), &wrote, NULL)) {
      return WindowsError(filename_, GetLastError());
    }
    if (wrote != data.size()) {
      return Status::IOError(filename_, "Could not write entire buffer to disk");
    }
    return Status::OK();
  }

  virtual Status Close() {
    Status result;
    if (!::CloseHandle(handle_)) {
      result = WindowsError(filename_, GetLastError());
    }
    handle_ = INVALID_HANDLE_VALUE;
    return result;
  }

  virtual Status Flush() {
    if (!::FlushFileBuffers(handle_)) {
      return WindowsError(filename_, GetLastError());
    }
    return Status::OK();
  }

  Status SyncDirIfManifest() {
    // Windows does not have the concept of flushing a diectory.
    // Although you can create a HANDLE to a directory with
    // CreateFile using FILE_FLAG_BACKUP_SEMANTICS, you cannot
    // call FileFlushBuffers() or any other data syncing routine.
    return Status::OK();
  }

  virtual Status Sync() {
    // Ensure new files referred to by the manifest are in the filesystem.
    Status s = SyncDirIfManifest();
    if (!s.ok()) {
      return s;
    }
    return Flush();
  }
};


static std::streamsize file_length(HANDLE handle)
{
  LARGE_INTEGER bytes;
  if (!::GetFileSizeEx(handle, &bytes)) {
      throw WindowsError("", GetLastError());
  }
  return static_cast<std::streamsize>(bytes.QuadPart);
}


static int LockOrUnlock(HANDLE handle, bool lock)
{
  LARGE_INTEGER bytes;
  bytes.QuadPart = file_length(handle);
  if (lock) {
    return ::LockFile(handle, 0, 0, bytes.LowPart, bytes.HighPart) ? 0 : -1;
  } else {
    return ::UnlockFile(handle, 0, 0, bytes.LowPart, bytes.HighPart) ? 0 : -1;
  }
}


class WindowsFileLock : public FileLock {
 public:
  HANDLE handle_;
  std::string name_;
};

// Set of locked files.  We keep a separate set instead of just
// relying on fcntrl(F_SETLK) since fcntl(F_SETLK) does not provide
// any protection against multiple uses from the same process.
class WindowsLockTable {
 private:
  port::Mutex mu_;
  std::set<std::string> locked_files_;
 public:
  bool Insert(const std::string& fname) {
    MutexLock l(&mu_);
    return locked_files_.insert(fname).second;
  }
  void Remove(const std::string& fname) {
    MutexLock l(&mu_);
    locked_files_.erase(fname);
  }
};


class WindowsEnv : public Env
{
 public:
  WindowsEnv();
  virtual ~WindowsEnv() {
    if (bgthread_) {
      bgthread_->join();
    }
    mu_.Lock();
    bgthread_.reset();
    mu_.Unlock();

    char msg[] = "Destroying Env::Default()\n";
    fwrite(msg, 1, sizeof(msg), stderr);
    abort();
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    HANDLE handle = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_READ,
      // Use shared read/write since corruption_test requires
      // access to a concurrently open logger, opened with shared
      // "w+" access.
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_SEQUENTIAL_SCAN,
      NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
      *result = NULL;
      return WindowsError(fname, GetLastError());
    } else {
      *result = new WindowsSequentialFile(fname, handle);
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) {
    *result = NULL;
    Status s;
    HANDLE handle = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_READ,
      // Use shared read/write since corruption_test requires
      // access to a concurrently open logger, opened with shared
      // "w+" access.
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_RANDOM_ACCESS,
      NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
      s = WindowsError(fname, GetLastError());
    } else if (mmap_limit_.Acquire()) {
      uint64_t size;
      // Windows defines GetFileSize and GetFileSizeEx.
      s = this->GetFileSize(fname, &size);
      if (s.ok()) {
        // map file to memory
        DWORD low = static_cast<DWORD>(size & 0xFFFFFFFFL);
        DWORD high = static_cast<DWORD>((size >> 32) & 0xFFFFFFFFL);
        HANDLE filemap = CreateFileMapping(handle, NULL, PAGE_READONLY, high, low, NULL);
        void* base = NULL;
        if (filemap == NULL) {
          s = WindowsError(fname, GetLastError());
        } else {
          base = MapViewOfFile(
            filemap,
            FILE_MAP_READ,
            0,
            0,
            size
          );
        }

        if (base != NULL) {
          *result = new WindowsMmapReadableFile(fname, base, size, filemap, &mmap_limit_);
        } else {
          CloseHandle(filemap);
          s = WindowsError(fname, GetLastError());
        }
      }
      CloseHandle(handle);
      if (!s.ok()) {
        mmap_limit_.Release();
      }
    } else {
      *result = new WindowsRandomAccessFile(fname, handle, &fd_limit_);
    }
    return s;
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) {
    HANDLE h = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_WRITE,
      // Use shared read/write since corruption_test requires
      // access to a concurrently open logger, opened with shared
      // "w+" access.
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );

    if (h == INVALID_HANDLE_VALUE) {
      *result = NULL;
      return WindowsError(fname, GetLastError());
    } else {
      *result = new WindowsWritableFile(fname, h);
      return Status::OK();
    }
  }

  virtual Status NewAppendableFile(const std::string& fname,
                                   WritableFile** result) {
    HANDLE h = CreateFileW(
      Utf8ToUtf16(fname).data(),
      FILE_APPEND_DATA,
      // Use shared read/write since corruption_test requires
      // access to a concurrently open logger, opened with shared
      // "w+" access.
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
      *result = NULL;
      return WindowsError(fname, GetLastError());
    } else {
      *result = new WindowsWritableFile(fname, h);
      return Status::OK();
    }
  }

  virtual bool FileExists(const std::string& fname) {
    return _waccess(Utf8ToUtf16(fname).data(), 0) == 0;
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) {
    result->clear();

    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(Utf8ToUtf16(dir + "/*").data(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
      return WindowsError(dir, GetLastError());
    }
    while (FindNextFileW(handle, &data) != 0) {
      std::vector<char> v = Utf16ToUtf8(data.cFileName);
      if (!(strcmp(v.data(), ".") == 0 || strcmp(v.data(), "..") == 0)) {
        result->push_back(std::string(v.data(), v.size()-1));
      }
    }
    FindClose(handle);
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    Status result;
    if (!DeleteFileW(Utf8ToUtf16(fname).data())) {
      return WindowsError(fname, GetLastError());
    }
    return result;
  }

  virtual Status CreateDir(const std::string& name) {
    Status result;
    if (!CreateDirectoryW(Utf8ToUtf16(name).data(), nullptr)) {
      result = WindowsError(name, GetLastError());
    }
    return result;
  }

  virtual Status DeleteDir(const std::string& name) {
    Status result;
    if (!RemoveDirectoryW(Utf8ToUtf16(name).data())) {
      result = WindowsError(name, GetLastError());
    }
    return result;
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
    Status s;
    HANDLE handle = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
      *size = 0;
      s = WindowsError(fname, GetLastError());
    } else {
      LARGE_INTEGER filesize;
      if (!::GetFileSizeEx(handle, &filesize)) {
        s = WindowsError(fname, GetLastError());
      } else {
        *size = filesize.QuadPart;
      }
      CloseHandle(handle);
    }
    return s;
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    Status result;
    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING;
    std::vector<wchar_t> s = Utf8ToUtf16(src);
    std::vector<wchar_t> t = Utf8ToUtf16(target);
    if (!MoveFileExW(s.data(), t.data(), flags)) {
      DWORD error = GetLastError();
      // Force a copy/delete operation as a fake rename if unable
      // to because of existing open HANDLES.
      // Note to maintainers: this logic should not have to exist, since
      // we explicitly open all file handles with `FILE_SHARE_DELETE`,
      // which should allow `MoveFile` operations, and pass
      // `MOVEFILE_REPLACE_EXISTING` to ensure existing files are
      // overwritten. However, `FaultInjectionTest.FaultTestWithLogReuse`
      // fails during the `Truncate` method if this logic is not provided
      // on WINE, so we provide this fallthrough.
      if (error == ERROR_SHARING_VIOLATION) {
        if (!(CopyFileW(s.data(), t.data(), false) && DeleteFileW(s.data()))) {
          result = WindowsError(src, GetLastError());
        }
      } else {
        result = WindowsError(src, error);
      }
    }
    return result;
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;
    Status result;
    HANDLE handle = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      NULL,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
      result = WindowsError(fname, GetLastError());
    } else if (!locks_.Insert(fname)) {
      CloseHandle(handle);
      result = Status::IOError("lock " + fname, "already held by process");
    } else if (LockOrUnlock(handle, true) == -1) {
      result = WindowsError("lock " + fname, GetLastError());
      CloseHandle(handle);
      locks_.Remove(fname);
    } else {
      WindowsFileLock* my_lock = new WindowsFileLock;
      my_lock->handle_ = handle;
      my_lock->name_ = fname;
      *lock = my_lock;
    }
    return result;
  }

  virtual Status UnlockFile(FileLock* lock) {
    WindowsFileLock* my_lock = reinterpret_cast<WindowsFileLock*>(lock);
    Status result;
    if (LockOrUnlock(my_lock->handle_, false) == -1) {
      result = WindowsError("unlock", GetLastError());
    }
    locks_.Remove(my_lock->name_);
    CloseHandle(my_lock->handle_);
    delete my_lock;
    return result;
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual Status GetTestDirectory(std::string* result) {
    const char* env = getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
      *result = env;
    } else {
      char buf[256];
      int size = snprintf(buf, sizeof(buf), "%s/leveldbtest-%d", getenv("TMP"), int(GetCurrentProcessId()));
      *result = std::string(buf, size);
    }
    // Directory may already exist
    CreateDir(*result);
    return Status::OK();
  }

  static uint64_t gettid() {
    return GetCurrentThreadId();
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    HANDLE h = CreateFileW(
      Utf8ToUtf16(fname).data(),
      GENERIC_READ | GENERIC_WRITE,
      // Use shared read/write since corruption_test requires
      // access to a concurrently open logger, opened with shared
      // "wb+" access.
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
      *result = NULL;
      return WindowsError(fname, GetLastError());
    } else {
      // get file descriptor
      int fd = _open_osfhandle((intptr_t) h, 0);
      if (fd == - 1) {
        CloseHandle(h);
        *result = NULL;
        return WindowsError(fname, GetLastError());
      }

      // get FILE*
      FILE* f = _fdopen(fd, "wb+");
      if (f == NULL) {
        _close(fd);
        *result = NULL;
        return WindowsError(fname, GetLastError());
      }
      *result = new FileLogger(f, &WindowsEnv::gettid);
      return Status::OK();
    }
  }

  virtual uint64_t NowMicros() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
  }

  virtual void SleepForMicroseconds(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }

private:

  // BGThread() is the body of the background thread
  void BGThread();
  static DWORD __stdcall BGThreadWrapper(void* arg) {
    reinterpret_cast<WindowsEnv*>(arg)->BGThread();
    return 0;
  }

  port::Mutex mu_;
  port::CondVar bgsignal_;
  std::unique_ptr<std::thread> bgthread_;
  std::atomic<bool> started_bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;

  WindowsLockTable locks_;
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

  // Allow use of 20% of available file descriptors for read-only files.
  open_read_only_file_limit = _getmaxstdio() / 5;

  return open_read_only_file_limit;
}

WindowsEnv::WindowsEnv()
    : bgsignal_(&mu_),
      started_bgthread_(false),
      mmap_limit_(MaxMmaps()),
      fd_limit_(MaxOpenFiles()) {
}

void WindowsEnv::Schedule(void (*function)(void*), void* arg) {
  MutexLock l(&mu_);

  // Start background thread if necessary
  if (!started_bgthread_) {
    started_bgthread_ = true;
    bgthread_.reset(new std::thread(std::bind(&WindowsEnv::BGThreadWrapper, this)));
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
}

void WindowsEnv::BGThread() {
  while (started_bgthread_) {
    // Wait until there is an item that is ready to run
    mu_.Lock();
    while (queue_.empty() && started_bgthread_) {
      bgsignal_.Wait();
    }

    if (!started_bgthread_) {
      break;
    }

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    mu_.Unlock();
    (*function)(arg);
  }
}

namespace {
struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
};
}
static DWORD __stdcall StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  delete state;
  return 0;
}

void WindowsEnv::StartThread(void (*function)(void* arg), void* arg) {
  StartThreadState* state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  std::thread t(std::bind(&StartThreadWrapper, state));
  t.detach();
}

}   // namespace

static std::once_flag once;
static Env* default_env;
static void InitDefaultEnv() { default_env = new WindowsEnv; }

char* FileLogger::AppendDateTo(char* first, char* last)
{
  const uint64_t thread_id = (*gettid_)();

  SYSTEMTIME st;
  GetLocalTime(&st);
  first += snprintf(first, last - first,
                "%04d/%02d/%02d-%02d:%02d:%02d.%03d %llx ",
                st.wYear,
                st.wMonth,
                st.wDay,
                st.wHour,
                st.wMinute,
                st.wSecond,
                st.wMilliseconds,  // No microsecond precision, use milliseconds
                static_cast<long long unsigned int>(thread_id));

  return first;
}

void EnvWindowsTestHelper::SetReadOnlyFDLimit(int limit) {
  assert(default_env == NULL);
  open_read_only_file_limit = limit;
}

void EnvWindowsTestHelper::SetReadOnlyMMapLimit(int limit) {
  assert(default_env == NULL);
  mmap_limit = limit;
}

Env* Env::Default() {
  port::InitOnce(&once, InitDefaultEnv);
  return default_env;
}

}  // namespace leveldb
