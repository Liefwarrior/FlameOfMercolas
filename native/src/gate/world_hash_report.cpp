#include "granadad/gate/world_hash_report.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "granadad/content/ascii.hpp"
#include "granadad/content/content_dir.hpp"
#include "granadad/content/lanes.hpp"
#include "granadad/content/world.hpp"
#include "granadad/content/world_reader.hpp"
#include "granadad/gate/workload.hpp"
#include "granadad/sim/system_id.hpp"
#include "granadad/sim/world_hash.hpp"

namespace granadad::gate {
namespace {

using content::dec;
using content::hex64;

/// The workload used inside the report. Small and fixed on purpose: this file
/// is compared byte for byte between two platforms, so its inputs must not
/// depend on anything a caller could pass in.
[[nodiscard]] WorkloadConfig report_workload(const std::string& world) {
    WorkloadConfig config;
    config.world = world;
    config.seed = 0x4752414E41444144ull;  // "GRANADAD"
    config.ticks = 240;
    config.walkers = 64;
    config.sample_every = 60;
    return config;
}

void report_world(std::string& out, const std::string& name) {
    out += "\nworld " + name + "\n";

    const content::World world = content::loadWorldFile(content::bakedMap(name));
    out += "  coords chunks=" + dec(world.coords().chunksX()) + "x"
           + dec(world.coords().chunksY()) + "x" + dec(world.coords().chunksZ())
           + " chunkCount=" + dec(static_cast<std::uint64_t>(world.chunkCount()))
           + " tileCount=" + dec(static_cast<std::uint64_t>(world.tileCount())) + "\n";
    out += "  lanes.count " + dec(static_cast<std::uint64_t>(world.lanes().count())) + "\n";
    for (const content::LaneDef& lane : world.lanes().all()) {
        out += "  lane " + dec(static_cast<std::uint64_t>(lane.index)) + " name=" + lane.name
               + " bytesPerTile=" + dec(static_cast<std::uint64_t>(lane.bytesPerTile)) + "\n";
    }

    // The hasher, alone, over the freshly decoded world.
    sim::WorldHasher hasher;
    hasher.hash_world(world);
    out += "  hash.wrld " + hex64(hasher.section_hash(sim::WORLD_SECTION_SALT)) + "\n";
    out += "  hash.combined " + hex64(hasher.combined_hash()) + "\n";

    // Hashing twice must not move it. finished() is documented pure and a sink
    // may keep being fed afterwards; if that ever stops being true, the first
    // symptom would otherwise be a cross-platform mismatch with no cause.
    out += "  hash.wrld.again " + hex64(hasher.section_hash(sim::WORLD_SECTION_SALT)) + "\n";

    // Then the whole spine over the same world.
    const RunResult run = run_workload(report_workload(name));
    out += "  run.ticks " + dec(report_workload(name).ticks) + "\n";
    out += "  run.wrld " + hex64(run.world_hash) + "\n";
    out += "  run.combined " + hex64(run.combined_hash) + "\n";
    out += "  run.report.bytes " + dec(static_cast<std::uint64_t>(run.report.size())) + "\n";

    // The walkers never write a lane, so the world's own sub-hash must be the
    // same after 240 ticks as it was before the engine booted. The Java soak
    // makes the same observation the hard way: WRLD is identical at tick 400
    // and at tick 15,000 with 692 actors running.
    out += "  run.wrld.unchanged ";
    out += (run.world_hash == hasher.section_hash(sim::WORLD_SECTION_SALT)) ? "yes" : "NO";
    out += "\n";
}

}  // namespace

std::string world_hash_report() {
    // Enumerated and sorted, not hardcoded, so a fourth baked world joins the
    // comparison on both platforms the day it is authored. Directory order is a
    // filesystem detail and NTFS and overlayfs do not agree about it -- that
    // difference is not the divergence being hunted.
    std::vector<std::string> names;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(content::bakedDir())) {
        if (entry.is_regular_file() && entry.path().extension() == ".trojsav") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());

    std::string out;
    out += "granadad world-hash cross-toolchain report v"
           + dec(static_cast<std::uint64_t>(kWorldHashReportVersion)) + "\n";
    out += "worlds " + dec(static_cast<std::uint64_t>(names.size())) + "\n";
    for (const std::string& name : names) {
        report_world(out, name);
    }
    return out;
}

}  // namespace granadad::gate
