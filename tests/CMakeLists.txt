set(BUXN_TESTS_COMMON_SOURCES
	"main.c"
	"common.c"
	"asm.c"
	"asm-extensions.c"
	"vm.c"
	"chess.c"
)
set(BUXN_TESTS_LINUX_SOURCES
	"dbg.c"  # socketpair is Linux only
)
set(BUXN_TESTS_WIN32_SOURCES "resources.rc")
if (LINUX)
	add_executable(buxn-tests ${BUXN_TESTS_COMMON_SOURCES} ${BUXN_TESTS_LINUX_SOURCES})
	target_link_libraries(buxn-tests PRIVATE
		blibs
		buxn-vm
		buxn-asm
		buxn-asm-chess
		buxn-devices
		buxn-dbg-core
		buxn-dbg-wire
		buxn-dbg-protocol
		buxn-dbg-transport
	)
elseif (WIN32)
	add_executable(buxn-tests ${BUXN_TESTS_COMMON_SOURCES} ${BUXN_TESTS_WIN32_SOURCES})
	target_link_libraries(buxn-tests PRIVATE
		blibs
		buxn-vm
		buxn-asm
		buxn-asm-chess
		buxn-devices
	)
endif ()
target_compile_definitions(buxn-tests PRIVATE BUXN_CMAKE_PATH)
