#include "../platform.h"
#include <blog.h>
#include <sokol_app.h>
#include <stdio.h>
#include <physfs.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <qoi.h>
#include "../bembd.h"
#include "../resources.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static struct {
	blog_file_logger_options_t log_options;
	blog_level_t log_level;
	const char* argv0;
	FILE* boot_rom_file;
	bool has_size_limit;
	uint64_t rom_size;
	int open_error;
	sapp_icon_desc icon;
} platform_windows = { 0 };

static int64_t
stdio_read(void* handle, void* buffer, uint64_t size) {
	if (platform_windows.has_size_limit) {
		size = platform_windows.rom_size < size ? platform_windows.rom_size : size;
	}

	int64_t bytes_read = fread(buffer, 1, size, handle);
	if (platform_windows.has_size_limit && bytes_read > 0) {
		platform_windows.rom_size -= (uint64_t)bytes_read;
	}
	return bytes_read;
}

static const char*
get_arg(const char* arg, const char* prefix) {
	size_t len = strlen(prefix);
	if (strncmp(arg, prefix, len) == 0) {
		return arg + len;
	} else {
		return NULL;
	}
}

static void
standalone_init(args_t* args) {
	int argc = args->argc;
	const char** argv = args->argv;

	int i;
	for (i = 0; i < argc; ++i) {
		const char* arg = argv[i];
		const char* value;
		if ((value = get_arg(arg, "-log-level=")) != NULL) {
			if (strcmp(value, "trace") == 0) {
				platform_windows.log_level = BLOG_LEVEL_TRACE;
			} else if (strcmp(value, "debug") == 0) {
				platform_windows.log_level = BLOG_LEVEL_DEBUG;
			} else if (strcmp(value, "info") == 0) {
				platform_windows.log_level = BLOG_LEVEL_INFO;
			} else if (strcmp(value, "warn") == 0) {
				platform_windows.log_level = BLOG_LEVEL_WARN;
			} else if (strcmp(value, "error") == 0) {
				platform_windows.log_level = BLOG_LEVEL_ERROR;
			} else if (strcmp(value, "fatal") == 0) {
				platform_windows.log_level = BLOG_LEVEL_FATAL;
			}
		} else if (strcmp(arg, "--") == 0) {
			++i;
			break;
		} else {
			break;
		}
	}
	argc -= i;
	argv += i;

	const char* boot_rom_filename = "boot.rom";
	if (argc > 0) {
		boot_rom_filename = argv[0];
		++argv;
		--argc;
	}
	platform_windows.boot_rom_file = fopen(boot_rom_filename, "rb");
	platform_windows.open_error = errno;

	args->argc = argc;
	args->argv = argv;
}

static void
embeded_init(args_t* args, FILE* rom_file, uint32_t rom_size) {
	(void)args;
	platform_windows.boot_rom_file = rom_file;
	platform_windows.has_size_limit = true;
	platform_windows.rom_size = rom_size;
}

void
platform_init(args_t* args) {
#ifdef _DEBUG
	platform_windows.log_level = BLOG_LEVEL_TRACE;
#else
	platform_windows.log_level = BLOG_LEVEL_INFO;
#endif

	// Icon
	xincbin_data_t logo_qoi = XINCBIN_GET(logo);
	struct qoidecoder decoder = qoidecoder(logo_qoi.data, logo_qoi.size);
	int num_pixels = decoder.count;
	size_t icon_size = sizeof(unsigned) * num_pixels;
	unsigned* pixels = malloc(icon_size);
	for (int i = 0; i < num_pixels; ++i) {
		pixels[i] = qoidecode(&decoder);
	}

	if (!decoder.error) {
		platform_windows.icon.images[0] = (sapp_image_desc){
			.width = decoder.width,
			.height = decoder.height,
			.pixels = {
				.ptr = pixels,
				.size = icon_size,
			},
		};
	} else {
		platform_windows.icon.sokol_default = true;
	}

	if (args->argc > 0) {
		platform_windows.argv0 = args->argv[0];
		++args->argv;
		--args->argc;
	}

	FILE* self = fopen(platform_windows.argv0, "rb");
	if (self == NULL) {
		standalone_init(args);
		return;
	}
	uint32_t rom_size = bembd_find(self);
	if (rom_size == 0) {
		fclose(self);
		standalone_init(args);
	} else {
		embeded_init(args, self, rom_size);
	}
}

void
platform_cleanup(void) {
	free((void*)platform_windows.icon.images[0].pixels.ptr);
}

sapp_icon_desc
platform_icon(void) {
	return platform_windows.icon;
}

void
platform_init_log(void) {
	platform_windows.log_options.file = stderr;
	platform_windows.log_options.with_colors = true;
	blog_add_file_logger(platform_windows.log_level, &platform_windows.log_options);
}

void
platform_init_fs(void) {
	PHYSFS_init(platform_windows.argv0);
	PHYSFS_setWriteDir(".");
	PHYSFS_mount(".", "/", 1);
}

void
platform_init_dbg(struct buxn_vm_s* vm) {
	(void)vm;
}

bool
platform_update_dbg(void) {
	return false;
}

bool
platform_open_boot_rom(stream_t* stream) {
	if (platform_windows.boot_rom_file == NULL) {
		BLOG_ERROR("Could not open rom file: %s", strerror(platform_windows.open_error));
		return false;
	}

	stream->handle = platform_windows.boot_rom_file;
	stream->read = stdio_read;
	return true;
}

const char*
platform_stream_error(stream_t stream) {
	(void)stream;
	return strerror(errno);
}

void
platform_close_stream(stream_t stream) {
	fclose(stream.handle);
}

void
platform_resize_window(uint16_t width, uint16_t height) {
	HWND hwnd = (HWND)sapp_win32_get_hwnd();

	RECT rect = {
		.left = 0,
		.top = 0,
		.right = width,
		.bottom= height,
	};
	AdjustWindowRect(&rect, GetWindowLong(hwnd, GWL_STYLE), FALSE);
	SetWindowPos(hwnd, 0,
		rect.left, rect.top,
		rect.right - rect.left,
		rect.bottom - rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
	);
}

int
platform_poll_stdin(char* ch, int size) {
	return 0;
}
