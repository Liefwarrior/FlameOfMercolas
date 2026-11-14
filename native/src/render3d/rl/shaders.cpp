#include "internal.hpp"

// THE GLSL SOURCES AND THEIR LOADERS.

namespace granadad::render3d {

using namespace rl;

namespace {

/// S LANE. The static pieces' own vertex shader: raylib's default with the
/// tint blended BILINEARLY over the piece's local X and Z between four
/// colours, so a wall segment reads the light of the cells it spans
/// instead of one flat step and a floor block reads its four corners
/// instead of one patch. A run piece passes an empty Z span (its factor
/// 0), a point piece both. 
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
/// falloff used to be a radial alpha in this fragment shader, which the old rlsw rasterizer
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

/// THE SCENE SHADER: raylib's default with the vertex NORMAL carried through
/// and four uniform groups the description sets per frame (SceneShading,
/// scene.hpp). Every default is a pass-through -- lightMix 0, fogDensity 0 --
/// so the output is texture x colDiffuse x vertex colour, byte for byte what
/// raylib's default shader returns, and phase 3's frame is the old frame drawn
/// by a shader we own. Per-pixel light and fog are switched on by the
/// description in the lighting phase, not here.
constexpr const char* kSceneVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matView;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "out float fragDistance;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    fragDistance = length(vec3(matView * matModel * vec4(vertexPosition, 1.0)));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kSceneFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "in float fragDistance;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float lightMix;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 sunColour;\n"
    "uniform vec3 ambientColour;\n"
    "uniform float fogDensity;\n"
    "uniform vec3 fogColour;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    float lambert = max(dot(normalize(fragNormal), sunDirection), 0.0);\n"
    "    vec3 lit = mix(vec3(1.0), ambientColour + sunColour * lambert, lightMix);\n"
    "    vec3 rgb = base.rgb * lit;\n"
    "    float fog = clamp(1.0 - exp(-fogDensity * fragDistance), 0.0, 1.0);\n"
    "    finalColor = vec4(mix(rgb, fogColour, fog), base.a);\n"
    "}\n";

/// THE POST PASS: the offscreen scene texture drawn over the window by one
/// full-screen quad. Exposure and vignette default to nothing; the alpha is
/// forced to 1 because the blended alpha of the scene pass is not a picture.
constexpr const char* kPostVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kPostFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float exposure;\n"
    "uniform float vignette;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 rgb = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb * fragColor.rgb * exposure;\n"
    "    vec2 d = fragTexCoord - vec2(0.5);\n"
    "    rgb *= 1.0 - vignette * clamp(4.0 * dot(d, d), 0.0, 1.0);\n"
    "    finalColor = vec4(rgb, 1.0);\n"
    "}\n";

[[nodiscard]] Shader loadChecked(const char* vertex, const char* fragment, const char* name) {
    Shader shader = LoadShaderFromMemory(vertex, fragment);
    if (shader.id == 0 || shader.id == rlGetShaderIdDefault()) {
        // A shader that does not compile must be loud: the frame would be
        // drawn by raylib's fallback and look right while proving nothing.
        std::printf("granadad: render3d: the %s shader did not compile; drawing with raylib's "
                    "default\n", name);
        return Shader{};
    }
    return shader;
}

void setFloat(const Shader& shader, int loc, float value) {
    if (loc >= 0) {
        SetShaderValue(shader, loc, &value, SHADER_UNIFORM_FLOAT);
    }
}

void setVec3(const Shader& shader, int loc, const Vec3& value) {
    if (loc >= 0) {
        const float v[3] = {value.x, value.y, value.z};
        SetShaderValue(shader, loc, v, SHADER_UNIFORM_VEC3);
    }
}

}  // namespace

void Backend::Impl::ensureSceneShader() {
    if (sceneTried) {
        return;
    }
    sceneTried = true;
    scene = loadChecked(kSceneVertexShader, kSceneFragmentShader, "scene");
    if (scene.id != 0) {
        sceneLightMix = GetShaderLocation(scene, "lightMix");
        sceneSun = GetShaderLocation(scene, "sunDirection");
        sceneSunColour = GetShaderLocation(scene, "sunColour");
        sceneAmbient = GetShaderLocation(scene, "ambientColour");
        sceneFogDensity = GetShaderLocation(scene, "fogDensity");
        sceneFogColour = GetShaderLocation(scene, "fogColour");
    }
    post = loadChecked(kPostVertexShader, kPostFragmentShader, "post");
    if (post.id != 0) {
        postExposure = GetShaderLocation(post, "exposure");
        postVignette = GetShaderLocation(post, "vignette");
    }
}

void Backend::Impl::applyShading(const SceneShading& shading) {
    ensureSceneShader();
    shadingNow = shading;
    if (scene.id != 0) {
        setFloat(scene, sceneLightMix, shading.lightMix);
        setVec3(scene, sceneSun, shading.sunDirection);
        setVec3(scene, sceneSunColour, shading.sunColour);
        setVec3(scene, sceneAmbient, shading.ambientColour);
        setFloat(scene, sceneFogDensity, shading.fogDensity);
        setVec3(scene, sceneFogColour, shading.fogColour);
    }
}

void Backend::Impl::ensureBlendShader() {
    if (blendTried) {
        return;
    }
    blendTried = true;
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
}

}  // namespace granadad::render3d
