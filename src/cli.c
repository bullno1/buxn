#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "vm.h"
#include "devices/console.h"
#include "devices/system.h"
#include "devices/datetime.h"

typedef struct {
	buxn_system_t system;
	buxn_console_t console;
} devices_t;

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	switch (buxn_device_id(address)) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, &devices->system, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	devices_t* devices = vm->userdata;
	switch (buxn_device_id(address)) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, &devices->system, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &devices->console, address);
			break;
	}
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stdout);
}

extern void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stderr);
}

int
main(int argc, const char* argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: buxn-cli <rom>\n");
		return 1;
	}
	int exit_code = 0;

	devices_t devices = { 0 };

	buxn_vm_t* vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	vm->userdata = &devices;
	vm->memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS;
	buxn_vm_reset(vm, BUXN_VM_RESET_ALL);

	FILE* rom_file;
	if ((rom_file = fopen(argv[1], "rb")) == NULL) {
		perror("Error while opening rom file");
		exit_code = 1;
		goto end;
	}

	uint8_t* read_pos = &vm->memory[BUXN_RESET_VECTOR];
	while (read_pos < vm->memory + vm->memory_size) {
		size_t num_bytes = fread(read_pos, 1, 1024, rom_file);
		if (num_bytes == 0) { break; }
		read_pos += num_bytes;
	}
	fclose(rom_file);

	buxn_console_init(vm, &devices.console, argc - 2, argv + 2);

	buxn_vm_execute(vm, BUXN_RESET_VECTOR);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	buxn_console_send_args(vm, &devices.console);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	while (
		buxn_system_exit_code(vm) < 0
		&& buxn_console_should_send_input(vm)
	) {
		char ch = fgetc(stdin);
		if (ch != EOF) {
			buxn_console_send_input(vm, &devices.console, ch);
		} else {
			buxn_console_send_input_end(vm, &devices.console);
			break;
		}
	}

	exit_code = buxn_system_exit_code(vm);
	if (exit_code < 0) { exit_code = 0; }
end:
	free(vm);
	return exit_code;
}
