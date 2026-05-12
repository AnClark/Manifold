/**
 * @file NFDIncludes.h
 * @brief Unified include entry point for NFD Extended (Native File Dialog).
 *
 * This header acts as a wrapper for the NFD Extended library and addresses
 * two problems:
 *
 * 1. **Platform macro pre-definition**: `nfd_glfw3.h` pulls in `glfw3native.h`,
 *    which requires the appropriate `GLFW_EXPOSE_NATIVE_<PLATFORM>` macro to be
 *    defined beforehand; otherwise `NFD_GetNativeWindowFromGLFWWindow()` will
 *    not compile. This file detects the target platform and defines the required
 *    macro automatically, so callers do not need to handle it themselves.
 *
 * 2. **Include-order guarantee**: Ensures that `nfd.hpp` is always included
 *    before `nfd_glfw3.h`, preventing compilation failures caused by an
 *    incorrect include order.
 *
 * @note Any source file that needs to use the NFD file dialog or
 *       `NFD_GetNativeWindowFromGLFWWindow()` should include this header
 *       instead of including `nfd.hpp` / `nfd_glfw3.h` directly.
 */
#pragma once

#include "nfd.hpp"  // IWYU pragma: keep

// nfd_glfw3.h provides NFD_GetNativeWindowFromGLFWWindow(), which converts a GLFW window to an NFD parent window handle.
// GLFW_EXPOSE_NATIVE_<PLATFORM> must be defined before including nfd_glfw3.h (which in turn includes glfw3native.h).
#if defined(_WIN32)         // Windows
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)    // macOS
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)    // Linux (X11 + Wayland, selected at runtime by GLFW)
#define GLFW_EXPOSE_NATIVE_X11
//#define GLFW_EXPOSE_NATIVE_WAYLAND    // FIXME: There is a link error when enabling this macro (Undefined reference to 'glfwGetWaylandWindow')
#else
#error "Unsupported platform: cannot determine GLFW native window type for NFD."
#error "If you see this error, it is highly possible that your platform does not support NFD, which is a dependency of Manifold."
#endif

#include "nfd_glfw3.h"  // IWYU pragma: keep

// Deal with conflicts brought by Xlib's pre-defined macros
// That should be a well-known troublesome issue for C++ developers in Linux world.
#ifdef Status
#  undef Status
#endif
#ifdef Success
#  undef Success
#endif
#ifdef None
#  undef None
#endif
