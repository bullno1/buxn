cmake_minimum_required(VERSION 3.15)
project(buxn)

# Fix output dir
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)
if (IPO_SUPPORTED AND (NOT RELOADABLE))
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Add deps first
add_subdirectory(deps)

# Then everything else to avoid leaking into deps

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

if (MSVC)
	add_compile_options(/W3 /WX)
	add_compile_options(/wd4200) # Flexible array member is a standard feature since C99
	add_compile_options(/wd4324) # _Alignof is intentional
	add_compile_options(/wd4100) # Unreferenced parameter
	add_compile_options(/wd4459) # Hiding global definition
	add_compile_options(/experimental:c11atomics)  # Atomics
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

	# Because CMake can't set proper flags even with CMAKE_INTERPROCEDURAL_OPTIMIZATION_Release
	set(RELEASE_COMPILER_FLAGS "/Gy;/Ob2;/GL;/Oi")
	set(RELEASE_LINKER_FLAGS "/LTCG")

	add_compile_options("$<$<CONFIG:RelWithDebInfo>:${RELEASE_COMPILER_FLAGS}>")
	add_link_options("$<$<CONFIG:RelWithDebInfo>:${RELEASE_LINKER_FLAGS}>")
else()
	add_compile_options(
		-Wall -Wextra -pedantic -Werror
		-Wno-unused-parameter
		-Wno-error=c23-extensions
	)
endif()

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "\${ORIGIN}")

add_subdirectory(src)
add_subdirectory(tests)
