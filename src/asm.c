#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <barena.h>
#include <bhash.h>
#include <barray.h>
#include <blog.h>
#include <barg.h>
#include <bmacro.h>
#include <buxn/asm/asm.h>
#include <buxn/asm/chess.h>
#include <buxn/dbg/symtab.h>
#include <utf8proc.h>
#define BSERIAL_STDIO
#include <bserial.h>

typedef struct {
	int len;
	const char* chars;
} str_t;

typedef BHASH_TABLE(const char*, FILE*) file_table_t;

struct buxn_asm_ctx_s {
	uint16_t rom_size;
	uint16_t num_macros;
	uint16_t num_labels;
	uint32_t num_files;
	char rom[UINT16_MAX];

	FILE* sym_file;

	barray(buxn_dbg_sym_t) debug_symbols;
	buxn_dbg_sym_t current_symbol;

	barena_t arena;
	file_table_t file_table;
	barray(utf8proc_uint8_t) line_buf;

	buxn_chess_t* chess;
	int trace_id;
	bool focus;
};

static void
buxn_asm_put_dbg_sym(
	buxn_asm_ctx_t* ctx,
	buxn_dbg_sym_type_t type,
	uint16_t addr,
	const buxn_asm_sym_t* sym
) {
	buxn_dbg_sym_t* current_symbol = &ctx->current_symbol;
	if (
		type == current_symbol->type
		&& sym->region.filename == current_symbol->region.filename  // filename is interned
		&& sym->region.range.start.byte == current_symbol->region.range.start.byte
		&& sym->region.range.end.byte == current_symbol->region.range.end.byte
	) {
		// Merge
		current_symbol->addr_max = addr;
	} else {
		// Flush previous
		if (current_symbol->region.range.start.line != 0) {
			barray_push(ctx->debug_symbols, *current_symbol, NULL);
		}

		// New symbol
		current_symbol->type = type;
		current_symbol->id = sym->id;
		current_symbol->addr_min = current_symbol->addr_max = addr;
		current_symbol->region = sym->region;
	}
}

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(&ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	uint16_t offset = address - 256;
	ctx->rom[offset] = value;
	ctx->rom_size = offset + 1 > ctx->rom_size ? offset + 1 : ctx->rom_size;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	switch (sym->type) {
		case BUXN_ASM_SYM_MACRO:
			++ctx->num_macros;
			break;
		case BUXN_ASM_SYM_LABEL: {
			++ctx->num_labels;

			if (ctx->sym_file) {
				uint8_t addr_hi = addr >> 8;
				uint8_t addr_lo = addr & 0xff;
				fwrite(&addr_hi, sizeof(addr_hi), 1, ctx->sym_file);
				fwrite(&addr_lo, sizeof(addr_hi), 1, ctx->sym_file);
				// All interned string are null-terminated so this is safe
				fwrite(sym->name, strlen(sym->name) + 1, 1, ctx->sym_file);

				if (ferror(ctx->sym_file)) {
					BLOG_ERROR("Error while writing symbol file: %s", strerror(errno));
				}
			}

			assert(sym->id != 0);
			buxn_asm_put_dbg_sym(ctx, BUXN_DBG_SYM_LABEL, addr, sym);
		} break;
		case BUXN_ASM_SYM_OPCODE:
			 buxn_asm_put_dbg_sym(ctx, BUXN_DBG_SYM_OPCODE, addr, sym);
			 break;
		case BUXN_ASM_SYM_LABEL_REF:
			assert(sym->id != 0);
			buxn_asm_put_dbg_sym(ctx, BUXN_DBG_SYM_LABEL_REF, addr, sym);
			break;
		case BUXN_ASM_SYM_TEXT:
			buxn_asm_put_dbg_sym(ctx, BUXN_DBG_SYM_TEXT, addr, sym);
			break;
		case BUXN_ASM_SYM_NUMBER:
			buxn_asm_put_dbg_sym(ctx, BUXN_DBG_SYM_NUMBER, addr, sym);
			break;
		case BUXN_ASM_SYM_COMMENT:
		case BUXN_ASM_SYM_MACRO_REF:
		case BUXN_ASM_SYM_MARK:
			break;
	}

	if (ctx->chess != NULL) {
		buxn_chess_handle_symbol(ctx->chess, addr, sym);
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	++ctx->num_files;
	return (void*)fopen(filename, "rb");
}

void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	fclose((void*)file);
}

int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	int result = fgetc((void*)file);
	if (result == EOF) {
		return BUXN_ASM_IO_EOF;
	} else if (result < 0) {
		return BUXN_ASM_IO_ERROR;
	} else {
		return result;
	}
}

static FILE*
open_file(buxn_asm_ctx_t* ctx, const char* filename) {
	bhash_index_t index = bhash_find(&ctx->file_table, filename);
	if (bhash_is_valid(index)) {
		return ctx->file_table.values[index];
	} else {
		FILE* file = fopen(filename, "rb");
		bhash_put(&ctx->file_table, filename, file);
		return file;
	}
}

static void
print_file_region(buxn_asm_ctx_t* ctx, const buxn_asm_source_region_t* region) {
	FILE* file = open_file(ctx, region->filename);
	if (file == NULL) { return; }

	// Seek to the beginning of the line starting from the start byte offset
	for (int i = region->range.start.byte; i >= 0; --i) {
		fseek(file, i, SEEK_SET);

		if (i > 0) {
			char ch = 0;
			fread(&ch, 1, 1, file);

			if (ch == '\r' || ch == '\n') {
				fseek(file, i + 1, SEEK_SET);
				break;
			}
		}
	}

	// Print the line
	barray_clear(ctx->line_buf);
	fprintf(stderr, "      | ");
	while (true) {
		int ch = fgetc(file);
		if (ch < 0 || ch == '\r' || ch == '\n') {
			break;
		}
		fputc(ch, stderr);
		barray_push(ctx->line_buf, ch, NULL);
	}
	fputc('\n', stderr);

	// Print the squiggly pointer
	fprintf(stderr, "      | ");
	utf8proc_ssize_t offset = 0;
	utf8proc_ssize_t line_len = (utf8proc_ssize_t)barray_len(ctx->line_buf);
	while (true) {
		utf8proc_int32_t codepoint;
		utf8proc_ssize_t num_bytes = utf8proc_iterate(ctx->line_buf + offset, line_len - offset, &codepoint);
		if (num_bytes < 0) { break; }
		if (offset + num_bytes >= region->range.start.col) { break; }

		offset += num_bytes;
		if (offset >= line_len) { break; }

		// Replay the characters in the exact order with space replacing any
		// non-tab characters
		if (codepoint == '\t') {
			fprintf(stderr, "\t");
		} else {
			fprintf(stderr, "%*s", utf8proc_charwidth(codepoint), "");
		}
	}

	// Print the caret
	fprintf(stderr, "^");
	// The first character could be wide
	{
		utf8proc_int32_t codepoint;
		utf8proc_ssize_t num_bytes = utf8proc_iterate(ctx->line_buf + offset, line_len - offset, &codepoint);
		int char_width = utf8proc_charwidth(codepoint);
		for (int i = 0; i < char_width - 1; ++i) {
			fprintf(stderr, "~");
		}
		offset += num_bytes;
	}

	for (; offset < region->range.end.col && offset < line_len;) {
		utf8proc_int32_t codepoint;
		utf8proc_ssize_t num_bytes = utf8proc_iterate(ctx->line_buf + offset, line_len - offset, &codepoint);
		offset += num_bytes;
		if (num_bytes < 0) { break; }
		if (offset >= region->range.end.col) { break; }

		int char_width = utf8proc_charwidth(codepoint);
		for (int i = 0; i < char_width; ++i) {
			fprintf(stderr, "~");
		}
	}
	fprintf(stderr, "\n");
}

void
buxn_asm_report(
	buxn_asm_ctx_t* ctx,
	buxn_asm_report_type_t type,
	const buxn_asm_report_t* report
) {
	(void)ctx;
	blog_level_t level = BLOG_LEVEL_INFO;
	switch (type) {
		case BUXN_ASM_REPORT_ERROR: level = BLOG_LEVEL_ERROR; break;
		case BUXN_ASM_REPORT_WARNING: level = BLOG_LEVEL_WARN; break;
	}

	if (report->token == NULL) {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	} else {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s (`%s`)", report->message, report->token
		);
	}

	if (report->region->range.start.line != 0 && level != BLOG_LEVEL_INFO) {
		print_file_region(ctx, report->region);
	}

	if (report->related_message != NULL) {
		blog_write(
			BLOG_LEVEL_INFO,
			report->related_region->filename, report->related_region->range.start.line,
			"%s:", report->related_message
		);
		print_file_region(ctx, report->related_region);
	}
}

void*
buxn_chess_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return buxn_asm_alloc(ctx, size, alignment);
}

void*
buxn_chess_begin_mem_region(buxn_asm_ctx_t* ctx) {
	return (void*)barena_snapshot(&ctx->arena);
}

void
buxn_chess_end_mem_region(buxn_asm_ctx_t* ctx, void* region) {
	barena_restore(&ctx->arena, (barena_snapshot_t)region);
}

uint8_t
buxn_chess_get_rom(buxn_asm_ctx_t* ctx, uint16_t address) {
	return ctx->rom[address - 256];
}

static void
buxn_chess_log(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	blog_level_t level,
	const buxn_asm_report_t* report
) {
	if (ctx->focus && ctx->trace_id != trace_id) { return; }

	if (trace_id != BUXN_CHESS_NO_TRACE) {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"[%d] %s", trace_id, report->message
		);
	} else {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	}

	if (report->region->range.start.line != 0 && level != BLOG_LEVEL_INFO) {
		print_file_region(ctx, report->region);
	}

	if (report->related_message != NULL) {
		blog_write(
			BLOG_LEVEL_INFO,
			report->related_region->filename, report->related_region->range.start.line,
			"%s:", report->related_message
		);
		print_file_region(ctx, report->related_region);
	}
}

BFORMAT_ATTRIBUTE(5, 6)
static void
buxn_chess_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const char* file,
	int line,
	const char* fmt,
	...
) {
	blog_level_t level;
	if (
		ctx->trace_id != BUXN_CHESS_NO_TRACE
		&&
		ctx->trace_id == trace_id
	) {
		level = BLOG_LEVEL_INFO;
	} else {
		level = BLOG_LEVEL_TRACE;
	}

	va_list args;
	va_start(args, fmt);
	blog_vwrite(level, file, line, fmt, args);
	va_end(args);
}

void
buxn_chess_report(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_report_type_t type,
	const buxn_asm_report_t* report
) {
	if (ctx->focus && ctx->trace_id != trace_id) { return; }

	switch (type) {
		case BUXN_CHESS_REPORT_TRACE:
			buxn_chess_trace(
				ctx,
				trace_id,
				report->region->filename, report->region->range.start.line,
				"%s",
				report->message
			);
			break;
		case BUXN_CHESS_REPORT_WARNING:
			buxn_chess_log(ctx, trace_id, BLOG_LEVEL_WARN, report);
			break;
		case BUXN_CHESS_REPORT_ERROR:
			buxn_chess_log(ctx, trace_id, BLOG_LEVEL_ERROR, report);
			break;
	}
}

void
buxn_chess_begin_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_id_t parent_id
) {
	if (ctx->focus && ctx->trace_id != trace_id) { return; }

	if (parent_id == BUXN_CHESS_NO_TRACE) {
		buxn_chess_trace(
			ctx,
			trace_id,
			__FILE__, __LINE__,
			"[%d] Trace begin", trace_id
		);
	} else {
		buxn_chess_trace(
			ctx,
			trace_id,
			__FILE__, __LINE__,
			"[%d] Trace begin (parent: %d)", trace_id, parent_id
		);
	}
}

void
buxn_chess_end_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	bool success
) {
	(void)success;
	buxn_chess_trace(
		ctx,
		trace_id,
		__FILE__, __LINE__,
		"[%d] Trace end", trace_id
	);
}

void
buxn_chess_deo(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const buxn_chess_vm_state_t* state,
	uint8_t value,
	uint8_t port
) {
	if (port == 0x0e && value == 0x2b) {
		void* mem_region = buxn_chess_begin_mem_region(ctx);

		buxn_chess_str_t wst_str = buxn_chess_format_stack(
			ctx->chess, state->wst.content, state->wst.len
		);
		buxn_chess_str_t rst_str = buxn_chess_format_stack(
			ctx->chess, state->rst.content, state->rst.len
		);
		blog_write(
			BLOG_LEVEL_INFO,
			state->src_region.filename,
			state->src_region.range.start.line,
			"[%d] Stack: (%.*s .%.*s )",
			trace_id,
			(int)wst_str.len, wst_str.chars,
			(int)rst_str.len, rst_str.chars
		);
		print_file_region(ctx, &state->src_region);

		buxn_chess_end_mem_region(ctx, mem_region);
	}
}

static bool
write_rom(buxn_asm_ctx_t* ctx, const char* rom_path) {
	FILE* rom_file = NULL;
	bool success = true;
	rom_file = fopen(rom_path, "wb");
	if (rom_file == NULL) {
		BLOG_ERROR("Error while opening rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

	// Trim trailing zeros from rom
	uint16_t rom_size = ctx->rom_size;
	while (rom_size > 0 && ctx->rom[rom_size - 1] == 0) {
		--rom_size;
	}
	ctx->rom_size = rom_size;

	if (ctx->rom_size && fwrite(ctx->rom, ctx->rom_size, 1, rom_file) != 1) {
		BLOG_ERROR("Error while writing rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

	if (fflush(rom_file) != 0) {
		BLOG_ERROR("Error while writing rom file: %s", strerror(errno));
		success = false;
		goto end;
	}

end:
	if (rom_file != NULL) { fclose(rom_file); }
	return success;
}

static bhash_hash_t
str_hash(const void* data, size_t size) {
	(void)size;
	const char* str = *(const char**)data;
	size_t len = strlen(str);
	return bhash_hash(str, len);
}

int
main(int argc, const char* argv[]) {
	blog_level_t log_level;
#ifdef _DEBUG
	log_level = BLOG_LEVEL_DEBUG;
#else
	log_level = BLOG_LEVEL_INFO;
#endif
	blog_init(&(blog_options_t){
		.current_filename = __FILE__,
		.current_depth_in_project = 1,
	});
	blog_logger_id_t logger = blog_add_file_logger(
		log_level,
		&(blog_file_logger_options_t){
			.file = stderr,
			.with_colors = true,
		}
	);

	bool type_check = false;
	bool verbose = false;
	bool focus = false;
	int trace_id = BUXN_CHESS_NO_TRACE;
	barg_opt_t opts[] = {
		{
			.name = "chess",
			.short_name = 'c',
			.summary = "Enable the type checker",
			.description = "Because chess is better than checker",
			.boolean = true,
			.parser = barg_boolean(&type_check),
		},
		{
			.name = "trace",
			.short_name = 't',
			.value_name = "trace-id",
			.summary = "Trace a specific static analysis path",
			.description = "This has no effect if --chess is not provided",
			.parser = barg_int(&trace_id),
		},
		{
			.name = "focus",
			.short_name = 'f',
			.summary = "Focus only on the chosen trace and display nothing else",
			.description =
				"This has no effect either --chess or --trace are not provided",
			.boolean = true,
			.parser = barg_boolean(&focus),
		},
		{
			.name = "verbose",
			.short_name = 'v',
			.summary = "Enable verbose logging",
			.boolean = true,
			.parser = barg_boolean(&verbose),
		},
		barg_opt_help(),
	};

	barg_t barg = {
		.usage = "buxn-asm [options] [--] <in.tal> [out.rom]",
		.summary = "uxntal assembler",
		.opts = opts,
		.num_opts = BCOUNT_OF(opts),
		.allow_positional = true,
	};

	barg_result_t result = barg_parse(&barg, argc, argv);
	if (result.status != BARG_OK) {
		barg_print_result(&barg, result, stderr);
		return result.status == BARG_PARSE_ERROR;
	}
	int num_args = argc - result.arg_index;
	if (num_args < 1 || num_args > 2) {
		result.status = BARG_SHOW_HELP;
		barg_print_result(&barg, result, stderr);
		return 1;
	}
	const char* src_filename = argv[result.arg_index];
	const char* rom_filename = num_args == 2
		? argv[result.arg_index + 1]
		: NULL;

	if (verbose) {
		blog_set_min_log_level(logger, BLOG_LEVEL_TRACE);
	}

	barena_pool_t arena_pool;
	barena_pool_init(&arena_pool, 1);

	buxn_asm_ctx_t ctx = { 0 };
	barena_init(&ctx.arena, &arena_pool);

	bhash_config_t config = bhash_config_default();
	config.removable = false;
	config.hash = str_hash;
	bhash_init(&ctx.file_table, config);

	// temp buf for extra filenames
	size_t rom_filename_len = 0;
	size_t namebuf_len = 0;
	char* namebuf = NULL;
	if (rom_filename != NULL) {
		rom_filename_len = strlen(rom_filename);
		namebuf_len = rom_filename_len + 5;
		namebuf = barena_memalign(
			&ctx.arena,
			namebuf_len,
			_Alignof(char)
		);
	}

	// .sym file
	if (rom_filename != NULL) {
		snprintf(namebuf, namebuf_len, "%s.sym", rom_filename);
		ctx.sym_file = fopen(namebuf, "wb");
		if (ctx.sym_file == NULL) {
			BLOG_ERROR("Error while opening symbol file: %s", strerror(errno));
		}
	}

	if (type_check) {
		ctx.chess = buxn_chess_begin(&ctx);
		ctx.trace_id = trace_id;
		ctx.focus = focus;
	}
	bool success = buxn_asm(&ctx, src_filename);
	if (ctx.chess != NULL && success) {
		success &= buxn_chess_end(ctx.chess);
	}

	// Write .dbg file
	if (success && rom_filename != NULL) {
		snprintf(namebuf, namebuf_len, "%s.dbg", rom_filename);
		FILE* dbg_file = fopen(namebuf, "wb");
		if (dbg_file != NULL) {
			// Flush last entry
			if (ctx.current_symbol.region.range.start.line != 0) {
				barray_push(ctx.debug_symbols, ctx.current_symbol, NULL);
			}

			bserial_stdio_out_t bserial_out;
			buxn_dbg_symtab_writer_opts_t writer_opts = {
				.num_files = ctx.num_files,
				.output = bserial_stdio_init_out(&bserial_out, dbg_file),
			};
			buxn_dbg_symtab_writer_t* writer = buxn_dbg_make_symtab_writer(
				barena_malloc(&ctx.arena, buxn_dbg_symtab_writer_mem_size(&writer_opts)),
				&writer_opts
			);
			buxn_dbg_symtab_io_status_t status = buxn_dbg_write_symtab(
				writer,
				&(buxn_dbg_symtab_t){
					.num_symbols = (uint32_t)barray_len(ctx.debug_symbols),
					.symbols = ctx.debug_symbols,
				}
			);

			switch (status) {
				case BUXN_DBG_SYMTAB_OK:
					break;
				case BUXN_DBG_SYMTAB_IO_ERROR:
					BLOG_ERROR("Error while writing debug file: %s", strerror(errno));
					break;
				case BUXN_DBG_SYMTAB_MALFORMED:
					BLOG_ERROR("Error while writing debug file: %s", "Malformed symbol table");
					break;
			}

			fclose(dbg_file);
		} else {
			BLOG_ERROR("Error while writing debug file: %s", strerror(errno));
		}
	}

	barray_free(NULL, ctx.line_buf);
	barray_free(NULL, ctx.debug_symbols);
	for (bhash_index_t i = 0; i < bhash_len(&ctx.file_table); ++i) {
		fclose(ctx.file_table.values[i]);
	}
	bhash_cleanup(&ctx.file_table);
	barena_reset(&ctx.arena);
	barena_pool_cleanup(&arena_pool);

	if (success && rom_filename != NULL) {
		success &= write_rom(&ctx, rom_filename);
	}

	if (success && rom_filename) {
		BLOG_INFO(
			"Assembled %s in %d byte(s), %d label(s), %d macro(s)",
			rom_filename,
			ctx.rom_size,
			ctx.num_labels,
			ctx.num_macros
		);
	}

	if (ctx.sym_file != NULL) {
		if (fflush(ctx.sym_file) != 0) {
			BLOG_ERROR("Error while writing symbol file: %s", strerror(errno));
		}

		fclose(ctx.sym_file);
	}

	return success ? 0 : 1;
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <blog.h>
#include <bhash.h>
#include <barray.h>
#include <bserial.h>
#include <barg.h>
