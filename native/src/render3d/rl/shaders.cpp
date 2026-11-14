#include "internal.hpp"

// THE GLSL SOURCES AND THEIR LOADERS.

namespace granadad::render3d {

using namespace rl;

namespace {

#if !defined(GRAPHICS_API_OPENGL_11)
/// S LANE. The static pieces' own vertex shader: raylib's default with the
/// tint blended BILINEARLY over the piece's local X and Z between four
/// colours, so a wall segment reads the light of the cells it spans
/// instead of one flat step and a floor block reads its four corners
/// instead of one patch. A run piece passes an empty Z span (its factor
/// 0), a point piece both. GL 3.3 only; the software rasterizer has no
/// shaders and never has the licensed files to draw with it anyway.
/// One more thing the same shader does, by `pieceMode` (scene.hpp): a
/// SHADED piece (2) darkens faces that look down (to half) and lifts faces
/// that look up (by an eighth) from the mesh's own normals, the vertical
/// faces untouched: the volume a prop needs when nothing lights it, and the
/// difference between a rowboat and a dome. A FACED piece (3) does that
/// and gives its upright faces the chunk's own facing factors by their
/// normal (render::kFacingX east and west, kFacingY north and south, the
/// two numbers printed into the source below at load), so a door post's
/// front and its inner face split the way the wall's own faces do. The
/// source is a format string for that reason, and the two %s are the only
/// ones in it.
/// A HALO (1) is NOT here any more, and that is the point of it: its
/// falloff used to be a radial alpha in this fragment shader, which rlsw
/// has not got, so the headless twin drew a flat pale rectangle where the
/// exe drew a disc. It is vertex-coloured fan geometry now (haloFanMesh,
/// scene.hpp) and both rasterizers draw the same glow.
constexpr const char* kBlendVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matNormal;\n"
    "uniform vec4 tintA;\n"
    "uniform vec4 tintB;\n"
    "uniform vec4 tintC;\n"
    "uniform vec4 tintD;\n"
    "uniform vec4 tintSpan;\n"
    "uniform int pieceMode;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float tx = clamp((vertexPosition.x - tintSpan.x) * tintSpan.y, 0.0, 1.0);\n"
    "    float tz = clamp((vertexPosition.z - tintSpan.z) * tintSpan.w, 0.0, 1.0);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    vec4 tint = mix(mix(tintA, tintB, tx), mix(tintC, tintD, tx), tz);\n"
    "    if (pieceMode >= 2) {\n"
    "        vec3 n = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "        float f = n.y < 0.0 ? 1.0 + 0.5 * n.y : 1.0 + 0.125 * n.y;\n"
    "        if (pieceMode == 3) {\n"
    "            float h = abs(n.x) + abs(n.z);\n"
    "            if (h > 0.001) f *= (abs(n.x) * %s + abs(n.z) * %s) / h;\n"
    "        }\n"
    "        tint.rgb *= f;\n"
    "    }\n"
    "    fragColor = vertexColor * tint;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kBlendFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform int pieceMode;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texelColor = texture(texture0, fragTexCoord);\n"
    "    finalColor = texelColor * colDiffuse * fragColor;\n"
    "}\n";
#endif

}  // namespace

void Backend::Impl::ensureBlendShader() {
    if (blendTried) {
        return;
    }
    blendTried = true;
#if !defined(GRAPHICS_API_OPENGL_11)
    // The facing factors are the chunk's (voxel_classify.hpp), printed
    // into the source so a faced piece and the wall it stands in read
    // one rule.
    char facingX[16];
    char facingY[16];
    std::snprintf(facingX, sizeof facingX, "%.4f", static_cast<double>(render::kFacingX));
    std::snprintf(facingY, sizeof facingY, "%.4f", static_cast<double>(render::kFacingY));
    char vertexSource[4096];
    std::snprintf(vertexSource, sizeof vertexSource, kBlendVertexShader, facingX, facingY);
    blend = LoadShaderFromMemory(vertexSource, kBlendFragmentShader);
    if (blend.id != 0 && blend.id != rlGetShaderIdDefault()) {
        blendTintA = GetShaderLocation(blend, "tintA");
        blendTintB = GetShaderLocation(blend, "tintB");
        blendTintC = GetShaderLocation(blend, "tintC");
        blendTintD = GetShaderLocation(blend, "tintD");
        blendSpan = GetShaderLocation(blend, "tintSpan");
        blendMode = GetShaderLocation(blend, "pieceMode");
    } else {
        blend = Shader{};
    }
#endif
}

}  // namespace granadad::render3d
