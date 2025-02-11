/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _BIONIC_MACROS_H_
#define _BIONIC_MACROS_H_

#include <stdint.h>

// Frameworks OpenGL code currently leaks this header and allows
// collisions with other declarations, e.g., from libnativehelper.
// TODO: Remove once cleaned up. b/18334516
#if !defined(DISALLOW_COPY_AND_ASSIGN)
// DISALLOW_COPY_AND_ASSIGN disallows the copy and operator= functions.
// It goes in the private: declarations in a class.
#    define DISALLOW_COPY_AND_ASSIGN(TypeName)                                                                         \
        TypeName(const TypeName&)       = delete;                                                                      \
        void operator=(const TypeName&) = delete
#endif  // !defined(DISALLOW_COPY_AND_ASSIGN)

// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName)                                                                       \
    TypeName() = delete;                                                                                               \
    DISALLOW_COPY_AND_ASSIGN(TypeName)

#define BIONIC_ROUND_UP_POWER_OF_2(value)                                                                              \
    ((sizeof(value) == 8) ? (1UL << (64 - __builtin_clzl(static_cast<unsigned long>(value))))                          \
                          : (1UL << (32 - __builtin_clz(static_cast<unsigned int>(value)))))

static constexpr uintptr_t align_down(uintptr_t p, size_t align) { return p & ~(align - 1); }

static constexpr uintptr_t align_up(uintptr_t p, size_t align) { return (p + align - 1) & ~(align - 1); }

template <typename T>
static inline T* align_down(T* p, size_t align) {
    return reinterpret_cast<T*>(align_down(reinterpret_cast<uintptr_t>(p), align));
}

template <typename T>
static inline T* align_up(T* p, size_t align) {
    return reinterpret_cast<T*>(align_up(reinterpret_cast<uintptr_t>(p), align));
}

#if defined(__arm__)
// Do not emit anything for arm, clang does not allow emiting an arm unwind
// directive.
// #define BIONIC_STOP_UNWIND asm volatile(".cantunwind")
#    define BIONIC_STOP_UNWIND
#elif defined(__aarch64__)
#    define BIONIC_STOP_UNWIND asm volatile(".cfi_undefined x30")
#elif defined(__i386__)
#    define BIONIC_STOP_UNWIND asm volatile(".cfi_undefined \%eip")
#elif defined(__x86_64__)
#    define BIONIC_STOP_UNWIND asm volatile(".cfi_undefined \%rip")
#elif defined(__mips__)
#    define BIONIC_STOP_UNWIND asm volatile(".cfi_undefined $ra")
#endif

// The arraysize(arr) macro returns the # of elements in an array arr.
// The expression is a compile-time constant, and therefore can be
// used in defining new arrays, for example.  If you use arraysize on
// a pointer by mistake, you will get a compile-time error.
//
// One caveat is that arraysize() doesn't accept any array of an
// anonymous type or a type defined inside a function.
//
// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];  // NOLINT(readability/casting)

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#endif  // _BIONIC_MACROS_H_
