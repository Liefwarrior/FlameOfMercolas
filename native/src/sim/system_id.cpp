#include "granadad/sim/system_id.hpp"

#include <string>

#include "granadad/sim/engine_error.hpp"

namespace granadad::sim {
namespace {

void require_valid_section_id(std::string_view section_id) {
    if (section_id.size() != SECTION_ID_LENGTH) {
        throw EngineError("section id must be exactly 4 chars: '" + std::string(section_id)
                          + "'");
    }
    for (const char c : section_id) {
        if (c < 0x20 || c > 0x7E) {
            throw EngineError("section id must be printable ASCII: '" + std::string(section_id)
                              + "'");
        }
    }
}

}  // namespace

std::string derive_section_id(std::string_view name) {
    std::string id;
    for (const char c : name) {
        if (id.size() >= SECTION_ID_LENGTH) {
            break;
        }
        if (c >= 'a' && c <= 'z') {
            id.push_back(static_cast<char>(c - ('a' - 'A')));
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            id.push_back(c);
        }
    }
    while (id.size() < SECTION_ID_LENGTH) {
        id.push_back('_');
    }
    return id;
}

SystemId SystemId::of(std::string_view name) {
    return of(name, derive_section_id(name));
}

SystemId SystemId::of(std::string_view name, std::string_view section_id) {
    if (name.empty()) {
        throw EngineError("system name must be non-empty");
    }
    require_valid_section_id(section_id);
    return SystemId(std::string(name), system_salt(name), std::string(section_id));
}

}  // namespace granadad::sim
