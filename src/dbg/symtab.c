#include <buxn/dbg/symtab.h>
#include <string.h>
#include <bserial.h>
#include <mem_layout.h>
#include "btmp_buf.h"

typedef struct {
	uint32_t num_symbols;
	uint32_t num_strings;
	uint32_t string_pool_size;
} buxn_dbg_symtab_header_t;

typedef struct {
	int exp;
	uint32_t num_entries;
	const char** strings;
	uint32_t* indices;
} buxn_dbg_symtab_strpool_t;

struct buxn_dbg_symtab_writer_s {
	buxn_dbg_symtab_writer_opts_t options;
	bserial_ctx_t* bserial;
	buxn_dbg_symtab_strpool_t strpool;
};

struct buxn_dbg_symtab_reader_s {
	buxn_dbg_symtab_reader_opts_t options;
	bserial_ctx_t* bserial;
	buxn_dbg_symtab_strpool_t strpool;
	buxn_dbg_symtab_header_t header;
	char* str_buf;
};

static const bserial_ctx_config_t buxn_dbg_symtab_bserial_config = {
	.max_num_symbols = 16,
	.max_symbol_len = 16,
	.max_record_fields = 16,
	.max_depth = 6,
};

// https://nullprogram.com/blog/2022/08/08/
static int32_t
buxn_msi(uint64_t hash, int exp, int32_t idx) {
	uint32_t mask = ((uint32_t)1 << exp) - 1;
	uint32_t step = (uint32_t)((hash >> (64 - exp)) | 1);
	return (idx + step) & mask;
}

// https://nullprogram.com/blog/2018/07/31/
static inline uint64_t
buxn_splittable64(uint64_t x) {
	x ^= x >> 30;
	x *= 0xbf58476d1ce4e5b9U;
	x ^= x >> 27;
	x *= 0x94d049bb133111ebU;
	x ^= x >> 31;
	return x;
}

static size_t
buxn_dbg_symtab_writer_layout(
	buxn_dbg_symtab_writer_t* writer,
	const buxn_dbg_symtab_writer_opts_t* options
) {
	mem_layout_t layout = { 0 };
	mem_layout_reserve(&layout, sizeof(buxn_dbg_symtab_writer_t), _Alignof(buxn_dbg_symtab_writer_t));

	size_t bserial_mem_size = bserial_ctx_mem_size(buxn_dbg_symtab_bserial_config);
	ptrdiff_t bserial = mem_layout_reserve(&layout, bserial_mem_size, _Alignof(void*));

	uint32_t num_strings = options->num_files;
	uint32_t strhash_size = 2;
	int strhash_exp = 1;
	while (strhash_size < num_strings) {
		strhash_exp += 1;
		strhash_size = strhash_size << 1;
	}
	ptrdiff_t strhash = mem_layout_reserve(
		&layout,
		sizeof(const char*) * strhash_size,
		_Alignof(const char*)
	);
	ptrdiff_t str_indices = mem_layout_reserve(
		&layout,
		sizeof(uint32_t) * strhash_size,
		_Alignof(uint32_t)
	);

	if (writer != NULL) {
		writer->options = *options;
		writer->bserial = bserial_make_ctx(
			mem_layout_locate(writer, bserial),
			buxn_dbg_symtab_bserial_config,
			NULL,
			options->output
		);
		writer->strpool.exp = strhash_exp;
		writer->strpool.strings = mem_layout_locate(writer, strhash);
		writer->strpool.indices = mem_layout_locate(writer, str_indices);
		writer->strpool.num_entries = num_strings;
		memset(writer->strpool.strings, 0, sizeof(const char*) * strhash_size);
	}

	return mem_layout_size(&layout);
}

static size_t
buxn_dbg_symtab_reader_layout(
	buxn_dbg_symtab_reader_t* reader,
	const buxn_dbg_symtab_reader_opts_t* options
) {
	mem_layout_t layout = { 0 };
	mem_layout_reserve(&layout, sizeof(buxn_dbg_symtab_reader_t), _Alignof(buxn_dbg_symtab_reader_t));

	size_t bserial_mem_size = bserial_ctx_mem_size(buxn_dbg_symtab_bserial_config);
	ptrdiff_t bserial = mem_layout_reserve(&layout, bserial_mem_size, _Alignof(void*));

	if (reader != NULL) {
		reader->options = *options;
		reader->bserial = bserial_make_ctx(
			mem_layout_locate(reader, bserial),
			buxn_dbg_symtab_bserial_config,
			options->input,
			NULL
		);
		reader->strpool = (buxn_dbg_symtab_strpool_t) { 0 };
		reader->header = (buxn_dbg_symtab_header_t) { 0 };
	}

	return mem_layout_size(&layout);
}

static buxn_dbg_symtab_io_status_t
buxn_dbg_convert_status(bserial_status_t status) {
	switch (status) {
		case BSERIAL_OK:
			return BUXN_DBG_SYMTAB_OK;
		case BSERIAL_IO_ERROR:
			return BUXN_DBG_SYMTAB_IO_ERROR;
		case BSERIAL_MALFORMED:
			return BUXN_DBG_SYMTAB_MALFORMED;
		default:
			return BUXN_DBG_SYMTAB_MALFORMED;
	}
}

static size_t
buxn_dbg_symtab_layout(buxn_dbg_symtab_t* symtab, buxn_dbg_symtab_reader_t* reader) {
	buxn_dbg_symtab_header_t header = reader->header;

	mem_layout_t layout = { 0 };
	mem_layout_reserve(&layout, sizeof(buxn_dbg_symtab_t), _Alignof(buxn_dbg_symtab_t));

	ptrdiff_t symbols = mem_layout_reserve(&layout, sizeof(buxn_dbg_sym_t) * header.num_symbols, _Alignof(buxn_dbg_sym_t));
	ptrdiff_t strings = mem_layout_reserve(&layout, sizeof(const char*) * header.num_strings, _Alignof(const char*));
	ptrdiff_t string_buf = mem_layout_reserve(&layout, header.string_pool_size, _Alignof(char));

	if (symtab != NULL) {
		symtab->symbols = mem_layout_locate(symtab, symbols);
		symtab->num_symbols = header.num_symbols;
		reader->strpool.num_entries = header.num_strings;
		reader->strpool.strings = mem_layout_locate(symtab, strings);
		reader->str_buf = mem_layout_locate(symtab, string_buf);
	}

	return mem_layout_size(&layout);
}

static bserial_status_t
buxn_dbg_serialize_symtab_header(bserial_ctx_t* ctx, buxn_dbg_symtab_header_t* header) {
	uint64_t len = 2;
	BSERIAL_CHECK_STATUS(bserial_array(ctx, &len));
	if (len != 2) { return BSERIAL_MALFORMED; }

	BSERIAL_RECORD(ctx, header) {
		BSERIAL_KEY(ctx, num_symbols) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &header->num_symbols));
		}

		BSERIAL_KEY(ctx, num_strings) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &header->num_strings));
		}

		BSERIAL_KEY(ctx, string_pool_size) {
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &header->string_pool_size));
		}
	}

	return bserial_status(ctx);
}

static bserial_status_t
buxn_dbg_serialize_symtab_body(
	bserial_ctx_t* ctx,
	btmp_buf_t* str_buf,
	buxn_dbg_symtab_strpool_t* strpool,
	buxn_dbg_symtab_t* symtab
) {
	BSERIAL_RECORD(ctx, symtab) {
		BSERIAL_KEY(ctx, strpool) {
			uint64_t len = strpool->num_entries;
			BSERIAL_CHECK_STATUS(bserial_array(ctx, &len));
			if (len != strpool->num_entries) { return BSERIAL_MALFORMED; }

			if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
				for (uint64_t i = 0; i < len; ++i) {
					uint64_t string_len;
					BSERIAL_CHECK_STATUS(bserial_blob_header(ctx, &string_len));
					char* str = btmp_buf_alloc_str(str_buf, string_len);
					if (str == NULL) { return BSERIAL_MALFORMED; }
					BSERIAL_CHECK_STATUS(bserial_blob_body(ctx, str));
					strpool->strings[i] = str;
				}
			} else {
				int strpool_len = 1 << strpool->exp;
				for (int i = 0; i < strpool_len; ++i) {
					const char* str = strpool->strings[i];
					if (str != NULL) {
						uint64_t string_len = strlen(str);
						BSERIAL_CHECK_STATUS(bserial_blob_header(ctx, &string_len));
						BSERIAL_CHECK_STATUS(bserial_blob_body(ctx, (char*)str));
					}
				}
			}
		}

		BSERIAL_KEY(ctx, symtab) {
			uint64_t len = symtab->num_symbols;
			BSERIAL_CHECK_STATUS(bserial_table(ctx, &len));
			if (len != symtab->num_symbols) { return BSERIAL_MALFORMED; }

			for (uint64_t i = 0; i < len; ++i) {
				buxn_dbg_sym_t* symbol = &symtab->symbols[i];

				BSERIAL_RECORD(ctx, symbol) {
					BSERIAL_KEY(ctx, type) {
						uint8_t sym_type = symbol->type;
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &sym_type));
						symbol->type = sym_type;
					}

					BSERIAL_KEY(ctx, id) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->id));
					}

					BSERIAL_KEY(ctx, addr_min) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->addr_min));
					}

					BSERIAL_KEY(ctx, addr_max) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->addr_max));
					}

					BSERIAL_KEY(ctx, filename) {
						uint32_t str_id;

						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &str_id));
							if (str_id >= strpool->num_entries) { return BSERIAL_MALFORMED; }
							symbol->region.filename = strpool->strings[str_id];
						} else {
							str_id = UINT32_MAX;

							uint64_t hash = buxn_splittable64((uint64_t)(uintptr_t)symbol->region.filename);
							for (int32_t hash_index = (int32_t)hash;;) {
								hash_index = buxn_msi(hash, strpool->exp, hash_index);
								if (strpool->strings[hash_index] == symbol->region.filename) {
									str_id = strpool->indices[hash_index];
									break;
								}
							}

							if (str_id >= symtab->num_symbols) { return BSERIAL_MALFORMED; }
							BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &str_id));
						}
					}

					BSERIAL_KEY(ctx, start.line) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.start.line));
					}

					BSERIAL_KEY(ctx, start.col) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.start.col));
					}

					BSERIAL_KEY(ctx, start.byte) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.start.byte));
					}

					BSERIAL_KEY(ctx, end.line) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.end.line));
					}

					BSERIAL_KEY(ctx, end.col) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.end.col));
					}

					BSERIAL_KEY(ctx, end.byte) {
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &symbol->region.range.end.byte));
					}
				}
			}
		}
	}

	return bserial_status(ctx);
}

size_t
buxn_dbg_symtab_writer_mem_size(const buxn_dbg_symtab_writer_opts_t* options) {
	return buxn_dbg_symtab_writer_layout(NULL, options);
}

buxn_dbg_symtab_writer_t*
buxn_dbg_make_symtab_writer(void* mem, const buxn_dbg_symtab_writer_opts_t* options) {
	buxn_dbg_symtab_writer_layout(mem, options);
	return mem;
}

buxn_dbg_symtab_io_status_t
buxn_dbg_write_symtab(buxn_dbg_symtab_writer_t* writer, const buxn_dbg_symtab_t* symtab) {
	buxn_dbg_symtab_header_t header = {
		// Files are the only strings right now
		.num_strings = writer->options.num_files,
		.num_symbols = symtab->num_symbols,
	};

	// Calculate string pool size
	uint32_t strpool_size = 0;
	for (uint32_t i = 0; i < symtab->num_symbols; ++i) {
		const buxn_dbg_sym_t* symbol = &symtab->symbols[i];
		// Filenames are interned so we can just hash the pointers
		const char* filename = symbol->region.filename;
		uint64_t hash = buxn_splittable64((uint64_t)(uintptr_t)filename);
		for (int32_t hash_index = (int32_t)hash;;) {
			hash_index = buxn_msi(hash, writer->strpool.exp, hash_index);
			if (writer->strpool.strings[hash_index] == NULL) {
				writer->strpool.strings[hash_index] = filename;
				strpool_size += (uint32_t)strlen(filename) + 1;
				break;
			} else if (writer->strpool.strings[hash_index] == filename) {
				break;
			}
		}
	}
	int strpool_len = 1 << writer->strpool.exp;
	uint32_t str_id = 0;
	for (int i = 0; i < strpool_len; ++i) {
		if (writer->strpool.strings[i] != NULL) {
			writer->strpool.indices[i] = str_id++;
		}
	}
	header.string_pool_size = strpool_size;

	bserial_status_t status;
	if ((status = buxn_dbg_serialize_symtab_header(writer->bserial, &header)) != BSERIAL_OK) {
		return buxn_dbg_convert_status(status);
	}

	return buxn_dbg_convert_status(
		buxn_dbg_serialize_symtab_body(
			writer->bserial, NULL, &writer->strpool, (buxn_dbg_symtab_t*)symtab
		)
	);
}

size_t
buxn_dbg_symtab_reader_mem_size(const buxn_dbg_symtab_reader_opts_t* options) {
	return buxn_dbg_symtab_reader_layout(NULL, options);
}

buxn_dbg_symtab_reader_t*
buxn_dbg_make_symtab_reader(void* mem, const buxn_dbg_symtab_reader_opts_t* options) {
	buxn_dbg_symtab_reader_layout(mem, options);
	return mem;
}

buxn_dbg_symtab_io_status_t
buxn_dbg_read_symtab_header(buxn_dbg_symtab_reader_t* reader) {
	return buxn_dbg_convert_status(
		buxn_dbg_serialize_symtab_header(reader->bserial, &reader->header)
	);
}

size_t
buxn_dbg_symtab_mem_size(buxn_dbg_symtab_reader_t* reader) {
	return buxn_dbg_symtab_layout(NULL, reader);
}

buxn_dbg_symtab_io_status_t
buxn_dbg_read_symtab(buxn_dbg_symtab_reader_t* reader, buxn_dbg_symtab_t* symtab) {
	buxn_dbg_symtab_layout(symtab, reader);
	btmp_buf_t str_buf = {
		.mem = reader->str_buf,
		.size = reader->header.string_pool_size,
	};

	return buxn_dbg_convert_status(
		buxn_dbg_serialize_symtab_body(reader->bserial, &str_buf, &reader->strpool, symtab)
	);
}
