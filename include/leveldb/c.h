/* Copyright (c) 2011 The LevelDB Authors. All rights reserved.
  Use of this source code is governed by a BSD-style license that can be
  found in the LICENSE file. See the AUTHORS file for names of contributors.

  C bindings for leveldb.  May be useful as a stable ABI that can be
  used by programs that keep leveldb in a shared library, or for
  a JNI api.

  Does not support:
  . getters for the option types
  . custom comparators that implement key shortening
  . custom iter, db, env, cache implementations using just the C bindings

  Some conventions:

  (1) We expose just opaque struct pointers and functions to clients.
  This allows us to change internal representations without having to
  recompile clients.

  (2) For simplicity, there is no equivalent to the Slice type.  Instead,
  the caller has to pass the pointer and length as separate
  arguments.

  (3) Errors are represented by a null-terminated c string.  NULL
  means no error.  All operations that can raise an error are passed
  a "char** errptr" as the last argument.  One of the following must
  be true on entry:
     *errptr == NULL
     *errptr points to a malloc()ed null-terminated error message
       (On Windows, *errptr must have been malloc()-ed by this library.)
  On success, a leveldb routine leaves *errptr unchanged.
  On failure, leveldb frees the old value of *errptr and
  set *errptr to a malloc()ed error message.

  (4) Bools have the type unsigned char (0 == false; rest == true)

  (5) All of the pointer arguments must be non-NULL.
*/

#ifndef STORAGE_LEVELDB_INCLUDE_C_H_
#define STORAGE_LEVELDB_INCLUDE_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef DllExport
#define DllExport __declspec(dllexport)
#else
#define DllExport __declspec(dllimport)
#endif // !DLLEXPORT
#else
#define DllExport
#endif // _WIN32

/* Exported types */

typedef struct leveldb_t               leveldb_t;
typedef struct leveldb_cache_t         leveldb_cache_t;
typedef struct leveldb_comparator_t    leveldb_comparator_t;
typedef struct leveldb_env_t           leveldb_env_t;
typedef struct leveldb_filelock_t      leveldb_filelock_t;
typedef struct leveldb_filterpolicy_t  leveldb_filterpolicy_t;
typedef struct leveldb_iterator_t      leveldb_iterator_t;
typedef struct leveldb_logger_t        leveldb_logger_t;
typedef struct leveldb_options_t       leveldb_options_t;
typedef struct leveldb_randomfile_t    leveldb_randomfile_t;
typedef struct leveldb_readoptions_t   leveldb_readoptions_t;
typedef struct leveldb_seqfile_t       leveldb_seqfile_t;
typedef struct leveldb_snapshot_t      leveldb_snapshot_t;
typedef struct leveldb_writablefile_t  leveldb_writablefile_t;
typedef struct leveldb_writebatch_t    leveldb_writebatch_t;
typedef struct leveldb_writeoptions_t  leveldb_writeoptions_t;

/* DB operations */

extern DllExport leveldb_t* leveldb_open(
    const leveldb_options_t* options,
    const char* name,
    char** errptr);

extern DllExport void leveldb_close(leveldb_t* db);

extern DllExport void leveldb_put(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    const char* key, size_t keylen,
    const char* val, size_t vallen,
    char** errptr);

extern DllExport void leveldb_delete(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    const char* key, size_t keylen,
    char** errptr);

extern DllExport void leveldb_write(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    leveldb_writebatch_t* batch,
    char** errptr);

/* Returns NULL if not found.  A malloc()ed array otherwise.
   Stores the length of the array in *vallen. */
extern DllExport char* leveldb_get(
    leveldb_t* db,
    const leveldb_readoptions_t* options,
    const char* key, size_t keylen,
    size_t* vallen,
    char** errptr);

extern DllExport leveldb_iterator_t* leveldb_create_iterator(
    leveldb_t* db,
    const leveldb_readoptions_t* options);

extern DllExport const leveldb_snapshot_t* leveldb_create_snapshot(
    leveldb_t* db);

extern DllExport void leveldb_release_snapshot(
    leveldb_t* db,
    const leveldb_snapshot_t* snapshot);

/* Returns NULL if property name is unknown.
   Else returns a pointer to a malloc()-ed null-terminated value. */
extern DllExport char* leveldb_property_value(
    leveldb_t* db,
    const char* propname);

extern DllExport void leveldb_approximate_sizes(
    leveldb_t* db,
    int num_ranges,
    const char* const* range_start_key, const size_t* range_start_key_len,
    const char* const* range_limit_key, const size_t* range_limit_key_len,
    uint64_t* sizes);

extern DllExport void leveldb_compact_range(
    leveldb_t* db,
    const char* start_key, size_t start_key_len,
    const char* limit_key, size_t limit_key_len);

/* Management operations */

extern DllExport void leveldb_destroy_db(
    const leveldb_options_t* options,
    const char* name,
    char** errptr);

extern DllExport void leveldb_repair_db(
    const leveldb_options_t* options,
    const char* name,
    char** errptr);

/* Iterator */

extern DllExport void leveldb_iter_destroy(leveldb_iterator_t*);
extern DllExport unsigned char leveldb_iter_valid(const leveldb_iterator_t*);
extern DllExport void leveldb_iter_seek_to_first(leveldb_iterator_t*);
extern DllExport void leveldb_iter_seek_to_last(leveldb_iterator_t*);
extern DllExport void leveldb_iter_seek(leveldb_iterator_t*, const char* k, size_t klen);
extern DllExport void leveldb_iter_next(leveldb_iterator_t*);
extern DllExport void leveldb_iter_prev(leveldb_iterator_t*);
extern DllExport const char* leveldb_iter_key(const leveldb_iterator_t*, size_t* klen);
extern DllExport const char* leveldb_iter_value(const leveldb_iterator_t*, size_t* vlen);
extern DllExport void leveldb_iter_get_error(const leveldb_iterator_t*, char** errptr);

/* Write batch */

extern DllExport leveldb_writebatch_t* leveldb_writebatch_create();
extern DllExport void leveldb_writebatch_destroy(leveldb_writebatch_t*);
extern DllExport void leveldb_writebatch_clear(leveldb_writebatch_t*);
extern DllExport void leveldb_writebatch_put(
    leveldb_writebatch_t*,
    const char* key, size_t klen,
    const char* val, size_t vlen);
extern DllExport void leveldb_writebatch_delete(
    leveldb_writebatch_t*,
    const char* key, size_t klen);
extern DllExport void leveldb_writebatch_iterate(
    leveldb_writebatch_t*,
    void* state,
    void (*put)(void*, const char* k, size_t klen, const char* v, size_t vlen),
    void (*deleted)(void*, const char* k, size_t klen));

/* Options */

extern DllExport leveldb_options_t* leveldb_options_create();
extern DllExport void leveldb_options_destroy(leveldb_options_t*);
extern DllExport void leveldb_options_set_comparator(
    leveldb_options_t*,
    leveldb_comparator_t*);
extern DllExport void leveldb_options_set_filter_policy(
    leveldb_options_t*,
    leveldb_filterpolicy_t*);
extern DllExport void leveldb_options_set_create_if_missing(
    leveldb_options_t*, unsigned char);
extern DllExport void leveldb_options_set_error_if_exists(
    leveldb_options_t*, unsigned char);
extern DllExport void leveldb_options_set_paranoid_checks(
    leveldb_options_t*, unsigned char);
extern DllExport void leveldb_options_set_env(leveldb_options_t*, leveldb_env_t*);
extern DllExport void leveldb_options_set_info_log(leveldb_options_t*, leveldb_logger_t*);
extern DllExport void leveldb_options_set_write_buffer_size(leveldb_options_t*, size_t);
extern DllExport void leveldb_options_set_max_open_files(leveldb_options_t*, int);
extern DllExport void leveldb_options_set_cache(leveldb_options_t*, leveldb_cache_t*);
extern DllExport void leveldb_options_set_block_size(leveldb_options_t*, size_t);
extern DllExport void leveldb_options_set_block_restart_interval(leveldb_options_t*, int);

enum {
  leveldb_no_compression = 0,
  leveldb_snappy_compression = 1
};
extern DllExport void leveldb_options_set_compression(leveldb_options_t*, int);

/* Comparator */

extern DllExport leveldb_comparator_t* leveldb_comparator_create(
    void* state,
    void (*destructor)(void*),
    int (*compare)(
        void*,
        const char* a, size_t alen,
        const char* b, size_t blen),
    const char* (*name)(void*));
extern DllExport void leveldb_comparator_destroy(leveldb_comparator_t*);

/* Filter policy */

extern DllExport leveldb_filterpolicy_t* leveldb_filterpolicy_create(
    void* state,
    void (*destructor)(void*),
    char* (*create_filter)(
        void*,
        const char* const* key_array, const size_t* key_length_array,
        int num_keys,
        size_t* filter_length),
    unsigned char (*key_may_match)(
        void*,
        const char* key, size_t length,
        const char* filter, size_t filter_length),
    const char* (*name)(void*));
extern DllExport void leveldb_filterpolicy_destroy(leveldb_filterpolicy_t*);

extern DllExport leveldb_filterpolicy_t* leveldb_filterpolicy_create_bloom(
    int bits_per_key);

/* Read options */

extern DllExport leveldb_readoptions_t* leveldb_readoptions_create();
extern DllExport void leveldb_readoptions_destroy(leveldb_readoptions_t*);
extern DllExport void leveldb_readoptions_set_verify_checksums(
    leveldb_readoptions_t*,
    unsigned char);
extern DllExport void leveldb_readoptions_set_fill_cache(
    leveldb_readoptions_t*, unsigned char);
extern DllExport void leveldb_readoptions_set_snapshot(
    leveldb_readoptions_t*,
    const leveldb_snapshot_t*);

/* Write options */

extern DllExport leveldb_writeoptions_t* leveldb_writeoptions_create();
extern DllExport void leveldb_writeoptions_destroy(leveldb_writeoptions_t*);
extern DllExport void leveldb_writeoptions_set_sync(
    leveldb_writeoptions_t*, unsigned char);

/* Cache */

extern DllExport leveldb_cache_t* leveldb_cache_create_lru(size_t capacity);
extern DllExport void leveldb_cache_destroy(leveldb_cache_t* cache);

/* Env */

extern DllExport leveldb_env_t* leveldb_create_default_env();
extern DllExport void leveldb_env_destroy(leveldb_env_t*);

/* Utility */

/* Calls free(ptr).
   REQUIRES: ptr was malloc()-ed and returned by one of the routines
   in this file.  Note that in certain cases (typically on Windows), you
   may need to call this routine instead of free(ptr) to dispose of
   malloc()-ed memory returned by this library. */
extern DllExport void leveldb_free(void* ptr);

/* Return the major version number for this release. */
extern DllExport int leveldb_major_version();

/* Return the minor version number for this release. */
extern DllExport int leveldb_minor_version();

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STORAGE_LEVELDB_INCLUDE_C_H_ */
