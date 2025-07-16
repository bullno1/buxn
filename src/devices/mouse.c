#include <buxn/devices/mouse.h>
#include <buxn/vm/vm.h>

uint8_t
buxn_mouse_dei(struct buxn_vm_s* vm, buxn_mouse_t* device, uint8_t address) {
	switch (address) {
		case 0x92: return (uint8_t)(device->x >> 8);
		case 0x93: return (uint8_t)device->x;
		case 0x94: return (uint8_t)(device->y >> 8);
		case 0x95: return (uint8_t)device->y;
		case 0x96: return (uint8_t)device->state;
		case 0x9a: return (uint8_t)(device->scroll_x >> 8);
		case 0x9b: return (uint8_t)device->scroll_x;
		case 0x9c: return (uint8_t)(device->scroll_y >> 8);
		case 0x9d: return (uint8_t)device->scroll_y;
		default: return vm->device[address];
	}
}

void
buxn_mouse_deo(struct buxn_vm_s* vm, buxn_mouse_t* device, uint8_t address) {
	(void)vm;
	(void)device;
	(void)address;
}

void
buxn_mouse_update(struct buxn_vm_s* vm) {
	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x90);
	if (vector_addr != 0) { buxn_vm_execute(vm, vector_addr); }
}
