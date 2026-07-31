#pragma once

// The one exception type the content readers throw.
//
// Every reader in this module is STRICT. The Java reference build treats a
// malformed save as unrecoverable — a corrupt world silently loaded as a
// slightly-different world would poison the world hash and every golden that
// depends on it. So there is no "tolerant" mode and no partial result: a reader
// either returns a fully validated structure or throws.

#include <stdexcept>
#include <string>

namespace granadad::content {

/// Thrown when on-disk content violates the format contract.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace granadad::content
