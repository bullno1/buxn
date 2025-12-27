#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <barena.h>
#include <barray.h>
#include <blog.h>
#include <buxn/asm/asm.h>
#include "bflag.h"

#define FLAG_OUTPUT "-output="

typedef struct {
	const char* name;
	buxn_asm_source_region_t region;
	char kind;
} tag_t;

struct buxn_asm_ctx_s {
	barena_t arena;
	barray(tag_t) tags;
};

static void
add_tag(buxn_asm_ctx_t* ctx, const buxn_asm_sym_t* sym, char kind) {
	tag_t tag = {
		.kind = kind,
		.name = sym->name,
		.region = sym->region,
	};
	barray_push(ctx->tags, tag, NULL);
}

static int
sort_tag(const void* lhs, const void* rhs) {
	const tag_t* lhs_tag = lhs;
	const tag_t* rhs_tag = rhs;
	return strcmp(lhs_tag->name, rhs_tag->name);
}

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(&ctx->arena, size, alignment);
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	(void)ctx;
	(void)address;
	(void)value;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	(void)addr;

	if (sym->type == BUXN_ASM_SYM_MACRO) {
		add_tag(ctx, sym, 'm');
	} else if (sym->type == BUXN_ASM_SYM_LABEL && !sym->name_is_generated) {
		add_tag(ctx, sym, 'l');
	}
}

void
buxn_asm_report(buxn_asm_ctx_t* ctx, buxn_asm_report_type_t type, const buxn_asm_report_t* report) {
	(void)ctx;
	(void)type;

	if (type == BUXN_ASM_REPORT_WARNING) { return; }

	if (report->token == NULL) {
		blog_write(
			BLOG_LEVEL_WARN,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	} else {
		blog_write(
			BLOG_LEVEL_WARN,
			report->region->filename, report->region->range.start.line,
			"%s (`%s`)", report->message, report->token
		);
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	(void)ctx;
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
	blog_add_file_logger(log_level, &(blog_file_logger_options_t){
		.file = stderr,
		.with_colors = true,
	});

	int exit_code = 1;
	bool tag_filename_set = false;
	const char* output_filename = "tags";
	const char* input_filename = NULL;

	for (int i = 1; i < argc; ++i) {
		const char* flag_value;
		const char* arg = argv[i];

		if ((flag_value = parse_flag(arg, "--help")) != NULL) {
			fprintf(stderr,
				"Usage: buxn-ctags [options] <input.tal>\n"
				"Create a ctags file from an input.tal file (and all its includes).\n"
				"\n"
				"--help             Print this message.\n"
				"-output=<file>     (Optional) Set the output filename.\n"
				"                   This defaults to 'tags'.\n"
			);
			return 0;
		} else if ((flag_value = parse_flag(arg, FLAG_OUTPUT)) != NULL) {
			if (tag_filename_set) {
				fprintf(stderr, "%s can only be specified once\n", FLAG_OUTPUT);
				return 1;
			} else {
				output_filename = flag_value;
				tag_filename_set = true;
			}
		} else {
			if (input_filename == NULL) {
				input_filename = arg;
			} else {
				fprintf(stderr, "Please specify only one input file\n");
				return 1;
			}
		}
	}

	if (input_filename == NULL) {
		fprintf(stderr, "Please specify an input\n");
		return 1;
	}

	barena_pool_t arena_pool;
	barena_pool_init(&arena_pool, 1);

	buxn_asm_ctx_t ctx = { 0 };
	barena_init(&ctx.arena, &arena_pool);

	bool success = buxn_asm(&ctx, input_filename);
	if (!success) {
		if (barray_len(ctx.tags) != 0) {
			BLOG_WARN("Error(s) encountered, tags file may be incomplete");
		} else {
			BLOG_ERROR("No tags found due to error");
			goto end;
		}
	}

	FILE* tag_file = fopen(output_filename, "wb");
	if (tag_file == NULL) {
		BLOG_ERROR("Error while opening tag file: %s", strerror(errno));
		goto end;
	}

	// Special tag for vim
	fprintf(tag_file, "!_TAG_FILE_SORTED\t1\tbuxn-ctags\n");

	qsort(ctx.tags, barray_len(ctx.tags), sizeof(ctx.tags[0]), sort_tag);
	for (int i = 0; i < (int)barray_len(ctx.tags); ++i) {
		const tag_t* tag = &ctx.tags[i];
		fprintf(
			tag_file,
			"%s\t%s\tgo %d|;\"\t%c\n",
			tag->name,
			tag->region.filename, tag->region.range.start.byte + 1,
			tag->kind
		);
	}

	fclose(tag_file);

	exit_code = 0;
end:
	barray_free(ctx.tags, NULL);
	barena_reset(&ctx.arena);
	barena_pool_cleanup(&arena_pool);

	return exit_code;
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <blog.h>
#include <barray.h>
