#include <buxn/metadata.h>
#include <buxn/vm/vm.h>

static const buxn_metadata_t BUXN_NO_METADATA = { 0 };

static buxn_metadata_t
buxn_metadata_parse_internal(uint8_t* addr, uint8_t* max_addr) {
	uint8_t version = *addr;
	char* content = (char*)++addr;

	while (1) {
		if (addr >= max_addr) { return BUXN_NO_METADATA; }

		if (*addr == 0) { break; }
		++addr;
	}
	int content_len = (int)((char*)addr - content);

	uint8_t* extension_address = NULL;
	uint8_t num_extensions = 0;

	if (addr < max_addr - 2) {
		num_extensions = *(addr + 1);
		if (num_extensions > 0) {
			extension_address = addr + 2;
			if ((max_addr - extension_address) / 3 < num_extensions) {
				extension_address = NULL;
				num_extensions = 0;
			}
		}
	}

	return (buxn_metadata_t){
		.version = version,
		.content = content,
		.content_len = content_len,
		.extension_address = extension_address,
		.num_extensions = num_extensions,
	};
}

buxn_metadata_t
buxn_metadata_parse_from_memory(struct buxn_vm_s* vm, uint16_t address) {
	if ((size_t)address >= vm->config.memory_size) { return BUXN_NO_METADATA; }

	return buxn_metadata_parse_internal(
		vm->memory + address,
		vm->memory + UINT16_MAX
	);
}

buxn_metadata_t
buxn_metadata_parse_from_rom(uint8_t* rom, size_t size) {
	if (size < 6) { return BUXN_NO_METADATA; }

	if (
		rom[0] == 0xa0     // LIT2
		                   // <addr>
		&& rom[3] == 0x80  // LIT
		&& rom[4] == 0x06  // .System/metadata
		&& rom[5] == 0x37  // DEO2
	) {
		uint16_t addr = (rom[1] << 8) | rom[2];
		if (addr <= 256) { return BUXN_NO_METADATA; }

		return buxn_metadata_parse_internal(rom + addr - 256, rom + size);
	} else {
		return BUXN_NO_METADATA;
	}
}

buxn_metadata_ext_t
buxn_metadata_get_ext(const buxn_metadata_t* metadata, uint8_t index) {
	if (index < metadata->num_extensions) {
		// The parsing functions have already validated the extensions
		// so it is safe to deference
		uint8_t id = metadata->extension_address[3 * index];
		uint8_t value_hi = metadata->extension_address[3 * index + 1];
		uint8_t value_lo = metadata->extension_address[3 * index + 2];
		return (buxn_metadata_ext_t){
			.id = id,
			.value = (value_hi << 8) | value_lo,
		};
	} else {
		return (buxn_metadata_ext_t){ .id = 0, .value = 0 };
	}
}
