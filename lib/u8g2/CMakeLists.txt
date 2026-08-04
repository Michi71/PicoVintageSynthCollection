# u8g2 source location, in order of precedence:
#   1. -DU8G2_PATH=... on the cmake command line
#   2. the U8G2_PATH environment variable
#   3. the lib/u8g2/u8g2 submodule in this repository
#
# Only csrc/ is compiled -- tools/ (183 MB of BDF font sources) and sys/ (46 MB
# of demo projects for other platforms) are never read, which is why a shared
# checkout can be sparse.
#
# CI passes neither, so it builds from the submodule and the pinned u8g2 commit
# stays part of the repository.
# Cached, unlike the plain set() this used to be. -DU8G2_PATH=... only lives
# for the invocation it is passed on; anything that makes CMake re-configure
# later -- editing a CMakeLists, adding a source file -- then loses it and the
# build stops at the FATAL_ERROR below with no obvious connection to what was
# changed. PICO_SDK_PATH survives because the SDK import script caches it.
if(NOT U8G2_PATH)
    if(DEFINED ENV{U8G2_PATH})
        set(U8G2_PATH "$ENV{U8G2_PATH}")
    else()
        set(U8G2_PATH "${CMAKE_CURRENT_LIST_DIR}/u8g2")
    endif()
endif()
set(U8G2_PATH "${U8G2_PATH}" CACHE PATH "u8g2 source tree (expects csrc/ below it)" FORCE)

if(NOT EXISTS "${U8G2_PATH}/csrc/u8g2.h")
    message(FATAL_ERROR
        "u8g2 sources not found at '${U8G2_PATH}/csrc'.\n"
        "Either initialise the submodule (git submodule update --init "
        "lib/u8g2/u8g2) or point U8G2_PATH at a shared checkout.")
endif()

file(GLOB COMPONENT_SRCS ${U8G2_PATH}/csrc/*.c)

add_library(u8g2 STATIC ${COMPONENT_SRCS})

target_include_directories(u8g2 PUBLIC
	${U8G2_PATH}/csrc
)

target_link_libraries(u8g2 pico_stdlib)

target_compile_options(u8g2 PUBLIC
	-Wno-deprecated-declarations
	-Wno-unused-variable
	-Wno-unused-function
	-Wno-unused-parameter
	-Wno-write-strings
	-Wno-narrowing
)
