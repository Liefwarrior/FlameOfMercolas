// The single translation unit that instantiates stb_image. Everywhere else
// includes stb_image.h without the implementation macro.
//
// stb is third-party and warns heavily under -Wconversion; silence it here
// rather than weakening the project's own warning level.

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          // the project's art pipeline is PNG only
#define STBI_NO_STDIO          // we hand stb bytes we already read ourselves
#include <stb_image.h>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
