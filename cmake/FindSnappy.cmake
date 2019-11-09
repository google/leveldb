# Copyright 2019 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

find_library(SNAPPY_LIBRARY
    NAMES snappy
    HINTS ${SNAPPY_ROOT_DIR}/lib
)

find_path(SNAPPY_INCLUDE_DIR
    NAMES snappy.h
    HINTS ${SNAPPY_ROOT_DIR}/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Snappy DEFAULT_MSG SNAPPY_LIBRARY SNAPPY_INCLUDE_DIR)

mark_as_advanced(SNAPPY_LIBRARY SNAPPY_INCLUDE_DIR)

if(SNAPPY_FOUND)
  set(HAVE_SNAPPY TRUE) # For compatibity with generating port_config.h.

  # Add imported targets.
  # Follow the package naming convetion 'Snappy::' from
  # https://github.com/google/snappy/blob/master/CMakeLists.txt#L211.
  add_library(Snappy::snappy UNKNOWN IMPORTED)
  set_target_properties(Snappy::snappy PROPERTIES
      IMPORTED_LOCATION ${SNAPPY_LIBRARY}
      INTERFACE_INCLUDE_DIRECTORIES ${SNAPPY_INCLUDE_DIR}
  )
endif()
