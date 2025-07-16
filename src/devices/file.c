#include <buxn/devices/file.h>
#include <buxn/vm/vm.h>
#include <string.h>

static char BUXN_FILE_INVALID_HANDLE = 0;

static char*
buxn_file_read_path(struct buxn_vm_s* vm, uint8_t* mem, char* buf) {
	uint16_t path_addr = buxn_vm_load2(mem, 0x08, BUXN_DEV_PRIV_ADDR_MASK);
	char ch;
	int len = 0;
	while (
		(ch = vm->memory[(path_addr++) & BUXN_MEM_ADDR_MASK]) != 0
		&& len < BUXN_FILE_MAX_PATH
	) {
		buf[len++] = ch;
	}

	if (len >= BUXN_FILE_MAX_PATH || len == 0) {
		return NULL;
	} else {
		buf[len] = '\0';
		return buf;
	}
}

static buxn_file_handle_t*
buxn_file_set_mode(
	struct buxn_vm_s* vm,
	buxn_file_t* device,
	uint8_t* mem,
	buxn_file_mode_t mode
) {
	if (device->handle != NULL && device->mode != mode) {
		if (device->handle == &BUXN_FILE_INVALID_HANDLE) {
			// no op
		} else if (device->stat.type == BUXN_FILE_TYPE_REGULAR) {
			buxn_file_fclose(vm, device->handle);
		} else if (device->stat.type == BUXN_FILE_TYPE_DIRECTORY) {
			buxn_file_closedir(vm, device->handle);
		}
		device->handle = NULL;
		device->success = 0;
	}

	if (device->handle == NULL) {
		char path_buf[BUXN_FILE_MAX_PATH];
		char* path = buxn_file_read_path(vm, mem, path_buf);
		if (path != NULL) {
			device->stat = buxn_file_stat(vm, path);
			if (device->stat.type == BUXN_FILE_TYPE_REGULAR) {
				device->handle = buxn_file_fopen(vm, path, mode);
			} else if (device->stat.type == BUXN_FILE_TYPE_DIRECTORY) {
				device->handle = buxn_file_opendir(vm, path);
			} else if (device ->stat.type == BUXN_FILE_TYPE_INVALID) {
				if (mode != BUXN_FILE_MODE_READ) {
					device->handle = buxn_file_fopen(vm, path, mode);
					device->stat.type = BUXN_FILE_TYPE_REGULAR;
				} else {
					device->handle = &BUXN_FILE_INVALID_HANDLE;
				}
			}
			device->mode = mode;
		}
	}

	return device->handle;
}

static void
buxn_file_format_stat(char* buf, uint16_t len, const buxn_file_stat_t* stat) {
	switch (stat->type) {
		case BUXN_FILE_TYPE_INVALID:
			memset(buf, '!', len);
			break;
		case BUXN_FILE_TYPE_REGULAR: {
			size_t size = stat->size;
			char* dest = buf + len - 1;
			for(uint16_t i = 0; i < len; ++i) {
				char c = '0' + (uint8_t)(size & 0xf);
				if(c > '9') { c += 39; }
				*(dest--) = c;
				size = size >> 4;
			}

			if (size != 0) { memset(buf, '?', len); }
		} break;
		case BUXN_FILE_TYPE_DIRECTORY:
			memset(buf, '-', len);
			break;
	}
}

static uint16_t
buxn_file_clamp_length(uint16_t length, uint16_t addr) {
	uint16_t max_len = 0x10000 - addr;
	return length > max_len ? max_len : length;
}

uint8_t
buxn_file_dei(struct buxn_vm_s* vm, buxn_file_t* device, uint8_t* mem, uint8_t port) {
	(void)vm;
	switch (port) {
		case 0x02: return device->success >> 8;
		case 0x03: return (uint8_t)device->success;
		default: return mem[port];
	}
}

void
buxn_file_deo(struct buxn_vm_s* vm, buxn_file_t* device, uint8_t* mem, uint8_t port) {
	switch (port) {
		case 0x05: {
			// stat
			uint16_t stat_addr = buxn_vm_load2(mem, port - 1, BUXN_DEV_PRIV_ADDR_MASK);
			uint16_t length = buxn_file_clamp_length(
				buxn_vm_load2(mem, 0xa, BUXN_DEV_PRIV_ADDR_MASK),
				stat_addr
			);
			if (device->handle != NULL) {
				buxn_file_format_stat((char*)&vm->memory[stat_addr], length, &device->stat);
				device->success = length;
			} else {
				char path_buf[BUXN_FILE_MAX_PATH];
				char* path = buxn_file_read_path(vm, mem, path_buf);
				if (path != NULL) {
					buxn_file_stat_t stat = buxn_file_stat(vm, path);
					buxn_file_format_stat((char*)&vm->memory[stat_addr], length, &stat);
					device->success = length;
				} else {
					device->success = 0;
				}
			}
		} break;
		case 0x06: {
			// delete
			char path_buf[BUXN_FILE_MAX_PATH];
			char* path = buxn_file_read_path(vm, mem, path_buf);
			device->success = path != NULL && buxn_file_delete(vm, path);
		} break;
		case 0x09: {
			// name
			if (device->handle != NULL) {
				if (device->stat.type == BUXN_FILE_TYPE_REGULAR) {
					buxn_file_fclose(vm, device->handle);
				} else if (device->stat.type == BUXN_FILE_TYPE_DIRECTORY) {
					buxn_file_closedir(vm, device->handle);
				}

				device->handle = NULL;
			}
		} break;
		case 0x0d: {
			// read
			buxn_file_handle_t* file = buxn_file_set_mode(vm, device, mem, BUXN_FILE_MODE_READ);
			uint16_t read_addr = buxn_vm_load2(mem, port - 1, BUXN_DEV_PRIV_ADDR_MASK);
			uint16_t length = buxn_file_clamp_length(
				buxn_vm_load2(mem, 0xa, BUXN_DEV_PRIV_ADDR_MASK),
				read_addr
			);
			uint16_t total_bytes_read = 0;

			if (file == NULL) {
				total_bytes_read = 0;
			} else if (device->stat.type == BUXN_FILE_TYPE_DIRECTORY) {
				// Read dir
				uint16_t read_dir_pos = device->read_dir_pos;
				uint16_t read_dir_len = device->read_dir_len;
				char* read_dir_buf = device->read_dir_buf;
				while (length > 0) {
					if (read_dir_pos >= read_dir_len) {
						// Refill read dir buffer
						read_dir_pos = 0;
						read_dir_len = 0;
						buxn_file_stat_t stat;
						const char* filename;
						if ((filename = buxn_file_readdir(vm, file, &stat)) == NULL) {
							break;
						}

						buxn_file_format_stat(read_dir_buf, 4, &stat);
						read_dir_len += 4;

						size_t name_len = strlen(filename);
						if (read_dir_len + name_len + 2 > sizeof(device->read_dir_buf)) {
							read_dir_len = 0;
							break;
						} else {
							read_dir_buf[read_dir_len++] = ' ';
							memcpy(&read_dir_buf[read_dir_len], filename, name_len);
							read_dir_len += (uint16_t)name_len;
							read_dir_buf[read_dir_len++] = '\n';
						}
					}

					uint16_t bytes_available = read_dir_len - read_dir_pos;
					uint16_t bytes_read = length < bytes_available ? length : bytes_available;
					memcpy(&vm->memory[read_addr], &read_dir_buf[read_dir_pos], bytes_read);
					read_addr += bytes_read;
					total_bytes_read += bytes_read;
					read_dir_pos += bytes_read;
					length -= bytes_read;
				}

				device->read_dir_pos = read_dir_pos;
				device->read_dir_len = read_dir_len;
			} else if (device->stat.type == BUXN_FILE_TYPE_REGULAR) {
				// Read file
				uint16_t bytes_read;
				while (
					length > 0
					&& (bytes_read = buxn_file_fread(vm, file, &vm->memory[read_addr], length)) != 0
				) {
					total_bytes_read += bytes_read;
					read_addr += bytes_read;
					length -= bytes_read;
				}
			}

			device->success = total_bytes_read;
		} break;
		case 0x0f: {
			// write
			uint8_t append = mem[0x07];
			buxn_file_mode_t mode = append == 0 ? BUXN_FILE_MODE_WRITE : BUXN_FILE_MODE_APPEND;
			buxn_file_handle_t* file = buxn_file_set_mode(vm, device, mem, mode);
			if (file != NULL && device->stat.type == BUXN_FILE_TYPE_REGULAR) {
				uint16_t total_bytes_written = 0;
				uint16_t write_addr = buxn_vm_load2(mem, port - 1, BUXN_DEV_PRIV_ADDR_MASK);
				uint16_t length = buxn_file_clamp_length(
					buxn_vm_load2(mem, 0xa, BUXN_DEV_PRIV_ADDR_MASK),
					write_addr
				);
				uint16_t bytes_written;

				while (
					length > 0
					&& (bytes_written = buxn_file_fwrite(vm, file, &vm->memory[write_addr], length)) != 0
				) {
					total_bytes_written += bytes_written;
					write_addr += bytes_written;
					length -= bytes_written;
				}
				device->success = total_bytes_written;
			} else {
				device->success = 0;
			}
		} break;
	}
}
