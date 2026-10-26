# Static libopenmpt for scripts/make-android-apk.sh, with its bundled decoders (minimp3, stb_vorbis,
# miniz) so it links standalone and the main CMakeLists' OPENMPT_LINKS_STANDALONE check passes.
cmake_minimum_required(VERSION 3.16)
project(openmpt LANGUAGES C CXX)

set(OPENMPT_SRC "" CACHE PATH "libopenmpt source tree (the +release.makefile tarball)")
set(OPENMPT_SOURCES "" CACHE FILEPATH "file with one source path per line, relative to OPENMPT_SRC")

file(STRINGS "${OPENMPT_SOURCES}" _rel)
set(_src)
foreach(f IN LISTS _rel)
	list(APPEND _src "${OPENMPT_SRC}/${f}")
endforeach()
list(APPEND _src
	"${OPENMPT_SRC}/include/minimp3/minimp3.c"
	"${OPENMPT_SRC}/include/stb_vorbis/stb_vorbis.c"
	"${OPENMPT_SRC}/include/miniz/miniz.c"
)

add_library(openmpt STATIC ${_src})
set_target_properties(openmpt PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON C_STANDARD 17)
target_include_directories(openmpt PRIVATE "${OPENMPT_SRC}" "${OPENMPT_SRC}/src" "${OPENMPT_SRC}/common" "${OPENMPT_SRC}/include")
target_compile_definitions(openmpt PRIVATE
	LIBOPENMPT_BUILD
	MPT_WITH_MINIMP3
	MPT_WITH_STBVORBIS STB_VORBIS_NO_PULLDATA_API STB_VORBIS_NO_STDIO
	MPT_WITH_MINIZ
)
target_compile_options(openmpt PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions -frtti> -w)

install(TARGETS openmpt ARCHIVE DESTINATION lib)
file(GLOB _headers "${OPENMPT_SRC}/libopenmpt/libopenmpt*.h" "${OPENMPT_SRC}/libopenmpt/libopenmpt*.hpp")
list(FILTER _headers EXCLUDE REGEX "_(impl|internal)\\.h")
install(FILES ${_headers} DESTINATION include/libopenmpt)
