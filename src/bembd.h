#ifndef BEMBD_H
#define BEMBD_H

#include <stdio.h>
#include <stdint.h>

#define BEMBD_HEADER_SIZE 8

static inline size_t
bembd_cp(FILE* to, FILE* from) {
	size_t total_size = 0;
	for (;;) {
		char buf[2048];
		size_t num_bytes = fread(buf, 1, sizeof(buf), from);
		if (num_bytes == 0) { break; }
		if (fwrite(buf, 1, num_bytes, to) != num_bytes) {
			return 0;
		}
		total_size += num_bytes;
	}

	return total_size;
}

static inline uint32_t
bembd_write_header(FILE* container, uint32_t embed_size) {
	uint8_t header[BEMBD_HEADER_SIZE] = {
		embed_size >> 24 & 0xff,
		embed_size >> 16 & 0xff,
		embed_size >>  8 & 0xff,
		embed_size >>  0 & 0xff,
		'b', 'e', 'm', 'b',
	};
	if (fwrite(header, sizeof(header), 1, container) != 1) {
		return 0;
	} else {
		return embed_size;
	}
}

static inline uint32_t
bembd_put(FILE* container, FILE* file) {
	uint32_t embed_size = (uint32_t)bembd_cp(container, file);
	if (embed_size == 0) { return embed_size; }

	return bembd_write_header(container, embed_size);
}

static inline uint32_t
bembd_find(FILE* file) {
	if (fseek(file, 0, SEEK_END) != 0) {
		return 0;
	}

	long file_size = ftell(file);
	if (file_size < (long)BEMBD_HEADER_SIZE) { return 0; }

	if (fseek(file, file_size - (long)BEMBD_HEADER_SIZE, SEEK_SET) != 0) {
		return 0;
	}

	uint8_t header[BEMBD_HEADER_SIZE];
	if (fread(header, sizeof(header), 1, file) != 1) {
		return 0;
	}

	if (
		header[4] != 'b'
		|| header[5] != 'e'
		|| header[6] != 'm'
		|| header[7] != 'b'
	) {
		return 0;
	}
	uint32_t embed_size =
		  ((uint8_t)header[0] << 24)
		| ((uint8_t)header[1] << 16)
		| ((uint8_t)header[2] <<  8)
		| ((uint8_t)header[3] <<  0);

	fseek(file, file_size - (long)embed_size - (long)BEMBD_HEADER_SIZE, SEEK_SET);

	return embed_size;
}

#endif
