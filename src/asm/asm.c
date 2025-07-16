// vim: set foldmethod=marker foldlevel=0:
#include <buxn/asm/asm.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "chibihash64.h"
#define BHAMT_HASH_TYPE uint64_t
#include "hamt.h"

#define BUXN_ASM_MAX_TOKEN_LEN 47
#define BUXN_ASM_MAX_LONG_STRING_LEN 1024
#define BUXN_ASM_DEFAULT_LABEL_SCOPE "RESET"
#define BUXN_ASM_RESET_VECTOR 0x0100
#define BUXN_ASM_MAX_PREPROCESSOR_DEPTH 32

#define BUXN_ASM_OP_FLAG_2 0x20
#define BUXN_ASM_OP_FLAG_R 0x40
#define BUXN_ASM_OP_FLAG_K 0x80

// Turn the 3 chars into a single number to make comparison faster
#define BUXN_OP_REF_CODE(A, B, C) \
	(uint32_t)(((uint32_t)A << 16) | ((uint32_t)B << 8) | ((uint32_t)C << 0))

static const uint32_t BUXN_BASE_OPCODE_REFS[] = {
	BUXN_OP_REF_CODE('L', 'I', 'T'),
	BUXN_OP_REF_CODE('I', 'N', 'C'),
	BUXN_OP_REF_CODE('P', 'O', 'P'),
	BUXN_OP_REF_CODE('N', 'I', 'P'),
	BUXN_OP_REF_CODE('S', 'W', 'P'),
	BUXN_OP_REF_CODE('R', 'O', 'T'),
	BUXN_OP_REF_CODE('D', 'U', 'P'),
	BUXN_OP_REF_CODE('O', 'V', 'R'),
	BUXN_OP_REF_CODE('E', 'Q', 'U'),
	BUXN_OP_REF_CODE('N', 'E', 'Q'),
	BUXN_OP_REF_CODE('G', 'T', 'H'),
	BUXN_OP_REF_CODE('L', 'T', 'H'),
	BUXN_OP_REF_CODE('J', 'M', 'P'),
	BUXN_OP_REF_CODE('J', 'C', 'N'),
	BUXN_OP_REF_CODE('J', 'S', 'R'),
	BUXN_OP_REF_CODE('S', 'T', 'H'),
	BUXN_OP_REF_CODE('L', 'D', 'Z'),
	BUXN_OP_REF_CODE('S', 'T', 'Z'),
	BUXN_OP_REF_CODE('L', 'D', 'R'),
	BUXN_OP_REF_CODE('S', 'T', 'R'),
	BUXN_OP_REF_CODE('L', 'D', 'A'),
	BUXN_OP_REF_CODE('S', 'T', 'A'),
	BUXN_OP_REF_CODE('D', 'E', 'I'),
	BUXN_OP_REF_CODE('D', 'E', 'O'),
	BUXN_OP_REF_CODE('A', 'D', 'D'),
	BUXN_OP_REF_CODE('S', 'U', 'B'),
	BUXN_OP_REF_CODE('M', 'U', 'L'),
	BUXN_OP_REF_CODE('D', 'I', 'V'),
	BUXN_OP_REF_CODE('A', 'N', 'D'),
	BUXN_OP_REF_CODE('O', 'R', 'A'),
	BUXN_OP_REF_CODE('E', 'O', 'R'),
	BUXN_OP_REF_CODE('S', 'F', 'T'),
};

static const uint32_t BUXN_BRK_REF = BUXN_OP_REF_CODE('B', 'R', 'K');

typedef struct {
	const char* chars;
	int len;
} buxn_asm_str_t;

typedef struct buxn_asm_pstr_s buxn_asm_pstr_t;

struct buxn_asm_pstr_s {
	buxn_asm_str_t key;
	buxn_asm_pstr_t* children[BHAMT_NUM_CHILDREN];

	BHAMT_HASH_TYPE hash;
};

typedef struct {
	buxn_asm_pstr_t* root;
} buxn_asm_strpool_t;

typedef enum {
	BUXN_ASM_UNIT_FILE,
	BUXN_ASM_UNIT_MACRO,
} buxn_asm_unit_type_t;

typedef struct {
	buxn_asm_str_t lexeme;
	buxn_asm_source_region_t region;
} buxn_asm_token_t;

typedef struct buxn_asm_token_link_s buxn_asm_token_link_t;

struct buxn_asm_token_link_s {
	buxn_asm_token_link_t* next;
	buxn_asm_token_t token;
};

typedef struct {
	buxn_asm_file_t* file;
	const buxn_asm_pstr_t* path;
	buxn_asm_file_pos_t pos;
} buxn_asm_file_unit_t;

typedef struct {
	buxn_asm_token_link_t* first;
	buxn_asm_token_link_t* last;
	buxn_asm_token_link_t* current;
	buxn_asm_token_t argument;
	uint16_t id;
	bool expanding;
} buxn_asm_macro_unit_t;

typedef struct {
	buxn_asm_unit_type_t type;
	union {
		buxn_asm_file_unit_t* file;
		buxn_asm_macro_unit_t* macro;
	};
} buxn_asm_unit_t;

typedef enum {
	BUXN_ASM_SYMTAB_ENTRY_UNKNOWN = 0,
	BUXN_ASM_SYMTAB_ENTRY_MACRO,
	BUXN_ASM_SYMTAB_ENTRY_LABEL,
	BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF,
} buxn_asm_symtab_entry_type_t;

typedef struct buxn_asm_forward_ref_s buxn_asm_forward_ref_t;
typedef buxn_asm_forward_ref_t buxn_asm_at_label_t;
typedef struct buxn_asm_symtab_node_s buxn_asm_symtab_node_t;

typedef struct {
	uint16_t id;
	uint16_t address;
} buxn_asm_label_info_t;

typedef struct {
	uint16_t id;
	buxn_asm_forward_ref_t* refs;
} buxn_asm_forward_ref_info_t;

struct buxn_asm_symtab_node_s {
	const buxn_asm_pstr_t* key;
	buxn_asm_symtab_node_t* children[BHAMT_NUM_CHILDREN];
	buxn_asm_symtab_node_t* next;
	buxn_asm_token_t defining_token;
	bool referenced;

	buxn_asm_symtab_entry_type_t type;
	union {
		buxn_asm_macro_unit_t macro;
		buxn_asm_label_info_t label;
		buxn_asm_forward_ref_info_t forward_ref;
	};
};

typedef struct {
	buxn_asm_symtab_node_t* root;
	buxn_asm_symtab_node_t* first;
	buxn_asm_symtab_node_t* last;
} buxn_asm_symtab_t;

typedef enum {
	BUXN_ASM_LABEL_REF_ZERO,
	BUXN_ASM_LABEL_REF_ABS,
	BUXN_ASM_LABEL_REF_REL,
} buxn_asm_label_ref_type_t;

typedef enum {
	BUXN_ASM_LABEL_REF_BYTE  = 1,
	BUXN_ASM_LABEL_REF_SHORT = 2,
} buxn_asm_label_ref_size_t;

struct buxn_asm_forward_ref_s {
	buxn_asm_forward_ref_t* next;
	buxn_asm_token_t token;
	uint16_t label_id;
	uint16_t lambda_id;
	uint16_t addr;
	buxn_asm_label_ref_type_t type;
	buxn_asm_label_ref_size_t size;
};

typedef struct {
	buxn_asm_ctx_t* ctx;
	uint16_t write_addr;

	int preprocessor_depth;
	buxn_asm_macro_unit_t* current_macro;
	char token_buf[BUXN_ASM_MAX_LONG_STRING_LEN + 1];
	char name_buf[BUXN_ASM_MAX_TOKEN_LEN + 1];
	char macro_expand_buf[BUXN_ASM_MAX_LONG_STRING_LEN + 1];

	int read_buf;
	bool has_read_buf;
	bool success;

	uint16_t num_labels;
	uint16_t num_lambdas;
	uint16_t num_macros;
	buxn_asm_strpool_t strpool;
	buxn_asm_symtab_t symtab;
	buxn_asm_str_t label_scope;
	buxn_asm_forward_ref_t* lambdas;
	buxn_asm_at_label_t* at_labels;
	buxn_asm_forward_ref_t* ref_pool;
} buxn_asm_t;

static bool
buxn_asm_process_file(buxn_asm_t* basm, const buxn_asm_pstr_t* path);

static bool
buxn_asm_process_unit(buxn_asm_t* basm, buxn_asm_unit_t* unit);

// Error/Warning {{{

static bool
buxn_asm_error_ex(buxn_asm_t* basm, const buxn_asm_report_t* report) {
	basm->success = false;
	buxn_asm_report(basm->ctx, BUXN_ASM_REPORT_ERROR, report);
	return false;
}

static bool
buxn_asm_error(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message
) {
	return buxn_asm_error_ex(
		basm,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
		}
	);
}

static bool
buxn_asm_error2(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message,
	const buxn_asm_token_t* related_token,
	const char* related_message
) {
	return buxn_asm_error_ex(
		basm,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
			.related_message = related_message,
			.related_region = &related_token->region,
		}
	);
}

static void
buxn_asm_warning(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	const char* message
) {
	buxn_asm_report(
		basm->ctx,
		BUXN_ASM_REPORT_WARNING,
		&(buxn_asm_report_t){
			.message = message,
			.token = token->lexeme.chars,
			.region = &token->region,
		}
	);
}

// }}}

// Tokenize {{{

static int
buxn_asm_peek_char(buxn_asm_t* basm, buxn_asm_file_unit_t* unit) {
	if (!basm->has_read_buf) {
		basm->read_buf = buxn_asm_fgetc(basm->ctx, unit->file);
		basm->has_read_buf = true;
		if (basm->read_buf == BUXN_ASM_IO_ERROR) {
			return buxn_asm_error_ex(
				basm,
				&(buxn_asm_report_t){
					.message = "I/O error",
					.region = &(buxn_asm_source_region_t){
						.filename = unit->path->key.chars
					}
				}
			);
		}
	}

	return basm->read_buf;
}

static void
buxn_asm_consume_char(buxn_asm_t* basm) {
	basm->has_read_buf = false;
}

static int
buxn_asm_get_char(buxn_asm_t* basm, buxn_asm_file_unit_t* unit) {
	int ch = buxn_asm_peek_char(basm, unit);

	buxn_asm_consume_char(basm);
	unit->pos.byte += 1;
	unit->pos.col += 1;

	if (ch == BUXN_ASM_IO_ERROR || ch == BUXN_ASM_IO_EOF) {
		return ch;
	} else if (ch == '\n') {
		unit->pos.line += 1;
		unit->pos.col = 1;
		return ch;
	} else if (ch == '\r') {
		int next_ch = buxn_asm_peek_char(basm, unit);
		if (next_ch == '\n') {
			buxn_asm_consume_char(basm);
		}

		unit->pos.line += 1;
		unit->pos.col = 1;
		return next_ch;
	} else {
		return ch;
	}
}

static bool
buxn_asm_is_runic(char ch) {
	return ch == '{' || ch == '}'
		|| ch == '[' || ch == ']'
		|| ch == '(' || ch == ')'
		|| ch == '|' || ch == '$'
		|| ch == '#'
		|| ch == '"'
		|| ch == '@' || ch == '&' || ch == '/'
		|| ch == ',' || ch == '_'
		|| ch == '.' || ch == '-'
		|| ch == ';' || ch == '='
		|| ch == '!' || ch == '?'
		|| ch == '%' || ch == '~';
}

static bool
buxn_asm_is_number(buxn_asm_str_t str) {
	if (str.len == 0) { return false; }

	int start_index = 0;
	if (start_index < str.len && str.chars[start_index] == '+') { ++start_index; }
	if (start_index < str.len && str.chars[start_index] == '+') { ++start_index; }

	if (start_index > 0) {
		// Decimal
		for (int i = start_index; i < str.len; ++i) {
			char ch = str.chars[i];
			if (!('0' <= ch && ch <= '9')) {
				return false;
			}
		}
	} else {
		// Hex
		for (int i = start_index; i < str.len; ++i) {
			char ch = str.chars[i];
			if (!(
				('0' <= ch && ch <= '9')
				|| ('a' <= ch && ch <= 'f')
			)) {
				return false;
			}
		}
	}

	return true;
}

static bool
buxn_asm_is_uppercased(char ch) {
	return 'A' <= ch && ch <= 'Z';
}

static bool
buxn_asm_is_sep(int ch) {
	return ch == ' '
		|| ch == '\n'
		|| ch == '\t'
		|| ch == '\r'
		|| ch == '\f'
		|| ch == '\v';
}

typedef struct {
	uint8_t opcode;
	bool has_redundant_flag;
} buxn_asm_opcode_result_t;

static bool
buxn_asm_parse_opcode(buxn_asm_str_t word, buxn_asm_opcode_result_t* result) {
	if (word.len < 3) { return false; }
	uint32_t ref_code = BUXN_OP_REF_CODE(word.chars[0], word.chars[1], word.chars[2]);

	// BRK is a special case
	if (ref_code == BUXN_BRK_REF) {
		if (word.len == 3) {  // BRK can only be written as-is
			if (result != NULL) {
				result->opcode =  0x00;
				result->has_redundant_flag = false;
			}
			return true;
		} else {
			return false;
		}
	}

	// Find base opcode
	uint8_t opcode;
	uint8_t num_base_opcodes = sizeof(BUXN_BASE_OPCODE_REFS) / sizeof(BUXN_BASE_OPCODE_REFS[0]);
	for (opcode = 0; opcode < num_base_opcodes; ++opcode) {
		if (BUXN_BASE_OPCODE_REFS[opcode] == ref_code) {
			break;
		}
	}
	if (opcode >= num_base_opcodes) { return false; }
	if (opcode == 0) { opcode = 0x80; }  // LIT always has k

	// Process flags
	bool has_redundant_flag = false;
	for (int i = 0; i < word.len - 3; ++i) {
		char flag = word.chars[i + 3];
		switch (flag) {
			case '2':
				has_redundant_flag |= (opcode & BUXN_ASM_OP_FLAG_2) > 0;
				opcode |= BUXN_ASM_OP_FLAG_2;
				break;
			case 'r':
				has_redundant_flag |= (opcode & BUXN_ASM_OP_FLAG_R) > 0;
				opcode |= BUXN_ASM_OP_FLAG_R;
				break;
			case 'k':
				has_redundant_flag |= (opcode & BUXN_ASM_OP_FLAG_K) > 0;
				opcode |= BUXN_ASM_OP_FLAG_K;
				break;
			default:
				// Having an unrecognized flag means it is not an opcode
				return false;
		}
	}

	if (result != NULL) {
		result->opcode = opcode;
		result->has_redundant_flag = has_redundant_flag;
	}

	return true;
}

static bool
buxn_asm_scan_regular_token(
	buxn_asm_t* basm,
	buxn_asm_file_unit_t* unit,
	buxn_asm_token_t* token,
	int ch,
	buxn_asm_file_pos_t start
) {
	int token_len = 0;
	buxn_asm_file_pos_t end = start;

	while (true) {
		if (token_len < BUXN_ASM_MAX_TOKEN_LEN) {
			basm->token_buf[token_len++] = (char)ch;
		} else {
			return buxn_asm_error_ex(
				basm,
				&(buxn_asm_report_t) {
					.message = "Token is too long",
					.region = &(buxn_asm_source_region_t){
						.filename = unit->path->key.chars,
						.range = { .start = start, .end = end },
					},
				}
			);
		}

		end = unit->pos;
		ch = buxn_asm_get_char(basm, unit);

		if (ch == BUXN_ASM_IO_ERROR) {
			return false;
		} else if (ch == BUXN_ASM_IO_EOF || buxn_asm_is_sep(ch)) {
			basm->token_buf[token_len] = '\0';
			*(token) = (buxn_asm_token_t){
				.lexeme = { .chars = basm->token_buf, .len = token_len },
				.region = {
					.filename = unit->path->key.chars,
					.range = { .start = start, .end = end },
				},
			};
			return true;
		}
	}
}

static bool
buxn_asm_scan_long_string(
	buxn_asm_t* basm,
	buxn_asm_file_unit_t* unit,
	buxn_asm_token_t* token,
	buxn_asm_file_pos_t start
) {
	int token_len = 0;
	buxn_asm_get_char(basm, unit);  // Consume the space following '"'
	basm->token_buf[token_len++] = '"';  // Incude the prefix

	while (true) {
		buxn_asm_file_pos_t end = unit->pos;
		int ch = buxn_asm_get_char(basm, unit);

		if (ch == BUXN_ASM_IO_ERROR) {
			return false;
		} else if (ch == BUXN_ASM_IO_EOF) {
			return buxn_asm_error_ex(
				basm,
				&(buxn_asm_report_t) {
					.message = "Unterminated long string",
					.region = &(buxn_asm_source_region_t){
						.filename = unit->path->key.chars,
						.range = { .start = start, .end = end },
					},
				}
			);
		} else if (ch == '"') {
			// Exclude the final '"'
			basm->token_buf[token_len] = '\0';
			*(token) = (buxn_asm_token_t){
				.lexeme = { .chars = basm->token_buf, .len = token_len },
				.region = {
					.filename = unit->path->key.chars,
					.range = { .start = start, .end = end },
				},
			};
			return true;
		} else {
			if (token_len < BUXN_ASM_MAX_LONG_STRING_LEN) {
				basm->token_buf[token_len++] = (char)ch;
			} else {
				return buxn_asm_error_ex(
					basm,
					&(buxn_asm_report_t) {
						.message = "String is too long",
						.region = &(buxn_asm_source_region_t){
							.filename = unit->path->key.chars,
							.range = { .start = start, .end = end },
						},
					}
				);
			}
		}
	}
}

static bool
buxn_asm_next_token_in_file(
	buxn_asm_t* basm,
	buxn_asm_file_unit_t* unit,
	buxn_asm_token_t* token
) {
	// Scan until a non separator is seen then transfer to a token scan function
	while (true) {
		buxn_asm_file_pos_t pos = unit->pos;
		int ch = buxn_asm_get_char(basm, unit);

		if (ch == BUXN_ASM_IO_ERROR) {
			return false;
		} else if (ch == BUXN_ASM_IO_EOF) {
			return false;
		} else if (buxn_asm_is_sep(ch)) {
			continue;
		} else if (ch == '"' && buxn_asm_peek_char(basm, unit) == ' ') {
			return buxn_asm_scan_long_string(basm, unit, token, pos);
		} else {
			return buxn_asm_scan_regular_token(basm, unit, token, ch, pos);
		}
	}
}

static bool
buxn_asm_next_token_in_macro(
	buxn_asm_t* basm,
	buxn_asm_macro_unit_t* unit,
	buxn_asm_token_t* token
) {
	if (unit->current != NULL) {
		buxn_asm_token_t template_token = unit->current->token;
		buxn_asm_token_t arg_token = unit->argument;
		buxn_asm_token_t expanded_token = { .region = template_token.region };

		int len = 0;
		char* expand_buf = basm->macro_expand_buf;
		bool expanded = false;
		if (arg_token.lexeme.len > 0) {  // Token expansion
			// Copy from template, replacing '^' with arg_token.lexeme
			for (int i = 0; i < template_token.lexeme.len; ++i) {
				char ch = template_token.lexeme.chars[i];
				if (ch == '^') {
					expanded = true;

					if (len + arg_token.lexeme.len > BUXN_ASM_MAX_LONG_STRING_LEN) {
						return buxn_asm_error(
							basm,
							&template_token,
							"Expanded token is too long"
						);
					}

					memcpy(
						expand_buf + len,
						arg_token.lexeme.chars,
						arg_token.lexeme.len
					);
					len += arg_token.lexeme.len;
				} else {
					if (len + 1 > BUXN_ASM_MAX_LONG_STRING_LEN) {
						return buxn_asm_error(
							basm,
							&template_token,
							"Expanded token is too long"
						);
					}
					expand_buf[len++] = ch;
				}
			}
			expand_buf[len] = '\0';

			bool is_long_string =
				expand_buf[0] == '"'
				&&
				len > 1
				&&
				expand_buf[1] == ' ';
			int limit = is_long_string
				? BUXN_ASM_MAX_LONG_STRING_LEN
				: BUXN_ASM_MAX_TOKEN_LEN;
			if (len > limit) {
				return buxn_asm_error(
					basm,
					&template_token,
					"Expanded token is too long"
				);
			}
		}

		if (expanded) {
			expanded_token.lexeme.chars = expand_buf;
			expanded_token.lexeme.len = len;
		} else {
			expanded_token.lexeme = template_token.lexeme;
		}

		*token = expanded_token;
		unit->current = unit->current->next;
		return true;
	} else {
		return false;
	}
}

static bool
buxn_asm_next_token(buxn_asm_t* basm, buxn_asm_unit_t* unit, buxn_asm_token_t* token) {
	switch (unit->type) {
		case BUXN_ASM_UNIT_FILE:
			return buxn_asm_next_token_in_file(basm, unit->file, token);
		case BUXN_ASM_UNIT_MACRO:
			return buxn_asm_next_token_in_macro(basm, unit->macro, token);
		default:
			return false;
	}
}

// }}}

// String {{{

static buxn_asm_str_t
buxn_asm_str_pop_front(buxn_asm_str_t str) {
	if (str.len > 0) {
		return (buxn_asm_str_t){ .chars = str.chars + 1, .len = str.len - 1 };
	} else {
		return str;
	}
}

static inline bool
buxn_asm_ptr_eq(const void* lhs, const void* rhs) {
	return lhs == rhs;
}

static inline bool
buxn_asm_str_eq(buxn_asm_str_t lhs, buxn_asm_str_t rhs) {
	return lhs.len == rhs.len
		&& memcmp(lhs.chars, rhs.chars, lhs.len) == 0;
}

static const buxn_asm_pstr_t*
buxn_asm_strintern(buxn_asm_t* basm, buxn_asm_str_t str) {
	BHAMT_HASH_TYPE hash = chibihash64(str.chars, str.len, 0);
	buxn_asm_pstr_t** itr;
	buxn_asm_pstr_t* node;
	BHAMT_SEARCH(basm->strpool.root, itr, node, hash, str, buxn_asm_str_eq);

	if (node != NULL) { return node; }

	node = *itr = buxn_asm_alloc(
		basm->ctx,
		sizeof(buxn_asm_symtab_node_t) + str.len + 1,
		_Alignof(buxn_asm_symtab_node_t)
	);

	char* chars = (char*)node + sizeof(*node);
	memcpy(chars, str.chars, str.len);
	chars[str.len] = '\0';

	*node = (buxn_asm_pstr_t){
		.key = { .chars = chars, .len = str.len },
		.hash = hash,
	};

	return node;
}

static const buxn_asm_pstr_t*
buxn_asm_strfind(buxn_asm_t* basm, buxn_asm_str_t str) {
	BHAMT_HASH_TYPE hash = chibihash64(str.chars, str.len, 0);
	buxn_asm_pstr_t* node;
	BHAMT_GET(basm->strpool.root, node, hash, str, buxn_asm_str_eq);
	return node;
}

static buxn_asm_token_t
buxn_asm_persist_token(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	if (token->lexeme.chars != basm->token_buf) {
		return *token;
	}

	char* str_copy = buxn_asm_alloc(basm->ctx, token->lexeme.len + 1, 1);
	memcpy(str_copy, token->lexeme.chars, token->lexeme.len);
	str_copy[token->lexeme.len] = '\0';
	return (buxn_asm_token_t){
		.lexeme = { .chars = str_copy, .len = token->lexeme.len },
		.region = token->region,
	};
}

// }}}

// Symbol {{{

static buxn_asm_symtab_node_t*
buxn_asm_find_or_create_symbol(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t name
) {
	if (name.len == 0) {
		buxn_asm_error(basm, token, "Symbol name cannot be empty");
		return NULL;
	}

	if (buxn_asm_is_runic(name.chars[0])) {
		buxn_asm_error(basm, token, "Symbol name cannot be runic");
		return NULL;
	}

	if (buxn_asm_parse_opcode(name, NULL)) {
		buxn_asm_error(basm, token, "Symbol name cannot be an opcode");
		return NULL;
	}

	if (buxn_asm_is_number(name)) {
		buxn_asm_error(basm, token, "Symbol name cannot be numeric");
		return NULL;
	}

	const buxn_asm_pstr_t* interned_name = buxn_asm_strintern(basm, name);
	buxn_asm_symtab_node_t** itr;
	buxn_asm_symtab_node_t* node;
	BHAMT_SEARCH(basm->symtab.root, itr, node, interned_name->hash, interned_name, buxn_asm_ptr_eq);

	if (node != NULL) {
		return node;
	}

	node = *itr = buxn_asm_alloc(
		basm->ctx,
		sizeof(buxn_asm_symtab_node_t),
		_Alignof(buxn_asm_symtab_node_t)
	);
	(*node) = (buxn_asm_symtab_node_t){
		.key = interned_name,
		.defining_token = buxn_asm_persist_token(basm, token),
	};

	if (basm->symtab.first == NULL) {
		basm->symtab.first = node;
	}
	if (basm->symtab.last != NULL) {
		basm->symtab.last->next = node;
	}
	basm->symtab.last = node;

	return node;
}

static buxn_asm_symtab_node_t*
buxn_asm_find_symbol(buxn_asm_t* basm, const buxn_asm_pstr_t* name) {
	buxn_asm_symtab_node_t* node;
	BHAMT_GET(basm->symtab.root, node, name->hash, name, buxn_asm_ptr_eq);
	return node;
}

// }}}

// Label {{{

static buxn_asm_forward_ref_t*
buxn_asm_alloc_forward_ref(buxn_asm_t* basm) {
	buxn_asm_forward_ref_t* ref;
	if (basm->ref_pool != NULL) {
		ref = basm->ref_pool;
		basm->ref_pool = ref->next;
	} else {
		ref = buxn_asm_alloc(
			basm->ctx,
			sizeof(buxn_asm_forward_ref_t), _Alignof(buxn_asm_forward_ref_t)
		);
	}

	return ref;
}

static inline void
buxn_asm_release_forward_ref(buxn_asm_t* basm, buxn_asm_forward_ref_t* ref) {
	ref->next = basm->ref_pool;
	basm->ref_pool = ref;
}

static bool
buxn_asm_emit_backward_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	const buxn_asm_symtab_node_t* label,
	bool with_symbol,
	bool is_runic
);

static buxn_asm_symtab_node_t*
buxn_asm_register_label(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t label_name
) {
	buxn_asm_symtab_node_t* symbol = buxn_asm_find_or_create_symbol(basm, token, label_name);
	if (symbol == NULL) {
		return NULL;
	} else if (
		symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN
		|| symbol->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF
	) {
		uint16_t id;
		buxn_asm_forward_ref_t* forward_refs;
		if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN) {
			id = ++basm->num_labels;  // Starting from 1 since 0 means no id
			forward_refs = NULL;
		} else {
			id = symbol->forward_ref.id;
			forward_refs = symbol->forward_ref.refs;
		}

		symbol->type = BUXN_ASM_SYMTAB_ENTRY_LABEL;
		symbol->label.id = id;
		symbol->label.address = basm->write_addr;

		// Resolve existing forward references
		uint16_t write_addr = basm->write_addr;
		for (buxn_asm_forward_ref_t* itr = forward_refs; itr != NULL;) {
			buxn_asm_forward_ref_t* next = itr->next;

			basm->write_addr = itr->addr;
			buxn_asm_emit_backward_ref(
				basm, &itr->token,
				itr->type, itr->size,
				symbol,
				false,
				false
			);

			buxn_asm_release_forward_ref(basm, itr);
			itr = next;
		}
		symbol->referenced = forward_refs != NULL;
		basm->write_addr = write_addr;

		buxn_asm_put_symbol(basm->ctx, basm->write_addr, &(buxn_asm_sym_t){
			.type = BUXN_ASM_SYM_LABEL,
			.name = symbol->key->key.chars,
			.region = token->region,
			.id = id,
		});
		return symbol;
	} else {
		buxn_asm_error2(
			basm,
			token, "Duplicated definition",
			&symbol->defining_token, "Previously defined here"
		);
		return NULL;
	}
}

static bool
buxn_asm_resolve_local_name(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t local_name,
	buxn_asm_str_t* name_out
) {
	buxn_asm_str_t scope_name = basm->label_scope;
	if (local_name.len + scope_name.len + 1 > BUXN_ASM_MAX_TOKEN_LEN) {
		return buxn_asm_error(basm, token, "Label name is too long");
	}

	memcpy(basm->name_buf, scope_name.chars, scope_name.len);
	basm->name_buf[scope_name.len] = '/';
	memcpy(basm->name_buf + scope_name.len + 1, local_name.chars, local_name.len);
	basm->name_buf[scope_name.len + local_name.len + 1] = '\0';

	name_out->chars = basm->name_buf;
	name_out->len = scope_name.len + local_name.len + 1;
	return true;
}

static bool
buxn_asm_resolve_label_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t ref,
	buxn_asm_str_t* name_out
) {
	if (ref.len == 0) {
		return buxn_asm_error(basm, token, "Invalid reference");
	} else if (ref.chars[0] == '&' || ref.chars[0] == '/') {
		return buxn_asm_resolve_local_name(
			basm, token,
			buxn_asm_str_pop_front(ref),
			name_out
		);
	} else {
		*name_out = ref;
		return true;
	}
}

// }}}

// Codegen {{{

static void
buxn_asm_put_symbol2(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
	buxn_asm_put_symbol(ctx, addr, sym);
	buxn_asm_put_symbol(ctx, addr + 1, sym);
}

static bool
buxn_asm_emit(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t byte) {
	uint16_t addr = basm->write_addr++;
	if (addr < BUXN_ASM_RESET_VECTOR) {
		return buxn_asm_error(basm, token, "Writing to zero page");
	}

	buxn_asm_put_rom(basm->ctx, addr, byte);
	return true;
}

static bool
buxn_asm_emit2(buxn_asm_t* basm, const buxn_asm_token_t* token, uint16_t short_) {
	if (!buxn_asm_emit(basm, token, short_ >> 8)) { return false; }
	if (!buxn_asm_emit(basm, token, short_ & 0xff)) { return false; }

	return true;
}

static bool
buxn_asm_emit_opcode(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t opcode, bool is_runic) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit(basm, token, opcode)) { return false; }

	// Map only the rune to this opcode
	buxn_asm_source_region_t region = token->region;
	if (is_runic) {
		region.range.end = region.range.start;
		region.range.end.col += 1;
		region.range.end.byte += 1;
	}
	buxn_asm_put_symbol(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_OPCODE,
		.region = region,
		.id = opcode,
	});

	return true;
}

static bool
buxn_asm_emit_byte(buxn_asm_t* basm, const buxn_asm_token_t* token, uint8_t byte, bool is_runic) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit(basm, token, byte)) { return false; }

	buxn_asm_source_region_t region = token->region;
	if (is_runic) {
		region.range.start.col += 1;
		region.range.start.byte += 1;
	}
	buxn_asm_put_symbol(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_NUMBER,
		.region = region,
		.id = byte,
	});

	return true;
}

static bool
buxn_asm_emit_short(buxn_asm_t* basm, const buxn_asm_token_t* token, uint16_t short_, bool is_runic) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit2(basm, token, short_)) { return false; }

	buxn_asm_source_region_t region = token->region;
	if (is_runic) {
		region.range.start.col += 1;
		region.range.start.byte += 1;
	}
	buxn_asm_put_symbol2(basm->ctx, addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_NUMBER,
		.region = region,
		.id = short_
	});

	return true;
}

static bool
buxn_asm_emit_addr_placeholder(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_size_t size,
	buxn_asm_symtab_node_t* symbol,
	bool is_runic
) {
	assert(symbol == NULL || symbol->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF);
	buxn_asm_source_region_t region = token->region;
	if (is_runic) {
		region.range.start.col += 1;
		region.range.start.byte += 1;
	}
	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_LABEL_REF,
		.name = symbol != NULL ? symbol->key->key.chars : 0,
		.region = region,
		.id = symbol != NULL ? symbol->forward_ref.id : ++basm->num_labels,
	};

	uint16_t addr = basm->write_addr;
	switch (size) {
		case BUXN_ASM_LABEL_REF_BYTE:
			if (!buxn_asm_emit(basm, token, 0x01)) { return false; }
			buxn_asm_put_symbol(basm->ctx, addr, &sym);
			break;
		case BUXN_ASM_LABEL_REF_SHORT:
			if (!buxn_asm_emit2(basm, token, 0x01)) { return false; }
			buxn_asm_put_symbol2(basm->ctx, addr, &sym);
			break;
	}

	return true;
}

static bool
buxn_asm_emit_forward_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	buxn_asm_symtab_node_t* symbol,
	bool is_runic
) {
	if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN) {
		symbol->type = BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF;
		symbol->forward_ref.id = ++basm->num_labels;
		symbol->forward_ref.refs = NULL;
	}

	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit_addr_placeholder(basm, token, size, symbol, is_runic)) {
		return false;
	}

	buxn_asm_forward_ref_t* ref = buxn_asm_alloc_forward_ref(basm);
	*ref = (buxn_asm_forward_ref_t){
		.next = symbol->forward_ref.refs,
		.token = buxn_asm_persist_token(basm, token),
		.addr = addr,
		.type = type,
		.size = size,
	};
	symbol->forward_ref.refs = ref;

	return true;
}

static bool
buxn_asm_emit_lambda_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	bool is_runic
) {
	uint16_t addr = basm->write_addr;
	if (!buxn_asm_emit_addr_placeholder(basm, token, size, NULL, is_runic)) {
		return false;
	}

	buxn_asm_forward_ref_t* ref = buxn_asm_alloc_forward_ref(basm);
	*ref = (buxn_asm_forward_ref_t){
		.next = basm->lambdas,
		// basm->num_labels is already incremented by buxn_asm_emit_addr_placeholder
		.label_id = basm->num_labels,
		.lambda_id = basm->num_lambdas++,
		.token = *token,
		.addr = addr,
		.type = type,
		.size = size,
	};
	basm->lambdas = ref;

	return true;
}

static int32_t
buxn_asm_calculate_addr(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	uint16_t from_addr,
	uint16_t to_addr
) {
	switch (type) {
		case BUXN_ASM_LABEL_REF_ZERO:
			if (to_addr > 0xff) {
				buxn_asm_warning(
					basm, token,
					"Taking zero-address of a label past page zero"
				);
			}
			return (int32_t)(to_addr & 0xff);
		case BUXN_ASM_LABEL_REF_ABS:
			return (int32_t)to_addr;
		case BUXN_ASM_LABEL_REF_REL:
			return (int32_t)(int)to_addr - (int)(from_addr + 2);
		default:
			assert(0 && "Invalid address reference type");
			return 0;
	}
}

static bool
buxn_asm_emit_addr(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	int32_t addr,
	const buxn_asm_token_t* token_at_addr,
	const buxn_asm_sym_t* sym
) {
	uint16_t write_addr = basm->write_addr;
	switch (size) {
		case BUXN_ASM_LABEL_REF_BYTE:
			if (
				type == BUXN_ASM_LABEL_REF_REL
				&& (addr > INT8_MAX || addr < INT8_MIN)
			) {
				return buxn_asm_error2(
					basm,
					token, "Referenced address is too far",
					token_at_addr, "Label defined here"
				);
			}
			if (!buxn_asm_emit(basm, token, (uint16_t)addr & 0xff)) { return false; }
			if (sym != NULL) { buxn_asm_put_symbol(basm->ctx, write_addr, sym); }
			break;
		case BUXN_ASM_LABEL_REF_SHORT:
			if (!buxn_asm_emit2(basm, token, (uint16_t)addr)) { return false; }
			if (sym != NULL) { buxn_asm_put_symbol2(basm->ctx, write_addr, sym); }
			break;
	}

	return true;
}

static bool
buxn_asm_emit_backward_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	const buxn_asm_symtab_node_t* symbol,
	bool with_symbol,
	bool is_runic
) {
	assert((symbol->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) && "Invalid symbol type");

	uint16_t write_addr = basm->write_addr;
	int32_t addr = buxn_asm_calculate_addr(
		basm, token,
		type,
		write_addr, symbol->label.address
	);

	buxn_asm_source_region_t region = token->region;
	if (is_runic) {
		region.range.start.col += 1;
		region.range.start.byte += 1;
	}
	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_LABEL_REF,
		// For anonymous backward ref, there is no name
		.name = symbol->key != NULL ? symbol->key->key.chars : NULL,
		.region = region,
		.id = symbol->label.id,
	};

	return buxn_asm_emit_addr(
		basm, token,
		type, size,
		addr,
		&symbol->defining_token,
		with_symbol ? &sym : NULL
	);
}

static buxn_asm_at_label_t*
buxn_asm_pop_at_label(buxn_asm_t* basm) {
	buxn_asm_at_label_t* label = basm->at_labels;
	if (label == NULL) { return NULL; }

	basm->at_labels = label->next;
	buxn_asm_release_forward_ref(basm, label);
	return label;
}

static bool
buxn_asm_emit_label_ref(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_label_ref_type_t type,
	buxn_asm_label_ref_size_t size,
	buxn_asm_str_t label_name,
	bool is_runic
) {
	if (label_name.len == 1 && label_name.chars[0] == '{') {
		return buxn_asm_emit_lambda_ref(basm, token, type, size, is_runic);
	}

	buxn_asm_str_t full_name;
	if (!buxn_asm_resolve_label_ref(basm, token, label_name, &full_name)) {
		return false;
	}

	if (full_name.len == 1 && full_name.chars[0] == '@') {
		// Anonymous backward ref
		buxn_asm_at_label_t* label = buxn_asm_pop_at_label(basm);

		if (label != NULL) {
			buxn_asm_symtab_node_t symbol = {
				.type = BUXN_ASM_SYMTAB_ENTRY_LABEL,
				.defining_token = label->token,
				.label = {
					.id = label->label_id,
					.address = label->addr,
				},
			};
			return buxn_asm_emit_backward_ref(basm, token, type, size, &symbol, true, is_runic);
		} else {
			return buxn_asm_error(basm, token, "No previously declared @-label");
		}
	} else {
		// Regular label ref
		if (full_name.len == 0 || buxn_asm_is_runic(full_name.chars[0])) {
			return buxn_asm_error(basm, token, "Invalid reference");
		}

		buxn_asm_symtab_node_t* symbol = buxn_asm_find_or_create_symbol(basm, token, full_name);
		if (symbol == NULL) {
			return false;
		} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) {
			symbol->referenced = true;
			return buxn_asm_emit_backward_ref(basm, token, type, size, symbol, true, is_runic);
		} else if (
			symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN
			|| symbol->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF
		) {
			return buxn_asm_emit_forward_ref(basm, token, type, size, symbol, is_runic);
		} else {
			return buxn_asm_error(basm, token, "Invalid reference");
		}
	}
}

static bool
buxn_asm_emit_jsi(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	if (!buxn_asm_emit_opcode(basm, token, 0x60, false)) { return false; }  // JSI
	return buxn_asm_emit_label_ref(
		basm, token,
		BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
		token->lexeme,
		false
	);
}

static bool
buxn_asm_resolve(buxn_asm_t* basm) {
	for (buxn_asm_forward_ref_t* itr = basm->lambdas; itr != NULL; itr = itr->next) {
		buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t){
				.message = "Unbalanced lambda",
				.region = &itr->token.region,
			}
		);
	}

	for (buxn_asm_forward_ref_t* itr = basm->at_labels; itr != NULL; itr = itr->next) {
		buxn_asm_report(
			basm->ctx,
			BUXN_ASM_REPORT_WARNING,
			&(buxn_asm_report_t){
				.message = "Unreferenced @-label",
				.region = &itr->token.region,
			}
		);
	}

	for (
		buxn_asm_symtab_node_t* itr = basm->symtab.first;
		itr != NULL;
		itr = itr->next
	) {
		if (itr->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF) {
			for (
				buxn_asm_forward_ref_t* jtr = itr->forward_ref.refs;
				jtr != NULL;
				jtr = jtr->next
			) {
				buxn_asm_error(basm, &jtr->token, "Invalid reference");
			}
		} else if (itr->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN) {
			buxn_asm_error(basm, &itr->defining_token, "Internal error: Unknown symbol");
		} else if (
			!itr->referenced
			&& !(
				itr->key->key.len > 0
				&& buxn_asm_is_uppercased(itr->key->key.chars[0])
			)
			&& !(
				itr->type == BUXN_ASM_SYMTAB_ENTRY_LABEL
				&& itr->label.address == BUXN_ASM_RESET_VECTOR
			)
		) {
			buxn_asm_warning(basm, &itr->defining_token, "Unreferenced symbol");
		}
	}

	return basm->success;
}

// }}}

static bool
buxn_asm_process_comment(
	buxn_asm_t* basm,
	const buxn_asm_token_t* start,
	buxn_asm_unit_t* unit
) {
	buxn_asm_put_symbol(basm->ctx, basm->write_addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_COMMENT,
		.name = start->lexeme.chars,
		.region = start->region,
	});

	int depth = 1;
	buxn_asm_token_t token;
	while (depth > 0 && buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		buxn_asm_put_symbol(basm->ctx, basm->write_addr, &(buxn_asm_sym_t){
			.type = BUXN_ASM_SYM_COMMENT,
			.name = token.lexeme.chars,
			.region = token.region,
			.id = depth,
		});

		switch (token.lexeme.chars[0]) {
			case '(': if (token.lexeme.len == 1) { ++depth; } break;
			case ')': if (token.lexeme.len == 1) { --depth; } break;
		}
	}

	if (depth != 0) {
		return buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t){
				.message = "Unbalanced comment",
				.region = &start->region,
			}
		);
	}

	return true;
}

static void
buxn_asm_process_mark(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	buxn_asm_put_symbol(basm->ctx, basm->write_addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_MARK,
		.name = token->lexeme.chars,
		.region = token->region,
	});
}

static bool
buxn_asm_create_macro(
	buxn_asm_t* basm,
	const buxn_asm_token_t* start,
	buxn_asm_unit_t* unit,
	buxn_asm_symtab_node_t* symbol
) {
	uint16_t id = ++basm->num_macros;
	buxn_asm_put_symbol(basm->ctx, 0, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_MACRO,
		.name = symbol->key->key.chars,
		.region = start->region,
		.id = id,
	});

	buxn_asm_token_t token;
	bool found_open_brace = false;
	while (!found_open_brace && buxn_asm_next_token(basm, unit, &token)) {
		switch (token.lexeme.chars[0]) {
			case '(':
				if (!buxn_asm_process_comment(basm, &token, unit)) {
					return false;
				}
				break;
			case '[':
				// [word is ignored
				buxn_asm_process_mark(basm, &token);
				break;
			case ']':
				// ] is standalone
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				buxn_asm_process_mark(basm, &token);
				break;
			case '{':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Macro must be followed by '{'");
				}
				found_open_brace = true;
				break;
			default:
				return buxn_asm_error(basm, &token, "Macro must be followed by '{'");
		}
	}
	if (!found_open_brace) {
		return buxn_asm_error(basm, &token, "Macro must be followed by '{'");
	}

	symbol->type = BUXN_ASM_SYMTAB_ENTRY_MACRO;
	buxn_asm_macro_unit_t* macro = &symbol->macro;
	*macro = (buxn_asm_macro_unit_t) { 0 };

	int depth = 1;
	while (depth > 0 && buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		char first_char = token.lexeme.chars[0];
		if (first_char == '%') {
			return buxn_asm_error2(
				basm,
				&token, "Nested macro definition detected",
				start, "In this macro definition"
			);
		} else if (first_char == '}' && token.lexeme.len == 1) {
			--depth;
		} else {
			if (
				(
					first_char == ',' || first_char == '_'
					||
					first_char == '.' || first_char == '-'
					||
					first_char == ';' || first_char == '='
					||
					first_char == '!' || first_char == '?'
				)
				&&
				token.lexeme.len == 2
				&&
				token.lexeme.chars[1] == '{'
			) {
				++depth;
			}
		}

		if (depth > 0) {
			buxn_asm_token_link_t* token_link = buxn_asm_alloc(
				basm->ctx,
				sizeof(buxn_asm_token_link_t),
				_Alignof(buxn_asm_token_link_t)
			);
			*token_link = (buxn_asm_token_link_t){
				.token = buxn_asm_persist_token(basm, &token),
			};

			if (macro->first == NULL) {
				macro->first = token_link;
			}

			if (macro->last != NULL) {
				macro->last->next = token_link;
			}
			macro->last = token_link;
		}
	}

	if (depth != 0) {
		return buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t){
				.message = "Macro has unbalanced `{`",
				.region = &start->region,
			}
		);
	}

	macro->id = id;

	return true;
}

static bool
buxn_asm_process_macro(
	buxn_asm_t* basm,
	const buxn_asm_token_t* start,
	buxn_asm_unit_t* unit
) {
	buxn_asm_str_t macro_name = buxn_asm_str_pop_front(start->lexeme);
	buxn_asm_symtab_node_t* symbol = buxn_asm_find_or_create_symbol(basm, start, macro_name);
	if (symbol == NULL) {
		return false;
	} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN) {
		return buxn_asm_create_macro(basm, start, unit, symbol);
	} else {
		const char* related_message = symbol->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF
			? "Previously seen here as a label"
			: "Previously defined here";
		return buxn_asm_error2(
			basm,
			start, "Conflicting definition",
			&symbol->defining_token, related_message
		);
	}
}

static const buxn_asm_pstr_t*
buxn_asm_make_lambda_name(buxn_asm_t* basm, uint16_t lambda_id) {
	// A label can't start with @ so we use that
	char lambda_name[sizeof("@ffff") - 1];
	lambda_name[0] = '@';
	char* name_ptr = lambda_name + 1;
	// Write the digits
	{
		bool start_write = false;
		for (int i = 0; i < 4; ++i) {
			uint8_t digit = (lambda_id >> ((3 - i) * 4)) & 0x0f;
			if (start_write || digit != 0 || i >= 2) {
				start_write = true;
				if (digit < 10) {
					*name_ptr++ = '0' + digit;
				} else {
					*name_ptr++ = 'a' + (digit - 10);
				}
			}
		}
	}
	*name_ptr = '\0';
	return buxn_asm_strintern( basm, (buxn_asm_str_t){
		.chars = lambda_name,
		.len = (int)(name_ptr - lambda_name),
	});
}

static bool
buxn_asm_process_global_label(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	buxn_asm_str_t label_name = buxn_asm_str_pop_front(start->lexeme);

	if (label_name.len == 1 && label_name.chars[0] == '@') {
		// Anonymous backward label
		// This is not really a forward reference but the structure is similar
		// enough and we can reuse the same alloc pool
		buxn_asm_at_label_t* label = buxn_asm_alloc_forward_ref(basm);
		*label = (buxn_asm_forward_ref_t){
			.next = basm->at_labels,
			.label_id = ++basm->num_labels,
			.lambda_id = basm->num_lambdas++,
			.token = *start,
			.addr = basm->write_addr,
		};
		basm->at_labels = label;

		buxn_asm_put_symbol(basm->ctx, label->addr, &(buxn_asm_sym_t){
			.type = BUXN_ASM_SYM_LABEL,
			.name = buxn_asm_make_lambda_name(basm, label->label_id)->key.chars,
			.name_is_generated = true,
			.region = start->region,
			.id = label->label_id,
		});
		return true;
	} else {
		// Normal global label
		buxn_asm_symtab_node_t* label = buxn_asm_register_label(basm, start, label_name);
		if (label == NULL) { return false; }

		buxn_asm_str_t interned_name = label->key->key;
		int slash_pos;
		for (slash_pos = 0; slash_pos < interned_name.len; ++slash_pos) {
			if (interned_name.chars[slash_pos] == '/') { break; }
		}

		// Name of the scope must exclude the slash ('/') if any
		basm->label_scope = (buxn_asm_str_t){
			.chars = interned_name.chars,
			.len = slash_pos,
		};
		return true;
	}
}

static bool
buxn_asm_process_local_label(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	buxn_asm_str_t label_name;
	buxn_asm_str_t local_name = buxn_asm_str_pop_front(start->lexeme);
	if (!buxn_asm_resolve_local_name(basm, start, local_name, &label_name)) {
		return false;
	}

	return buxn_asm_register_label(basm, start, label_name);
}

static bool
buxn_asm_parse_number(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_str_t str,
	uint16_t* number_out,
	uint8_t* num_bytes_out
) {
	if (str.len == 0) {
		return buxn_asm_error(basm, token, "Invalid number: Empty string");
	}

	int start_index = 0;
	if (start_index < str.len && str.chars[start_index] == '+') { ++start_index; }
	if (start_index < str.len && str.chars[start_index] == '+') { ++start_index; }

	if (start_index == 0) {
		// Hex
		uint16_t number = 0;

		if (str.len > 4) {
			return buxn_asm_error(basm, token, "Invalid number: Too many characters");
		}

		for (int i = 0; i < str.len; ++i) {
			char ch = str.chars[str.len - i - 1];
			if ('0' <= ch && ch <= '9') {
				number |= (ch - '0') << (i * 4);
			} else if ('a' <= ch && ch <= 'f') {
				number |= (ch - 'a' + 10) << (i * 4);
			} else {
				return buxn_asm_error(basm, token, "Invalid number: Unexpected character found");
			}
		}

		*number_out = number;
		*num_bytes_out = str.len <= 2 ? 1 : 2;
	} else {
		// Decimal
		uint32_t number = 0;
		uint32_t limit = start_index == 1 ? UINT8_MAX : UINT16_MAX;
		for (int i = start_index; i < str.len; ++i) {
			char ch = str.chars[i];
			if ('0' <= ch && ch <= '9') {
				number *= 10;
				number += ch - '0';
				if (number > limit) {
					return buxn_asm_error(basm, token, "Invalid number: Too big");
				}
			} else {
				return buxn_asm_error(basm, token, "Invalid number: Unexpected character found");
			}
		}

		*number_out = (uint16_t)number;
		*num_bytes_out = (uint8_t)start_index;
	}

	return true;
}

static bool
buxn_asm_resolve_padding(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	uint16_t* padding_out
) {
	buxn_asm_str_t padding_str = buxn_asm_str_pop_front(token->lexeme);
	if (padding_str.len == 0) {
		return buxn_asm_error(basm, token, "Invalid padding");
	} else if (buxn_asm_is_number(padding_str)) {
		uint8_t num_bytes;
		return buxn_asm_parse_number(basm, token, padding_str, padding_out, &num_bytes);
	} else {
		buxn_asm_str_t padding_label;
		if (!buxn_asm_resolve_label_ref(basm, token, padding_str, &padding_label)) {
			return false;
		}

		if (padding_label.len == 1 && padding_label.chars[0] == '@') {
			// Anonymous backward reference
			buxn_asm_at_label_t* label = buxn_asm_pop_at_label(basm);

			if (label == NULL) {
				return buxn_asm_error(basm, token, "No previously declared @-label");
			}

			*padding_out = label->addr;
		} else {
			// Regular label
			const buxn_asm_pstr_t* interned_name = buxn_asm_strfind(basm, padding_label);
			if (interned_name == NULL) {
				return buxn_asm_error(basm, token, "Undeclared label is used for padding");
			}

			buxn_asm_symtab_node_t* symbol = buxn_asm_find_symbol(basm, interned_name);
			if (symbol == NULL) {
				return buxn_asm_error(basm, token, "Undeclared label is used for padding");
			}
			if (symbol->type != BUXN_ASM_SYMTAB_ENTRY_LABEL) {
				return buxn_asm_error(basm, token, "Invalid symbol is being used for padding");
			}

			symbol->referenced = true;
			*padding_out = symbol->label.address;
		}

		return true;
	}
}

static bool
buxn_asm_process_abs_padding(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t padding;
	if (!buxn_asm_resolve_padding(basm, start, &padding)) {
		return false;
	}

	basm->write_addr = padding;
	return true;
}

static bool
buxn_asm_process_rel_padding(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t padding;
	if (!buxn_asm_resolve_padding(basm, start, &padding)) {
		return false;
	}

	uint16_t old_addr = basm->write_addr;
	uint16_t new_addr = basm->write_addr += padding;
	if (new_addr < old_addr) {
		buxn_asm_warning(basm, start, "Relative padding caused wrap around");
	}

	return true;
}

static bool
buxn_asm_process_lit_number(buxn_asm_t* basm, const buxn_asm_token_t* start) {
	uint16_t number;
	buxn_asm_str_t str = buxn_asm_str_pop_front(start->lexeme);

	if (str.len == 0) {
		return buxn_asm_error(basm, start, "Invalid number: Empty string");
	}

	if (str.chars[0] != '+' && str.len != 2 && str.len != 4) {
		return buxn_asm_error(basm, start, "Invalid number: Invalid number of hex digits");
	}

	uint8_t num_bytes;
	if (!buxn_asm_parse_number(basm, start, str, &number, &num_bytes)) {
		return false;
	}

	if (num_bytes == 1) {
		if (!buxn_asm_emit_opcode(basm, start, 0x80, true)) { return false; }  // LIT
		if (!buxn_asm_emit_byte(basm, start, (uint8_t)number, true)) { return false; }
	} else {
		if (!buxn_asm_emit_opcode(basm, start, 0xa0, true)) { return false; }  // LIT2
		if (!buxn_asm_emit_short(basm, start, number, true)) { return false; }
	}

	return true;
}

static bool
buxn_asm_process_raw_number(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	uint16_t number;

	buxn_asm_str_t str = token->lexeme;
	if (str.len == 0) {
		return buxn_asm_error(basm, token, "Invalid number: Empty string");
	}

	if (str.chars[0] != '+' && str.len != 2 && str.len != 4) {
		return buxn_asm_error(basm, token, "Invalid number: Invalid number of hex digits");
	}

	uint8_t num_bytes;
	if (!buxn_asm_parse_number(basm, token, token->lexeme, &number, &num_bytes)) {
		return false;
	}

	if (num_bytes == 1) {
		if (!buxn_asm_emit_byte(basm, token, (uint8_t)number, false)) { return false; }
	} else {
		if (!buxn_asm_emit_short(basm, token, number, false)) { return false; }
	}

	return true;
}

static bool
buxn_asm_expand_macro(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_symtab_node_t* symbol,
	buxn_asm_unit_t* unit
) {
	buxn_asm_macro_unit_t* macro = &symbol->macro;
	if (basm->preprocessor_depth >= BUXN_ASM_MAX_PREPROCESSOR_DEPTH) {
		return buxn_asm_error(basm, token, "Max preprocessor depth depth reached");
	}

	if (macro->expanding) {
		return buxn_asm_error(basm, token, "Macro recursion detected");
	}

	buxn_asm_put_symbol(basm->ctx, 0, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_MACRO_REF,
		.name = symbol->key->key.chars,
		.region = token->region,
		.id = macro->id,
	});

	buxn_asm_token_t trigger_token = {
		.region = token->region,
		.lexeme = symbol->key->key,
	};

	// A macro whose name ends with ':' expects an argument
	if (symbol->key->key.chars[symbol->key->key.len - 1] == ':') {
		buxn_asm_token_t arg;
		bool got_arg = buxn_asm_next_token(basm, unit, &arg);
		if (!got_arg && basm->success) {  // EOF
			buxn_asm_error(basm, token, "Macro expects an argument");
		}

		if (!got_arg) {
			return false;
		}

		// We want to reuse the expansion format buffer so the lexeme needs to
		// have its own buffer
		macro->argument = buxn_asm_persist_token(basm, &arg);
	} else {
		macro->argument.lexeme.len = 0;
	}

	macro->expanding = true;
	macro->current = macro->first;
	++basm->preprocessor_depth;
	bool success = buxn_asm_process_unit(basm, &(buxn_asm_unit_t){
		.type = BUXN_ASM_UNIT_MACRO,
		.macro = macro,
	});
	--basm->preprocessor_depth;
	macro->current = NULL;
	macro->expanding = false;

	// Append an error to explain expansion chain
	if (!success) {
		buxn_asm_error(basm, &trigger_token, "Error while expanding macro");
	}

	return success;
}

static bool
buxn_asm_process_lambda_close(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	buxn_asm_forward_ref_t* ref = basm->lambdas;
	if (ref == NULL) {
		return buxn_asm_error(basm, token, "Unbalanced lambda");
	}

	uint16_t current_addr = basm->write_addr;
	int32_t addr = buxn_asm_calculate_addr(
		basm, &ref->token,
		ref->type,
		ref->addr, current_addr
	);

	basm->write_addr = ref->addr;
	if (!buxn_asm_emit_addr(
		basm, &ref->token,
		ref->type, ref->size,
		addr,
		token,
		NULL
	)) {
		return false;
	}
	basm->write_addr = current_addr;

	buxn_asm_put_symbol(basm->ctx, current_addr, &(buxn_asm_sym_t){
		.type = BUXN_ASM_SYM_LABEL,
		.name = buxn_asm_make_lambda_name(basm, ref->lambda_id)->key.chars,
		.name_is_generated = true,
		.region = token->region,
		.id = ref->label_id,
	});

	basm->lambdas = ref->next;
	buxn_asm_release_forward_ref(basm, ref);

	return true;
}

static bool
buxn_asm_process_word(
	buxn_asm_t* basm,
	const buxn_asm_token_t* token,
	buxn_asm_unit_t* unit
) {
	assert((!buxn_asm_is_runic(token->lexeme.chars[0])) && "Runic word encountered");

	buxn_asm_opcode_result_t opcode_result;
	if (buxn_asm_parse_opcode(token->lexeme, &opcode_result)) {
		if (opcode_result.has_redundant_flag) {
			buxn_asm_warning(basm, token, "Opcode contains redundant flags");
		}

		return buxn_asm_emit_opcode(basm, token, opcode_result.opcode, false);
	} else {
		buxn_asm_symtab_node_t* symbol = buxn_asm_find_or_create_symbol(basm, token, token->lexeme);
		if (symbol == NULL) {
			return false;
		} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_MACRO) {
			symbol->referenced = true;
			return buxn_asm_expand_macro(basm, token, symbol, unit);
		} else if (symbol->type == BUXN_ASM_SYMTAB_ENTRY_LABEL) {
			symbol->referenced = true;
			if (!buxn_asm_emit_opcode(basm, token, 0x60, false)) { return false; }  // JSI
			return buxn_asm_emit_backward_ref(
				basm, token,
				BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
				symbol,
				true,
				false
			);
		} else if (
			symbol->type == BUXN_ASM_SYMTAB_ENTRY_UNKNOWN
			|| symbol->type == BUXN_ASM_SYMTAB_ENTRY_FORWARD_REF
		) {
			if (!buxn_asm_emit_opcode(basm, token, 0x60, false)) { return false; }  // JSI
			return buxn_asm_emit_forward_ref(
				basm, token,
				BUXN_ASM_LABEL_REF_REL, BUXN_ASM_LABEL_REF_SHORT,
				symbol,
				false
			);
		} else {
			return buxn_asm_error(basm, token, "Unknown symbol type");
		}
	}
}

static bool
buxn_asm_process_text(buxn_asm_t* basm, const buxn_asm_token_t* token) {
	if (token->lexeme.len <= 1) {
		return buxn_asm_error(basm, token, "Invalid raw text");
	}

	buxn_asm_sym_t sym = {
		.type = BUXN_ASM_SYM_TEXT,
		.region = token->region,
		.id = token->lexeme.len,
	};
	uint16_t addr = basm->write_addr;
	for (int i = 1; i < token->lexeme.len; ++i) {
		if (!buxn_asm_emit(basm, token, token->lexeme.chars[i])) {
			return false;
		}
		buxn_asm_put_symbol(basm->ctx, addr + i - 1, &sym);
	}

	return true;
}

static bool
buxn_asm_process_unit(buxn_asm_t* basm, buxn_asm_unit_t* unit) {
	buxn_asm_token_t token;
	while (buxn_asm_next_token(basm, unit, &token)) {
		assert((token.lexeme.len > 0) && "Invalid token");

		switch (token.lexeme.chars[0]) {
			case '(':
				if (!buxn_asm_process_comment(basm, &token, unit)) {
					return false;
				}

				break;
			case ')':
				return buxn_asm_error(basm, &token, "Unexpected rune");
			case '[':
				// [word is accepted and ignored
				buxn_asm_process_mark(basm, &token);
				break;
			case ']':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				buxn_asm_process_mark(basm, &token);
				break;
			case '~': {
				if (basm->preprocessor_depth >= BUXN_ASM_MAX_PREPROCESSOR_DEPTH) {
					return buxn_asm_error(basm, &token, "Max preprocessor depth depth reached");
				}

				const buxn_asm_pstr_t* included_filename = buxn_asm_strintern(
					basm, buxn_asm_str_pop_front(token.lexeme)
				);
				++basm->preprocessor_depth;
				bool success = buxn_asm_process_file(basm, included_filename);
				--basm->preprocessor_depth;

				if (!success) {
					// Append another error to explain include chain
					return buxn_asm_error(
						basm,
						&(buxn_asm_token_t){
							.lexeme = included_filename->key,
							.region = token.region,
						},
						"Error while processing include"
					);
				}
			} break;
			case '%':
				if (!buxn_asm_process_macro(basm, &token, unit)) {
					return false;
				}
				break;
			case '@':
				if (!buxn_asm_process_global_label(basm, &token)) {
					return false;
				}
				break;
			case '&':
				if (!buxn_asm_process_local_label(basm, &token)) {
					return false;
				}
				break;
			case '!':
				if (!buxn_asm_emit_opcode(basm, &token, 0x40, true)) {  // JMI
					return false;
				}
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme),
					true
				)) {
					return false;
				}
				break;
			case '?':
				if (!buxn_asm_emit_opcode(basm, &token, 0x20, true)) {  // JCI
					return false;
				}
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme),
					true
				)) {
					return false;
				}
				break;
			case '}':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				if (!buxn_asm_process_lambda_close(basm, &token)) {
					return false;
				}
				break;
			case '{':
				if (token.lexeme.len != 1) {
					return buxn_asm_error(basm, &token, "Invalid runic token");
				}
				// fall-through
			case '/':
				if (!buxn_asm_emit_jsi(basm, &token)) {
					return false;
				}
				break;
			case '|':
				if (!buxn_asm_process_abs_padding(basm, &token)) {
					return false;
				}
				break;
			case '$':
				if (!buxn_asm_process_rel_padding(basm, &token)) {
					return false;
				}
				break;
			case '#':
				if (!buxn_asm_process_lit_number(basm, &token)) {
					return false;
				}
				break;
			case '.':
				if (!buxn_asm_emit_opcode(basm, &token, 0x80, true)) {  // LIT
					return false;
				}
				// fallthrough
			case '-':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_ZERO,
					BUXN_ASM_LABEL_REF_BYTE,
					buxn_asm_str_pop_front(token.lexeme),
					true
				)) {
					return false;
				}
				break;
			case ',':
				if (!buxn_asm_emit_opcode(basm, &token, 0x80, true)) {  // LIT
					return false;
				}
				// fallthrough
			case '_':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_REL,
					BUXN_ASM_LABEL_REF_BYTE,
					buxn_asm_str_pop_front(token.lexeme),
					true
				)) {
					return false;
				}
				break;
			case ';':
				if (!buxn_asm_emit_opcode(basm, &token, 0xa0, true)) {  // LIT2
					return false;
				}
				// fallthrough
			case '=':
				if (!buxn_asm_emit_label_ref(
					basm, &token,
					BUXN_ASM_LABEL_REF_ABS,
					BUXN_ASM_LABEL_REF_SHORT,
					buxn_asm_str_pop_front(token.lexeme),
					true
				)) {
					return false;
				}
				break;
			case '"':
				if (!buxn_asm_process_text(basm, &token)) {
					return false;
				}
				break;
			default: {
				if (buxn_asm_is_number(token.lexeme)) {
					if (!buxn_asm_process_raw_number(basm, &token)) {
						return false;
					}
				} else {
					if (!buxn_asm_process_word(basm, &token, unit)) {
						return false;
					}
				}
			} break;
		}
	}

	return basm->success;
}

static bool
buxn_asm_process_file(buxn_asm_t* basm, const buxn_asm_pstr_t* path) {
	buxn_asm_file_t* file = buxn_asm_fopen(basm->ctx, path->key.chars);
	if (file == NULL) {
		return buxn_asm_error_ex(
			basm,
			&(buxn_asm_report_t) {
				.message = "Could not open file",
				.region = &(buxn_asm_source_region_t){ .filename = path->key.chars }
			}
		);
	}

	buxn_asm_unit_t unit = {
		.type = BUXN_ASM_UNIT_FILE,
		.file = &(buxn_asm_file_unit_t){
			.file = file,
			.path = path,
			.pos = {
				.line = 1,
				.col = 1,
				.byte = 0,
			},
		}
	};
	bool success = buxn_asm_process_unit(basm, &unit);

	buxn_asm_fclose(basm->ctx, file);
	return success;
}

bool
buxn_asm(buxn_asm_ctx_t* ctx, const char* filename) {
	buxn_asm_t basm = {
		.ctx = ctx,
		.write_addr = BUXN_ASM_RESET_VECTOR,
		.success = true,
		.label_scope = {
			.chars = BUXN_ASM_DEFAULT_LABEL_SCOPE,
			.len = sizeof(BUXN_ASM_DEFAULT_LABEL_SCOPE) - 1,
		},
	};

	const buxn_asm_pstr_t* interned_name = buxn_asm_strintern(
		&basm,
		(buxn_asm_str_t){.chars = filename, .len = (int)strlen(filename) }
	);
	if (!buxn_asm_process_file(&basm, interned_name)) {
		return basm.success;
	}

	if (!buxn_asm_resolve(&basm)) {
		return basm.success;
	}

	return basm.success;
}
