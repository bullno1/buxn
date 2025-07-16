#include <buxn/vm/vm.h>
#include <buxn/vm/opcodes.h>
#include <stdbool.h>

#define BUXN_LOAD_STATE() \
	do { \
		wsp = vm->wsp; \
		rsp = vm->rsp; \
	} while (0)

#define BUXN_SAVE_STATE() \
	do { \
		vm->wsp = wsp; \
		vm->rsp = rsp; \
	} while (0)

// This is to make clangd stop complaining
#ifndef BUXN_VM_HOOK
#define BUXN_VM_HOOK()
#endif

#if defined(__GNUC__) || defined(__clang__)
// Dispatch using computed goto

#define BUXN_OPCODE_DISPATCH_TABLE_ENTRY(NAME, VALUE) [VALUE] = &&NAME,

#define BUXN_BEGIN_DISPATCH() \
	static const void* dispatch_table[256] = { \
		BUXN_OPCODE_DISPATCH(BUXN_OPCODE_DISPATCH_TABLE_ENTRY) \
	}; \
	{ \
		BUXN_NEXT_OPCODE();
#define BUXN_END_DISPATCH() }
#define BUXN_NEXT_OPCODE() \
	do { \
		BUXN_VM_HOOK() \
		uint8_t opcode = mem[pc++]; \
		goto *dispatch_table[opcode]; \
	} while (0)

#else
// Dispatch using switched goto

#define BUXN_BEGIN_DISPATCH() { BUXN_NEXT_OPCODE();
#define BUXN_END_DISPATCH() }
#define BUXN_NEXT_OPCODE() \
	do { \
		BUXN_VM_HOOK() \
		uint8_t opcode = mem[pc++]; \
		switch (opcode) { \
			BUXN_OPCODE_DISPATCH(BUXN_OPCODE_DISPATCH_TABLE_ENTRY) \
		} \
	} while (0)
#define BUXN_OPCODE_DISPATCH_TABLE_ENTRY(NAME, VALUE) case VALUE: goto NAME;

#endif

#define BUXN_SELECT(WHICH, LEFT, RIGHT) BUXN_CONCAT(BUXN_SELECT_, WHICH)(LEFT, RIGHT)
#define BUXN_SELECT_0(LEFT, RIGHT) LEFT
#define BUXN_SELECT_1(LEFT, RIGHT) RIGHT

// Expand a single polymorphic opcode implementation into multiple monomorphic implementations
#define BUXN_IMPL_MONO_OPCODE(NAME, IMPL) NAME: IMPL BUXN_NEXT_OPCODE();
#define BUXN_IMPL_POLY_OPCODE(BASE) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 0, 0, 0),             BUXN_CONCAT(BUXN_POLY_OP_, BASE)(0, 0, 0)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 0, 0, 1),             BUXN_CONCAT(BUXN_POLY_OP_, BASE)(0, 0, 1)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 0, 1, 0),             BUXN_CONCAT(BUXN_POLY_OP_, BASE)(0, 1, 0)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 0, 1, 1),             BUXN_CONCAT(BUXN_POLY_OP_, BASE)(0, 1, 1)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 1, 0, 0), kwsp = wsp; BUXN_CONCAT(BUXN_POLY_OP_, BASE)(1, 0, 0)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 1, 0, 1), kwsp = wsp; BUXN_CONCAT(BUXN_POLY_OP_, BASE)(1, 0, 1)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 1, 1, 0), krsp = rsp; BUXN_CONCAT(BUXN_POLY_OP_, BASE)(1, 1, 0)) \
	BUXN_IMPL_MONO_OPCODE(BUXN_OPCODE_NAME(BASE, 1, 1, 1), krsp = rsp; BUXN_CONCAT(BUXN_POLY_OP_, BASE)(1, 1, 1))
#define BUXN_STRINGIFY(X) BUXN_STRINGIFY_1(X)
#define BUXN_STRINGIFY_1(X) #X

// Implement all variants of POP
#define BUXN_POLY_POP(K_, R_, S_) \
	BUXN_CONCAT4( \
		BUXN_UOP_POP, \
		BUXN_MAYBE_PICK(2, S_), \
		BUXN_MAYBE_PICK(K, K_), \
		BUXN_MAYBE_PICK(R, R_) \
	)
#define BUXN_UOP_POP()    BUXN_UOP_POP1X(ws, wsp)
#define BUXN_UOP_POP2()   BUXN_UOP_POP2X(ws, wsp)
#define BUXN_UOP_POPR()   BUXN_UOP_POP1X(rs, rsp)
#define BUXN_UOP_POP2R()  BUXN_UOP_POP2X(rs, rsp)
#define BUXN_UOP_POPK()   BUXN_UOP_POP1X(ws, kwsp)
#define BUXN_UOP_POP2K()  BUXN_UOP_POP2X(ws, kwsp)
#define BUXN_UOP_POPKR()  BUXN_UOP_POP1X(rs, krsp)
#define BUXN_UOP_POP2KR() BUXN_UOP_POP2X(rs, krsp)
#define BUXN_UOP_POP1X(STACK, PTR) (STACK[(uint8_t)(--PTR)])
#define BUXN_UOP_POP2X(STACK, PTR) \
	(PTR -= 2, ((uint16_t)STACK[PTR] << 8) | ((uint16_t)STACK[(uint8_t)(PTR + 1)]))

// Implement all variants of PUSH
#define BUXN_POLY_PUSH(R_, S_) \
	BUXN_CONCAT4( \
		BUXN_UOP_PUSH, \
		BUXN_MAYBE_PICK(2, S_), \
		BUXN_MAYBE_PICK(R, R_), \
	)
#define BUXN_UOP_PUSH(VALUE)   BUXN_UOP_PUSH1X(ws, wsp, VALUE)
#define BUXN_UOP_PUSH2(VALUE)  BUXN_UOP_PUSH2X(ws, wsp, VALUE)
#define BUXN_UOP_PUSHR(VALUE)  BUXN_UOP_PUSH1X(rs, rsp, VALUE)
#define BUXN_UOP_PUSH2R(VALUE) BUXN_UOP_PUSH2X(rs, rsp, VALUE)
#define BUXN_UOP_PUSH1X(STACK, PTR, VALUE) STACK[PTR++] = (uint8_t)(VALUE & 0xff)
#define BUXN_UOP_PUSH2X(STACK, PTR, VALUE) \
	do { \
		STACK[PTR]                = (uint8_t)(((VALUE) >> 8) & 0xff); \
		STACK[(uint8_t)(PTR + 1)] = (uint8_t)((VALUE)        & 0xff); \
		PTR += 2; \
	} while (0)

#define BUXN_SIMPLE_POLY_OP(NAME, K_, R_, S_) \
	BUXN_CONCAT4(BUXN_POLY_OP_, NAME, _IMPL,)( \
		BUXN_POLY_POP(K_, R_, S_), \
		BUXN_POLY_PUSH(R_, S_) \
	)

// Implement all variants of JMP
#define BUXN_POLY_JMP(S_) BUXN_SELECT(S_, BUXN_UOP_JMP_REL, BUXN_UOP_JMP_ABS)
#define BUXN_UOP_JMP_REL(OFFSET)   pc = BUXN_PC_REL(OFFSET)
#define BUXN_UOP_JMP_ABS(POSITION) pc = POSITION
#define BUXN_PC_REL(OFFSET) ((uint16_t)((int32_t)pc + (int32_t)(int8_t)OFFSET))

// Full memory load/store
#define BUXN_POLY_LOAD(S_) BUXN_SELECT(S_, BUXN_UOP_LOAD1, BUXN_UOP_LOAD2)
#define BUXN_UOP_LOAD1(ADDR) BUXN_UOP_LOAD1_GEN(mem, ADDR, 0xffff)
#define BUXN_UOP_LOAD2(ADDR) BUXN_UOP_LOAD2_GEN(mem, ADDR, 0xffff)

#define BUXN_POLY_STORE(S_) BUXN_SELECT(S_, BUXN_UOP_STORE1, BUXN_UOP_STORE2)
#define BUXN_UOP_STORE1(ADDR, VALUE) BUXN_UOP_STORE1_GEN(mem, ADDR, 0xffff, VALUE)
#define BUXN_UOP_STORE2(ADDR, VALUE) BUXN_UOP_STORE2_GEN(mem, ADDR, 0xffff, VALUE)

// Zero page load/store
#define BUXN_POLY_LOADZ(S_) BUXN_SELECT(S_, BUXN_UOP_LOADZ1, BUXN_UOP_LOADZ2)
#define BUXN_UOP_LOADZ1(ADDR) BUXN_UOP_LOAD1_GEN(mem, ADDR, 0xff)
#define BUXN_UOP_LOADZ2(ADDR) BUXN_UOP_LOAD2_GEN(mem, ADDR, 0xff)

#define BUXN_POLY_STOREZ(S_) BUXN_SELECT(S_, BUXN_UOP_STOREZ1, BUXN_UOP_STOREZ2)
#define BUXN_UOP_STOREZ1(ADDR, VALUE) BUXN_UOP_STORE1_GEN(mem, ADDR, 0xff, VALUE)
#define BUXN_UOP_STOREZ2(ADDR, VALUE) BUXN_UOP_STORE2_GEN(mem, ADDR, 0xff, VALUE)

// Wrap-around generic load/store
#define BUXN_UOP_LOAD1_GEN(SRC, ADDR, ADDR_MASK) (SRC[(ADDR) & ADDR_MASK])
#define BUXN_UOP_LOAD2_GEN(SRC, ADDR, ADDR_MASK) \
	(((uint16_t)BUXN_UOP_LOAD1_GEN(SRC, ADDR    , ADDR_MASK) << 8) \
	|((uint16_t)BUXN_UOP_LOAD1_GEN(SRC, ADDR + 1, ADDR_MASK)     ))
#define BUXN_UOP_STORE1_GEN(DST, ADDR, ADDR_MASK, VALUE) \
	DST[(ADDR) & ADDR_MASK] = (uint8_t)VALUE
#define BUXN_UOP_STORE2_GEN(DST, ADDR, ADDR_MASK, VALUE) \
	do { \
		BUXN_UOP_STORE1_GEN(DST, ADDR    , ADDR_MASK, (uint8_t)((VALUE >> 8) & 0xff)); \
		BUXN_UOP_STORE1_GEN(DST, ADDR + 1, ADDR_MASK, (uint8_t)((VALUE     ) & 0xff)); \
	} while (0)

// Device I/O
#define BUXN_POLY_DEV_IN(S_) BUXN_SELECT(S_, BUXN_UOP_DEV_IN1, BUXN_UOP_DEV_IN2)
#define BUXN_UOP_DEV_IN1(ADDR) buxn_vm_dei(vm, (uint8_t)ADDR)
#define BUXN_UOP_DEV_IN2(ADDR) \
	((uint16_t)(BUXN_UOP_DEV_IN1(ADDR    ) << 8) \
	|(uint16_t)(BUXN_UOP_DEV_IN1(ADDR + 1)     ))

#define BUXN_POLY_DEV_OUT(S_) BUXN_SELECT(S_, BUXN_UOP_DEV_OUT1, BUXN_UOP_DEV_OUT2)
#define BUXN_UOP_DEV_OUT1(ADDR, VALUE) \
	do { \
		dev[(ADDR) & 0xff] = (uint8_t)VALUE; \
		buxn_vm_deo(vm, (uint8_t)ADDR); \
	} while (0)
#define BUXN_UOP_DEV_OUT2(ADDR, VALUE) \
	do { \
		dev[(ADDR    ) & 0xff] = (uint8_t)(VALUE >> 8); \
		dev[(ADDR + 1) & 0xff] = (uint8_t)VALUE; \
		buxn_vm_deo(vm, (uint8_t)ADDR); \
		buxn_vm_deo(vm, (uint8_t)(ADDR + 1)); \
	} while (0)

// Polymorphic opcodes parameterized over polymorphic uops

// a -- a+1
#define BUXN_POLY_OP_INC(...) BUXN_SIMPLE_POLY_OP(INC, __VA_ARGS__)
#define BUXN_POLY_OP_INC_IMPL(POP, PUSH) \
	{ \
		a = POP(); \
		a += 1; \
		PUSH(a); \
	}

// a --
#define BUXN_POLY_OP_POP(...) BUXN_SIMPLE_POLY_OP(POP, __VA_ARGS__)
#define BUXN_POLY_OP_POP_IMPL(POP, PUSH) \
	{ \
		POP(); \
	}

// a b -- b
#define BUXN_POLY_OP_NIP(...) BUXN_SIMPLE_POLY_OP(NIP, __VA_ARGS__)
#define BUXN_POLY_OP_NIP_IMPL(POP, PUSH) \
	{ \
		b = POP(); \
		/* a = */ POP(); \
		PUSH(b); \
	}

// a b -- b a
#define BUXN_POLY_OP_SWP(...) BUXN_SIMPLE_POLY_OP(SWP, __VA_ARGS__)
#define BUXN_POLY_OP_SWP_IMPL(POP, PUSH) \
	{ \
		b = POP(); \
		a = POP(); \
		PUSH(b); \
		PUSH(a); \
	}

// a b c -- b c a
#define BUXN_POLY_OP_ROT(...) BUXN_SIMPLE_POLY_OP(ROT, __VA_ARGS__)
#define BUXN_POLY_OP_ROT_IMPL(POP, PUSH) \
	{ \
		c = POP(); \
		b = POP(); \
		a = POP(); \
		PUSH(b); \
		PUSH(c); \
		PUSH(a); \
	}

// a -- a a
#define BUXN_POLY_OP_DUP(...) BUXN_SIMPLE_POLY_OP(DUP, __VA_ARGS__)
#define BUXN_POLY_OP_DUP_IMPL(POP, PUSH) \
	{ \
		a = POP(); \
		PUSH(a); \
		PUSH(a); \
	}

// a b -- a b a
#define BUXN_POLY_OP_OVR(...) BUXN_SIMPLE_POLY_OP(OVR, __VA_ARGS__)
#define BUXN_POLY_OP_OVR_IMPL(POP, PUSH) \
	{ \
		b = POP(); \
		a = POP(); \
		PUSH(a); \
		PUSH(b); \
		PUSH(a); \
	}

// a b -- a=b
#define BUXN_POLY_OP_EQU(...) BUXN_POLY_CMP_OP(==, __VA_ARGS__)

// a b -- a!=b
#define BUXN_POLY_OP_NEQ(...) BUXN_POLY_CMP_OP(!=, __VA_ARGS__)

// a b -- a>b
#define BUXN_POLY_OP_GTH(...) BUXN_POLY_CMP_OP(>, __VA_ARGS__)

// a b -- a<b
#define BUXN_POLY_OP_LTH(...) BUXN_POLY_CMP_OP(<, __VA_ARGS__)

#define BUXN_POLY_CMP_OP(BIN_OP, K_, R_, S_) \
	{ \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		c = a BIN_OP b; \
		BUXN_POLY_PUSH(R_, 0)(c); \
	}

// addr --
#define BUXN_POLY_OP_JMP(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_JMP(S_)(a); \
	}

// cond8 addr --
#define BUXN_POLY_OP_JCN(K_, R_, S_) \
	{ \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		if (a != 0) { \
			BUXN_POLY_JMP(S_)(b); \
		} \
	}

// addr -- | ret16
#define BUXN_POLY_OP_JSR(K_, R_, S_) \
	{ \
		BUXN_POLY_PUSH(BUXN_NOT(R_), 1)(pc); \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_JMP(S_)(a); \
	}

// a -- | a
#define BUXN_POLY_OP_STH(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_PUSH(BUXN_NOT(R_), S_)(a); \
	}
#define BUXN_NOT(X) BUXN_CONCAT(BUXN_NOT_, X)
#define BUXN_NOT_0 1
#define BUXN_NOT_1 0

// addr8 -- value
#define BUXN_POLY_OP_LDZ(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		b = BUXN_POLY_LOADZ(S_)(a); \
		BUXN_POLY_PUSH(R_, S_)(b); \
	}

// val addr8 --
#define BUXN_POLY_OP_STZ(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_STOREZ(S_)(a, b); \
	}

// addr8 -- value
#define BUXN_POLY_OP_LDR(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		b = BUXN_POLY_LOAD(S_)(BUXN_PC_REL(a)); \
		BUXN_POLY_PUSH(R_, S_)(b); \
	}

// val addr8 --
#define BUXN_POLY_OP_STR(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_STORE(S_)(BUXN_PC_REL(a), b); \
	}

// addr16 -- value
#define BUXN_POLY_OP_LDA(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 1)(); \
		b = BUXN_POLY_LOAD(S_)(a); \
		BUXN_POLY_PUSH(R_, S_)(b); \
	}

// val addr16 --
#define BUXN_POLY_OP_STA(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 1)(); \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_POLY_STORE(S_)(a, b); \
	}

// device8 -- value
#define BUXN_POLY_OP_DEI(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		BUXN_SAVE_STATE(); \
		b = BUXN_POLY_DEV_IN(S_)(a); \
		BUXN_LOAD_STATE(); \
		BUXN_POLY_PUSH(R_, S_)(b); \
	}

// value device8 --
#define BUXN_POLY_OP_DEO(K_, R_, S_) \
	{ \
		a = BUXN_POLY_POP(K_, R_, 0)(); \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		BUXN_SAVE_STATE(); \
		BUXN_POLY_DEV_OUT(S_)(a, b); \
		BUXN_LOAD_STATE(); \
	}

// a b -- a BIN_OP b
#define BUXN_POLY_OP_ADD(...) BUXN_POLY_BIN_OP(+, __VA_ARGS__)
#define BUXN_POLY_OP_SUB(...) BUXN_POLY_BIN_OP(-, __VA_ARGS__)
#define BUXN_POLY_OP_MUL(...) BUXN_POLY_BIN_OP(*, __VA_ARGS__)
#define BUXN_POLY_OP_AND(...) BUXN_POLY_BIN_OP(&, __VA_ARGS__)
#define BUXN_POLY_OP_ORA(...) BUXN_POLY_BIN_OP(|, __VA_ARGS__)
#define BUXN_POLY_OP_EOR(...) BUXN_POLY_BIN_OP(^, __VA_ARGS__)
#define BUXN_POLY_OP_DIV(...) BUXN_SIMPLE_POLY_OP(DIV, __VA_ARGS__)
#define BUXN_POLY_OP_DIV_IMPL(POP, PUSH) \
	{ \
		b = POP(); \
		a = POP(); \
		c = b != 0 ? a / b : 0; \
		PUSH(c); \
	}

// a shift8 -- c
#define BUXN_POLY_OP_SFT(K_, R_, S_) \
	{ \
		b = BUXN_POLY_POP(K_, R_, 0)(); \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		c = (a >> (b & 0x0f)) << ((b & 0xf0) >> 4); \
		BUXN_POLY_PUSH(R_, S_)(c); \
	}

#define BUXN_POLY_BIN_OP(BIN_OP, K_, R_, S_) \
	{ \
		b = BUXN_POLY_POP(K_, R_, S_)(); \
		a = BUXN_POLY_POP(K_, R_, S_)(); \
		c = a BIN_OP b; \
		BUXN_POLY_PUSH(R_, S_)(c); \
	}

#if defined(__clang__)
#define BUXN_WARNING_PUSH() _Pragma("clang diagnostic push")
#define BUXN_WARNING_POP() _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#define BUXN_WARNING_PUSH() _Pragma("GCC diagnostic push")
#define BUXN_WARNING_POP() _Pragma("GCC diagnostic pop")
#else
#define BUXN_WARNING_PUSH()
#define BUXN_WARNING_POP()
#endif

BUXN_WARNING_PUSH()

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#pragma clang diagnostic ignored "-Wunused-value"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

#if defined(__clang__) || defined(__GNUC__)
__attribute__((unused))
#endif
static void
BUXN_VM_EXECUTE(buxn_vm_t* vm, uint16_t pc, const buxn_vm_hook_t hook) {
	(void)hook;

	uint8_t* restrict const ws = vm->ws;
	uint8_t* restrict const rs = vm->rs;
	uint8_t* restrict const mem = vm->memory;
	uint8_t* restrict const dev = vm->device;
	uint8_t wsp, rsp;
	uint8_t kwsp, krsp;
	uint16_t a, b, c;  // Temporary variables following stack notation
	BUXN_LOAD_STATE();

	BUXN_BEGIN_DISPATCH()

		BUXN_IMPL_MONO_OPCODE(BRK, { BUXN_SAVE_STATE(); return; })
		BUXN_IMPL_POLY_OPCODE(INC)
		BUXN_IMPL_POLY_OPCODE(POP)
		BUXN_IMPL_POLY_OPCODE(NIP)
		BUXN_IMPL_POLY_OPCODE(SWP)
		BUXN_IMPL_POLY_OPCODE(ROT)
		BUXN_IMPL_POLY_OPCODE(DUP)
		BUXN_IMPL_POLY_OPCODE(OVR)
		BUXN_IMPL_POLY_OPCODE(EQU)
		BUXN_IMPL_POLY_OPCODE(NEQ)
		BUXN_IMPL_POLY_OPCODE(GTH)
		BUXN_IMPL_POLY_OPCODE(LTH)
		BUXN_IMPL_POLY_OPCODE(JMP)
		BUXN_IMPL_POLY_OPCODE(JCN)
		BUXN_IMPL_POLY_OPCODE(JSR)
		BUXN_IMPL_POLY_OPCODE(STH)
		BUXN_IMPL_POLY_OPCODE(LDZ)
		BUXN_IMPL_POLY_OPCODE(STZ)
		BUXN_IMPL_POLY_OPCODE(LDR)
		BUXN_IMPL_POLY_OPCODE(STR)
		BUXN_IMPL_POLY_OPCODE(LDA)
		BUXN_IMPL_POLY_OPCODE(STA)
		BUXN_IMPL_POLY_OPCODE(DEI)
		BUXN_IMPL_POLY_OPCODE(DEO)
		BUXN_IMPL_POLY_OPCODE(ADD)
		BUXN_IMPL_POLY_OPCODE(SUB)
		BUXN_IMPL_POLY_OPCODE(MUL)
		BUXN_IMPL_POLY_OPCODE(DIV)
		BUXN_IMPL_POLY_OPCODE(AND)
		BUXN_IMPL_POLY_OPCODE(ORA)
		BUXN_IMPL_POLY_OPCODE(EOR)
		BUXN_IMPL_POLY_OPCODE(SFT)
		BUXN_IMPL_MONO_OPCODE(JCI, {
			// cond8 --
			a = BUXN_UOP_POP();
			if (a != 0) {
				b = BUXN_UOP_LOAD2(pc);
				pc += (b + 2);
			} else {
				pc += 2;
			}
		})
		BUXN_IMPL_MONO_OPCODE(JMI, {
			// --
			a = BUXN_UOP_LOAD2(pc);
			pc += (a + 2);
		})
		BUXN_IMPL_MONO_OPCODE(JSI, {
			// --
			BUXN_UOP_PUSH2R(pc + 2);
			a = BUXN_UOP_LOAD2(pc);
			pc += (a + 2);
		})
		// -- a
		BUXN_IMPL_MONO_OPCODE(LIT, {
			a = BUXN_UOP_LOAD1(pc);
			pc += 1;
			BUXN_UOP_PUSH(a);
		})
		BUXN_IMPL_MONO_OPCODE(LIT2, {
			a = BUXN_UOP_LOAD2(pc);
			pc += 2;
			BUXN_UOP_PUSH2(a);
		})
		BUXN_IMPL_MONO_OPCODE(LITr, {
			a = BUXN_UOP_LOAD1(pc);
			pc += 1;
			BUXN_UOP_PUSHR(a);
		})
		BUXN_IMPL_MONO_OPCODE(LIT2r, {
			a = BUXN_UOP_LOAD2(pc);
			pc += 2;
			BUXN_UOP_PUSH2R(a);
		})

	BUXN_END_DISPATCH()
}

BUXN_WARNING_POP()
