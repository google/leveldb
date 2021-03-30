// Copyright 2008 Google Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Type utilities needed for implementing typed and type-parameterized
// tests.

// GOOGLETEST_CM0001 DO NOT DELETE

#ifndef GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TYPE_UTIL_H_
#define GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TYPE_UTIL_H_

#include "gtest/internal/gtest-port.h"

// #ifdef __GNUC__ is too general here.  It is possible to use gcc without using
// libstdc++ (which is where cxxabi.h comes from).
# if GTEST_HAS_CXXABI_H_
#  include <cxxabi.h>
# elif defined(__HP_aCC)
#  include <acxx_demangle.h>
# endif  // GTEST_HASH_CXXABI_H_

namespace testing {
namespace internal {

// Canonicalizes a given name with respect to the Standard C++ Library.
// This handles removing the inline namespace within `std` that is
// used by various standard libraries (e.g., `std::__1`).  Names outside
// of namespace std are returned unmodified.
inline std::string CanonicalizeForStdLibVersioning(std::string s) {
  static const char prefix[] = "std::__";
  if (s.compare(0, strlen(prefix), prefix) == 0) {
    std::string::size_type end = s.find("::", strlen(prefix));
    if (end != s.npos) {
      // Erase everything between the initial `std` and the second `::`.
      s.erase(strlen("std"), end - strlen("std"));
    }
  }
  return s;
}

// GetTypeName<T>() returns a human-readable name of type T.
// NB: This function is also used in Google Mock, so don't move it inside of
// the typed-test-only section below.
template <typename T>
std::string GetTypeName() {
# if GTEST_HAS_RTTI

  const char* const name = typeid(T).name();
#  if GTEST_HAS_CXXABI_H_ || defined(__HP_aCC)
  int status = 0;
  // gcc's implementation of typeid(T).name() mangles the type name,
  // so we have to demangle it.
#   if GTEST_HAS_CXXABI_H_
  using abi::__cxa_demangle;
#   endif  // GTEST_HAS_CXXABI_H_
  char* const readable_name = __cxa_demangle(name, nullptr, nullptr, &status);
  const std::string name_str(status == 0 ? readable_name : name);
  free(readable_name);
  return CanonicalizeForStdLibVersioning(name_str);
#  else
  return name;
#  endif  // GTEST_HAS_CXXABI_H_ || __HP_aCC

# else

  return "<type>";

# endif  // GTEST_HAS_RTTI
}

#if GTEST_HAS_TYPED_TEST || GTEST_HAS_TYPED_TEST_P

// A unique type indicating an empty node
struct None {};

# define GTEST_TEMPLATE_ template <typename T> class

// The template "selector" struct TemplateSel<Tmpl> is used to
// represent Tmpl, which must be a class template with one type
// parameter, as a type.  TemplateSel<Tmpl>::Bind<T>::type is defined
// as the type Tmpl<T>.  This allows us to actually instantiate the
// template "selected" by TemplateSel<Tmpl>.
//
// This trick is necessary for simulating typedef for class templates,
// which C++ doesn't support directly.
template <GTEST_TEMPLATE_ Tmpl>
struct TemplateSel {
  template <typename T>
  struct Bind {
    typedef Tmpl<T> type;
  };
};

# define GTEST_BIND_(TmplSel, T) \
  TmplSel::template Bind<T>::type

template <GTEST_TEMPLATE_ Head_, GTEST_TEMPLATE_... Tail_>
struct Templates {
  using Head = TemplateSel<Head_>;
  using Tail = Templates<Tail_...>;
};

template <GTEST_TEMPLATE_ Head_>
struct Templates<Head_> {
  using Head = TemplateSel<Head_>;
  using Tail = None;
};

// Tuple-like type lists
template <typename Head_, typename... Tail_>
struct Types {
  using Head = Head_;
  using Tail = Types<Tail_...>;
};

template <typename Head_>
struct Types<Head_> {
  using Head = Head_;
  using Tail = None;
};

// Helper metafunctions to tell apart a single type from types
// generated by ::testing::Types
template <typename... Ts>
struct ProxyTypeList {
  using type = Types<Ts...>;
};

template <typename>
struct is_proxy_type_list : std::false_type {};

template <typename... Ts>
struct is_proxy_type_list<ProxyTypeList<Ts...>> : std::true_type {};

// Generator which conditionally creates type lists.
// It recognizes if a requested type list should be created
// and prevents creating a new type list nested within another one.
template <typename T>
struct GenerateTypeList {
 private:
  using proxy = typename std::conditional<is_proxy_type_list<T>::value, T,
                                          ProxyTypeList<T>>::type;

 public:
  using type = typename proxy::type;
};

#endif  // GTEST_HAS_TYPED_TEST || GTEST_HAS_TYPED_TEST_P

}  // namespace internal

template <typename... Ts>
using Types = internal::ProxyTypeList<Ts...>;

}  // namespace testing

#endif  // GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TYPE_UTIL_H_
