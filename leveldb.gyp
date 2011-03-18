# Copyright (c) 2011 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

{
  'variables': {
    'use_snappy%': 0,
  },
  'target_defaults': {
    'defines': [
      'LEVELDB_PLATFORM_CHROMIUM=1',
    ],
    'include_dirs': [
      # MOE:begin_strip
      '../..',
      # MOE:end_strip_and_replace '.',
    ],
    'conditions': [
      ['OS == "win"', {
        'include_dirs': [
          'port/win',
        ],
      }],
      ['use_snappy', {
        'defines': [
          'USE_SNAPPY=1',
        ],
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'leveldb',
      'type': '<(library)',
      'dependencies': [
        # The base libary is a lightweight abstraction layer for things like
        # threads and IO. http://src.chromium.org/viewvc/chrome/trunk/src/base/
        # MOE:begin_strip
        '../../../../base/base.gyp:base',
        # MOE:end_strip_and_replace '../../base/base.gyp:base',
      ],
      'conditions': [
        ['use_snappy', {
          'dependencies': [
            '../../../../third_party/snappy/snappy.gyp:snappy',
          ],
        }],
      ],
      'sources': [
        # Include and then exclude so that all files show up in IDEs, even if
        # they don't build.
        'db/builder.cc',
        'db/builder.h',
        'db/db_impl.cc',
        'db/db_impl.h',
        'db/db_iter.cc',
        'db/db_iter.h',
        'db/filename.cc',
        'db/filename.h',
        'db/dbformat.cc',
        'db/dbformat.h',
        'db/log_format.h',
        'db/log_reader.cc',
        'db/log_reader.h',
        'db/log_writer.cc',
        'db/log_writer.h',
        'db/memtable.cc',
        'db/memtable.h',
        'db/repair.cc',
        'db/skiplist.h',
        'db/snapshot.h',
        'db/table_cache.cc',
        'db/table_cache.h',
        'db/version_edit.cc',
        'db/version_edit.h',
        'db/version_set.cc',
        'db/version_set.h',
        'db/write_batch.cc',
        'db/write_batch_internal.h',
        'include/cache.h',
        'include/comparator.h',
        'include/db.h',
        'include/env.h',
        'include/iterator.h',
        'include/options.h',
        'include/slice.h',
        'include/status.h',
        'include/table.h',
        'include/table_builder.h',
        'include/write_batch.h',
        'port/port.h',
        'port/port_chromium.cc',
        'port/port_chromium.h',
        'port/port_example.h',
        'port/port_posix.cc',
        'port/port_posix.h',
        'port/sha1_portable.cc',
        'port/sha1_portable.h',
        'table/block.cc',
        'table/block.h',
        'table/block_builder.cc',
        'table/block_builder.h',
        'table/format.cc',
        'table/format.h',
        'table/iterator.cc',
        'table/iterator_wrapper.h',
        'table/merger.cc',
        'table/merger.h',
        'table/table.cc',
        'table/table_builder.cc',
        'table/two_level_iterator.cc',
        'table/two_level_iterator.h',
        'util/arena.cc',
        'util/arena.h',
        'util/cache.cc',
        'util/coding.cc',
        'util/coding.h',
        'util/comparator.cc',
        'util/crc32c.cc',
        'util/crc32c.h',
        'util/env.cc',
        'util/env_chromium.cc',
        'util/env_posix.cc',
        'util/hash.cc',
        'util/hash.h',
        'util/logging.cc',
        'util/logging.h',
        'util/mutexlock.h',
        'util/options.cc',
        'util/random.h',
        'util/status.cc',
      ],
      'sources/': [
        ['exclude', '_(android|example|portable|posix)\\.cc$'],
      ],
    },
    {
      'target_name': 'leveldb_testutil',
      'type': '<(library)',
      'dependencies': [
        # MOE:begin_strip
        '../../../../base/base.gyp:base',
        # MOE:end_strip_and_replace '../../base/base.gyp:base',
        'leveldb',
      ],
      'export_dependent_settings': [
        # The tests use include directories from these projects.
        # MOE:begin_strip
        '../../../../base/base.gyp:base',
        # MOE:end_strip_and_replace '../../base/base.gyp:base',
        'leveldb',
      ],
      'sources': [
        'util/histogram.cc',
        'util/histogram.h',
        'util/testharness.cc',
        'util/testharness.h',
        'util/testutil.cc',
        'util/testutil.h',
      ],
    },
    {
      'target_name': 'leveldb_arena_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'util/arena_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_cache_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'util/cache_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_coding_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'util/coding_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_corruption_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/corruption_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_crc32c_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'util/crc32c_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_db_bench',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/db_bench.cc',
      ],
    },
    {
      'target_name': 'leveldb_db_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/db_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_dbformat_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/dbformat_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_env_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'util/env_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_filename_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/filename_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_log_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/log_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_sha1_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'port/sha1_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_skiplist_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/skiplist_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_table_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'table/table_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_version_edit_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/version_edit_test.cc',
      ],
    },
    {
      'target_name': 'leveldb_write_batch_test',
      'type': 'executable',
      'dependencies': [
        'leveldb_testutil',
      ],
      'sources': [
        'db/write_batch_test.cc',
      ],
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
