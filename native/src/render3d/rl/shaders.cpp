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
    "uniform mat4 matModel;\n"
    "uniform mat4 matView;\n"
    "uniform vec4 tintA;\n"
    "uniform vec4 tintB;\n"
    "uniform vec4 tintC;\n"
    "uniform vec4 tintD;\n"
    "uniform vec4 tintSpan;\n"
    "uniform int pieceMode;\n"
    "uniform float lightMix;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragWorld;\n"
    "out vec3 fragNormal;\n"
    "out float fragDistance;\n"
    "void main() {\n"
    "    float tx = clamp((vertexPosition.x - tintSpan.x) * tintSpan.y, 0.0, 1.0);\n"
    "    float tz = clamp((vertexPosition.z - tintSpan.z) * tintSpan.w, 0.0, 1.0);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    vec4 tint = mix(mix(tintA, tintB, tx), mix(tintC, tintD, tx), tz);\n"
    "    vec3 n = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    if (pieceMode >= 2 && lightMix <= 0.0) {\n"
    "        float f = n.y < 0.0 ? 1.0 + 0.5 * n.y : 1.0 + 0.125 * n.y;\n"
    "        if (pieceMode == 3) {\n"
    "            float h = abs(n.x) + abs(n.z);\n"
    "            if (h > 0.001) f *= (abs(n.x) * %s + abs(n.z) * %s) / h;\n"
    "        }\n"
    "        tint.rgb *= f;\n"
    "    }\n"
    "    fragColor = vertexColor * tint;\n"
    "    fragNormal = n;\n"
    "    vec4 world = matModel * vec4(vertexPosition, 1.0);\n"
    "    fragWorld = world.xyz;\n"
    "    fragDistance = length(vec3(matView * world));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

/// THE LIGHT, shared by every lit fragment shader (the scene's, the blend's, the
/// water's): the uniforms the description sets per frame (SceneShading), the
/// point lights the adapter picks per draw, the shadow map, and the one
/// function that turns a world position and a normal into light. It is the GLSL
/// twin of render3d::litSurface (light_model.cpp), and its lamp term is
/// the sim's own curve, peak * (1 - (d/R)^2)^2 (render3d::lampFalloff).
constexpr const char* kLitHeader =
    "uniform float lightMix;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 sunColour;\n"
    "uniform vec3 ambientColour;\n"
    "uniform vec3 ambientGround;\n"
    "uniform float fogDensity;\n"
    "uniform vec3 fogColour;\n"
    "uniform float lightGain;\n"
    "uniform float lightClock;\n"
    "uniform int lightCount;\n"
    "uniform vec4 lightPosR[12];\n"
    "uniform vec4 lightColPeak[12];\n"
    "uniform vec2 lightFlick[12];\n"
    "uniform sampler2D shadowMap;\n"
    "uniform mat4 matLightVP;\n"
    "uniform float shadowStrength;\n"
    "uniform float shadowTexel;\n"
    "uniform float emissive;\n"
    "uniform float unlit;\n"
    "float lampFalloff(float d, float radius, float peak) {\n"
    "    if (radius <= 0.0 || d >= radius) return 0.0;\n"
    "    float t = d / radius;\n"
    "    float k = 1.0 - t * t;\n"
    "    return peak * k * k;\n"
    "}\n"
    "float flameFlicker(int i) {\n"
    "    vec2 f = lightFlick[i];\n"
    "    if (f.y <= 0.0) return 1.0;\n"
    "    float w = 0.55 * sin(lightClock * 8.7 + f.x) + 0.30 * sin(lightClock * 21.3 + f.x * 2.3)\n"
    "            + 0.15 * sin(lightClock * 47.0 + f.x * 5.1);\n"
    "    return 1.0 + f.y * w;\n"
    "}\n"
    "// The shadow map: a 3x3 percentage-closer filter, a normal offset and a\n"
    "// slope bias. Returns the multiplier on the direct light (1 = lit).\n"
    "float shadowTerm(vec3 world, vec3 n, float nd) {\n"
    "    if (shadowStrength <= 0.0) return 1.0;\n"
    "    vec4 ls = matLightVP * vec4(world + n * 0.07, 1.0);\n"
    "    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;\n"
    "    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n"
    "    float bias = 0.0004 + 0.0012 * (1.0 - nd);\n"
    "    float lit = 0.0;\n"
    "    for (int y = -1; y <= 1; ++y) {\n"
    "        for (int x = -1; x <= 1; ++x) {\n"
    "            float depth = texture(shadowMap, p.xy + vec2(float(x), float(y)) * shadowTexel).r;\n"
    "            lit += (p.z - bias > depth) ? 0.0 : 1.0;\n"
    "        }\n"
    "    }\n"
    "    lit /= 9.0;\n"
    "    return mix(1.0 - shadowStrength, 1.0, lit);\n"
    "}\n"
    "// `vis` is how much of the sky reaches the surface: a room is lit by its own lamps.\n"
    "vec3 litColour(vec3 world, vec3 n, float vis) {\n"
    "    vec3 ground = ambientGround.x < 0.0 ? ambientColour : ambientGround;\n"
    "    vec3 total = mix(ground, ambientColour, clamp(n.y * 0.5 + 0.5, 0.0, 1.0)) * mix(0.18, 1.0, vis);\n"
    "    float nd = max(dot(n, sunDirection), 0.0);\n"
    "    if (nd > 0.0) total += sunColour * nd * shadowTerm(world, n, nd);\n"
    "    for (int i = 0; i < lightCount; ++i) {\n"
    "        vec3 d = lightPosR[i].xyz - world;\n"
    "        float dist = length(d);\n"
    "        float fall = lampFalloff(dist, lightPosR[i].w, lightColPeak[i].w);\n"
    "        if (fall > 0.0) {\n"
    "            float facing = max(dot(n, d / max(dist, 0.0001)), 0.0);\n"
    "            total += lightColPeak[i].rgb * (lightGain * fall * flameFlicker(i) * (0.25 + 0.75 * facing));\n"
    "        }\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "vec3 fogged(vec3 rgb, float dist) {\n"
    "    float fog = clamp(1.0 - exp(-fogDensity * dist), 0.0, 1.0);\n"
    "    return mix(rgb, fogColour, fog);\n"
    "}\n";

constexpr const char* kBlendFragmentBody =
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragWorld;\n"
    "in vec3 fragNormal;\n"
    "in float fragDistance;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform int pieceMode;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texelColor = texture(texture0, fragTexCoord);\n"
    "    vec4 base = texelColor * colDiffuse * fragColor;\n"
    "    vec3 rgb = base.rgb;\n"
    "    if (lightMix > 0.0 && emissive < 0.5) {\n"
    "        rgb *= mix(vec3(1.0), litColour(fragWorld, normalize(fragNormal), 1.0), lightMix);\n"
    "    }\n"
    "    if (fogDensity > 0.0) rgb = fogged(rgb, fragDistance);\n"
    "    finalColor = vec4(rgb, base.a);\n"
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
    "in vec2 vertexTexCoord2;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matView;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec2 fragTexCoord2;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "out vec3 fragWorld;\n"
    "out float fragDistance;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragTexCoord2 = vertexTexCoord2;\n"
    "    fragColor = vertexColor;\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    vec4 world = matModel * vec4(vertexPosition, 1.0);\n"
    "    fragWorld = world.xyz;\n"
    "    fragDistance = length(vec3(matView * world));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kSceneFragmentBody =
    "in vec2 fragTexCoord;\n"
    "in vec2 fragTexCoord2;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "in vec3 fragWorld;\n"
    "in float fragDistance;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 atlasCell;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "// A tiling atlas (the world's surfaces): fragTexCoord counts repeats of a\n"
    "// surface, fragTexCoord2 names the atlas cell it lives in. The fract() is\n"
    "// folded here, and the mip level is taken from the derivative of the UNFOLDED\n"
    "// coordinate, so the seam at a repeat never shows as a line. The level is\n"
    "// held to 3: the cell's gutter is eight texels wide, which is as far as a\n"
    "// level-3 footprint reaches, so no neighbouring cell is ever read.\n"
    "vec4 sampleSurface() {\n"
    "    if (atlasCell.x <= 0.0) return texture(texture0, fragTexCoord);\n"
    "    vec2 f = fract(fragTexCoord);\n"
    "    vec2 size = vec2(textureSize(texture0, 0));\n"
    "    vec2 dx = dFdx(fragTexCoord) * atlasCell * size;\n"
    "    vec2 dy = dFdy(fragTexCoord) * atlasCell * size;\n"
    "    float lod = clamp(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0)), 0.0, 3.0);\n"
    "    return textureLod(texture0, fragTexCoord2 + f * atlasCell, lod);\n"
    "}\n"
    "void main() {\n"
    "    // A lit chunk carries what the sky reaches in its vertex alpha (it is not a blend).\n"
    "    bool litAtlas = lightMix > 0.0 && emissive < 0.5 && atlasCell.x > 0.0;\n"
    "    float vis = litAtlas ? fragColor.a : 1.0;\n"
    "    vec4 vc = litAtlas ? vec4(fragColor.rgb, 1.0) : fragColor;\n"
    "    vec4 base = sampleSurface() * colDiffuse * vc;\n"
    "    if (unlit > 0.5) { finalColor = base; return; }\n"
    "    vec3 rgb = base.rgb;\n"
    "    if (lightMix > 0.0 && emissive < 0.5) {\n"
    "        rgb *= mix(vec3(1.0), litColour(fragWorld, normalize(fragNormal), vis), lightMix);\n"
    "    }\n"
    "    finalColor = vec4(fogged(rgb, fragDistance), base.a);\n"
    "}\n";

/// THE SHADOW PASS: depth only, from the sun or the moon.
constexpr const char* kDepthVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "uniform mat4 mvp;\n"
    "void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";
constexpr const char* kDepthFragmentShader =
    "#version 330\n"
    "out vec4 finalColor;\n"
    "void main() { finalColor = vec4(1.0); }\n";

/// THE SKY: the dome, drawn from the description's colours and not from its
/// vertices. A horizon-to-zenith gradient (the horizon is the fog's colour, so
/// the far street and the sky meet seamlessly), the sun and the moon as discs
/// along their own directions with a glow, stars faded in by the dark, and one
/// layer of cloud, scrolled by the clock.
constexpr const char* kSkyVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragDir;\n"
    "void main() {\n"
    "    fragDir = vertexPosition;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kSkyFragmentShader =
    "#version 330\n"
    "in vec3 fragDir;\n"
    "uniform vec3 skyZenith;\n"
    "uniform vec3 skyHorizon;\n"
    "uniform vec3 sunSkyDirection;\n"
    "uniform vec3 sunDisc;\n"
    "uniform vec3 moonDirection;\n"
    "uniform vec3 moonDisc;\n"
    "uniform float stars;\n"
    "uniform float cloudCover;\n"
    "uniform vec3 cloudColour;\n"
    "uniform float cloudClock;\n"
    "out vec4 finalColor;\n"
    "float hash21(vec2 p) {\n"
    "    p = fract(p * vec2(123.34, 456.21));\n"
    "    p += dot(p, p + 45.32);\n"
    "    return fract(p.x * p.y);\n"
    "}\n"
    "float vnoise(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),\n"
    "               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "float fbm(vec2 p) {\n"
    "    float v = 0.0;\n"
    "    float a = 0.5;\n"
    "    for (int i = 0; i < 5; ++i) { v += a * vnoise(p); p = p * 2.03 + 11.7; a *= 0.5; }\n"
    "    return v;\n"
    "}\n"
    "void main() {\n"
    "    vec3 d = normalize(fragDir);\n"
    "    float up = max(d.y, 0.0);\n"
    "    vec3 col = mix(skyHorizon, skyZenith, pow(up, 0.55));\n"
    "    // The glow round the sun and the moon, then their discs.\n"
    "    float sd = dot(d, sunSkyDirection);\n"
    "    col += sunDisc * (0.10 * pow(max(sd, 0.0), 6.0) + 0.28 * pow(max(sd, 0.0), 90.0));\n"
    "    float md = dot(d, moonDirection);\n"
    "    col += moonDisc * (0.06 * pow(max(md, 0.0), 14.0));\n"
    "    // The stars: a hashed grid over the sphere, a few bright, fading at the horizon.\n"
    "    if (stars > 0.0 && d.y > 0.0) {\n"
    "        vec2 sp = vec2(atan(d.z, d.x) * 57.0, asin(clamp(d.y, -1.0, 1.0)) * 114.0);\n"
    "        vec2 cell = floor(sp);\n"
    "        float h = hash21(cell);\n"
    "        vec2 at = fract(sp) - vec2(0.5) + (vec2(hash21(cell + 3.1), hash21(cell + 7.7)) - 0.5) * 0.6;\n"
    "        float s = smoothstep(0.17, 0.0, length(at)) * step(0.965, h);\n"
    "        col += vec3(0.85, 0.9, 1.0) * s * (0.5 + 1.5 * hash21(cell + 1.7)) * stars * smoothstep(0.0, 0.25, d.y);\n"
    "    }\n"
    "    // The cloud: one scrolling layer on a plane over the eye.\n"
    "    float c = 0.0;\n"
    "    if (d.y > 0.0) {\n"
    "        vec2 uv = d.xz / (d.y + 0.12) * 0.55 + vec2(cloudClock * 0.0016, cloudClock * 0.0006);\n"
    "        float n = fbm(uv);\n"
    "        c = smoothstep(1.0 - cloudCover - 0.12, 1.0 - cloudCover + 0.18, n) * smoothstep(0.0, 0.22, d.y);\n"
    "        vec3 under = cloudColour * (0.78 + 0.22 * fbm(uv * 2.3 + 4.0));\n"
    "        // A cloud near the sun is lit from behind.\n"
    "        under += sunDisc * 0.06 * pow(max(sd, 0.0), 4.0);\n"
    "        col = mix(col, under, c * 0.88);\n"
    "        // Stars and discs are behind it.\n"
    "    }\n"
    "    // The discs themselves, hard-edged, hidden by cloud.\n"
    "    float sunEdge = smoothstep(0.99955, 0.99975, sd);\n"
    "    float moonEdge = smoothstep(0.99965, 0.99985, md);\n"
    "    col += sunDisc * sunEdge * 2.2 * (1.0 - c);\n"
    "    col += moonDisc * moonEdge * (1.0 - c);\n"
    "    finalColor = vec4(col, 1.0);\n"
    "}\n";

/// THE HARBOUR: two scrolling procedural normal layers, a Fresnel mix of the
/// sky and a dark depth tint, glints of the sun, the moon and the lamps on the
/// ripples, and foam along the banks (the vertex colour carries the shore
/// distance and the pack's depth tint). No refraction, no planar reflection: a
/// sky-only reflection is the 2006 step.
constexpr const char* kWaterVertexShader =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matView;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragWorld;\n"
    "out vec4 fragColor;\n"
    "out float fragDistance;\n"
    "void main() {\n"
    "    vec4 world = matModel * vec4(vertexPosition, 1.0);\n"
    "    fragWorld = world.xyz;\n"
    "    fragColor = vertexColor;\n"
    "    fragDistance = length(vec3(matView * world));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
constexpr const char* kWaterFragmentBody =
    "in vec3 fragWorld;\n"
    "in vec4 fragColor;\n"
    "in float fragDistance;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 skyZenith;\n"
    "uniform vec3 skyHorizon;\n"
    "uniform vec3 waterShallow;\n"
    "uniform vec3 waterDeep;\n"
    "uniform float waterClock;\n"
    "out vec4 finalColor;\n"
    "float hash21(vec2 p) {\n"
    "    p = fract(p * vec2(123.34, 456.21));\n"
    "    p += dot(p, p + 45.32);\n"
    "    return fract(p.x * p.y);\n"
    "}\n"
    "float vnoise(vec2 p) {\n"
    "    vec2 i = floor(p);\n"
    "    vec2 f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),\n"
    "               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "// The gradient of one layer of swell: three crossed sines.\n"
    "vec2 swell(vec2 p, float t, float scale, vec2 drift) {\n"
    "    p = p * scale + drift * t;\n"
    "    vec2 g = vec2(0.0);\n"
    "    g += 0.50 * cos(dot(p, vec2(1.3, 0.7)) * 1.1 + t * 1.2) * vec2(1.3, 0.7) * 1.1;\n"
    "    g += 0.35 * cos(dot(p, vec2(-0.6, 1.5)) * 1.9 + t * 1.7) * vec2(-0.6, 1.5) * 1.9;\n"
    "    g += 0.20 * cos(dot(p, vec2(2.1, 1.1)) * 3.1 + t * 2.3) * vec2(2.1, 1.1) * 3.1;\n"
    "    return g * scale;\n"
    "}\n"
    "void main() {\n"
    "    float shore = fragColor.r * 3.0;\n"
    "    float t = waterClock;\n"
    "    vec2 g = swell(fragWorld.xz, t, 0.9, vec2(0.05, 0.02)) * 0.030\n"
    "           + swell(fragWorld.xz + 17.0, t, 2.3, vec2(-0.03, 0.06)) * 0.018;\n"
    "    vec3 n = normalize(vec3(-g.x, 1.0, -g.y));\n"
    "    vec3 v = normalize(viewPos - fragWorld);\n"
    "    float cosv = clamp(dot(v, n), 0.0, 1.0);\n"
    "    float fresnel = 0.04 + 0.96 * pow(1.0 - cosv, 5.0);\n"
    "    vec3 r = reflect(-v, n);\n"
    "    vec3 skyRefl = mix(skyHorizon, skyZenith, pow(max(r.y, 0.0), 0.55));\n"
    "    // The body: a dark tint, a little lighter over the shallows, lit by the\n"
    "    // hemisphere and the light that casts.\n"
    "    float shallow = smoothstep(2.2, 0.0, shore);\n"
    "    vec3 body = mix(waterDeep, waterShallow, shallow * 0.8);\n"
    "    vec3 sky = mix(ambientGround.x < 0.0 ? ambientColour : ambientGround, ambientColour, 0.8) + sunColour * max(sunDirection.y, 0.0) * 0.5;\n"
    "    body *= sky * 1.6;\n"
    "    vec3 rgb = mix(body, skyRefl, clamp(fresnel * 0.9 + 0.12, 0.0, 1.0));\n"
    "    // Glints: the light that casts and every lamp near, on the ripples.\n"
    "    vec3 h = normalize(sunDirection + v);\n"
    "    rgb += sunColour * pow(max(dot(n, h), 0.0), 220.0) * 3.5 * step(0.0, sunDirection.y);\n"
    "    for (int i = 0; i < lightCount; ++i) {\n"
    "        vec3 d = lightPosR[i].xyz - fragWorld;\n"
    "        float dist = length(d);\n"
    "        float fall = lampFalloff(dist, lightPosR[i].w * 1.6, lightColPeak[i].w);\n"
    "        if (fall > 0.0) {\n"
    "            vec3 l = d / max(dist, 0.0001);\n"
    "            vec3 hl = normalize(l + v);\n"
    "            float spec = pow(max(dot(n, hl), 0.0), 70.0);\n"
    "            rgb += lightColPeak[i].rgb * (lightGain * fall * flameFlicker(i) * (spec * 1.6 + 0.10 * max(dot(n, l), 0.0)));\n"
    "        }\n"
    "    }\n"
    "    // Foam: a moving line along the banks.\n"
    "    float fn = vnoise(fragWorld.xz * 3.1 + vec2(t * 0.35, -t * 0.2)) * 0.6 + vnoise(fragWorld.xz * 7.3 - vec2(t * 0.2, t * 0.3)) * 0.4;\n"
    "    float foam = smoothstep(0.62, 0.0, shore + fn * 0.55 - 0.25) * (0.55 + 0.45 * sin(t * 1.4 + shore * 5.0));\n"
    "    vec3 foamCol = (ambientColour * 0.9 + sunColour * 0.4 + vec3(0.04)) * vec3(0.95, 0.97, 0.98);\n"
    "    rgb = mix(rgb, foamCol, clamp(foam, 0.0, 1.0) * 0.75);\n"
    "    finalColor = vec4(fogged(rgb, fragDistance), 1.0);\n"
    "}\n";

/// THE POST PASS: the offscreen scene (half-float, so a lamp's core is over one
/// and the bloom has something to take) drawn over the window by one full-screen
/// quad. Exposure, a filmic shoulder, the bloom, the grade and a vignette; the
/// defaults leave the picture alone, and the alpha is forced to 1 because the
/// blended alpha of the scene pass is not a picture.
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
    "uniform sampler2D bloomTexture;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float exposure;\n"
    "uniform float vignette;\n"
    "uniform float bloom;\n"
    "uniform float toneCurve;\n"
    "uniform float saturation;\n"
    "uniform float contrast;\n"
    "uniform vec3 gradeTint;\n"
    "out vec4 finalColor;\n"
    "vec3 filmic(vec3 x) {\n"
    "    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    vec3 rgb = texture(texture0, fragTexCoord).rgb * colDiffuse.rgb * fragColor.rgb * exposure;\n"
    "    if (bloom > 0.0) rgb += texture(bloomTexture, fragTexCoord).rgb * bloom;\n"
    "    if (toneCurve > 0.0) {\n"
    "        rgb = mix(clamp(rgb, 0.0, 1.0), filmic(rgb), toneCurve);\n"
    "        float luma = dot(rgb, vec3(0.30, 0.59, 0.11));\n"
    "        rgb = mix(vec3(luma), rgb, saturation);\n"
    "        rgb = (rgb - 0.5) * contrast + 0.5;\n"
    "        rgb *= gradeTint;\n"
    "    }\n"
    "    vec2 d = fragTexCoord - vec2(0.5);\n"
    "    rgb *= 1.0 - vignette * clamp(4.0 * dot(d, d), 0.0, 1.0);\n"
    "    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);\n"
    "}\n";

/// THE BLOOM'S TWO SMALL PASSES, at quarter resolution: what is over the
/// threshold (a soft knee), taken from four taps of the scene; then one
/// separable blur, a nine-tap gaussian each way.
constexpr const char* kBrightFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 texel;\n"
    "uniform float threshold;\n"
    "uniform float exposure;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 c = texture(texture0, fragTexCoord + texel * vec2(-1.5, -1.5)).rgb\n"
    "           + texture(texture0, fragTexCoord + texel * vec2(1.5, -1.5)).rgb\n"
    "           + texture(texture0, fragTexCoord + texel * vec2(-1.5, 1.5)).rgb\n"
    "           + texture(texture0, fragTexCoord + texel * vec2(1.5, 1.5)).rgb;\n"
    "    c *= 0.25 * exposure;\n"
    "    float l = max(c.r, max(c.g, c.b));\n"
    "    float knee = 0.35;\n"
    "    float soft = clamp((l - threshold + knee) / (2.0 * knee), 0.0, 1.0);\n"
    "    float w = max(soft * soft * knee * 2.0, l - threshold) / max(l, 0.0001);\n"
    "    finalColor = vec4(c * w, 1.0);\n"
    "}\n";
constexpr const char* kBlurFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 direction;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float w[5] = float[5](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);\n"
    "    vec3 sum = texture(texture0, fragTexCoord).rgb * w[0];\n"
    "    for (int i = 1; i < 5; ++i) {\n"
    "        sum += texture(texture0, fragTexCoord + direction * float(i)).rgb * w[i];\n"
    "        sum += texture(texture0, fragTexCoord - direction * float(i)).rgb * w[i];\n"
    "    }\n"
    "    finalColor = vec4(sum, 1.0);\n"
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

namespace {

[[nodiscard]] std::string withLight(const char* body) {
    return std::string("#version 330\n") + kLitHeader + body;
}

void setInt(const Shader& shader, int loc, int value) {
    if (loc >= 0) {
        SetShaderValue(shader, loc, &value, SHADER_UNIFORM_INT);
    }
}

}  // namespace

rl::LitLocs rl::fetchLitLocs(const Shader& shader) {
    LitLocs l;
    l.lightMix = GetShaderLocation(shader, "lightMix");
    l.sunDirection = GetShaderLocation(shader, "sunDirection");
    l.sunColour = GetShaderLocation(shader, "sunColour");
    l.ambientColour = GetShaderLocation(shader, "ambientColour");
    l.ambientGround = GetShaderLocation(shader, "ambientGround");
    l.fogDensity = GetShaderLocation(shader, "fogDensity");
    l.fogColour = GetShaderLocation(shader, "fogColour");
    l.lightGain = GetShaderLocation(shader, "lightGain");
    l.lightClock = GetShaderLocation(shader, "lightClock");
    l.lightCount = GetShaderLocation(shader, "lightCount");
    l.lightPosR = GetShaderLocation(shader, "lightPosR[0]");
    l.lightColPeak = GetShaderLocation(shader, "lightColPeak[0]");
    l.lightFlick = GetShaderLocation(shader, "lightFlick[0]");
    l.shadowMap = GetShaderLocation(shader, "shadowMap");
    l.matLightVP = GetShaderLocation(shader, "matLightVP");
    l.shadowStrength = GetShaderLocation(shader, "shadowStrength");
    l.shadowTexel = GetShaderLocation(shader, "shadowTexel");
    l.emissive = GetShaderLocation(shader, "emissive");
    l.unlit = GetShaderLocation(shader, "unlit");
    return l;
}

void Backend::Impl::ensureSceneShader() {
    if (sceneTried) {
        return;
    }
    sceneTried = true;
    const std::string sceneFragment = withLight(kSceneFragmentBody);
    scene = loadChecked(kSceneVertexShader, sceneFragment.c_str(), "scene");
    if (scene.id != 0) {
        sceneLit = fetchLitLocs(scene);
        sceneAtlasCell = GetShaderLocation(scene, "atlasCell");
    }
    post = loadChecked(kPostVertexShader, kPostFragmentShader, "post");
    if (post.id != 0) {
        postExposure = GetShaderLocation(post, "exposure");
        postVignette = GetShaderLocation(post, "vignette");
        postBloom = GetShaderLocation(post, "bloom");
        postBloomTexture = GetShaderLocation(post, "bloomTexture");
        postTone = GetShaderLocation(post, "toneCurve");
        postSaturation = GetShaderLocation(post, "saturation");
        postContrast = GetShaderLocation(post, "contrast");
        postTint = GetShaderLocation(post, "gradeTint");
    }
    depth = loadChecked(kDepthVertexShader, kDepthFragmentShader, "shadow");
    sky = loadChecked(kSkyVertexShader, kSkyFragmentShader, "sky");
    if (sky.id != 0) {
        skyLoc.zenith = GetShaderLocation(sky, "skyZenith");
        skyLoc.horizon = GetShaderLocation(sky, "skyHorizon");
        skyLoc.sunDirection = GetShaderLocation(sky, "sunSkyDirection");
        skyLoc.sunDisc = GetShaderLocation(sky, "sunDisc");
        skyLoc.moonDirection = GetShaderLocation(sky, "moonDirection");
        skyLoc.moonDisc = GetShaderLocation(sky, "moonDisc");
        skyLoc.stars = GetShaderLocation(sky, "stars");
        skyLoc.cloudCover = GetShaderLocation(sky, "cloudCover");
        skyLoc.cloudColour = GetShaderLocation(sky, "cloudColour");
        skyLoc.cloudClock = GetShaderLocation(sky, "cloudClock");
    }
    const std::string waterFragment = withLight(kWaterFragmentBody);
    water = loadChecked(kWaterVertexShader, waterFragment.c_str(), "water");
    if (water.id != 0) {
        waterLit = fetchLitLocs(water);
        waterLoc.viewPos = GetShaderLocation(water, "viewPos");
        waterLoc.zenith = GetShaderLocation(water, "skyZenith");
        waterLoc.horizon = GetShaderLocation(water, "skyHorizon");
        waterLoc.shallow = GetShaderLocation(water, "waterShallow");
        waterLoc.deep = GetShaderLocation(water, "waterDeep");
        waterLoc.clock = GetShaderLocation(water, "waterClock");
    }
    bright = loadChecked(kPostVertexShader, kBrightFragmentShader, "bloom bright-pass");
    if (bright.id != 0) {
        brightTexel = GetShaderLocation(bright, "texel");
        brightThreshold = GetShaderLocation(bright, "threshold");
        brightExposure = GetShaderLocation(bright, "exposure");
    }
    blur = loadChecked(kPostVertexShader, kBlurFragmentShader, "bloom blur");
    if (blur.id != 0) {
        blurDirection = GetShaderLocation(blur, "direction");
    }
}

namespace {

void uploadGlobals(const Shader& shader, const LitLocs& l, const SceneShading& s, bool shadows,
                   const Matrix& lightVP, int shadowSize) {
    setFloat(shader, l.lightMix, s.lightMix);
    setVec3(shader, l.sunDirection, s.sunDirection);
    setVec3(shader, l.sunColour, s.sunColour);
    setVec3(shader, l.ambientColour, s.ambientColour);
    setVec3(shader, l.ambientGround, s.ambientGround);
    setFloat(shader, l.fogDensity, s.fogDensity);
    setVec3(shader, l.fogColour, s.fogColour);
    setFloat(shader, l.lightGain, s.lightGain);
    setFloat(shader, l.lightClock, s.lightClock);
    setFloat(shader, l.shadowStrength, shadows ? s.shadowStrength : 0.0F);
    if (shadows) {
        setInt(shader, l.shadowMap, kShadowSlot);
        setFloat(shader, l.shadowTexel, 1.0F / static_cast<float>(std::max(1, shadowSize)));
        if (l.matLightVP >= 0) {
            SetShaderValueMatrix(shader, l.matLightVP, lightVP);
        }
    }
    setFloat(shader, l.emissive, 0.0F);
    setFloat(shader, l.unlit, 0.0F);
    setInt(shader, l.lightCount, 0);
}

}  // namespace

void Backend::Impl::applyShading(const SceneShading& shading) {
    ensureSceneShader();
    shadingNow = shading;
    shadowsOn = false;
    if (scene.id != 0) {
        uploadGlobals(scene, sceneLit, shading, false, Matrix{}, shadowSize);
    }
}

/// Everything the lit shaders share, again, once the shadow map is known (or
/// known to be off): the scene's, the blend's and the water's.
void Backend::Impl::applyShadows() {
    if (scene.id != 0) {
        uploadGlobals(scene, sceneLit, shadingNow, shadowsOn, lightViewProj, shadowSize);
    }
    if (blend.id != 0) {
        uploadGlobals(blend, blendLit, shadingNow, shadowsOn, lightViewProj, shadowSize);
    }
    if (water.id != 0) {
        uploadGlobals(water, waterLit, shadingNow, shadowsOn, lightViewProj, shadowSize);
        setVec3(water, waterLoc.zenith, shadingNow.skyZenith);
        setVec3(water, waterLoc.horizon, shadingNow.skyHorizon);
        setVec3(water, waterLoc.shallow, shadingNow.waterShallow);
        setVec3(water, waterLoc.deep, shadingNow.waterDeep);
        setFloat(water, waterLoc.clock, shadingNow.waterClock);
        setVec3(water, waterLoc.viewPos, eyeNow);
    }
    if (sky.id != 0) {
        setVec3(sky, skyLoc.zenith, shadingNow.skyZenith);
        setVec3(sky, skyLoc.horizon, shadingNow.skyHorizon);
        setVec3(sky, skyLoc.sunDirection, shadingNow.sunSkyDirection);
        setVec3(sky, skyLoc.sunDisc, shadingNow.sunDisc);
        setVec3(sky, skyLoc.moonDirection, shadingNow.moonDirection);
        setVec3(sky, skyLoc.moonDisc, shadingNow.moonDisc);
        setFloat(sky, skyLoc.stars, shadingNow.stars);
        setFloat(sky, skyLoc.cloudCover, shadingNow.cloudCover);
        setVec3(sky, skyLoc.cloudColour, shadingNow.cloudColour);
        setFloat(sky, skyLoc.cloudClock, shadingNow.cloudClock);
    }
}

/// THE LIGHTS OF ONE DRAW: the nearest of the description's point lights that
/// reach the box (`lo`..`hi`, scene space), at most kMaxDrawLights, nearest first
/// (OBLIVION-PLAN 6.6), written to the shader's arrays. Nothing is read back; the
/// choice is a pure function of the description and the draw's own bounds.
void Backend::Impl::setDrawLights(const Shader& shader, const LitLocs& l, const Vec3& lo,
                                  const Vec3& hi) {
    if (l.lightCount < 0) {
        return;
    }
    int count = 0;
    if (shadingNow.lightMix > 0.0F && sceneNow != nullptr) {
        struct Pick {
            float distanceSq;
            const ScenePointLight* light;
        };
        Pick picks[kMaxDrawLights];
        for (const ScenePointLight& light : sceneNow->lights) {
            const float dx = std::max({lo.x - light.position.x, light.position.x - hi.x, 0.0F});
            const float dy = std::max({lo.y - light.position.y, light.position.y - hi.y, 0.0F});
            const float dz = std::max({lo.z - light.position.z, light.position.z - hi.z, 0.0F});
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (!(light.radius > 0.0F) || d2 >= light.radius * light.radius) {
                continue;
            }
            // Insert, keeping the nearest kMaxDrawLights in order.
            int at = count;
            while (at > 0 && picks[at - 1].distanceSq > d2) {
                --at;
            }
            if (at >= static_cast<int>(kMaxDrawLights)) {
                continue;
            }
            const int last = std::min(count, static_cast<int>(kMaxDrawLights) - 1);
            for (int i = last; i > at; --i) {
                picks[i] = picks[i - 1];
            }
            picks[at] = Pick{d2, &light};
            count = std::min(count + 1, static_cast<int>(kMaxDrawLights));
        }
        float posR[kMaxDrawLights * 4];
        float colPeak[kMaxDrawLights * 4];
        float flick[kMaxDrawLights * 2];
        for (int i = 0; i < count; ++i) {
            const ScenePointLight& light = *picks[i].light;
            posR[i * 4] = light.position.x;
            posR[i * 4 + 1] = light.position.y;
            posR[i * 4 + 2] = light.position.z;
            posR[i * 4 + 3] = light.radius;
            colPeak[i * 4] = light.colour.x;
            colPeak[i * 4 + 1] = light.colour.y;
            colPeak[i * 4 + 2] = light.colour.z;
            colPeak[i * 4 + 3] = light.peak;
            flick[i * 2] = static_cast<float>(light.flicker % 6283U) * 0.001F;
            flick[i * 2 + 1] = light.flicker != 0U ? light.flickerDepth : 0.0F;
        }
        if (count > 0) {
            SetShaderValueV(shader, l.lightPosR, posR, SHADER_UNIFORM_VEC4, count);
            SetShaderValueV(shader, l.lightColPeak, colPeak, SHADER_UNIFORM_VEC4, count);
            SetShaderValueV(shader, l.lightFlick, flick, SHADER_UNIFORM_VEC2, count);
        }
    }
    setInt(shader, l.lightCount, count);
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
    const std::string blendFragment = withLight(kBlendFragmentBody);
    blend = LoadShaderFromMemory(vertexSource, blendFragment.c_str());
    if (blend.id != 0 && blend.id != rlGetShaderIdDefault()) {
        blendTintA = GetShaderLocation(blend, "tintA");
        blendTintB = GetShaderLocation(blend, "tintB");
        blendTintC = GetShaderLocation(blend, "tintC");
        blendTintD = GetShaderLocation(blend, "tintD");
        blendSpan = GetShaderLocation(blend, "tintSpan");
        blendMode = GetShaderLocation(blend, "pieceMode");
        blendLit = fetchLitLocs(blend);
    } else {
        blend = Shader{};
    }
}

}  // namespace granadad::render3d
