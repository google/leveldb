
// port of env for win32
#ifdef LEVELDB_PLATFORM_WINDOWS

#include "leveldb/env.h"
#include "port/port.h"
#include "testharness.h"
#include "util/mutexlock.h"
#include "posix_logger.h" // PosixLogger

#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <Windows.h>
#include <process.h>

// because of this line in windows standard includes 
// #define DeleteFile  DeleteFileA 
// prevents the DeleteFile from having the good name
#undef DeleteFile


// to be sure we are really implementing an ovveride. 
// VS print error C3668 it not.
#define OVERRIDE override
// to make win32 api calls more obvious
// ex : win32api::CreateFile( ... )
#define win32api  
// default win32 sharing flags for CreateFile()
// FILE_SHARE_DELETE : for MoveFileEx
static const DWORD DEFAULT_WIN32_SHARE_FLAGS = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

namespace leveldb {

class Win32Env : public Env
{
protected:
	// for safe Schedule access.
	port::Mutex mutexSchedule_;
	// schrduler objet for the Schedule() method
	class SchedulerThread *schedulerThread_ = nullptr;


public:
	// constructor
	Win32Env();


	// --- Inmplametation of de Env interface 

	// Create a brand new sequentially-readable file with the specified name.
	// On success, stores a pointer to the new file in *result and returns OK.
	// On failure stores NULL in *result and returns non-OK.  If the file does
	// not exist, returns a non-OK status.
	//
	// The returned file will only be accessed by one thread at a time.
	virtual Status NewSequentialFile(const std::string& fname,
		SequentialFile** result) OVERRIDE;

	// Create a brand new random access read-only file with the
	// specified name.  On success, stores a pointer to the new file in
	// *result and returns OK.  On failure stores NULL in *result and
	// returns non-OK.  If the file does not exist, returns a non-OK
	// status.
	//
	// The returned file may be concurrently accessed by multiple threads.
	virtual Status NewRandomAccessFile(const std::string& fname,
		RandomAccessFile** result) OVERRIDE;

	// Create an object that writes to a new file with the specified
	// name.  Deletes any existing file with the same name and creates a
	// new file.  On success, stores a pointer to the new file in
	// *result and returns OK.  On failure stores NULL in *result and
	// returns non-OK.
	//
	// The returned file will only be accessed by one thread at a time.
	virtual Status NewWritableFile(const std::string& fname,
		WritableFile** result) OVERRIDE;

	// Create an object that either appends to an existing file, or
	// writes to a new file (if the file does not exist to begin with).
	// On success, stores a pointer to the new file in *result and
	// returns OK.  On failure stores NULL in *result and returns
	// non-OK.
	//
	// The returned file will only be accessed by one thread at a time.
	//
	// May return an IsNotSupportedError error if this Env does
	// not allow appending to an existing file.  Users of Env (including
	// the leveldb implementation) must be prepared to deal with
	// an Env that does not support appending.
	virtual Status NewAppendableFile(const std::string& fname,
		WritableFile** result);

	// Returns true if the named file exists.
	virtual bool FileExists(const std::string& fname) OVERRIDE;

	// Store in *result the names of the children of the specified directory.
	// The names are relative to "dir".
	// Original contents of *results are dropped.
	virtual Status GetChildren(const std::string& dir,
		std::vector<std::string>* result) OVERRIDE;

	// Delete the named file.
	virtual Status DeleteFile(const std::string& fname) OVERRIDE;

	// Create the specified directory.
	virtual Status CreateDir(const std::string& dirname) OVERRIDE;

	// Delete the specified directory.
	virtual Status DeleteDir(const std::string& dirname) OVERRIDE;

	// Store the size of fname in *file_size.
	virtual Status GetFileSize(const std::string& fname, uint64_t* file_size) OVERRIDE;

	// Rename file src to target.
	virtual Status RenameFile(const std::string& src,
		const std::string& target) OVERRIDE;

	// Lock the specified file.  Used to prevent concurrent access to
	// the same db by multiple processes.  On failure, stores NULL in
	// *lock and returns non-OK.
	//
	// On success, stores a pointer to the object that represents the
	// acquired lock in *lock and returns OK.  The caller should call
	// UnlockFile(*lock) to release the lock.  If the process exits,
	// the lock will be automatically released.
	//
	// If somebody else already holds the lock, finishes immediately
	// with a failure.  I.e., this call does not wait for existing locks
	// to go away.
	//
	// May create the named file if it does not already exist.
	virtual Status LockFile(const std::string& fname, FileLock** lock) OVERRIDE;

	// Release the lock acquired by a previous successful call to LockFile.
	// REQUIRES: lock was returned by a successful LockFile() call
	// REQUIRES: lock has not already been unlocked.
	virtual Status UnlockFile(FileLock* lock) OVERRIDE;

	// Arrange to run "(*function)(arg)" once in a background thread.
	//
	// "function" may run in an unspecified thread.  Multiple functions
	// added to the same Env may run concurrently in different threads.
	// I.e., the caller may not assume that background work items are
	// serialized.
	virtual void Schedule(
		void (*function)(void* arg),
		void* arg) OVERRIDE;

	// Start a new thread, invoking "function(arg)" within the new thread.
	// When "function(arg)" returns, the thread will be destroyed.
	virtual void StartThread(void (*function)(void* arg), void* arg) OVERRIDE;

	// *path is set to a temporary directory that can be used for testing. It may
	// or many not have just been created. The directory may or may not differ
	// between runs of the same process, but subsequent calls will return the
	// same directory.
	virtual Status GetTestDirectory(std::string* path) OVERRIDE;

	// Create and return a log file for storing informational messages.
	virtual Status NewLogger(const std::string& fname, Logger** result) OVERRIDE;

	// Returns the number of micro-seconds since some fixed point in time. Only
	// useful for computing deltas of time.
	virtual uint64_t NowMicros() OVERRIDE {
		// *1000 : the win32 api is in millisec
		return win32api::GetTickCount() * 1000;
	}

	// Sleep/delay the thread for the prescribed number of micro-seconds.
	virtual void SleepForMicroseconds(int micros) OVERRIDE {
		// *1000 : the win32 api is in millisec
		win32api::Sleep(micros / 1000);
	}

};

// constructor
Win32Env::Win32Env() {
}

// max lenght for a system error message.
#define MAX_ERRORTXT_LEN 2048

// return an error status from libc error code <err_number>
static Status LibcIOError(const std::string& context, int libcErrorCode) {
	// get system error string in buffer
	char strErrBuffer[MAX_ERRORTXT_LEN] = {};
	strerror_s(strErrBuffer, MAX_ERRORTXT_LEN, libcErrorCode);
	// convert to Status
	return Status::IOError(context, strErrBuffer);
}
// return an error status from win32 error code <err_number>
static Status Win32IOError(const std::string& context, int getLastErrorCode) {
	// get system error string in <errorMessage>
	std::string errorMessage;
	// api FormatMessage, allocate lpErrorText
	LPTSTR  lpErrorText = NULL;
	DWORD ret = win32api::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, 	0,
								getLastErrorCode,
								0,
								(LPTSTR)&lpErrorText,
								MAX_ERRORTXT_LEN,
								0);
	// if FormatMessage failed = no system string for <getLastErrorCode>
	if (ret=0)
	{
		// return a string like this : "unknow system error 4455"
		char strErrBuffer[MAX_ERRORTXT_LEN] = {};
		snprintf(strErrBuffer, MAX_ERRORTXT_LEN, "unknow system error %d", getLastErrorCode );
		errorMessage = strErrBuffer;
	}
	else
	{
		// copy message
		errorMessage = lpErrorText;
		// free de string allocated by FormatMessage
		win32api::LocalFree(lpErrorText);
	}
	// convert to Status
	return Status::IOError(context, errorMessage);
}

// wrapper to win32 api SetFilePointer()
DWORD win32SetFilePointer64( HANDLE hFile, uint64_t offset, DWORD  moveMethod ) 
{
	// init api parameters
#define LODWORD(l)          (DWORD)( l        & 0xffffffff)
#define HIDWORD(l)          (DWORD)((l >> 32) & 0xffffffff)
	LONG   distanceToMove = LODWORD(offset); // 32 low bits
	LONG   distanceToMoveHigh = HIDWORD(offset); // 32 high bits
												 // move the current file pointeur forward 
												 // NB : no sign pb here. quote from msdn :
												 //    If lpDistanceToMoveHigh is not NULL, lpDistanceToMoveHigh and lDistanceToMove form a single 64-bit signed value that specifies the distance to move.
	DWORD retCode = win32api::SetFilePointer(hFile, distanceToMove, &distanceToMoveHigh, moveMethod);
	return retCode;
}


// base classe from win32 api based file 
class Win32FileBase {
protected:
	// filename for error reports
	std::string filename_;
	// win32 file handle for api calls
	HANDLE hFile_;

public:
	// construct from name and win32 handle
	Win32FileBase(const std::string& filename, HANDLE h)
		: filename_(filename), hFile_(h)
	{
		assert(hFile_ != INVALID_HANDLE_VALUE);
	}
	// destructor 
	virtual ~Win32FileBase() {
		// free the win32 handle => close the file
		if (hFile_ != INVALID_HANDLE_VALUE)
			win32api::CloseHandle(hFile_);
	}

protected:
	// move file pointer
	// call the api SetFilePointer with 64 bits offset dans interpretation of the return value.
	Status _apiSetFilePointer(uint64_t offset, DWORD  moveMethod = FILE_CURRENT) const
	{
		// move the current file pointeur f
		DWORD retCode = win32SetFilePointer64(hFile_, offset, moveMethod);
		if (retCode == INVALID_SET_FILE_POINTER) {
			// return fail code from win32 error
			return Win32IOError(filename_, win32api::GetLastError());
		}
		// success
		return Status::OK();
	}

};//class Win32FileBase


// sequentially-readable file for win32 api
class Win32SequentialFile :  public SequentialFile
							,public Win32FileBase {
public:
	// construct from name and win32 handle
	Win32SequentialFile(const std::string& filename, HANDLE h) :
		Win32FileBase(filename, h) {}

	// Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
	// written by this routine.  Sets "*result" to the data that was
	// read (including if fewer than "n" bytes were successfully read).
	// May set "*result" to point at data in "scratch[0..n-1]", so
	// "scratch[0..n-1]" must be live when "*result" is used.
	// If an error was encountered, returns a non-OK status.
	//
	// REQUIRES: External synchronization
	virtual Status Read(size_t n, Slice* result, char* scratch) OVERRIDE {
	
		DWORD numberOfBytesToRead = static_cast<DWORD>(n);
		DWORD numberOfBytesRead	  = 0;
		BOOL retCode = win32api::ReadFile(hFile_, scratch, numberOfBytesToRead, &numberOfBytesRead, NULL);
		// on failure
		if (retCode == FALSE) {
			// return fail code from win32 error
			return Win32IOError(filename_, win32api::GetLastError());
		}
		// success
		*result = Slice(scratch, numberOfBytesRead);
		return Status::OK();
	
	}
	
	// Skip "n" bytes from the file. This is guaranteed to be no
	// slower that reading the same data, but may be faster.
	//
	// If end of file is reached, skipping will stop at the end of the
	// file, and Skip will return OK.
	//
	// REQUIRES: External synchronization
	virtual Status Skip(uint64_t n) OVERRIDE {
			return _apiSetFilePointer(n, FILE_CURRENT);

	}

};// class Win32SequentialFileb


  // sequentially-readable file for win32 api
class Win32RandomAccessFile : public RandomAccessFile
						    , public Win32FileBase {
protected:
	// for safe Read access.
	mutable port::Mutex mutex_;
public:
	// construct from name and win32 handle
	Win32RandomAccessFile(const std::string& filename, HANDLE h) :
		Win32FileBase(filename, h) {}

	// Read up to "n" bytes from the file starting at "offset".
	// "scratch[0..n-1]" may be written by this routine.  Sets "*result"
	// to the data that was read (including if fewer than "n" bytes were
	// successfully read).  May set "*result" to point at data in
	// "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
	// "*result" is used.  If an error was encountered, returns a non-OK
	// status.
	//
	// Safe for concurrent use by multiple threads.
	virtual Status Read(uint64_t offset, size_t n, Slice* result,
		char* scratch) const OVERRIDE
	{
		// lock for thread safe file position
		MutexLock lock(&mutex_);

		// goto to offset
		Status status = _apiSetFilePointer(offset, FILE_BEGIN);
		if (!status.ok())
		{
			return status; // failure
		}

		//  Read <n> bytes from file in <scratch>
		DWORD numberOfBytesToRead = static_cast<DWORD>(n);
		DWORD numberOfBytesRead = 0;
		BOOL retCode = win32api::ReadFile(hFile_, scratch, numberOfBytesToRead, &numberOfBytesRead, NULL);
		
		// on failure
		if (retCode == FALSE) {
			// return fail code from win32 error
			return Win32IOError(filename_, win32api::GetLastError());
		}
		// success
		*result = Slice(scratch, numberOfBytesRead);
		return Status::OK();
	}


}; // class Win32SequentialFile


 // writable file for win32 api
 // A file abstraction for sequential writing.  The implementation
 // must provide buffering since callers may append small fragments
 // at a time to the file.
class Win32WritableFile : public WritableFile
						, public Win32FileBase {
public:
	// construct from name and win32 handle
	Win32WritableFile(const std::string& filename, HANDLE h) :
		Win32FileBase(filename, h) {}

	// Add some bytes to the file
	virtual Status Append(const Slice& data) OVERRIDE {
		//@Direct api call here, windows does some buffering internaly.
		// if the benchs show it's not sufficient, will need to add some buffering code.

		// init api parameters 
		LPCVOID buffer				 = data.data();
		DWORD   numberOfBytesToWrite = data.size();
		DWORD   numberOfBytesWritten = 0;
		BOOL retCode = win32api::WriteFile(hFile_, buffer, numberOfBytesToWrite, &numberOfBytesWritten, NULL);
		// on failure
		if (retCode == FALSE) {
			// return fail code from win32 error
			return Win32IOError(filename_, win32api::GetLastError());
		}
		// succes
		return Status::OK();
	}
	// close the file
	virtual Status Close() OVERRIDE {
		
		if (hFile_ == INVALID_HANDLE_VALUE)
		{
			win32api::CloseHandle(hFile_);
			// handle si no more valid
			hFile_ = INVALID_HANDLE_VALUE;
		}
		return Status::OK();
	}
	// flushes the internal buffers
	virtual Status Flush()  OVERRIDE {
		//win32api::FlushFileBuffers(hFile_);
		return Status::OK();
	}
	// flush buffer to disk/IO
	virtual Status Sync()  OVERRIDE {
		win32api::FlushFileBuffers(hFile_);
		return Status::OK();
	}

};//class Win32WritableFile

// Create a brand new sequentially-readable file with the specified name.
// On success, stores a pointer to the new file in *result and returns OK.
// On failure stores NULL in *result and returns non-OK.  If the file does
// not exist, returns a non-OK status.
//
// The returned file will only be accessed by one thread at a time.
Status Win32Env::NewSequentialFile(const std::string& fname,
	SequentialFile** result) {
	// Init with non random value
	*result = NULL;

	// open the file <fname> with read-only accces : GENERIC_READ
	HANDLE h = win32api::CreateFile(fname.c_str(),
		GENERIC_READ,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	// on failure
	if (h == INVALID_HANDLE_VALUE) {
		// return fail code
		return Win32IOError(fname, ::GetLastError());
	}
	
	// write parameter out
	// with a new instantance  of a SequentialFile derivate for win32
	*result = new Win32SequentialFile(fname, h);
	// success
	return Status::OK();
}


// Create a brand new random access read-only file with the
// specified name.  On success, stores a pointer to the new file in
// *result and returns OK.  On failure stores NULL in *result and
// returns non-OK.  If the file does not exist, returns a non-OK
// status.
//
// The returned file may be concurrently accessed by multiple threads.
Status Win32Env::NewRandomAccessFile(const std::string& fname,
	RandomAccessFile** result) {

	// Init with non random value
	*result = NULL;

	// open the file <fname> with read-only accces : "r"
	HANDLE h = win32api::CreateFile(fname.c_str(),
		GENERIC_READ,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
		NULL);
	// on failure
	if (h == INVALID_HANDLE_VALUE) {
		// return failure code
		return Win32IOError(fname, ::GetLastError());
	}

	// write the out parameter with a new instantance  of a SequentialFile derivate for win32
	*result = new Win32RandomAccessFile(fname, h);
	// success
	return Status::OK();

}

// Create an object that writes to a new file with the specified
// name.  Deletes any existing file with the same name and creates a
// new file.  On success, stores a pointer to the new file in
// *result and returns OK.  On failure stores NULL in *result and
// returns non-OK.
//
// The returned file will only be accessed by one thread at a time.
//
Status Win32Env::NewWritableFile(const std::string& fname,
	WritableFile** result) 
{
	// Init with non random value
	*result = NULL;

	// open the file <fname> with read/write accces :GENERIC_READ + GENERIC_WRITE
	HANDLE h = win32api::CreateFile(fname.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		CREATE_ALWAYS, // create a new file, alaways
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	// on failure
	if (h == INVALID_HANDLE_VALUE) {
		// return fail code
		return Win32IOError(fname, ::GetLastError());
	}
	// RAZ file : 0 bytes
	win32api::SetEndOfFile (h);

	// write the out parameter with a new instantance  of a SequentialFile derivate for win32
	*result = new Win32WritableFile(fname, h);
	// success
	return Status::OK();

}

// Create an object that either appends to an existing file, or
// writes to a new file (if the file does not exist to begin with).
// On success, stores a pointer to the new file in *result and
// returns OK.  On failure stores NULL in *result and returns
// non-OK.
//
// The returned file will only be accessed by one thread at a time.
//
// May return an IsNotSupportedError error if this Env does
// not allow appending to an existing file.  Users of Env (including
// the leveldb implementation) must be prepared to deal with
// an Env that does not support appending.
Status  Win32Env::NewAppendableFile(const std::string& fname,
	WritableFile** result)
{
	// open the file <fname> with read/write accces :GENERIC_READ + GENERIC_WRITE
	HANDLE h = win32api::CreateFile(fname.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		OPEN_ALWAYS, // create a new file if it does not exit, else open it
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	// on failure
	if (h == INVALID_HANDLE_VALUE) {
		// return fail code
		return Win32IOError(fname, ::GetLastError());
	}
	// set current pos a the end of file
	DWORD retCode = win32SetFilePointer64(h, 0 , FILE_END);
	if (retCode == INVALID_SET_FILE_POINTER) {
		CloseHandle(h);
		// return failure code
		return Win32IOError(fname, ::GetLastError());
	}

	// write the out parameter with a new instantance  of a SequentialFile derivate for win32
	*result = new Win32WritableFile(fname, h);
	// success
	return Status::OK();

}	


// Returns true if the named file exists.
bool Win32Env::FileExists(const std::string& fname) {
	// GetFileAttributes is the fastest api to test if a file exist in win32
	DWORD result = win32api::GetFileAttributes(fname.c_str());
	if (   (result == INVALID_FILE_ATTRIBUTES) // error
		|| ((result & FILE_ATTRIBUTE_DIRECTORY) != 0) // a directory is not a file
       )
	{
		// file does not exist, or not a file
		return false;
	}
	// exist
	return true;
}

// Store in *result the names of the children of the specified directory.
// The names are relative to "dir".
// Original contents of *results are dropped.
//virtual 
Status Win32Env::GetChildren(const std::string& dir,
	std::vector<std::string>* result) {
	// raz out array
	result->clear();
	// init dir parameter
	std::string dirFilter = dir;
	dirFilter.append("\\*.*");
	WIN32_FIND_DATA findFileData;
	ZeroMemory(&findFileData, sizeof(WIN32_FIND_DATA));
	// start enumeration on files in <dir>
	HANDLE  hFind =win32api::FindFirstFile(dirFilter.c_str(), &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)	{ 
		// return failure code
		return Win32IOError(dir, ::GetLastError());
	}
	// for each file
	BOOL fileFound = TRUE;
	while (fileFound) {
		// ignore . and ..
		bool ignoreFile = false;
		if (strcmp(findFileData.cFileName, ".") == 0)  ignoreFile = true;
		if (strcmp(findFileData.cFileName, "..") == 0) ignoreFile = true;

		// add filename to the result
		if (!ignoreFile)
			result->push_back( findFileData.cFileName );
		// next file
		fileFound = win32api::FindNextFile(hFind, &findFileData);
	}
	// free win32 ressources allocated by FindFirstFile/FinNextFile
	win32api::FindClose(hFind);
	// succes
	return Status::OK();
}

// Delete the named file.
Status Win32Env::DeleteFile(const std::string& fname) {
	// DeleteFileA because we #undef the win32 define
	BOOL ok = win32api::DeleteFileA(fname.c_str());
	if (!ok) {
		// return failure code
		return Win32IOError(fname, ::GetLastError());
	}
	// succes
	return Status::OK();
}
// Create the specified directory.
Status Win32Env::CreateDir(const std::string& dirname) {
	// direct api call
	// NULL : default security attributes
	BOOL ok = win32api::CreateDirectory(dirname.c_str(), NULL);
	if (!ok) {
		// OK if already exist
		if (::GetLastError() == ERROR_ALREADY_EXISTS)
			return Status::OK();
		
		// return failure code
		return Win32IOError(dirname, ::GetLastError());
	}
	// succes
	return Status::OK();
}

// Delete the specified directory.
Status Win32Env::DeleteDir(const std::string& dirname) {
	// direct api call
	BOOL ok = win32api::RemoveDirectory (dirname.c_str());
	if (!ok) {
		// return failure code
		return Win32IOError(dirname, ::GetLastError());
	}
	// succes
	return Status::OK();
}

// Store the size of fname in *file_size.
Status Win32Env::GetFileSize(const std::string& fname, uint64_t* file_size_out) {
	// CreateFile() is slower than GetFileSizeEx() to get file size, but is more accurate if the file is still opened somewhere
	// open the file <fname> with read-only accces : GENERIC_READ
	HANDLE h = win32api::CreateFile(fname.c_str(),
		GENERIC_READ,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	// on failure
	if (h == INVALID_HANDLE_VALUE) {
		// return fail code
		return Win32IOError(fname, ::GetLastError());
	}
	// get file size
	LARGE_INTEGER fileSize = {};
	win32api::GetFileSizeEx(h, &fileSize);
	// close file 
	CloseHandle(h);

	// put the file size in the fuction result
	*file_size_out = fileSize.QuadPart;

	// succes
	return Status::OK();
}
// Rename file src to target.
Status Win32Env::RenameFile(const std::string& src,	const std::string& target) {

	// direct api call
	// MOVEFILE_REPLACE_EXISTING , to match unix rename() :
	//   If newpath already exists, it will be atomically replaced,
	BOOL ok = win32api::MoveFileEx (src.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING| MOVEFILE_WRITE_THROUGH);
	if (!ok) {
		// return failure code
		return Win32IOError(src, ::GetLastError());
	}
	// succes
	return Status::OK();
}

class Win32FileLock : public FileLock
				    , public Win32FileBase {
public:
	// construct from name and win32 handle
	Win32FileLock(const std::string& filename, HANDLE h) :
		Win32FileBase(filename, h) {}
	// destructor 
	virtual ~Win32FileLock() {
		// unlock the file
		// direct api call, ignore return code
		win32api::UnlockFile(hFile_,
			0, 0,
			UINT_MAX, UINT_MAX // all the file
			);
	}

};


// Lock the specified file.  Used to prevent concurrent access to
// the same db by multiple processes.  On failure, stores NULL in
// *lock and returns non-OK.
//
// On success, stores a pointer to the object that represents the
// acquired lock in *lock and returns OK.  The caller should call
// UnlockFile(*lock) to release the lock.  If the process exits,
// the lock will be automatically released.
//
// If somebody else already holds the lock, finishes immediately
// with a failure.  I.e., this call does not wait for existing locks
// to go away.
//
// May create the named file if it does not already exist.
Status Win32Env::LockFile(const std::string& fname, FileLock** lock) {
	// open the file <fname> with read-only accces : GENERIC_READ
	// OPEN_ALWAYS : create the file if it does not exist
	HANDLE hFile = win32api::CreateFile(fname.c_str(),
		GENERIC_READ,
		DEFAULT_WIN32_SHARE_FLAGS,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	// on failure
	if (hFile == INVALID_HANDLE_VALUE) {
		// return fail code
		return Win32IOError(fname, ::GetLastError());
	}
	//create lock 
	// LOCKFILE_FAIL_IMMEDIATELY : fail if lock cannot be gained immediatly
	BOOL ok = win32api::LockFile(  hFile,
									0, 0, // start
									UINT_MAX, UINT_MAX   // all the file
								);
	if (!ok) {
		// Free handle alllocted by CreateFile()
		CloseHandle(hFile);
		// return failure code
		return Win32IOError(fname, ::GetLastError());
	}
	// lock aquired
	// create the FileLock objet
	*lock = new Win32FileLock(fname, hFile);
	// success
	return Status::OK();
}

// Release the lock acquired by a previous successful call to LockFile.
// REQUIRES: lock was returned by a successful LockFile() call
// REQUIRES: lock has not already been unlocked.
Status Win32Env::UnlockFile(FileLock* lock) {
	delete lock;
	// success
	return Status::OK();
}

// 1 entry per Schedule() call
struct BGItem { void* arg; void (*function)(void*); };
// FIFO with calls
typedef std::queue<BGItem> BGQueue;


// helper class for  Win32Env::Schedule() implementation
class SchedulerThread {
protected:
	// functions waiting tu run
	BGQueue queue_;
	// mutex to add/remove from the fifo
	port::Mutex mutexQueue_;
	// win32 event to wake up thread on adding new func
	HANDLE hEvent_;

public:
	SchedulerThread() {
		// create win32 event objet
		hEvent_ = win32api::CreateEvent(NULL,
										FALSE, // FALSE : system automatically resets the event state to nonsignaled after a single waiting thread has been released.
										FALSE, // Initial state 
										NULL  // no name
										);
	}
	~SchedulerThread(){
		win32api::CloseHandle(hEvent_);
		hEvent_ = NULL;
	}
	// Run the thead
	void IniMainThreadLoop(void);

	// Add a new function to be run
	void AddEntry(BGItem bgItem) {
		// Thread safe operation
		MutexLock lock(&mutexQueue_);
		// Add to priority queue
		queue_.push(bgItem);
		// wake up thread in _WaitForNewFunc()
		win32api::SetEvent(hEvent_);
	}
protected:
	// Retrive a function to be run or return false (empty queue)
	bool _bPopEntry(BGItem *bgItem) {
		// Thread safe operation
		MutexLock lock(&mutexQueue_);
		if (queue_.empty())
			return false;
		// reteave item
		*bgItem = queue_.front();
		// remove item
		queue_.pop();
		return true;
	}


	// main loop in thread
	void _MainThreadLoop();
	// static callsback in thead
	static void _ThreadCallback(void *p)
	{
		// run the non statci _MainThreadLoop method
		SchedulerThread *schedulerThread = (SchedulerThread *)p;
		schedulerThread->_MainThreadLoop();
	}
	// run all function in queue
	void _RunAllFuncInQueue();
	// wait for new fuctions to be assed
	void _WaitForNewFunc()	{
		// wait for a call of SetEvent(hEvent_)
		win32api::WaitForSingleObject(hEvent_, INFINITE);
	}
};

// Run the thead
void SchedulerThread::IniMainThreadLoop(void)
{
	// Note : not using CreateThread because it  does not init libc
	// create a new threa with _MainThreadLoopCallback as entry point
	_beginthread(_ThreadCallback, 0, (void *)this);
}
// main loop in thread
void SchedulerThread::_MainThreadLoop()
{
	while (1) {
		// run all function in queue
		_RunAllFuncInQueue();
		// wait for new fuctions to be added in the queue
		_WaitForNewFunc();
	}
}
// run all function in queue
void SchedulerThread::_RunAllFuncInQueue() {
	
	//while therie is a function to run 
	BGItem bgItem;
	while (_bPopEntry(&bgItem)) {
		//run it
		bgItem.function(bgItem.arg);
	}
}

// Arrange to run "(*function)(arg)" once in a background thread.
//
// "function" may run in an unspecified thread.  Multiple functions
// added to the same Env may run concurrently in different threads.
// I.e., the caller may not assume that background work items are
// serialized.
//virtual 
void Win32Env::Schedule(
	void (*function)(void* arg),
	void* arg) {
	// one caller at a time
	MutexLock lock(&mutexSchedule_);

	// create and init Scheduler thread at first call
	if (schedulerThread_ == nullptr) {
		schedulerThread_ = new SchedulerThread();
		// launch  the executer thread 
		schedulerThread_->IniMainThreadLoop();
	}

	// add callback and parameter
	BGItem item = { arg, function };
	schedulerThread_->AddEntry(item);
}

// Start a new thread, invoking "function(arg)" within the new thread.
// When "function(arg)" returns, the thread will be destroyed.
void Win32Env::StartThread(void (*function)(void* arg), void* arg)
{
	// Note : not using CreateThread because it  does not init libc
	// create a new threa with _MainThreadLoopCallback as entry point
	_beginthread(function, 0, arg);
}

// *path is set to a temporary directory that can be used for testing. It may
// or many not have just been created. The directory may or may not differ
// between runs of the same process, but subsequent calls will return the
// same directory.
Status Win32Env::GetTestDirectory(std::string* path) {
	// get system temp dir
	TCHAR winTempDir[MAX_PATH];
	win32api::GetTempPath(MAX_PATH, winTempDir);
	// create temp dir different for each processs
	int pid = win32api::GetCurrentProcessId();
	char buf[MAX_PATH];
	snprintf(buf, sizeof(buf), "%sleveldbtest-%d", winTempDir, pid);

	// create the dir
	BOOL ok =win32api::CreateDirectory(buf, NULL);
	// if failure and not  beceause dir already exists
	if (!ok && (::GetLastError() != ERROR_ALREADY_EXISTS)) {
		// return failure code
		std::string tempPath = buf;
		return Win32IOError(tempPath, ::GetLastError());
	}

	// set result
	*path = buf;
	// succes
	return Status::OK();
}

// Return the thread id for the current thread
// callback for PosixLogger()
uint64_t win32gettid() {
	return GetCurrentThreadId();
}

// Create and return a log file for storing informational messages.
Status Win32Env::NewLogger(const std::string& fname, Logger** result) {

	// open the file <fname> with read/write accces
	FILE* f = fopen(fname.c_str(), "w");
	if (f == NULL) {
		*result = NULL;
		return LibcIOError(fname, errno);
	}

	// create a posix logger object
	*result = new PosixLogger(f, win32gettid );

	// succes
	return Status::OK();
}

// global Env creation, singleton
static Win32Env* default_env = nullptr;
static void InitDefaultEnv() {
	// allocate the Env class for WIN32 API
	default_env = new Win32Env();
}

static bool oneCall = false;
static port::Mutex mutexOneCall;
Env* Env::Default() {
	// Executes InitDefaultEnv exactly once
	// = std::call_once(once, InitDefaultEnv); 
	// not supported by mingw
	{
		MutexLock lock(&mutexOneCall);
		if (!oneCall)
		{
			oneCall = true;
			InitDefaultEnv();
		}
	}
	// return the global env
	return default_env;
}




}  // namespace leveldb

#endif// LEVELDB_PLATFORM_WINDOWS