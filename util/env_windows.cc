// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <deque>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "util/win_logger.h"
#include "port/port.h"
#include "util/logging.h"
#include <filesystem>
#include <fstream>

namespace leveldb {
namespace {

static char global_read_only_buf[0x8000];

class PosixSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  FILE* file_;

 public:
  PosixSequentialFile(const std::string& fname, FILE* f)
    : filename_(fname), file_(f) { }
  virtual ~PosixSequentialFile() { fclose(file_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
  Status s;
#ifdef BSD
  // fread_unlocked doesn't exist on FreeBSD
  size_t r = fread(scratch, 1, n, file_);
#else
  size_t r = fread_unlocked(scratch, 1, n, file_);
#endif
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

class PosixRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  int fd_;
  mutable CRITICAL_SECTION cs;

 public:
  PosixRandomAccessFile(const std::string& fname, int fd)
    : filename_(fname), fd_(fd) {
	  InitializeCriticalSection(&cs);
  }
  virtual ~PosixRandomAccessFile() { 
	  close(fd_);
	  DeleteCriticalSection(&cs);
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
            char* scratch) const {
    Status s;
#ifdef WIN32
    // no pread on Windows so we emulate it with a mutex
	EnterCriticalSection(&cs);

    if (::_lseeki64(fd_, offset, SEEK_SET) == -1L) {
      return Status::IOError(filename_, strerror(errno));
    }

    int r = ::_read(fd_, scratch, n);
    *result = Slice(scratch, (r < 0) ? 0 : r);
	LeaveCriticalSection(&cs);
#else
    ssize_t r = pread(fd_, scratch, n, static_cast<off_t>(offset));
    *result = Slice(scratch, (r < 0) ? 0 : r);
#endif
    if (r < 0) {
      // An error: return a non-ok status
      s = Status::IOError(filename_, strerror(errno));
    }
    return s;
  }
};

// We preallocate up to an extra megabyte and use memcpy to append new
// data to the file.  This is safe since we either properly close the
// file before reading from it, or for log files, the reading code
// knows enough to skip zero suffixes.

class BoostFile : public WritableFile {

public:
  explicit BoostFile(std::string path) : path_(path), written_(0) {
    Open();
  }

  virtual ~BoostFile() {
    Close();
  }

private:
  void Open() {
    // we truncate the file as implemented in env_posix
     file_.open(path_, 
         std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
     written_ = 0;
  }

public:
  virtual Status Append(const Slice& data) {
    Status result;
    file_.write(data.data(), data.size());
    if (!file_.good()) {
      result = Status::IOError(
          path_ + " Append", "cannot write");
    }
    return result;
  }

  virtual Status Close() {
    Status result;

    try {
      if (file_.is_open()) {
        Sync();
        file_.close();
      }
    } catch (const std::exception & e) {
      result = Status::IOError(path_ + " close", e.what());
    }

    return result;
  }

  virtual Status Flush() {
    file_.flush();
    return Status::OK();
  }

  virtual Status Sync() {
    Status result;
    try {
      Flush();
    } catch (const std::exception & e) {
      result = Status::IOError(path_ + " sync", e.what());
    }

    return result;
  }

private:
  std::string path_;
  uint64_t written_;
  std::ofstream file_;
};



class BoostFileLock : public FileLock {
 private:
  HANDLE mutex;
public:
	BoostFileLock(const std::string& fname) {
		char lpName[MAX_PATH];
		strcpy_s(lpName, fname.c_str());
		for (char* p = lpName; *p != 0; p++)
			if (*p == '\\')
				*p = '_';
			else if (*p >= 'A' && *p <= 'Z')
				*p += 0x20;
		strcat_s(lpName, ":FileLock");
		mutex = CreateMutex(NULL, FALSE, lpName);
		WaitForSingleObject(mutex, INFINITE);
	}
	~BoostFileLock() {
		if (mutex != NULL)
		{
			ReleaseMutex(mutex);
			CloseHandle(mutex);
		}
	}
};

class PosixEnv : public Env {
 public:
  PosixEnv();
  virtual ~PosixEnv() {
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
      *result = new PosixSequentialFile(fname, f);
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                   RandomAccessFile** result) {
#ifdef WIN32
    int fd = _open(fname.c_str(), _O_RDONLY | _O_RANDOM | _O_BINARY);
#else
    int fd = open(fname.c_str(), O_RDONLY);
#endif
    if (fd < 0) {
      *result = NULL;
      return Status::IOError(fname, strerror(errno));
    }
    *result = new PosixRandomAccessFile(fname, fd);
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                 WritableFile** result) {
    Status s;
    try {
      // will create a new empty file to write to
      *result = new BoostFile(fname);
    }
    catch (const std::exception & e) {
      s = Status::IOError(fname, e.what());
    }

    return s;
  }

  virtual bool FileExists(const std::string& fname) {
	return std::tr2::sys::exists(fname);
  }

  virtual Status GetChildren(const std::string& dir,
               std::vector<std::string>* result) {
    result->clear();

    std::error_code ec;
	std::tr2::sys::directory_iterator current(dir, ec);
    if (ec) {
      return Status::IOError(dir, ec.message());
    }

	std::tr2::sys::directory_iterator end;

    for(; current != end; ++current) {
      result->push_back(current->path().filename().generic_string());
    }

    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
	std::tr2::sys::remove(fname);
	return Status::OK();
  }

  virtual Status CreateDir(const std::string& name) {
      Status result;

	  try
	  {
		  std::tr2::sys::create_directories(name);
	  }
	  catch (std::tr2::sys::filesystem_error ex)
	  {
		  result = Status::IOError(name, ex.what());
	  }

      return result;
    }

  virtual Status DeleteDir(const std::string& name) {
	std::tr2::sys::remove_all(name);
	return Status::OK();
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
	*size = std::tr2::sys::file_size(fname);
	return Status::OK();
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    std::tr2::sys::rename(src, target);
	return Status::OK();
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;

    Status result;

    try {
		if (!std::tr2::sys::exists(fname)) {
        std::ofstream of(fname, std::ios_base::trunc | std::ios_base::out);
      }

	  assert(std::tr2::sys::exists(fname));

	  BoostFileLock * my_lock = new BoostFileLock(fname);
      *lock = my_lock;
    } catch (const std::exception & e) {
      result = Status::IOError("lock " + fname, e.what());
    }

    return result;
  }

  virtual Status UnlockFile(FileLock* lock) {

    Status result;

    try {
		delete lock;
    } catch (const std::exception & e) {
      result = Status::IOError("unlock", e.what());
    }

    return result;
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void(*function)(void* arg), void* arg);

#ifndef WIN32
  static uint64_t gettid() {
    pthread_t tid = pthread_self();
    uint64_t thread_id = 0;
    memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
    return thread_id;
  }
#endif

  virtual Status GetTestDirectory(std::string* result) {
	  std::error_code ec;
	  std::tr2::sys::path temp_dir = std::tr2::sys::temp_directory_path(ec);
	  if (ec) {
		  temp_dir = "tmp";
	  }
	  temp_dir /= "leveldb_tests";
	  temp_dir /= std::to_string(GetCurrentProcessId());
	  CreateDir(temp_dir.generic_string());
	  *result = temp_dir.generic_string();
	  return Status::OK();
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
  FILE* f = fopen(fname.c_str(), "wt");
  if (f == NULL) {
    *result = NULL;
    return Status::IOError(fname, strerror(errno));
  } else {
#ifdef WIN32
    *result = new WinLogger(f);
#else
    *result = new PosixLogger(f, &PosixEnv::gettid);
#endif
    return Status::OK();
  }
  }

  virtual uint64_t NowMicros() {
	union
	{
		uint64_t ns100;
		FILETIME ft;
	} now;
	GetSystemTimeAsFileTime(&now.ft);
	return now.ns100 / 10 % 86400000000;
  }

  virtual void SleepForMicroseconds(int micros) {
    Sleep(micros / 1000);
  }

 private:
  void PthreadCall(const char* label, int result) {
  if (result != 0) {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    exit(1);
  }
  }

  // BGThread() is the body of the background thread
  void BGThread();

  static DWORD WINAPI BGThreadWrapper(void* arg) {
    reinterpret_cast<PosixEnv*>(arg)->BGThread();
    return NULL;
  }

  CRITICAL_SECTION cs;
  CONDITION_VARIABLE cv;
  HANDLE bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;
};

PosixEnv::PosixEnv() {
	InitializeCriticalSection(&cs);
	InitializeConditionVariable(&cv);
	bgthread_ = NULL;
}

void PosixEnv::Schedule(void (*function)(void*), void* arg) {
  EnterCriticalSection(&cs);

  // Start background thread if necessary
  if (bgthread_ == NULL) {
	 bgthread_ = CreateThread(NULL, 0, &PosixEnv::BGThreadWrapper, this, 0, NULL);
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;

  LeaveCriticalSection(&cs);

  WakeConditionVariable(&cv);

}

void PosixEnv::BGThread() {
  while (true) {
  // Wait until there is an item that is ready to run
  EnterCriticalSection(&cs);

  while (queue_.empty()) {
	  SleepConditionVariableCS(&cv, &cs, INFINITE);
  }

  void (*function)(void*) = queue_.front().function;
  void* arg = queue_.front().arg;
  queue_.pop_front();

  LeaveCriticalSection(&cs);
  (*function)(arg);
  }
}

struct StartThreadContext {
	void(*function)(void* arg);
	void* arg;
};

DWORD WINAPI StartThreadCallback(LPVOID lpThreadParameter) {
	StartThreadContext* context = (StartThreadContext*)lpThreadParameter;
	void(*function)(void* arg) = context->function;
	void* arg = context->arg;
	delete context;
	function(arg);
	return 0;
}

void PosixEnv::StartThread(void(*function)(void* arg), void* arg) {
	StartThreadContext* context = new StartThreadContext;
	context->function = function;
	context->arg = arg;
	CreateThread(NULL, 0, StartThreadCallback, context, 0, NULL);
}

}

static port::OnceType once = LEVELDB_ONCE_INIT;
static Env* default_env = NULL;

static void InitModule() {
	::memset(global_read_only_buf, 0, sizeof(global_read_only_buf));
	default_env = new PosixEnv;
}

Env* Env::Default() {
	port::InitOnce(&once, InitModule);
	return default_env;
}

}
