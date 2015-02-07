#include <stdio.h>
#include <string.h>
#include <deque>
#include <process.h>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "port/port_win.h"
#include "util/logging.h"
#include "util/win_logger.h"

// To properly support file names on Windows we should be using Unicode
// (WCHAR) strings. To accomodate existing interface which uses std::string,
// we use the following convention:
// * all filenames that we return (e.g. from GetTestDirectory()) are
//   utf8-encoded
// * we'll try to interpret all input file names as if they're
//   utf8-encoded. If they're not valid utf8 strings, we'll try
//   to interpret them according to a current code page
// This just works for names that don't use characters outside ascii
// and for those that do, the caller needs to be aware of this convention
// whenever it bubbles up to the user-level API.

namespace leveldb {

static Status IOError(const std::string& context, DWORD err = (DWORD)-1) {
  char *err_msg = NULL;
  Status s;
  if ((DWORD)-1 == err)
    err = GetLastError();
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&err_msg, 0, NULL);
    if (!err_msg) 
        return Status::IOError(context);
    s = Status::IOError(context, err_msg);
    LocalFree(err_msg);
    return s;
}

class WinSequentialFile: public SequentialFile {
private:
  std::string fname_;
  HANDLE file_;

public:
  WinSequentialFile(const std::string& fname, HANDLE f)
      : fname_(fname), file_(f) { }
  virtual ~WinSequentialFile() { CloseHandle(file_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    DWORD n2 = n;
    DWORD r = 0;
    BOOL ok = ReadFile(file_, (void*)scratch, n2, &r, NULL);
    *result = Slice(scratch, r);
    if (!ok) {
        // We leave status as ok if we hit the end of the file
        if (GetLastError() != ERROR_HANDLE_EOF) {
            return IOError(fname_);
        }
    }
    return Status::OK();
  }

  virtual Status Skip(uint64_t n) {
    LARGE_INTEGER pos;
    pos.QuadPart = n;
    DWORD res = SetFilePointerEx(file_, pos, NULL, FILE_CURRENT);
    if (res == 0)
        return IOError(fname_);
    return Status::OK();
  }
};

class WinRandomAccessFile: public RandomAccessFile {
 private:
  std::string fname_;
  HANDLE file_;

 public:
  WinRandomAccessFile(const std::string& fname, HANDLE file)
      : fname_(fname), file_(file) { }
  virtual ~WinRandomAccessFile() { CloseHandle(file_); }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    OVERLAPPED overlapped = { 0 };
    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD bytes_read = 0;
    BOOL success = ReadFile(file_, scratch, n, &bytes_read, &overlapped);
    *result = Slice(scratch, bytes_read);
    return success != FALSE ? Status::OK() : Status::IOError(fname_);
  }
};

class WinWritableFile : public WritableFile {
private:
  std::string name_;
  HANDLE file_;

public:
  WinWritableFile(std::string name, HANDLE h) : name_(name), file_(h) {
  }

  virtual ~WinWritableFile() {
    Close();
  }

  virtual Status Append(const Slice& data) {
    DWORD n = data.size();
    DWORD pos = 0;
    while (pos < n) {
      DWORD written = 0;
      BOOL ok = WriteFile(file_,  data.data() + pos, n - pos, &written, NULL);
      if (!ok)
        return IOError(name_+ "Append: cannot write");
      pos += written;
    }
    return Status::OK();
  }

  virtual Status Close() {
    if (INVALID_HANDLE_VALUE == file_)
      return Status::OK();
    Status s = Sync();
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    return s;
  }

  virtual Status Flush() {
    return Status::OK();
  }

  virtual Status Sync() {
    BOOL ok = FlushFileBuffers(file_);
    if (!ok)
      return IOError(name_);
    return Status::OK();
  }
};

namespace {

#define DIR_SEP_CHAR L'\\'
#define DIR_SEP_STR L"\\"

WCHAR *ToWcharFromCodePage(const char *src, UINT cp) {
  int required_buf_size = MultiByteToWideChar(cp, 0, src, -1, NULL, 0);
  if (0 == required_buf_size) // indicates an error
    return NULL;
  WCHAR *res = reinterpret_cast<WCHAR*>(malloc(sizeof(WCHAR) * required_buf_size));
  if (!res)
    return NULL;
  MultiByteToWideChar(cp, 0, src, -1, res, required_buf_size);
  return res;
}

// try to convert to WCHAR string trying most common code pages
// to be as permissive as we can be
WCHAR *ToWcharPermissive(const char *s) {
  WCHAR *ws = ToWcharFromCodePage(s, CP_UTF8);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_ACP);
  if (ws != NULL)
    return ws;
  ws = ToWcharFromCodePage(s, CP_OEMCP);
  return ws;
}

char *ToUtf8(const WCHAR *s) {
    int required_buf_size = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *res = (char*)malloc(sizeof(char) * required_buf_size);
    if (!res)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, res, required_buf_size, NULL, NULL);
    return res;
}

static size_t WstrLen(const WCHAR *s) {
    if (NULL == s)
        return 0;
    return wcslen(s);
}

static WCHAR *WstrJoin(const WCHAR *s1, const WCHAR *s2, const WCHAR *s3=NULL) {
    size_t s1_len = WstrLen(s1);
    size_t s2_len = WstrLen(s2);
    size_t s3_len = WstrLen(s3);
    size_t len =s1_len + s2_len + s3_len + 1;
    WCHAR *res = (WCHAR*)malloc(sizeof(WCHAR) * len);
    if (!res)
        return NULL;
    WCHAR *tmp = res;
    if (s1 != NULL) {
        memcpy(tmp, s1, s1_len * sizeof(WCHAR));
        tmp += s1_len;
    }
    if (s2 != NULL) {
        memcpy(tmp, s2, s2_len * sizeof(WCHAR));
        tmp += s2_len;
    }
    if (s3 != NULL) {
        memcpy(tmp, s3, s3_len * sizeof(WCHAR));
        tmp += s3_len;
    }
    *tmp = 0;
    return res;
}

static bool WstrEndsWith(const WCHAR *s1, WCHAR c) {
    size_t len = WstrLen(s1);
    return ((len > 0) && (s1[len-1] == c));
}

static WCHAR *WstrPathJoin(const WCHAR *s1, const WCHAR *s2) {
    if (WstrEndsWith(s1, DIR_SEP_CHAR))
        return WstrJoin(s1, s2);
    return WstrJoin(s1, DIR_SEP_STR, s2);
}

// Return true if s is "." or "..", which are 2 directories
// we should skip when enumerating a directory
static bool SkipDir(const WCHAR *s) {
    if (*s == L'.') {
      if (s[1] == 0)
        return true;
      return ((s[1] == '.') && (s[2] == 0));
    }
    return false;
}

class WinFileLock : public FileLock {
 public:
  WinFileLock(const std::string &fname, HANDLE file)
    : fname_(fname), file_(file) {
  }

  virtual ~WinFileLock() {
    Close();
  }

  bool Close() {
    bool ok = true;
    if (file_ != INVALID_HANDLE_VALUE)
      ok = (CloseHandle(file_) != FALSE);
    file_ = INVALID_HANDLE_VALUE;
    return ok;
  }

  std::string fname_;
  HANDLE file_;
};

class WinEnv : public Env {
 public:
  WinEnv();
  virtual ~WinEnv() {
    fprintf(stderr, "Destroying Env::Default()\n");
    //exit(1);
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    *result = NULL;
    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)file_name);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinSequentialFile(fname, h);
    return Status::OK();
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                   RandomAccessFile** result) {
    *result = NULL;
    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)file_name);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinRandomAccessFile(fname, h);
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                 WritableFile** result) {
    *result = NULL;
    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL)
      return Status::InvalidArgument("Invalid file name");
    HANDLE h = CreateFileW(file_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)file_name);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError(fname);
    }
    *result = new WinWritableFile(fname, h);
    return Status::OK();
  }

  virtual bool FileExists(const std::string& fname) {
    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL)
        return false;

    WIN32_FILE_ATTRIBUTE_DATA   file_info;
    BOOL res = GetFileAttributesExW(file_name, GetFileExInfoStandard, &file_info);
    free((void*)file_name);
    if (0 == res)
        return false;

    if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;
    return true;
  }

  virtual Status GetChildren(const std::string& dir,
               std::vector<std::string>* result) {
    result->clear();
    WCHAR *dir_name = ToWcharPermissive(dir.c_str());
    if (dir_name == NULL)
      return Status::InvalidArgument("Invalid file name");
    WCHAR *pattern = WstrPathJoin(dir_name, L"*");
    free(dir_name);
    if (NULL == pattern)
      return Status::InvalidArgument("Invalid file name");
    WIN32_FIND_DATAW file_data;
    HANDLE h = FindFirstFileW(pattern, &file_data);
    free(pattern);
    if (INVALID_HANDLE_VALUE == h) {
        if (ERROR_FILE_NOT_FOUND == GetLastError())
          return Status::OK();
        return IOError(dir);
    }
    for (;;) {
        WCHAR *s = file_data.cFileName;
        if (!SkipDir(s)) {
            char *s2 = ToUtf8(s);
            result->push_back(s2);
            free(s2);
        }
        if (FALSE == FindNextFileW(h, &file_data))
            break;
    }
    FindClose(h);
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    WCHAR *file_path = ToWcharPermissive(fname.c_str());
    if (file_path == NULL)
        return Status::InvalidArgument("Invalid file name");

    BOOL ok = DeleteFileW(file_path);
    free(file_path);
    if (!ok) {
        DWORD err = GetLastError();
        if ((ERROR_PATH_NOT_FOUND == err) || (ERROR_FILE_NOT_FOUND == err))
          return Status::OK();
      return IOError("DeleteFile " + fname);
    }
    return Status::OK();
  }

  bool CreateDirIfNotExists(const WCHAR *dir) {
    BOOL ok = CreateDirectoryW(dir, NULL);
    if (ok)
      return true;
    return (ERROR_ALREADY_EXISTS == GetLastError());
  }

  bool DirExists(const WCHAR *dir) {
    WIN32_FILE_ATTRIBUTE_DATA   file_info;
    BOOL res = GetFileAttributesExW(dir, GetFileExInfoStandard, &file_info);
    if (0 == res)
        return false;

    return (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  }

  WCHAR *WstrDupN(const WCHAR *s, size_t len) {
      void *res = malloc((len + 1) * sizeof(WCHAR));
      if (!res)
          return NULL;
      memcpy(res, s, len * sizeof(WCHAR));
      WCHAR *res2 = reinterpret_cast<WCHAR*>(res);
      res2[len] = 0;
      return res2;
  }

  bool IsPathSep(WCHAR c) {
      return (c == '\\') || (c == '/');
  }

  WCHAR *GetPathParent(const WCHAR *path) {
      const WCHAR *last_sep = NULL;
      const WCHAR *tmp = path;
      // find the last path separator 
      // (ignoring one at the end of the string)
      while (*tmp) {
          if (IsPathSep(*tmp)) {
              if (0 != tmp[1])
                  last_sep = tmp;
          }
          ++tmp;
      }
      if (NULL == last_sep)
          return NULL;
      size_t len = last_sep - path;
      return WstrDupN(path, len);
  }

  bool CreateDirRecursive(WCHAR *dir) {
    WCHAR *parent = GetPathParent(dir);
    bool ok = true;
    if (parent && !DirExists(parent)) {
        ok = CreateDirRecursive(parent);
    }
    free(parent);
    if (!ok)
        return false;
    return CreateDirIfNotExists(dir);
  }

  virtual Status CreateDir(const std::string& name) {
    WCHAR *dir = ToWcharPermissive(name.c_str());
    if (dir == NULL)
      return Status::InvalidArgument("Invalid file name");
    bool ok = CreateDirRecursive(dir);
    free(dir);
    if (!ok)
        return IOError(name);
    return Status::OK();
  }

#if 1
  virtual Status DeleteDir(const std::string& name) {
    WCHAR *dir = ToWcharPermissive(name.c_str());
    if (dir == NULL)
      return Status::InvalidArgument("Invalid file name");
    BOOL ok = RemoveDirectoryW(dir);
    free(dir);
    if (!ok)
        return IOError(name);
    return Status::OK();
  }
#else
  virtual Status DeleteDir(const std::string& dirname) {
    WCHAR *dir = ToWcharPermissive(dirname.c_str());
    if (dir == NULL)
      return Status::InvalidArgument("Invalid file name");

    SHFILEOPSTRUCTW fileop = { 0 };
    fileop.wFunc = FO_DELETE;
    fileop.pFrom = (const WCHAR*)dir;
    fileop.fFlags = FOF_NO_UI;
    int res = SHFileOperationW(&fileop);
    free((void*)dir);
    if (res == 0 && fileop.fAnyOperationsAborted == FALSE)
      return Status::OK();
    return IOError("DeleteDir " + dirname);
  }
#endif

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {

    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL)
      return Status::InvalidArgument("Invalid file name");
    HANDLE h = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,  
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,  NULL); 
    free(file_name);
    if (h == INVALID_HANDLE_VALUE)
      return  IOError("GetFileSize " + fname);

    // Not using GetFileAttributesEx() as it doesn't interact well with symlinks, etc.
    LARGE_INTEGER lsize;
    BOOL ok = GetFileSizeEx(h, &lsize);
    CloseHandle(h);
    if (!ok)
      return  IOError("GetFileSize " + fname);

    *size = static_cast<uint64_t>(lsize.QuadPart);
    return Status::OK();
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    WCHAR *src2 = ToWcharPermissive(src.c_str());
    WCHAR *target2 = ToWcharPermissive(target.c_str());
    if ((src2 == NULL) || (target2 == NULL)) {
      free(src2);
      free(target2);
      return Status::InvalidArgument("Invalid file name");
    }
    BOOL ok = MoveFileExW(src2, target2, MOVEFILE_REPLACE_EXISTING);
    free(src2);
    free(target2);
    if (!ok)
        return IOError("RenameFile " + src + " " + target);
    return Status::OK();
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;
    WCHAR *file_name = ToWcharPermissive(fname.c_str());
    if (file_name == NULL) {
      return Status::InvalidArgument("Invalid file name");
    }
    HANDLE h = CreateFileW(file_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)file_name);
    if (h == INVALID_HANDLE_VALUE) {
      return IOError("LockFile " + fname);
    }
    *lock = new WinFileLock(fname, h);
    return Status::OK();
  }

  virtual Status UnlockFile(FileLock* lock) {
    Status s;
    WinFileLock* my_lock = reinterpret_cast<WinFileLock*>(lock);
    if (!my_lock->Close()) {
      s = Status::IOError(my_lock->fname_, "Could not close lock file.");
    }
    delete my_lock;
    return Status::OK();
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual Status GetTestDirectory(std::string* result) {
    WCHAR buf[MAX_PATH];
    DWORD res = GetTempPathW(MAX_PATH, buf);
    if (0 == res) {
      return IOError("Can't get test directory");
    }
    char *s = ToUtf8(buf);
    if (!s) {
      return IOError("Can't get test directory");
    }
    *result = std::string(s);
    free(s);
    return Status::OK();
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    *result = NULL;
    FILE* f = fopen(fname.c_str(), "wt");
    if (f == NULL)
      return Status::IOError(fname, strerror(errno));
    *result = new WinLogger(f);
    return Status::OK();
  }

  virtual uint64_t NowMicros() {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return count.QuadPart * 1000000i64 / freq_.QuadPart;
  }

  virtual void SleepForMicroseconds(int micros) {
     // round up to the next millisecond
    Sleep((micros + 999) / 1000);
  }

private:
  LARGE_INTEGER freq_;

  // BGThread() is the body of the background thread
  void BGThread();

  static unsigned __stdcall BGThreadWrapper(void* arg) {
    (reinterpret_cast<WinEnv*>(arg))->BGThread();
    _endthreadex(0);
    return 0;
  }

  leveldb::port::Mutex mu_;
  leveldb::port::CondVar bgsignal_;
  HANDLE  bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;
};


WinEnv::WinEnv() : bgthread_(NULL), bgsignal_(&mu_) {
  QueryPerformanceFrequency(&freq_);
}

void WinEnv::Schedule(void (*function)(void*), void* arg) {
  mu_.Lock();

  // Start background thread if necessary
  if (NULL == bgthread_) {
    bgthread_ = (HANDLE)_beginthreadex(NULL, 0, &WinEnv::BGThreadWrapper, this, 0, NULL);
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;

  mu_.Unlock();

  bgsignal_.Signal();
}

void WinEnv::BGThread() {
  while (true) {
    // Wait until there is an item that is ready to run
    mu_.Lock();

    while (queue_.empty()) {
      bgsignal_.Wait();
    }

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    mu_.Unlock();
    (*function)(arg);
  }
  // TODO: CloseHandle(bgthread_) ??
}

namespace {
struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
  HANDLE threadHandle;
};
}

static unsigned __stdcall StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  _endthreadex(0);
  CloseHandle(state->threadHandle);
  delete state;
  return 0;
}

void WinEnv::StartThread(void (*function)(void* arg), void* arg) {
  StartThreadState* state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  state->threadHandle = (HANDLE)_beginthreadex(NULL, 0, &StartThreadWrapper, state, 0, NULL);
}
}

static Env* default_env;
static void InitDefaultEnv() { default_env = new WinEnv(); }
static leveldb::port::Mutex default_env_mutex;

Env* Env::Default() {
  default_env_mutex.Lock();
  if (NULL == default_env)
    InitDefaultEnv();
  default_env_mutex.Unlock();
  return default_env;
}

}
