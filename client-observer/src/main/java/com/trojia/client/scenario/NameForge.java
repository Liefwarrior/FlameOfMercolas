package com.trojia.client.scenario;

import com.trojia.client.inspect.JobDisplay;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorRngStream;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.HouseholdFormer;
import com.trojia.sim.actor.NamedDraws;
import com.trojia.sim.actor.RelationshipEdge;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The Trojian NameForge (Sprint 1 S1-1/S1-2): a bake-time pass that gives every soul in a
 * finished population an identity — household surnames read off the HOUSEHOLD relationship
 * components, given names/epithets from the {@code names.json} pools, kennel names for
 * keeper-owned beasts, template one-line bios derived from each actor's REAL job/home/employer
 * data, and the Forty Notables' authored identities bound by spawn site over the top.
 *
 * <p><b>Determinism (binding).</b> A pure function of {@code (worldSeed, finished bake,
 * committed raws)}: every draw goes through {@link NamedDraws} on the appended
 * {@code identity.names} stream at {@link HouseholdFormer#BAKE_TICK}, keyed by ActorId (or the
 * household component's lowest ActorId for surnames) — so no draw of any pre-existing stream
 * shifts, and forging at bake or after any number of ticks yields byte-identical tables
 * (anchors, homes, jobs and edges are bake-immutable). Iteration is ascending-id everywhere;
 * hash containers are used for membership only, never iterated.
 *
 * <p><b>Presented-identity rule.</b> Template bios read the PRESENTED job
 * ({@link JobDisplay#presentedJobId}), and a notable villain's authored name is its COVER name
 * — identity data never leaks a secret the inspector would not show.
 */
final class NameForge {

    /** Bounded deterministic redraw budget before a pool is declared exhausted (bake error). */
    private static final int MAX_RETRIES = 64;

    // Draw-index map on the identity.names stream (append-only — never renumber).
    private static final int DRAW_SURNAME = 0;       // keyed by the component's lowest ActorId
    private static final int DRAW_GIVEN_BASE = 1;    // +k per uniqueness retry, k < MAX_RETRIES
    private static final int DRAW_EPITHET = 96;
    private static final int DRAW_KENNEL_BASE = 128; // +k per uniqueness retry, k < MAX_RETRIES

    /** The seven citizen groups that carry full names (given + household surname). */
    private static final Set<String> HUMAN_GROUPS = Set.of("serf", "wastrel", "shopkeeper",
            "militia_watch", "priest_of_the_flame", "disciple_of_the_flame", "animal_keeper");

    /** Same-z Chebyshev slack when matching a keeper's home to its species site. */
    private static final int SPECIES_SITE_RADIUS = 3;

    private NameForge() {
    }

    /**
     * Forges the full identity table for {@code registry}. See the class Javadoc for the
     * determinism contract; every input is bake-immutable, so the result is too.
     */
    static IdentityRegistry forge(long worldSeed, ActorRegistry registry, HomeRegistry homes,
            RelationshipRegistry relationships, JobRegistry jobs, NameRaws raws,
            List<NotableRaws.Notable> notables, Map<String, Integer> spawnSites,
            Map<Integer, String> siteNames) {
        int n = registry.size();

        // ---- 1. Bind the notables by spawn site (never by raw id) -----------------------
        NotableRaws.Notable[] boundNotable = bindNotables(registry, homes, notables, spawnSites);

        // ---- 2. Household components off the HOUSEHOLD edges ----------------------------
        int[] componentRoot = householdComponents(n, relationships);

        // ---- 3. Component surnames: authored family names win, drawn otherwise ----------
        Map<Integer, String> authoredSurnames = new HashMap<>(); // root -> family name
        for (int i = 0; i < n; i++) {
            NotableRaws.Notable notable = boundNotable[i];
            if (notable == null || notable.householdSurname() == null) {
                continue;
            }
            String previous = authoredSurnames.put(componentRoot[i], notable.householdSurname());
            if (previous != null && !previous.equals(notable.householdSurname())) {
                throw new IllegalStateException("two notables author conflicting surnames (\""
                        + previous + "\" vs \"" + notable.householdSurname()
                        + "\") onto one household component");
            }
        }
        Map<Integer, String> drawnSurnames = new HashMap<>(); // root -> drawn surname (memo)

        // ---- 4. Names + epithets (ascending id; retries bounded and deterministic) ------
        String[] given = new String[n];
        String[] surname = new String[n];
        String[] full = new String[n];
        String[] epithet = new String[n];
        boolean[] named = new boolean[n];
        Set<String> usedNames = new HashSet<>();      // membership only, never iterated
        for (NotableRaws.Notable notable : notables) {
            if (!usedNames.add(notable.name())) {
                throw new IllegalStateException(
                        "duplicate authored notable name \"" + notable.name() + "\"");
            }
        }
        for (int i = 0; i < n; i++) {
            Actor actor = registry.get(i);
            String type = actor.typeId().key();
            NotableRaws.Notable notable = boundNotable[i];
            if (notable != null) {
                given[i] = "";
                surname[i] = notable.householdSurname() == null ? "" : notable.householdSurname();
                full[i] = notable.name();
                epithet[i] = notable.epithet();
                named[i] = true;
            } else if (HUMAN_GROUPS.contains(type)) {
                String family = componentSurname(worldSeed, componentRoot[i], authoredSurnames,
                        drawnSurnames, raws);
                given[i] = drawUniqueGiven(worldSeed, i, raws.givenFor(type), family, usedNames);
                surname[i] = family;
                full[i] = given[i] + " " + family;
                List<String> pool = raws.epithetsFor(type);
                epithet[i] = pool.get(pick(draw(worldSeed, i, DRAW_EPITHET), pool.size()));
                named[i] = true;
            } else if (type.equals("animal") && actor.ownerId() != Actor.NONE) {
                given[i] = "";
                surname[i] = "";
                full[i] = drawUniqueKennel(worldSeed, i, raws.kennel(), usedNames);
                epithet[i] = "";
                named[i] = true;
            } else {
                given[i] = "";
                surname[i] = "";
                full[i] = namelessDescriptor(type);
                epithet[i] = "";
                named[i] = false;
            }
        }

        // ---- 5. Bios (second pass, so an employer's or owner's name is already forged) --
        int[] employerOf = employerOf(n, relationships);
        List<IdentityRegistry.Identity> rows = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            Actor actor = registry.get(i);
            NotableRaws.Notable notable = boundNotable[i];
            String bio;
            if (notable != null) {
                bio = notable.bio();
            } else if (HUMAN_GROUPS.contains(actor.typeId().key())) {
                bio = templateBio(actor, homes, jobs, employerOf[i], full, siteNames);
            } else if (named[i]) {
                bio = ownedBeastBio(actor, registry, homes, full, spawnSites, siteNames);
            } else {
                bio = "";
            }
            rows.add(new IdentityRegistry.Identity(i, given[i], surname[i], full[i],
                    epithet[i], bio, named[i], notable == null ? null : notable.id()));
        }
        return new IdentityRegistry(rows);
    }

    // ==================================================================================
    // Notable binding
    // ==================================================================================

    private static NotableRaws.Notable[] bindNotables(ActorRegistry registry, HomeRegistry homes,
            List<NotableRaws.Notable> notables, Map<String, Integer> spawnSites) {
        NotableRaws.Notable[] bound = new NotableRaws.Notable[registry.size()];
        for (NotableRaws.Notable notable : notables) {
            Integer site = spawnSites.get(notable.site());
            if (site == null) {
                throw new IllegalStateException("notable \"" + notable.id()
                        + "\" names unknown spawn site \"" + notable.site() + "\"");
            }
            int actorId = resolveBinding(registry, homes, notable, site);
            if (bound[actorId] != null) {
                throw new IllegalStateException("notables \"" + bound[actorId].id() + "\" and \""
                        + notable.id() + "\" both bind actor#" + actorId);
            }
            bound[actorId] = notable;
        }
        return bound;
    }

    private static int resolveBinding(ActorRegistry registry, HomeRegistry homes,
            NotableRaws.Notable notable, int siteCell) {
        int seen = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (!actor.typeId().key().equals(notable.type())) {
                continue;
            }
            int cell = notable.match().equals("home") ? homeCell(actor, homes)
                    : actor.anchorCell();
            if (cell == Actor.NONE || !within(cell, siteCell, notable.radius())) {
                continue;
            }
            if (seen == notable.rank()) {
                return i;
            }
            seen++;
        }
        throw new IllegalStateException("notable \"" + notable.id() + "\" did not bind: no "
                + notable.type() + " with " + notable.match() + " within " + notable.radius()
                + " of site " + notable.site() + " at rank " + notable.rank());
    }

    // ==================================================================================
    // Households and surnames
    // ==================================================================================

    /** Union-find over the HOUSEHOLD edges; every actor's root is its component's lowest id. */
    private static int[] householdComponents(int n, RelationshipRegistry relationships) {
        int[] parent = new int[n];
        for (int i = 0; i < n; i++) {
            parent[i] = i;
        }
        for (int e = 0; e < relationships.size(); e++) {
            RelationshipEdge edge = relationships.get(e);
            if (edge.kind() != RelationshipKind.HOUSEHOLD) {
                continue;
            }
            int rootA = find(parent, edge.fromId());
            int rootB = find(parent, edge.toId());
            if (rootA != rootB) {
                parent[Math.max(rootA, rootB)] = Math.min(rootA, rootB); // lowest id wins
            }
        }
        int[] root = new int[n];
        for (int i = 0; i < n; i++) {
            root[i] = find(parent, i);
        }
        return root;
    }

    private static int find(int[] parent, int i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }

    private static String componentSurname(long worldSeed, int root,
            Map<Integer, String> authored, Map<Integer, String> drawn, NameRaws raws) {
        String family = authored.get(root);
        if (family != null) {
            return family;
        }
        return drawn.computeIfAbsent(root, r -> raws.surnames()
                .get(pick(draw(worldSeed, r, DRAW_SURNAME), raws.surnames().size())));
    }

    // ==================================================================================
    // Name draws
    // ==================================================================================

    private static String drawUniqueGiven(long worldSeed, int actorId, List<String> pool,
            String family, Set<String> usedNames) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            String candidate = pool.get(
                    pick(draw(worldSeed, actorId, DRAW_GIVEN_BASE + attempt), pool.size()));
            if (usedNames.add(candidate + " " + family)) {
                return candidate;
            }
        }
        throw new IllegalStateException("given-name pool exhausted for surname \"" + family
                + "\" at actor#" + actorId + " — grow the names.json pools");
    }

    private static String drawUniqueKennel(long worldSeed, int actorId, List<String> pool,
            Set<String> usedNames) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            String candidate = pool.get(
                    pick(draw(worldSeed, actorId, DRAW_KENNEL_BASE + attempt), pool.size()));
            if (usedNames.add(candidate)) {
                return candidate;
            }
        }
        throw new IllegalStateException(
                "kennel pool exhausted at actor#" + actorId + " — grow the names.json pool");
    }

    private static long draw(long worldSeed, int spatialKey, int drawIndex) {
        return NamedDraws.draw(ActorRngStream.IDENTITY_NAMES, worldSeed,
                HouseholdFormer.BAKE_TICK, spatialKey, drawIndex);
    }

    private static int pick(long drawValue, int poolSize) {
        return (int) Long.remainderUnsigned(drawValue, poolSize);
    }

    private static String namelessDescriptor(String typeKey) {
        return switch (typeKey) {
            case "feral" -> "a harbor gull";
            case "mouse" -> "a wharf mouse";
            case "cat" -> "a dock cat";
            default -> "a stray beast";
        };
    }

    // ==================================================================================
    // Template bios
    // ==================================================================================

    private static String templateBio(Actor actor, HomeRegistry homes, JobRegistry jobs,
            int employerId, String[] fullNames, Map<Integer, String> siteNames) {
        Job job = actor.jobOrdinal() >= 0 ? jobs.get(actor.jobOrdinal()) : null;
        StringBuilder bio = new StringBuilder(96);
        bio.append(jobPhrase(JobDisplay.presentedJobId(job)))
                .append(" of ").append(siteName(actor.anchorCell(), siteNames));
        if (employerId != Actor.NONE) {
            bio.append(", in ").append(fullNames[employerId]).append("'s pay");
        }
        int home = homeCell(actor, homes);
        if (home == Actor.NONE) {
            bio.append('.');
        } else if (within(home, actor.anchorCell(), 2)) {
            bio.append("; lives on the spot.");
        } else {
            bio.append("; lodges at ").append(siteName(home, siteNames)).append('.');
        }
        return bio.toString();
    }

    /** The presented-job display phrase (§4.1 presented-identity rule: covers read as covers). */
    private static String jobPhrase(String presentedJobId) {
        return switch (presentedJobId) {
            case "serf.laborer" -> "A dock laborer";
            case "serf.farmer" -> "A courtyard farmer";
            case "maritime.sailor" -> "A sailor";
            case "trade.trader" -> "A counter-clerk";
            case "trade.stallkeep" -> "A shopkeeper";
            case "watch.patrol" -> "A Watch spear";
            case "clergy.shepherd" -> "A priest of the Flame";
            case "clergy.acolyte" -> "A disciple of the Flame";
            case "husbandry.keeper" -> "An animal keeper";
            case "wastrel.streetlife" -> "A wastrel";
            default -> "A worker";
        };
    }

    private static String ownedBeastBio(Actor beast, ActorRegistry registry, HomeRegistry homes,
            String[] fullNames, Map<String, Integer> spawnSites, Map<Integer, String> siteNames) {
        Actor owner = registry.get(beast.ownerId());
        String species = species(homeCell(owner, homes), spawnSites);
        return fullNames[owner.id()] + "'s " + species + ", stabled at "
                + siteName(homeCell(beast, homes), siteNames) + ".";
    }

    /** Species by the owner's home site — the four Animal Keeper stations of gazetteer §4.2. */
    private static String species(int ownerHomeCell, Map<String, Integer> spawnSites) {
        if (nearSite(ownerHomeCell, spawnSites.get("K25_KENNEL_ROW"))) {
            return "rat-dog";
        }
        if (nearSite(ownerHomeCell, spawnSites.get("K02_IMPOUND"))) {
            return "yard dog";
        }
        if (nearSite(ownerHomeCell, spawnSites.get("PEN_GOATS"))) {
            return "goat";
        }
        if (nearSite(ownerHomeCell, spawnSites.get("CARTER_STAND"))) {
            return "dray horse";
        }
        return "beast";
    }

    private static boolean nearSite(int cell, Integer siteCell) {
        return siteCell != null && cell != Actor.NONE
                && within(cell, siteCell, SPECIES_SITE_RADIUS);
    }

    /**
     * The display name for {@code cell}: the nearest listed site within same-z Chebyshev 2
     * (insertion order breaks distance ties — deterministic), else the walk-plane band name.
     */
    private static String siteName(int cell, Map<Integer, String> siteNames) {
        if (cell == Actor.NONE) {
            return "the ward";
        }
        String best = null;
        int bestDistance = Integer.MAX_VALUE;
        for (Map.Entry<Integer, String> entry : siteNames.entrySet()) {
            int site = entry.getKey();
            if (PackedPos.z(site) != PackedPos.z(cell)) {
                continue;
            }
            int distance = chebyshev(site, cell);
            if (distance <= 2 && distance < bestDistance) {
                best = entry.getValue();
                bestDistance = distance;
            }
        }
        return best != null ? best : bandName(PackedPos.z(cell));
    }

    /** Authored-band fallback: world z -> the gazetteer's walk-plane register. */
    private static String bandName(int worldZ) {
        return switch (worldZ - Coords.CHUNK_SIZE_Z) {
            case 10 -> "the Beaching Strand";
            case 11 -> "the quayside";
            case 12 -> "the mid-slope terraces";
            case 13 -> "the upper terraces";
            case 14 -> "the high roofs";
            default -> "the ward";
        };
    }

    // ==================================================================================
    // Shared plumbing
    // ==================================================================================

    /** The first EMPLOYER edge's senior for each actor (ascending edge order), or NONE. */
    private static int[] employerOf(int n, RelationshipRegistry relationships) {
        int[] employer = new int[n];
        java.util.Arrays.fill(employer, Actor.NONE);
        for (int e = 0; e < relationships.size(); e++) {
            RelationshipEdge edge = relationships.get(e);
            if (edge.kind() == RelationshipKind.EMPLOYER
                    && employer[edge.toId()] == Actor.NONE) {
                employer[edge.toId()] = edge.fromId();
            }
        }
        return employer;
    }

    private static int homeCell(Actor actor, HomeRegistry homes) {
        return actor.homeId() == Actor.NONE ? Actor.NONE
                : homes.get(actor.homeId()).homeCell();
    }

    private static boolean within(int cellA, int cellB, int radius) {
        return PackedPos.z(cellA) == PackedPos.z(cellB) && chebyshev(cellA, cellB) <= radius;
    }

    private static int chebyshev(int cellA, int cellB) {
        return Math.max(Math.abs(PackedPos.x(cellA) - PackedPos.x(cellB)),
                Math.abs(PackedPos.y(cellA) - PackedPos.y(cellB)));
    }
}
