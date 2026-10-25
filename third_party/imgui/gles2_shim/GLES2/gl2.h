// ImGui's OpenGL3 backend in ES2 mode includes <GLES2/gl2.h>. Point it at the game's glad
// loader so ImGui calls the same GL entry points (GLES 2.0, or the GL 2.1 fallback on macOS).
#include <glad/gles2.h>
