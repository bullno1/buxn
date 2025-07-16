// vim: set foldmethod=marker foldlevel=0:
#include <stdlib.h>
#include <string.h>
#include <barena.h>
#include <bmacro.h>
#include <blog.h>
#include <bestline.h>
#include <utf8proc.h>
#include <physfs.h>
#include <buxn/vm/vm.h>
#include <buxn/asm/asm.h>
#include <buxn/asm/chess.h>
#include <buxn/devices/console.h>
#include <buxn/devices/system.h>
#include <buxn/devices/datetime.h>
#include <buxn/devices/file.h>
#include "repl.rc"

typedef struct {
	buxn_console_t console;
	buxn_file_t file[BUXN_NUM_FILE_DEVICES];

	buxn_vm_t* vm;
	buxn_chess_vm_state_t print_stack_state;
	bool terminated;
	bool need_reset;
} buxn_repl_t;

struct buxn_asm_ctx_s {
	barena_t* arena;
	buxn_repl_t* repl;
	buxn_chess_t* chess;
	int num_chess_errors;
	int mark_depth;
	bool assembled_line;
};

struct buxn_asm_file_s {
	int (*getc)(buxn_asm_file_t* file);
	void (*close)(buxn_asm_file_t* file);
};

typedef struct {
	buxn_asm_file_t base;

	const char* content;
	int len;
	int pos;
} buxn_repl_file_memory_t;

typedef enum {
	BUXN_REPL_READLINE_OPEN,
	BUXN_REPL_READLINE_EOL,
	BUXN_REPL_READLINE_EOF,
} buxn_repl_readline_state_t;

typedef struct {
	buxn_asm_file_t base;

	buxn_asm_ctx_t* basm;
	buxn_repl_readline_state_t state;
	char* line;
	int pos;
} buxn_repl_file_readline_t;

typedef struct {
	buxn_asm_file_t base;

	PHYSFS_file* handle;
} buxn_repl_file_passthrough_t;

static int
buxn_repl_str_width(
	const char* str,
	int len
) {
	int offset = 0;
	int width = 0;
	while (offset < len) {
		utf8proc_int32_t codepoint;
		utf8proc_ssize_t num_bytes = utf8proc_iterate(
			(const utf8proc_uint8_t*)str + offset,
			len - offset,
			&codepoint
		);
		if (num_bytes < 0) { break; }
		width += utf8proc_charwidth(codepoint);
		offset += (int)num_bytes;
	}
	return width;
}

static void
buxn_repl_print_stack(
	buxn_chess_t* chess,
	const char* name,
	const buxn_chess_stack_t* sym_stack,
	const uint8_t* vm_stack, uint8_t vm_stack_size
) {
	if (vm_stack_size == sym_stack->size) {
		// Do fancy printing
		fprintf(stderr, "%s:", name);
		int widths[256];
		for (uint8_t i = 0; i < sym_stack->len; ++i) {
			buxn_chess_value_t value = sym_stack->content[i];
			int min_size = (value.semantics & BUXN_CHESS_SEM_SIZE_SHORT) == 0
				? 2
				: 4;
			buxn_chess_str_t value_name = buxn_chess_format_value(chess, &value);
			int name_width = (int)buxn_repl_str_width(value_name.chars, value_name.len);
			fprintf(stderr, "|%*.*s", (int)min_size, value_name.len, value_name.chars);
			widths[i] = name_width >= min_size ? name_width : min_size;
		}
		fprintf(stderr, "|\n");
		fprintf(stderr, "%*s ", (int)strlen(name), "");

		uint8_t offset = 0;
		for (uint8_t i = 0; i < sym_stack->len; ++i) {
			char str[5];
			if ((sym_stack->content[i].semantics & BUXN_CHESS_SEM_SIZE_MASK) == BUXN_CHESS_SEM_SIZE_SHORT) {
				uint8_t hi = vm_stack[offset];
				uint8_t lo = vm_stack[offset + 1];
				offset += 2;
				snprintf(str, sizeof(str), "%04x", (hi << 8) | lo);
			} else {
				uint8_t value = vm_stack[offset++];
				snprintf(str, sizeof(str), "%02x", value);
			}
			fprintf(stderr, "|%*s", widths[i], str);
		}
		fprintf(stderr, "|\n");
	}else {
		// Overflow or underflow happened, do simple printing
		fprintf(stderr, "%s:", name);
		for (uint8_t i = 0; i < vm_stack_size; ++i) {
			fprintf(stderr, " %02hhX", vm_stack[i]);
		}
		fprintf(stderr, "\n");
	}
}

int
main(int argc, const char* argv[]) {
	(void)argc;
	PHYSFS_init(argv[0]);
	PHYSFS_mount(".", "", 1);
	PHYSFS_setWriteDir(".");

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
	blog_add_file_logger(log_level, &(blog_file_logger_options_t){
		.file = stderr,
		.with_colors = true,
	});

	barena_pool_t arena_pool;
	barena_pool_init(&arena_pool, 1);

	buxn_repl_t repl = { 0 };
	repl.vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	repl.vm->config = (buxn_vm_config_t){
		.userdata = &repl,
		.memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS,
	};
	buxn_vm_reset(repl.vm, BUXN_VM_RESET_ALL);

	barena_t arena_a;
	barena_t arena_b;
	barena_init(&arena_a, &arena_pool);
	barena_init(&arena_b, &arena_pool);
	barena_t* current_arena = &arena_a;
	while (!repl.terminated) {
		current_arena = current_arena == &arena_a ? &arena_b : &arena_a;
		buxn_asm_ctx_t basm = {
			.arena = current_arena,
			.repl = &repl,
		};
		barena_reset(basm.arena);

		basm.chess = buxn_chess_begin(&basm);
		bool success = buxn_asm(&basm, "/repl/main");
		if (success && basm.assembled_line) {
			buxn_chess_end(basm.chess);
			success &= basm.num_chess_errors == 0;
		}

		if (success && basm.assembled_line) {
			buxn_vm_execute(repl.vm, BUXN_RESET_VECTOR);

			if (repl.need_reset) {
				buxn_vm_reset(repl.vm, BUXN_VM_RESET_SOFT);
				memset(&repl.print_stack_state, 0, sizeof(repl.print_stack_state));
				repl.need_reset = false;
			} else {
				buxn_repl_print_stack(
					basm.chess,
					"WST",
					&repl.print_stack_state.wst,
					repl.vm->ws, repl.vm->wsp
				);
				buxn_repl_print_stack(
					basm.chess,
					"RST",
					&repl.print_stack_state.rst,
					repl.vm->rs, repl.vm->rsp
				);
			}
		}
	}
	barena_reset(&arena_a);
	barena_reset(&arena_b);

	free(repl.vm);
	barena_pool_cleanup(&arena_pool);

	PHYSFS_deinit();
	return 0;
}

// vm {{{

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	buxn_repl_t* repl = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &repl->console, address);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			return buxn_file_dei(
				vm,
				repl->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	buxn_repl_t* repl = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &repl->console, address);
			break;
		case BUXN_DEVICE_FILE_0:
		case BUXN_DEVICE_FILE_1:
			buxn_file_deo(
				vm,
				repl->file + (device_id - BUXN_DEVICE_FILE_0) / (BUXN_DEVICE_FILE_1 - BUXN_DEVICE_FILE_0),
				vm->device + device_id,
				buxn_device_port(address)
			);
			break;
	}
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
	buxn_repl_t* repl = vm->config.userdata;
	if (value == 0xff) {
		repl->need_reset = true;
	}
}

void
buxn_system_set_metadata(buxn_vm_t* vm, uint16_t address) {
	(void)vm;
	(void)address;
}

void
buxn_system_theme_changed(buxn_vm_t* vm) {
	(void)vm;
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stdout);
	fflush(stdout);
}

void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stderr);
	fflush(stdout);
}

/// }}}

// vfs {{{

static int
buxn_repl_mem_file_getc(buxn_asm_file_t* base) {
	buxn_repl_file_memory_t* file = BCONTAINER_OF(base, buxn_repl_file_memory_t, base);
	if (file->pos >= file->len) {
		return BUXN_ASM_IO_EOF;
	}

	return file->content[file->pos++];
}

static void
buxn_repl_mem_file_close(buxn_asm_file_t* base) {
	buxn_repl_file_memory_t* file = BCONTAINER_OF(base, buxn_repl_file_memory_t, base);
	free(file);
}

static buxn_asm_file_t*
buxn_repl_open_mem_file(const void* content, size_t size) {
	buxn_repl_file_memory_t* file = malloc(sizeof(buxn_repl_file_memory_t));
	*file = (buxn_repl_file_memory_t){
		.base = {
			.getc = buxn_repl_mem_file_getc,
			.close = buxn_repl_mem_file_close,
		},
		.content = content,
		.len = (int)size,
	};
	return &file->base;
}

static int
buxn_repl_readline_file_getc(buxn_asm_file_t* base) {
	buxn_repl_file_readline_t* file = BCONTAINER_OF(base, buxn_repl_file_readline_t, base);

	switch (file->state) {
		case BUXN_REPL_READLINE_EOF:
			return BUXN_ASM_IO_EOF;
		case BUXN_REPL_READLINE_EOL: {
			if (file->basm->mark_depth > 0) {
				file->state = BUXN_REPL_READLINE_OPEN;
			} else {
				return BUXN_ASM_IO_EOF;
			}
		}
		// fallthrough
		case BUXN_REPL_READLINE_OPEN: {
			if (file->line == NULL) {
				char fmt_buf[sizeof("[4294967295> ")];
				const char* prompt;
				if (file->basm->mark_depth > 0) {
					snprintf(fmt_buf, sizeof(fmt_buf), "[%d> ", file->basm->mark_depth);
					prompt = fmt_buf;
				} else {
					prompt = "> ";
				}

				file->pos = 0;
				file->line = bestlineWithHistory(prompt, "buxn-repl");
				if (file->line == NULL) {
					file->basm->repl->terminated = true;
					file->state = BUXN_REPL_READLINE_EOF;
					return BUXN_ASM_IO_EOF;
				}
			}
		} break;
	}

	char ch = file->line[file->pos++];
	if (ch == 0) {
		bestlineFree(file->line);
		file->line = NULL;
		file->state = BUXN_REPL_READLINE_EOL;
		return '\n';
	} else {
		return ch;
	}
}

static void
buxn_repl_readline_file_close(buxn_asm_file_t* base) {
	buxn_repl_file_readline_t* file = BCONTAINER_OF(base, buxn_repl_file_readline_t, base);
	bestlineFree(file->line);
	free(file);
}

static buxn_asm_file_t*
buxn_repl_open_readline_file(buxn_asm_ctx_t* basm) {
	buxn_repl_file_readline_t* file = malloc(sizeof(buxn_repl_file_readline_t));
	*file = (buxn_repl_file_readline_t){
		.state = BUXN_REPL_READLINE_OPEN,
		.base = {
			.getc = buxn_repl_readline_file_getc,
			.close = buxn_repl_readline_file_close,
		},
		.basm = basm,
	};
	return &file->base;
}

static int
buxn_repl_passthrough_file_getc(buxn_asm_file_t* base) {
	buxn_repl_file_passthrough_t* file = BCONTAINER_OF(base, buxn_repl_file_passthrough_t, base);

	char ch;
	PHYSFS_sint64 bytes_read = PHYSFS_readBytes(file->handle, &ch, sizeof(ch));
	if (bytes_read == 0) {
		return BUXN_ASM_IO_EOF;
	} else if (bytes_read < 0) {
		return BUXN_ASM_IO_ERROR;
	} else {
		return ch;
	}
}

static void
buxn_repl_passthrough_file_close(buxn_asm_file_t* base) {
	buxn_repl_file_passthrough_t* file = BCONTAINER_OF(base, buxn_repl_file_passthrough_t, base);
	PHYSFS_close(file->handle);
	free(file);
}

static buxn_asm_file_t*
buxn_repl_open_passthrough_file(const char* filename) {
	PHYSFS_file* handle = PHYSFS_openRead(filename);
	if (handle == NULL) { return NULL; }

	buxn_repl_file_passthrough_t* file = malloc(sizeof(buxn_repl_file_passthrough_t));
	*file = (buxn_repl_file_passthrough_t){
		.base = {
			.getc = buxn_repl_passthrough_file_getc,
			.close = buxn_repl_passthrough_file_close,
		},
		.handle = handle,
	};
	return &file->base;
}

// }}}

// asm {{{

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	ctx->repl->vm->memory[address] = value;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	buxn_chess_handle_symbol(ctx->chess, addr, sym);

	if (sym->type == BUXN_ASM_SYM_MARK) {
		ctx->mark_depth += sym->name[0] == '[' ? 1 : -1;
	}

	if (strcmp(sym->region.filename, "/repl/line") == 0) {
		ctx->assembled_line = true;
	}
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

	if (strcmp(report->region->filename, "/repl/main") == 0) {
		return;
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

	// TODO: print region
	if (report->region->range.start.line != 0 && level != BLOG_LEVEL_INFO) {
		/*print_file_region(ctx, report->region);*/
	}

	if (report->related_message != NULL) {
		blog_write(
			BLOG_LEVEL_INFO,
			report->related_region->filename, report->related_region->range.start.line,
			"%s:", report->related_message
		);
		/*print_file_region(ctx, report->related_region);*/
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	(void)ctx;
	if (strcmp(filename, "/repl/main") == 0) {
		xincbin_data_t repl_main = XINCBIN_GET(repl_main);
		return buxn_repl_open_mem_file(repl_main.data, repl_main.size);
	} else if (strcmp(filename, "/repl/signature") == 0) {
		buxn_chess_str_t wst_str = buxn_chess_format_stack(
			ctx->chess,
			ctx->repl->print_stack_state.wst.content,
			ctx->repl->print_stack_state.wst.len
		);
		buxn_chess_str_t rst_str = buxn_chess_format_stack(
			ctx->chess,
			ctx->repl->print_stack_state.rst.content,
			ctx->repl->print_stack_state.rst.len
		);
		int len = snprintf(
			NULL, 0,
			"(%.*s .%.*s -> )",
			wst_str.len, wst_str.chars,
			rst_str.len, rst_str.chars
		);
		char* signature = buxn_chess_alloc(ctx, len + 1, _Alignof(char));
		snprintf(
			signature, len + 1,
			"(%.*s .%.*s -> )",
			wst_str.len, wst_str.chars,
			rst_str.len, rst_str.chars
		);
		return buxn_repl_open_mem_file(signature, len);
	} else if (strcmp(filename, "/repl/line") == 0) {
		return buxn_repl_open_readline_file(ctx);
	} else {
		return buxn_repl_open_passthrough_file(filename);
	}
}

void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	file->close(file);
}

int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	return file->getc(file);
}

// }}}

// chess {{{

void
buxn_chess_begin_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_id_t parent_id
) {
	(void)ctx;
	(void)trace_id;
	(void)parent_id;
}

void
buxn_chess_end_trace(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	bool success
) {
	(void)success;
	(void)ctx;
	(void)trace_id;
}

void
buxn_chess_deo(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	const buxn_chess_vm_state_t* state,
	uint8_t value,
	uint8_t port
) {
	(void)trace_id;
	if (port == 0x0e) {
		if (value == 0x2b) {
			ctx->repl->print_stack_state = *state;
		}
	}
}

uint8_t
buxn_chess_get_rom(buxn_asm_ctx_t* ctx, uint16_t address) {
	return ctx->repl->vm->memory[address];
}

static void
buxn_chess_log(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	blog_level_t level,
	const buxn_asm_report_t* report
) {
	(void)ctx;
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

	// TODO: print region
	if (report->region->range.start.line != 0 && level != BLOG_LEVEL_INFO) {
		/*print_file_region(ctx, report->region);*/
	}

	if (report->related_message != NULL) {
		blog_write(
			BLOG_LEVEL_INFO,
			report->related_region->filename, report->related_region->range.start.line,
			"%s:", report->related_message
		);
		/*print_file_region(ctx, report->related_region);*/
	}
}

void
buxn_chess_report(
	buxn_asm_ctx_t* ctx,
	buxn_chess_id_t trace_id,
	buxn_chess_report_type_t type,
	const buxn_asm_report_t* report
) {
	switch (type) {
		case BUXN_CHESS_REPORT_TRACE:
			break;
		case BUXN_CHESS_REPORT_WARNING:
			buxn_chess_log(ctx, trace_id, BLOG_LEVEL_WARN, report);
			break;
		case BUXN_CHESS_REPORT_ERROR:
			// Suppress stack error in reset
			if (strcmp(report->region->filename, "/repl/main") != 0) {
				buxn_chess_log(ctx, trace_id, BLOG_LEVEL_ERROR, report);
				++ctx->num_chess_errors;
			}
			break;
	}
}

void*
buxn_chess_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return buxn_asm_alloc(ctx, size, alignment);
}

void*
buxn_chess_begin_mem_region(buxn_asm_ctx_t* ctx) {
	return (void*)barena_snapshot(ctx->arena);
}

void
buxn_chess_end_mem_region(buxn_asm_ctx_t* ctx, void* region) {
	barena_restore(ctx->arena, (barena_snapshot_t)region);
}

// }}}

#define BLIB_IMPLEMENTATION
#include <blog.h>
#include <barena.h>

#define XINCBIN_IMPLEMENTATION
#include "repl.rc"
