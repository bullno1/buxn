#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "bembd.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
#	define RUNNER_EXT ".exe"
#else
#	define RUNNER_EXT ""
#endif

static const char*
get_arg(const char* arg, const char* prefix) {
	size_t len = strlen(prefix);
	if (strncmp(arg, prefix, len) == 0) {
		return arg + len;
	} else {
		return NULL;
	}
}

static inline char*
find_relative_path(const char* exe_path, char* file) {
	int slash_pos;
	for (slash_pos = (int)strlen(exe_path) - 1; slash_pos >= 0; --slash_pos) {
		if (exe_path[slash_pos] == '/' || exe_path[slash_pos] == '\\') {
			break;
		}
	}

	if (slash_pos < 0) { return file; }

	size_t name_len = strlen(file);
	char* path_buf = malloc(slash_pos + 1 + name_len + 1);
	memcpy(path_buf, exe_path, slash_pos + 1);
	memcpy(path_buf + slash_pos + 1, file, name_len + 1);
	return path_buf;
}

int
main(int argc, const char* argv[]) {
	const char* exe_path = argv[0];
	char* runner = "cli";

	int i;
	for (i = 1; i < argc; ++i) {
		const char* arg_val;
		if ((arg_val = get_arg(argv[i], "-runner=")) != NULL) {
			runner = (char*)arg_val;
		} else if (strcmp(argv[i], "--") == 0) {
			++i;
			break;
		} else {
			break;
		}
	}

	argc -= i;
	argv += i;
	if (argc != 2) {
		fprintf(stderr, "Usage: buxn-rom2exe [-runner=<runner>] <input.rom> <output>\n");
		return 1;
	}

	char* runner_path;
	if (strcmp(runner, "cli") == 0) {
		runner_path = find_relative_path(exe_path, "buxn-cli" RUNNER_EXT);
	} else if (strcmp(runner, "gui") == 0) {
		runner_path = find_relative_path(exe_path, "buxn-gui" RUNNER_EXT);
	} else {
		runner_path = runner;
	}

	FILE* runner_file = NULL;
	FILE* input_file = NULL;
	FILE* output_file = NULL;
	int exit_code = 1;

	runner_file = fopen(runner_path, "rb");
	if (runner_file == NULL) {
		perror("Could not open runner");
		goto end;
	}

	output_file = fopen(argv[1], "wb");
	if (output_file == NULL) {
		perror("Could not open output");
		goto end;
	}

	input_file = fopen(argv[0], "rb");
	if (output_file == NULL) {
		perror("Could not open input");
		goto end;
	}

	if (bembd_cp(output_file, runner_file) == 0) {
		perror("Error while copying runner");
	}

	if (bembd_put(output_file, input_file) == 0) {
		perror("Error while embedding");
		goto end;
	}

#ifdef __linux__
	struct stat stat;
	int fd = fileno(output_file);
	fstat(fd, &stat);
	mode_t mode = stat.st_mode;
	mode |= S_IXUSR | S_IXGRP | S_IXOTH;
	fchmod(fd, mode);
#endif

	exit_code = 0;
end:
	if (input_file != NULL) { fclose(input_file); }
	if (output_file != NULL) { fclose(output_file); }
	if (runner_file != NULL) { fclose(runner_file); }
	if (runner_path != runner) { free(runner_path); }
	return exit_code;
}
