#include "granadad/render/lamps.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace granadad::render {

namespace {

/// Authored tile size in pixels (content/maps/README.md map-level rule).
constexpr std::int32_t kTilePx = 16;
/// The one-chunk VOID border every baked world carries.
constexpr std::int32_t kBorderX = 32;
constexpr std::int32_t kBorderY = 32;
constexpr std::int32_t kBorderZ = 8;

constexpr std::string_view kFireTokens[] = {"brazier", "cauldron", "oven",  "torch",
                                            "candle",  "hearth",   "fire"};

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// the tag scanner
// ---------------------------------------------------------------------------

struct Tag {
    std::string_view name;
    /// The raw attribute text between the name and the closing bracket.
    std::string_view attributes;
    bool closing = false;
    bool selfClosing = false;
};

/// Reads the attribute named `key` out of a tag's attribute text. Values are
/// double-quoted in every .tmx Tiled writes. Returns `fallback` when absent.
[[nodiscard]] std::string_view attribute(std::string_view attributes, std::string_view key,
                                         std::string_view fallback) {
    std::size_t at = 0;
    while (true) {
        at = attributes.find(key, at);
        if (at == std::string_view::npos) {
            return fallback;
        }
        // Must be a whole attribute name: preceded by whitespace or the start,
        // followed by '=' possibly after spaces. Without this, looking for "x"
        // would match the "x" inside "maxwidth".
        const bool leftOk = at == 0 || std::isspace(static_cast<unsigned char>(attributes[at - 1]));
        std::size_t after = at + key.size();
        while (after < attributes.size() &&
               std::isspace(static_cast<unsigned char>(attributes[after]))) {
            ++after;
        }
        if (leftOk && after < attributes.size() && attributes[after] == '=') {
            ++after;
            while (after < attributes.size() &&
                   std::isspace(static_cast<unsigned char>(attributes[after]))) {
                ++after;
            }
            if (after < attributes.size() && attributes[after] == '"') {
                const std::size_t end = attributes.find('"', after + 1);
                if (end == std::string_view::npos) {
                    return fallback;
                }
                return attributes.substr(after + 1, end - after - 1);
            }
        }
        at += key.size();
    }
}

/// Parses `z:+11` / `z:-3` group names. Returns false for any other name.
[[nodiscard]] bool parseZGroupName(std::string_view name, std::int32_t& out) {
    if (name.size() < 4 || name.substr(0, 2) != "z:") {
        return false;
    }
    const char sign = name[2];
    if (sign != '+' && sign != '-') {
        return false;
    }
    std::int32_t magnitude = 0;
    for (std::size_t i = 3; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
        magnitude = magnitude * 10 + (name[i] - '0');
    }
    out = sign == '-' ? -magnitude : magnitude;
    return true;
}

/// Tiled writes coordinates as decimals. Only the integer part matters here
/// because the marker is floored to a tile, so this parses a leading signed
/// integer and stops at the point.
[[nodiscard]] std::int32_t parseLeadingInt(std::string_view text, std::int32_t fallback) {
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
        return fallback;
    }
    std::int64_t value = 0;
    for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
        value = value * 10 + (text[i] - '0');
        if (value > 1'000'000'000) {
            break;
        }
    }
    return static_cast<std::int32_t>(negative ? -value : value);
}

/// A lamp still in authored coordinates, before minZ is known.
struct AuthoredLamp {
    std::string name;
    std::int32_t tileX = 0;
    std::int32_t tileY = 0;
    std::int32_t authoredZ = 0;
    std::int32_t luminance = 0;
    LampWarmth warmth = LampWarmth::Lantern;
};

}  // namespace

std::string_view lampWarmthName(LampWarmth warmth) noexcept {
    return warmth == LampWarmth::Fire ? "fire" : "lantern";
}

bool isFireLampName(std::string_view markerName) noexcept {
    const std::string lower = lowered(markerName);
    for (const std::string_view token : kFireTokens) {
        if (lower.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<Lamp> scanTmxLightSources(std::string_view tmx) {
    std::vector<AuthoredLamp> authored;
    std::int32_t minZ = 0;
    bool sawZ = false;

    // Group nesting: each <group> pushes a level carrying its own z or the
    // enclosing one, each </group> pops. Tiled nests the z-groups inside an
    // outer container group in some fixtures, so a stack is not optional — and
    // the level has to record whether it HAS a z, or popping a z-group back to
    // an unnumbered wrapper would silently place the next marker at z 0.
    struct GroupLevel {
        std::int32_t z = 0;
        bool has = false;
    };
    std::vector<GroupLevel> groups;
    bool haveZ = false;
    std::int32_t currentZ = 0;
    const auto refreshFromStack = [&groups, &haveZ, &currentZ]() {
        if (groups.empty()) {
            haveZ = false;
            currentZ = 0;
            return;
        }
        haveZ = groups.back().has;
        currentZ = groups.back().z;
    };

    bool inMarkers = false;
    int objectGroupDepth = 0;

    bool inLight = false;
    AuthoredLamp pending;
    bool pendingHasLuminance = false;
    int objectDepth = 0;

    std::size_t at = 0;
    while (true) {
        const std::size_t open = tmx.find('<', at);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = tmx.find('>', open + 1);
        if (close == std::string_view::npos) {
            break;
        }
        std::string_view body = tmx.substr(open + 1, close - open - 1);
        at = close + 1;
        if (body.empty() || body.front() == '?' || body.front() == '!') {
            continue;  // declaration, comment or doctype
        }

        Tag tag;
        if (body.front() == '/') {
            tag.closing = true;
            body.remove_prefix(1);
        } else if (body.back() == '/') {
            tag.selfClosing = true;
            body.remove_suffix(1);
        }
        std::size_t nameEnd = 0;
        while (nameEnd < body.size() && !std::isspace(static_cast<unsigned char>(body[nameEnd]))) {
            ++nameEnd;
        }
        tag.name = body.substr(0, nameEnd);
        tag.attributes = body.substr(nameEnd);

        if (tag.name == "group") {
            if (tag.closing) {
                if (!groups.empty()) {
                    groups.pop_back();
                }
                refreshFromStack();
            } else {
                std::int32_t z = currentZ;
                const bool named = parseZGroupName(attribute(tag.attributes, "name", ""), z);
                if (named && (!sawZ || z < minZ)) {
                    minZ = z;
                    sawZ = true;
                }
                groups.push_back(GroupLevel{z, named || haveZ});
                refreshFromStack();
                if (tag.selfClosing) {
                    groups.pop_back();
                    refreshFromStack();
                }
            }
            continue;
        }

        if (tag.name == "objectgroup") {
            if (tag.closing) {
                if (objectGroupDepth > 0) {
                    --objectGroupDepth;
                }
                if (objectGroupDepth == 0) {
                    inMarkers = false;
                }
            } else if (!tag.selfClosing) {
                ++objectGroupDepth;
                inMarkers = attribute(tag.attributes, "name", "") == "markers";
            }
            continue;
        }

        if (tag.name == "object") {
            if (tag.closing) {
                if (objectDepth > 0) {
                    --objectDepth;
                }
                if (inLight && objectDepth == 0) {
                    if (pendingHasLuminance && pending.luminance >= 0 && pending.luminance <= 31) {
                        authored.push_back(pending);
                    }
                    inLight = false;
                }
                continue;
            }
            const bool isLight = inMarkers && haveZ &&
                                 attribute(tag.attributes, "type", "") == "light_source";
            if (isLight) {
                pending = AuthoredLamp{};
                pending.name = std::string(attribute(tag.attributes, "name", ""));
                pending.tileX = parseLeadingInt(attribute(tag.attributes, "x", "0"), 0) / kTilePx;
                pending.tileY = parseLeadingInt(attribute(tag.attributes, "y", "0"), 0) / kTilePx;
                pending.authoredZ = currentZ;
                pending.warmth =
                    isFireLampName(pending.name) ? LampWarmth::Fire : LampWarmth::Lantern;
                pendingHasLuminance = false;
                if (tag.selfClosing) {
                    // A point object with no properties block: no luminance, so
                    // it is not a light this build can use.
                    inLight = false;
                } else {
                    inLight = true;
                    objectDepth = 1;
                }
            } else if (!tag.selfClosing && inLight) {
                ++objectDepth;
            }
            continue;
        }

        if (tag.name == "property" && inLight && !tag.closing) {
            if (attribute(tag.attributes, "name", "") == "luminance") {
                pending.luminance = parseLeadingInt(attribute(tag.attributes, "value", ""), -1);
                pendingHasLuminance = pending.luminance >= 0;
            }
        }
    }

    std::vector<Lamp> lamps;
    lamps.reserve(authored.size());
    for (const AuthoredLamp& lamp : authored) {
        lamps.push_back(Lamp{lamp.name, kBorderX + lamp.tileX, kBorderY + lamp.tileY,
                             kBorderZ + (lamp.authoredZ - minZ), lamp.luminance, lamp.warmth});
    }
    return lamps;
}

std::vector<Lamp> scanTmxFile(const std::filesystem::path& tmxFile) {
    std::ifstream in(tmxFile, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + tmxFile.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    return scanTmxLightSources(text);
}

std::string writeLampBake(const std::string& worldName, const std::string& sourceRelativePath,
                          const std::vector<Lamp>& lamps) {
    // Hand-written rather than nlohmann::dump so the layout is one lamp per
    // line and a diff of a re-bake reads as a list of lamps that changed.
    std::ostringstream out;
    out << "{\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"provenance\": \"Generated by granadad-bake-lamps from the authored Tiled "
           "source. Do not hand-edit; re-run the tool. See "
           "native/include/granadad/render/lamps.hpp.\",\n";
    out << "  \"world\": \"" << worldName << "\",\n";
    out << "  \"source\": \"" << sourceRelativePath << "\",\n";
    out << "  \"lamps\": [\n";
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        const Lamp& lamp = lamps[i];
        out << "    {\"name\": \"" << lamp.name << "\", \"x\": " << lamp.x
            << ", \"y\": " << lamp.y << ", \"z\": " << lamp.z
            << ", \"luminance\": " << lamp.luminance << ", \"warmth\": \""
            << lampWarmthName(lamp.warmth) << "\"}";
        if (i + 1 < lamps.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

std::vector<Lamp> readLampBake(std::string_view json) {
    nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw std::runtime_error("lamp bake is not a JSON object");
    }
    if (!parsed.contains("lamps") || !parsed["lamps"].is_array()) {
        throw std::runtime_error("lamp bake has no lamps array");
    }
    std::vector<Lamp> lamps;
    for (const nlohmann::json& entry : parsed["lamps"]) {
        Lamp lamp;
        lamp.name = entry.value("name", std::string());
        lamp.x = entry.value("x", 0);
        lamp.y = entry.value("y", 0);
        lamp.z = entry.value("z", 0);
        lamp.luminance = entry.value("luminance", 0);
        lamp.warmth =
            entry.value("warmth", std::string("lantern")) == "fire" ? LampWarmth::Fire
                                                                    : LampWarmth::Lantern;
        lamps.push_back(std::move(lamp));
    }
    return lamps;
}

std::filesystem::path lampBakePath(const std::filesystem::path& contentDir,
                                   const std::string& worldName) {
    return contentDir / "maps" / "baked" / (worldName + ".lamps.json");
}

std::vector<Lamp> loadLamps(const std::filesystem::path& contentDir,
                            const std::string& worldName) {
    const std::filesystem::path file = lampBakePath(contentDir, worldName);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    try {
        return readLampBake(buffer.str());
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace granadad::render
