// Sets up common environment for Shay Green's libraries.
// To change configuration options, modify blargg_config.h, not this file.

#ifndef BLARGG_COMMON_H
#define BLARGG_COMMON_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#ifndef _BV
#define _BV(n) (1UL << (n))
#endif

#undef BLARGG_COMMON_H
// allow blargg_config.h to #include blargg_common.h
#include "blargg_config.h"
#ifndef BLARGG_COMMON_H
#define BLARGG_COMMON_H

// BLARGG_RESTRICT: equivalent to restrict, where supported
#if __GNUC__ >= 3 || _MSC_VER >= 1100
#define BLARGG_RESTRICT __restrict
#else
#define BLARGG_RESTRICT
#endif

// STATIC_CAST(T,expr): Used in place of static_cast<T> (expr)
#ifndef STATIC_CAST
#define STATIC_CAST(T, expr) ((T) (expr))
#endif

// blargg_err_t (0 on success, otherwise error string)
#ifndef blargg_err_t
typedef const char *blargg_err_t;
#endif

// blargg_vector - very lightweight vector of POD types (no
// constructor/destructor)
template<class T> class blargg_vector {
  T *m_begin;
  size_t m_size;

 public:
  blargg_vector() : m_begin(nullptr), m_size(0) {}
  ~blargg_vector() { free(m_begin); }
  size_t size() const { return m_size; }
  T *begin() const { return m_begin; }
  T *end() const { return m_begin + m_size; }
  blargg_err_t resize(size_t n) {
    void *p = realloc(this->m_begin, n * sizeof(T));
    if (!p && n)
      return "Out of memory";
    this->m_begin = (T *) p;
    this->m_size = n;
    return nullptr;
  }
  void clear() {
    free(this->m_begin);
    this->m_begin = nullptr;
    this->m_size = 0;
  }
  T &operator[](size_t n) const {
    assert(n <= this->m_size);  // <= to allow past-the-end value
    return this->m_begin[n];
  }
};

// Use to force disable exceptions for a specific allocation no matter what
// class
#define BLARGG_NEW new

// BLARGG_4CHAR('a','b','c','d') = 'abcd' (four character integer constant)
#define BLARGG_4CHAR(a, b, c, d) ((a & 0xFF) * 0x1000000L + (b & 0xFF) * 0x10000L + (c & 0xFF) * 0x100L + (d & 0xFF))

#define BLARGG_2CHAR(a, b) ((a & 0xFF) * 0x100L + (b & 0xFF))

// BLARGG_COMPILER_HAS_BOOL: If 0, provides bool support for old compiler. If 1,
// compiler is assumed to support bool. If undefined, availability is
// determined.
#ifndef BLARGG_COMPILER_HAS_BOOL
#if defined(__MWERKS__)
#if !__option(bool)
#define BLARGG_COMPILER_HAS_BOOL 0
#endif
#elif defined(_MSC_VER)
#if _MSC_VER < 1100
#define BLARGG_COMPILER_HAS_BOOL 0
#endif
#elif defined(__GNUC__)
// supports bool
#elif __cplusplus < 199711
#define BLARGG_COMPILER_HAS_BOOL 0
#endif
#endif
#if defined(BLARGG_COMPILER_HAS_BOOL) && !BLARGG_COMPILER_HAS_BOOL
// If you get errors here, modify your blargg_config.h file
typedef int bool;
const bool true = 1;
const bool false = 0;
#endif

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(maybe_unused)
#define BLARGG_MAYBE_UNUSED [[maybe_unused]]
#endif
#endif

#ifndef BLARGG_MAYBE_UNUSED
#define BLARGG_MAYBE_UNUSED
#endif

// blargg_long/blargg_ulong = at least 32 bits, int if it's big enough

#if INT_MAX < 0x7FFFFFFF || LONG_MAX == 0x7FFFFFFF
typedef long blargg_long;
#else
typedef int blargg_long;
#endif

#if UINT_MAX < 0xFFFFFFFF || ULONG_MAX == 0xFFFFFFFF
typedef unsigned long blargg_ulong;
#else
typedef unsigned blargg_ulong;
#endif

// int8_t etc.

// TODO: Add CMake check for this, although I'd likely just point affected
// persons to a real compiler...
#if 1 || defined(HAVE_STDINT_H)
#include <stdint.h>
#endif

#if __GNUC__ >= 3
#define BLARGG_DEPRECATED __attribute__((deprecated))
#else
#define BLARGG_DEPRECATED
#endif

// Use in place of "= 0;" for a pure virtual, since these cause calls to std C++
// lib. During development, BLARGG_PURE( x ) expands to = 0; virtual int func()
// BLARGG_PURE( { return 0; } )
#ifndef BLARGG_PURE
#define BLARGG_PURE(def) def
#endif

#endif
#endif
