// The single translation unit that instantiates stb_image and stb_image_write.
// Everywhere else includes the headers without the implementation macros.
//
// It lives in granadad-render rather than in the client because the RENDERER is
// what decodes the tile sheet and encodes a captured frame, and the renderer
// has to build and run with no SDL and no window — that is what makes
// `--screenshot` work inside the docker gate.
//
// stb is third-party and warns heavily under -Wconversion; silence it here
// rather than weakening the project's own warning level.

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG          // the project's art pipeline is PNG only
#define STBI_NO_STDIO          // we hand stb bytes we already read ourselves
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
