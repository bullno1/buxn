#include <buxn/vm/vm.h>
#include <stdbool.h>
#include <string.h>

static void
buxn_vm_execute_without_hook(buxn_vm_t* vm, uint16_t pc, const buxn_vm_hook_t hook);

static void
buxn_vm_execute_with_hook(buxn_vm_t* vm, uint16_t pc, const buxn_vm_hook_t hook);

void
buxn_vm_reset(buxn_vm_t* vm, uint8_t reset_flags) {
	if ((reset_flags & BUXN_VM_RESET_STACK) > 0) {
		vm->wsp = 0;
		vm->rsp = 0;
		memset(vm->ws, 0, sizeof(vm->ws));
		memset(vm->rs, 0, sizeof(vm->rs));
	}

	if ((reset_flags & BUXN_VM_RESET_DEVICE) > 0) {
		memset(vm->device, 0, sizeof(vm->device));
	}

	if ((reset_flags & BUXN_VM_RESET_ZERO_PAGE) > 0) {
		memset(vm->memory, 0, BUXN_RESET_VECTOR);
	}

	if ((reset_flags & BUXN_VM_RESET_HIGH_MEM) > 0) {
		memset(vm->memory + BUXN_RESET_VECTOR, 0, vm->config.memory_size - BUXN_RESET_VECTOR);
	}
}

void
buxn_vm_execute(buxn_vm_t* vm, uint16_t pc) {
	if (pc == 0) { return; }

	// Creating 2 separate versions is the only way to have optimized opcode
	// dispatch when no debug hook is attached
	if (vm->config.hook.fn != NULL) {
		buxn_vm_execute_with_hook(vm, pc, vm->config.hook);
	} else {
		buxn_vm_execute_without_hook(vm, pc, vm->config.hook);
	}
}

#define BUXN_VM_HOOK()
#define BUXN_VM_EXECUTE buxn_vm_execute_without_hook
#include "exec.h"

#undef BUXN_VM_HOOK
#undef BUXN_VM_EXECUTE

#define BUXN_VM_EXECUTE buxn_vm_execute_with_hook
#define BUXN_VM_HOOK() BUXN_SAVE_STATE(); hook.fn(vm, pc, hook.userdata);
#include "exec.h"
