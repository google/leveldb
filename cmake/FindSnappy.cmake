# Copyright 2017 The LevelDB Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file. See the AUTHORS file for names of contributors.

#[=======================================================================[.rst:
FindSnappy
-----------

Find the Snappy libraries

IMPORTED targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target:

``Snappy::Snappy``

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables if found:

``Snappy_INCLUDE_DIRS``
  where to find snappy.h, etc.
``Snappy_LIBRARIES``
  the libraries to link against to use Snappy.
``Snappy_FOUND``
  TRUE if found

#]=======================================================================]

# Look for the necessary header
find_path(Snappy_INCLUDE_DIR NAMES snappy.h)
mark_as_advanced(Snappy_INCLUDE_DIR)

# Look for the necessary library
find_library(Snappy_LIBRARY NAMES snappy)
mark_as_advanced(Snappy_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Snappy
    REQUIRED_VARS Snappy_INCLUDE_DIR Snappy_LIBRARY
)

# Create the imported target
if(Snappy_FOUND)
    set(Snappy_INCLUDE_DIRS ${Snappy_INCLUDE_DIR})
    set(Snappy_LIBRARIES ${Snappy_LIBRARY})
    if(NOT TARGET Snappy::Snappy)
        add_library(Snappy::Snappy UNKNOWN IMPORTED)
        set_target_properties(Snappy::Snappy PROPERTIES
            IMPORTED_LOCATION             "${Snappy_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Snappy_INCLUDE_DIR}")
    endif()
endif()
