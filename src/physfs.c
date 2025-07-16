#include <buxn/devices/file.h>
#include <buxn/vm/vm.h>
#include <physfs.h>
#include <string.h>
#include <blog.h>

typedef struct physfs_dir_buf_t {
	char** files;
	char** current;
} physfs_dir_buf_t;

static physfs_dir_buf_t dir_buf[BUXN_NUM_FILE_DEVICES] = { 0 };

static inline buxn_file_handle_t*
buxn_file_log_open_error(const char* path, PHYSFS_file* file) {
	if (file == NULL) {
		BLOG_ERROR("Could not open %s: %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}

	return file;
}

buxn_file_handle_t*
buxn_file_fopen(struct buxn_vm_s* vm, const char* path, buxn_file_mode_t mode) {
	(void)vm;

	BLOG_DEBUG("Opening %s with mode %d", path, mode);
	if (strncmp(path, "./", 2) == 0) {
		path += 2;
	}

	switch (mode) {
		case BUXN_FILE_MODE_READ:
			return buxn_file_log_open_error(path, PHYSFS_openRead(path));
		case BUXN_FILE_MODE_WRITE:
			return buxn_file_log_open_error(path, PHYSFS_openWrite(path));
		case BUXN_FILE_MODE_APPEND:
			return buxn_file_log_open_error(path, PHYSFS_openAppend(path));
		default:
			return NULL;
	}
}

void
buxn_file_fclose(struct buxn_vm_s* vm, buxn_file_handle_t* handle) {
	(void)vm;

	PHYSFS_close(handle);
}

uint16_t
buxn_file_fread(struct buxn_vm_s* vm, buxn_file_handle_t* handle, void* buffer, uint16_t size) {
	(void)vm;

	if (PHYSFS_eof(handle)) { return 0; }

	PHYSFS_sint64 bytes_read = PHYSFS_readBytes(handle, buffer, size);
	return bytes_read >= 0 ? (uint16_t)bytes_read : 0;
}

uint16_t
buxn_file_fwrite(struct buxn_vm_s* vm, buxn_file_handle_t* handle, const void* buffer, uint16_t size) {
	(void)vm;

	PHYSFS_sint64 bytes_written = PHYSFS_writeBytes(handle, buffer, size);
	return bytes_written >= 0 ? (uint16_t)bytes_written : 0;
}

buxn_file_handle_t*
buxn_file_opendir(struct buxn_vm_s* vm, const char* path) {
	(void)vm;

	BLOG_DEBUG("Opening directory %s", path);
	if (strncmp(path, "./", 2) == 0) {
		path += 2;
	}

	for (int i = 0; i < BUXN_NUM_FILE_DEVICES; ++i) {
		if (dir_buf[i].files == NULL) {
			dir_buf[i].current = dir_buf[i].files = PHYSFS_enumerateFiles(
				strcmp(path, ".") == 0 ? "/" : path
			);
			return &dir_buf[i];
		}
	}

	return NULL;
}

void
buxn_file_closedir(struct buxn_vm_s* vm, buxn_file_handle_t* handle) {
	(void)vm;

	physfs_dir_buf_t* dir_buf = handle;
	PHYSFS_freeList(dir_buf->files);
	dir_buf->files = NULL;
	dir_buf->current = NULL;
}

const char*
buxn_file_readdir(
	struct buxn_vm_s* vm,
	buxn_file_handle_t* handle,
	buxn_file_stat_t* stat
) {
	(void)vm;

	physfs_dir_buf_t* dir_buf = handle;
	const char* path = *dir_buf->current;
	if (path == NULL) { return NULL; }

	// TODO: Fix this for subdir
	// The path must be prepended with the base path
	// Check with potato.rom or left.rom
	// ../ may be needed too
	*stat = buxn_file_stat(vm, path);

	++dir_buf->current;
	return path;
}

bool
buxn_file_delete(struct buxn_vm_s* vm, const char* path) {
	(void)vm;

	if (strncmp(path, "./", 2) == 0) { path += 2; }
	BLOG_DEBUG("Deleting %s", path);
	bool success = PHYSFS_delete(path);
	if (!success) {
		BLOG_ERROR("Could not delete %s: %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	return success;
}

buxn_file_stat_t
buxn_file_stat(struct buxn_vm_s* vm, const char* path) {
	(void)vm;

	BLOG_DEBUG("Stating %s", path);
	if (strncmp(path, "./", 2) == 0) {
		path += 2;
	}

	PHYSFS_Stat stat;
	if (strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
		return (buxn_file_stat_t){
			.type = BUXN_FILE_TYPE_DIRECTORY,
			.size = 0,
		};
	} else if (PHYSFS_stat(path, &stat)) {
		if (stat.filetype == PHYSFS_FILETYPE_REGULAR) {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_REGULAR,
				.size = stat.filesize,
			};
		} else if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_DIRECTORY,
				.size = 0,
			};
		} else {
			return (buxn_file_stat_t){
				.type = BUXN_FILE_TYPE_INVALID,
				.size = 0,
			};
		}
	} else {
		BLOG_ERROR("Could not stat %s: %s", path, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return (buxn_file_stat_t){ .type = BUXN_FILE_TYPE_INVALID };
	}
}
