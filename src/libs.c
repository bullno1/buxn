#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#endif

#if defined(__linux__)
#define _XOPEN_SOURCE 700
#elif defined(_WIN32)
#	if !defined(NDEBUG)
#		define SOKOL_WIN32_FORCE_MAIN
#	endif
#endif

#define SOKOL_IMPL
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_time.h>
#include <sokol_audio.h>
#include <sokol_gp.h>

#define BLIB_IMPLEMENTATION
#include <blog.h>
#include <bserial.h>

#define QOI_IMPLEMENTATION
#include <qoi.h>

#ifndef __ANDROID__

#define XINCBIN_IMPLEMENTATION
#include "resources.h"

#endif
