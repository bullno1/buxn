#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wextra-semi"
#endif

#define SOKOL_IMPL
#ifdef __ANDROID__
#	define SAUDIO_ANDROID_SLES
#endif
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
