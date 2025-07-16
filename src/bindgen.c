#include <buxn/asm/asm.h>
#include <buxn/asm/annotation.h>
#include <barena.h>
#include <barg.h>
#include <bmacro.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct {
	const char* chars;
	int len;
} str_t;

typedef struct {
	const char* prefix;
	const char* suffix;
} format_options_t;

typedef struct cmd_field_info_s cmd_field_info_t;
typedef struct cmd_struct_info_s cmd_struct_info_t;

struct cmd_field_info_s {
	str_t name;
	uint16_t size;

	cmd_field_info_t* next;
};

struct cmd_struct_info_s {
	str_t name;

	cmd_field_info_t* first_field;
	cmd_field_info_t* last_field;
	cmd_struct_info_t* next;
};

typedef enum {
	ANNO_DEVICE,
	ANNO_ENUM,
	ANNO_COMMAND,
} annotation_type_t;

struct buxn_asm_ctx_s {
	barena_t arena;
	buxn_anno_spec_t anno_spec;
	str_t current_scope;
	annotation_type_t current_anno_type;

	format_options_t format_options[3];
	uint16_t base_addr;
	uint16_t last_addr;

	cmd_struct_info_t* first_cmd;
	cmd_struct_info_t* last_cmd;
	str_t last_name;
};

static void
handle_annotation(
	void* ctx,
	uint16_t addr,
	const buxn_asm_sym_t* sym,
	const buxn_anno_t* annotation,
	const buxn_asm_source_region_t* region
);

static void
finalize_section(buxn_asm_ctx_t* ctx);

static void
print_str(str_t str, int (*char_transform)(int));

static void
print_null_str(const char* str, int (*char_transform)(int));

int
main(int argc, const char* argv[]) {
	buxn_anno_t annotations[] = {
		[ANNO_DEVICE] = { .name = "device", .type = BUXN_ANNOTATION_PREFIX },
		[ANNO_ENUM] = { .name = "enum", .type = BUXN_ANNOTATION_PREFIX },
		[ANNO_COMMAND] = { .name = "command", .type = BUXN_ANNOTATION_PREFIX },
	};
	buxn_asm_ctx_t basm = {
		.anno_spec = {
			.annotations = annotations,
			.num_annotations = BCOUNT_OF(annotations),
			.handler = handle_annotation,
			.ctx = &basm,
		},
		.format_options = {
			[ANNO_DEVICE] = { .prefix = "dev_", .suffix = "_layout_t", },
			[ANNO_ENUM] = { .prefix = "", .suffix = "_t", },
			[ANNO_COMMAND] = { .prefix = "cmd_", .suffix = "_t", },
		},
	};

	const char* include_guard = NULL;

	barg_opt_t opts[] = {
		{
			.name = "include-guard",
			.summary = "Include guard string",
			.parser = barg_str(&include_guard),
		},
		{
			.name = "device-prefix",
			.summary = "Prefix for device type name",
			.parser = barg_str(&basm.format_options[ANNO_DEVICE].prefix),
		},
		{
			.name = "device-suffix",
			.summary = "Suffix for device type name",
			.parser = barg_str(&basm.format_options[ANNO_DEVICE].suffix),
		},
		{
			.name = "enum-prefix",
			.summary = "Prefix for enum type name",
			.parser = barg_str(&basm.format_options[ANNO_ENUM].prefix),
		},
		{
			.name = "enum-suffix",
			.summary = "Suffix for enum type name",
			.parser = barg_str(&basm.format_options[ANNO_ENUM].suffix),
		},
		{
			.name = "command-prefix",
			.summary = "Prefix for command type name",
			.parser = barg_str(&basm.format_options[ANNO_COMMAND].prefix),
		},
		{
			.name = "command-suffix",
			.summary = "Suffix for command type name",
			.parser = barg_str(&basm.format_options[ANNO_COMMAND].suffix),
		},
		barg_opt_help(),
	};

	barg_t barg = {
		.usage = "buxn-bindgen [options] [--] <in.tal>",
		.summary = "Binding generator",
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
	if (num_args != 1) {
		result.status = BARG_SHOW_HELP;
		barg_print_result(&barg, result, stderr);
		return 1;
	}

	barena_pool_t pool;
	barena_pool_init(&pool, 1);
	barena_init(&basm.arena, &pool);

	if (include_guard != NULL) {
		printf("#ifndef %s\n", include_guard);
		printf("#define %s\n\n", include_guard);
	}

	printf("// buxn-bindgen");
	for (int i = 1; i < argc; ++i) {
		printf(" %s", argv[i]);
	}
	printf("\n");

	bool success = buxn_asm(&basm, argv[result.arg_index]);
	finalize_section(&basm);

	for (
		cmd_struct_info_t* struct_info = basm.first_cmd;
		struct_info != NULL;
		struct_info = struct_info->next
	) {
		printf("\ntypedef struct {\n");
		for (
			cmd_field_info_t* field_info = struct_info->first_field;
			field_info != NULL;
			field_info = field_info->next
		) {
			printf(
				"\tuint%d_t %.*s;\n",
				field_info->size == 1 ? 8 : 16,
				field_info->name.len, field_info->name.chars
			);
		}

		format_options_t fmt_options = basm.format_options[ANNO_COMMAND];

		printf("} %s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("%s;\n", fmt_options.suffix);

		// Reader
		printf("\nstatic inline %s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("%s\n", fmt_options.suffix);
		printf("%s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("_read(buxn_vm_t* vm, uint16_t addr) {\n");
		printf("\treturn (%s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("%s){\n", fmt_options.suffix);
		for (
			cmd_field_info_t* field_info = struct_info->first_field;
			field_info != NULL;
			field_info = field_info->next
		) {
			printf(
				"\t\t.%.*s = buxn_vm_mem_load%s(vm, addr + ",
				field_info->name.len, field_info->name.chars,
				field_info->size == 1 ? "" : "2"
			);

			print_null_str(fmt_options.prefix, toupper);
			print_str(struct_info->name, toupper);
			printf("_");
			print_str(field_info->name, toupper);
			printf("),\n");
		}
		printf("\t};\n}\n");

		// Writer
		printf("\nstatic void\n");
		printf("%s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("_write(buxn_vm_t* vm, uint16_t addr, %s", fmt_options.prefix);
		print_str(struct_info->name, tolower);
		printf("%s value){\n", fmt_options.suffix);
		for (
			cmd_field_info_t* field_info = struct_info->first_field;
			field_info != NULL;
			field_info = field_info->next
		) {
			printf(
				"\tbuxn_vm_mem_store%s(vm, addr + ",
				field_info->size == 1 ? "" : "2"
			);

			print_null_str(fmt_options.prefix, toupper);
			print_str(struct_info->name, toupper);
			printf("_");
			print_str(field_info->name, toupper);
			printf(", value.%.*s);\n", field_info->name.len, field_info->name.chars);
		}
		printf("}\n");
	}

	if (!success) {
		printf("\n#warning \"Error encountered, header might be incomplete\"\n");
	}

	if (include_guard != NULL) {
		printf("\n#endif\n");
	}

	barena_pool_cleanup(&pool);
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
buxn_asm_report(
	buxn_asm_ctx_t* ctx,
	buxn_asm_report_type_t type,
	const buxn_asm_report_t* report
) {
	(void)ctx;
	(void)type;
	(void)report;
}

static void
split_label_name(const char* name, str_t* scope, str_t* local_name) {
	char* slash = strchr(name, '/');
	if (slash == NULL) {
		scope->chars = name;
		scope->len = (int)strlen(name);
		local_name->chars = NULL;
		local_name->len = 0;
	} else {
		scope->chars = name;
		scope->len = (int)(slash - name);
		local_name->chars = slash + 1;
		local_name->len = (int)strlen(slash + 1);
	}
}

static void
print_null_str(const char* str, int (*char_transform)(int)) {
	for (const char* ch = str; *ch != '\0'; ++ch) {
		if (*ch == '-') {
			putc('_', stdout);
		} else {
			putc(char_transform(*ch), stdout);
		}
	}
}

static void
print_str(str_t str, int (*char_transform)(int)) {
	for (int i = 0; i < str.len; ++i) {
		char ch = str.chars[i];
		if (ch == '-') {
			putc('_', stdout);
		} else {
			putc(char_transform(ch), stdout);
		}
	}
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	buxn_anno_handle_symbol(&ctx->anno_spec, addr, sym);

	if (sym->type == BUXN_ASM_SYM_LABEL) {
		str_t scope, local_name;
		split_label_name(sym->name, &scope, &local_name);

		if (
			scope.len == ctx->current_scope.len
			&&
			strncmp(scope.chars, ctx->current_scope.chars, scope.len) == 0
			&&
			local_name.len > 0
		) {
			if (
				addr != ctx->last_addr
				&&
				ctx->last_name.len != 0
			) {
				uint16_t size = addr - ctx->last_addr;
				cmd_field_info_t* field_info = buxn_asm_alloc(
					ctx,
					sizeof(cmd_field_info_t),
					_Alignof(cmd_field_info_t)
				);
				*field_info = (cmd_field_info_t){
					.name = ctx->last_name,
					.size = size,
				};

				cmd_struct_info_t* struct_info = ctx->last_cmd;
				if (struct_info->first_field == NULL) {
					struct_info->first_field = field_info;
				}
				if (struct_info->last_field != NULL) {
					struct_info->last_field->next = field_info;
				}
				struct_info->last_field = field_info;

				ctx->last_name.len = 0;
			}

			format_options_t fmt_options = ctx->format_options[ctx->current_anno_type];
			printf("\t/// %s\n", sym->name);
			printf("\t");
			print_null_str(fmt_options.prefix, toupper);
			print_str(scope, toupper);
			printf("_");
			print_str(local_name, toupper);

			switch (ctx->current_anno_type) {
				case ANNO_DEVICE:
					printf(" = 0x%02x,\n", addr);
					break;
				case ANNO_ENUM:
					printf(" = %d,\n", addr);
					break;
				case ANNO_COMMAND:
					printf(" = %d,\n", addr - ctx->base_addr);
					ctx->last_name = local_name;
					break;
			}

			ctx->last_addr = addr;
		}
	} else {
		if (
			addr != ctx->last_addr
			&&
			ctx->last_name.len != 0
		) {
			uint16_t size = addr - ctx->last_addr;
			cmd_field_info_t* field_info = buxn_asm_alloc(
				ctx,
				sizeof(cmd_field_info_t),
				_Alignof(cmd_field_info_t)
			);
			*field_info = (cmd_field_info_t){
				.name = ctx->last_name,
				.size = size,
			};

			cmd_struct_info_t* struct_info = ctx->last_cmd;
			if (struct_info->first_field == NULL) {
				struct_info->first_field = field_info;
			}
			if (struct_info->last_field != NULL) {
				struct_info->last_field->next = field_info;
			}
			struct_info->last_field = field_info;

			ctx->last_name.len = 0;
		}
	}
}

static void
handle_annotation(
	void* anno_ctx,
	uint16_t addr,
	const buxn_asm_sym_t* sym,
	const buxn_anno_t* annotation,
	const buxn_asm_source_region_t* region
) {
	(void)region;

	buxn_asm_ctx_t* ctx = anno_ctx;
	if (annotation != NULL) {
		finalize_section(ctx);

		ctx->current_anno_type = annotation - ctx->anno_spec.annotations;
		str_t scope, local_name;
		split_label_name(sym->name, &scope, &local_name);
		ctx->current_scope = scope;
		ctx->base_addr = addr;
		ctx->last_addr = addr;

		switch (ctx->current_anno_type) {
			case ANNO_ENUM:
			case ANNO_DEVICE:
				printf("\ntypedef enum {\n");
				break;
			case ANNO_COMMAND:
				printf("\nenum {\n");
				break;
		}

		if (ctx->current_anno_type == ANNO_COMMAND) {
			cmd_struct_info_t* struct_info = buxn_asm_alloc(
				ctx, sizeof(cmd_struct_info_t), _Alignof(cmd_struct_info_t)
			);
			*struct_info = (cmd_struct_info_t){
				.name = scope,
			};

			if (ctx->first_cmd == NULL) {
				ctx->first_cmd = struct_info;
			}

			if (ctx->last_cmd != NULL) {
				ctx->last_cmd->next = struct_info;
			}
			ctx->last_cmd = struct_info;
		}
	}
}

static void
finalize_section(buxn_asm_ctx_t* ctx) {
	if (ctx->current_scope.len != 0){
		format_options_t fmt_options = ctx->format_options[ctx->current_anno_type];

		switch (ctx->current_anno_type) {
			case ANNO_ENUM:
			case ANNO_DEVICE:
				printf("} %s", fmt_options.prefix);
				print_str(ctx->current_scope, tolower);
				printf("%s;\n", fmt_options.suffix);
				break;
			case ANNO_COMMAND:
				printf("};\n");
				break;
		}
		ctx->current_scope.len = 0;
	}
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
#include <barg.h>
