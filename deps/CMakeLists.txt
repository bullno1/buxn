# --- blibs ---

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")
set_target_properties(blibs PROPERTIES FOLDER "deps")

# --- bestline ---

if (LINUX)

add_library(bestline STATIC "bestline/bestline.c")
target_include_directories(bestline PUBLIC "bestline")
set_target_properties(bestline PROPERTIES FOLDER "deps")

endif ()

# --- physfs ---

set(PHYSFS_COMMON_SOURCES
	"physfs/src/physfs.c"
	"physfs/src/physfs_unicode.c"
	"physfs/src/physfs_byteorder.c"
	"physfs/src/physfs_archiver_zip.c"
	"physfs/src/physfs_archiver_dir.c"
)
set(PHYSFS_LINUX_SOURCES
	"physfs/src/physfs_platform_unix.c"
	"physfs/src/physfs_platform_posix.c"
)
set(PHYSFS_WIN32_SOURCES
	"physfs/src/physfs_platform_windows.c"
)

if (LINUX)
	add_library(physfs STATIC ${PHYSFS_COMMON_SOURCES} ${PHYSFS_LINUX_SOURCES})
elseif (WIN32)
	add_library(physfs STATIC ${PHYSFS_COMMON_SOURCES} ${PHYSFS_WIN32_SOURCES})
endif ()
target_include_directories(physfs PUBLIC "physfs/src")
target_compile_definitions(physfs PRIVATE
	PHYSFS_SUPPORTS_DEFAULT=0
	PHYSFS_SUPPORTS_ZIP=1
	PHYSFS_NO_CDROM_SUPPORT=1
)
set_target_properties(physfs PROPERTIES FOLDER "deps")

# --- sokol ---

add_library(sokol INTERFACE)
target_include_directories(sokol INTERFACE "sokol")
set_target_properties(sokol PROPERTIES FOLDER "deps")

# --- sokol_gp ---

add_library(sokol_gp INTERFACE)
target_include_directories(sokol_gp INTERFACE "sokol_gp")
set_target_properties(sokol_gp PROPERTIES FOLDER "deps")

# --- termbox2 ---

add_library(termbox2 INTERFACE)
target_include_directories(termbox2 INTERFACE "termbox2")
set_target_properties(termbox2 PROPERTIES FOLDER "deps")

# --- utf8proc ---

add_library(utf8proc STATIC "utf8proc/utf8proc.c")
target_include_directories(utf8proc PUBLIC "utf8proc")
target_compile_definitions(utf8proc PUBLIC UTF8PROC_STATIC)
set_target_properties(utf8proc PROPERTIES FOLDER "deps")
