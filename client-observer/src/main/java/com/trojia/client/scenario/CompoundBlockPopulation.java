package com.trojia.client.scenario;

import com.trojia.client.boot.RepoPaths;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRawsLoader;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.actor.ActorTypeStats;
import com.trojia.sim.actor.ActorTypeStatsTable;
import com.trojia.sim.actor.ActorTypes;
import com.trojia.sim.actor.ActorsSystem;
import com.trojia.sim.actor.HomeRegistry;
import com.trojia.sim.actor.HouseholdFormer;
import com.trojia.sim.actor.HouseholdRaws;
import com.trojia.sim.actor.HouseholdRawsLoader;
import com.trojia.sim.actor.ItemsLiteRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.RelationshipKind;
import com.trojia.sim.actor.RelationshipRegistry;
import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobBinder;
import com.trojia.sim.actor.job.JobId;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.AnimalKeeper;
import com.trojia.sim.actor.type.DiscipleOfTheFlame;
import com.trojia.sim.actor.type.FeralActor;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.PriestOfTheFlame;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.Wastrel;
import com.trojia.sim.world.Coords;
import com.trojia.sim.world.PackedPos;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The deterministic Trojian-Compound population (ACTORS-SPEC.md §11.4, DECISIONS.md
 * "Trojian housing: Compounds"): spawns a lore-derived, wealth-stratified roster (~64 actors)
 * onto the baked {@code compound_block} world and wires their jobs, homes, household/
 * employment/neighbour/friend/mentor relationship edges and starting inventory — the single
 * source of truth shared by the observer's compound boot path (which renders it,
 * {@code ObserverApp}) and the headless legibility listing ({@code CompoundBlockActorsMain}).
 *
 * <p><b>Lore-derived density (DOCKS-GAZETTEER §4, canon troop numbers).</b> The whole Docks
 * ward's lore target is derived from canon: a single harbour ward is garrisoned at
 * internal-security scale — ~1 platoon (canon platoon ≈ 100 men, novel L2074; cf. palace
 * household-guard ≈ 60, L2074), <em>not</em> the field army deployed to the fifteen-year
 * six-front war (30–40k at one front, L2146). Applying the ruled 1:10 enlisted-to-not-enlisted
 * heads-of-household ratio to ~100 enlisted heads gives 1,100 households; the canonical
 * household-size distribution ({@code household.json} weights {1:20,2:35,3:25,4:15,5:5},
 * mean 2.5) yields a ward target of <b>~2,750 people</b>. One fully-occupied Compound holds
 * ~64 — so the ward implies ~40 Compounds; this fixture realizes ONE of them (~2.3% of the
 * ward), the rest being off-map narrative context (gazetteer framing). Every number above the
 * platoon anchor is <b>(placeholder)</b> — canon fixes no Docks garrison.
 *
 * <p><b>Placement.</b> Actors live in <em>world-tile</em> coordinates, matching the
 * {@code TiledWorldImporter} placement rule the baked world uses: authored map cell
 * {@code (x, y)} on z-level {@code z} lands at world tile {@code (CHUNK_SIZE_X + x,
 * CHUNK_SIZE_Y + y, CHUNK_SIZE_Z + z)} (this map's authored z-range is {@code +0..+2}, so
 * {@code minZ = 0}). The map-cell anchors below are read straight from the committed
 * {@code compound_block.tmx} {@code markers} object layer (pixel/16, floor-divided).
 *
 * <p><b>Determinism.</b> Actors spawn in one fixed order (ascending ActorId by
 * construction, {@link ActorRegistry}); the only randomness — household grouping and
 * employer hiring — flows through {@link HouseholdFormer}'s named {@code household.*} RNG
 * streams ({@code RandomSource}/{@code ActorRngStream}), never {@code java.util.Random}.
 * The distribution is authored per dwelling unit, not a flat spread: Reman-concrete
 * mansion/condo units hold settled better-off households (Serfs, a resident Priest/Disciple
 * pairing, shop-owning families); the cloth/leather rooftop huts hold poorer Wastrels plus
 * one household secretly running {@code villain.skyrunner} under a rooftop-tenant cover.
 */
public final class CompoundBlockPopulation {

    // ---- authored map-cell anchors (compound_block.tmx markers, pixel/16) ---------------
    // z:+0 dwelling units + businesses
    private static final int[] MANSION_01 = {27, 65};
    private static final int[] CONDO_02 = {99, 44};
    private static final int[] CONDO_03 = {99, 65};
    private static final int[] CONDO_04 = {99, 86};
    private static final int[] CONDO_05 = {55, 44};
    private static final int[] CONDO_06 = {63, 86};
    private static final int[] NETMENDERS = {27, 19};   // K22 Netmenders' Arcade
    private static final int[] EELPOTS = {99, 19};      // K24 The Eel-Pots
    // Workplace anchors distinct from any dwelling (derived from known coordinates, no new map
    // marker): commute destinations for the compound's daily round trips (§4.6, JobBehaviors
    // pursueAtAnchor). All lie on authored walkable floor (courtyard dirt / gate-corridor dirt).
    private static final int[] ALMS_STATION = {68, 44};        // gate-corridor alms post (clergy)
    private static final int[] COURTYARD_FARM = {63, 65};      // courtyard atrium centre (farmers)
    private static final int[] COURTYARD_STALL = {70, 66};     // courtyard market stall (a stallholder)
    // z:+1 upper-floor condos (east wing built up a second story)
    private static final int[] CONDO_07 = {99, 44};
    private static final int[] CONDO_08 = {99, 65};
    private static final int[] CONDO_09 = {99, 86};
    // z:+2 rooftop slum huts
    private static final int[] ROOF_HUT_10 = {89, 45};
    private static final int[] ROOF_HUT_11 = {104, 63};
    private static final int[] ROOF_HUT_12 = {87, 84};

    // ---- placeholder ItemsLite kind ids (no items-raws system yet, §11.2) ---------------
    private static final short KIND_COIN = 1;
    private static final short KIND_STOCK = 2;
    private static final short KIND_ALMS_TOKEN = 3;
    private static final short KIND_SCRAP = 4;
    private static final short KIND_LOCKPICK = 5;   // the Skyrunner's tell, carried not presented
    private static final short KIND_FEED = 6;
    private static final short KIND_CUDGEL = 7;

    private final ActorsSystem system;
    private final ActorTypeStatsTable typeStats;
    private final JobRegistry jobs;
    private final HomeRegistry homes;
    private final RelationshipRegistry relationships;
    private final ItemsLiteRegistry items;
    private final ActorRegistry registry;
    private final long worldSeed;
    private final int trackedGroundMoverId;
    private final List<Integer> moverIds;

    private CompoundBlockPopulation(ActorsSystem system, ActorTypeStatsTable typeStats,
            JobRegistry jobs, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, ActorRegistry registry, long worldSeed,
            int trackedGroundMoverId, List<Integer> moverIds) {
        this.system = system;
        this.typeStats = typeStats;
        this.jobs = jobs;
        this.homes = homes;
        this.relationships = relationships;
        this.items = items;
        this.registry = registry;
        this.worldSeed = worldSeed;
        this.trackedGroundMoverId = trackedGroundMoverId;
        this.moverIds = List.copyOf(moverIds);
    }

    public ActorsSystem system() {
        return system;
    }

    public ActorRegistry registry() {
        return registry;
    }

    public HomeRegistry homes() {
        return homes;
    }

    public RelationshipRegistry relationships() {
        return relationships;
    }

    public ItemsLiteRegistry items() {
        return items;
    }

    public JobRegistry jobs() {
        return jobs;
    }

    public ActorTypeStatsTable typeStats() {
        return typeStats;
    }

    public long worldSeed() {
        return worldSeed;
    }

    /**
     * A ground-floor ({@code z:+0}) actor deliberately spawned displaced from its home
     * with a depleted REST need so its {@code RETURN_HOME} policy fires immediately and it
     * visibly walks home as the world ticks — the movement the render/smoke proof tracks.
     */
    public int trackedGroundMoverId() {
        return trackedGroundMoverId;
    }

    /**
     * The ids of every actor deliberately displaced from its home at spawn (movers), in
     * ascending id order — the set whose {@code RETURN_HOME} walk the proofs track. Stable
     * across ticks (recorded at build, not re-derived from live positions).
     */
    public List<Integer> moverIds() {
        return moverIds;
    }

    /**
     * Builds the full population over a fresh set of registries, keyed to {@code worldSeed}
     * (the baked world's own seed). Loads the committed actor/job/household raws from
     * {@code content/raws}.
     */
    public static CompoundBlockPopulation build(long worldSeed) {
        Path rawsRoot = RepoPaths.locate("content", "raws");
        ActorTypeStatsTable typeStats = ActorRawsLoader.load(rawsRoot.resolve("actors"));
        HouseholdRaws householdRaws = HouseholdRawsLoader.load(
                rawsRoot.resolve("actors").resolve("household.json"));
        JobRegistry jobs = JobBinder.bind(rawsRoot.resolve("jobs").resolve("jobs.json"),
                ActorTypes.allTypeIds());

        ActorRegistry registry = new ActorRegistry();
        HomeRegistry homes = new HomeRegistry();
        RelationshipRegistry relationships = new RelationshipRegistry();
        ItemsLiteRegistry items = new ItemsLiteRegistry();

        Builder builder = new Builder(registry, homes, relationships, items, typeStats, jobs,
                householdRaws, worldSeed);
        builder.populate();

        ActorsSystem system = new ActorsSystem(worldSeed, typeStats, jobs, registry, homes,
                relationships, items);
        return new CompoundBlockPopulation(system, typeStats, jobs, homes, relationships, items,
                registry, worldSeed, builder.trackedGroundMoverId, builder.movers);
    }

    /** Packs an authored map cell {@code (mapX, mapY)} on z-level {@code mapZ} to its world tile. */
    private static int worldCell(int[] mapXY, int mapZ) {
        return PackedPos.pack(Coords.CHUNK_SIZE_X + mapXY[0], Coords.CHUNK_SIZE_Y + mapXY[1],
                Coords.CHUNK_SIZE_Z + mapZ);
    }

    /** The mutable spawn walker — all wiring lives here so the outer type stays an immutable handle. */
    private static final class Builder {
        private final ActorRegistry registry;
        private final HomeRegistry homes;
        private final RelationshipRegistry relationships;
        private final ItemsLiteRegistry items;
        private final ActorTypeStatsTable typeStats;
        private final JobRegistry jobs;
        private final HouseholdRaws householdRaws;
        private final long seed;
        private final List<Integer> movers = new ArrayList<>();
        private int trackedGroundMoverId = Actor.NONE;

        Builder(ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
                ItemsLiteRegistry items, ActorTypeStatsTable typeStats, JobRegistry jobs,
                HouseholdRaws householdRaws, long seed) {
            this.registry = registry;
            this.homes = homes;
            this.relationships = relationships;
            this.items = items;
            this.typeStats = typeStats;
            this.jobs = jobs;
            this.householdRaws = householdRaws;
            this.seed = seed;
        }

        void populate() {
            // Lore-derived FULL-OCCUPANCY population of ONE Trojian Compound (~64 actors) — the
            // realistic ceiling for a single compound (owning-family mansion + servant households,
            // one household per condo, canon-packed rooftop slum, business proprietors + commuting
            // staff, civic texture). It realizes ~1/40 of the ~2,750-person Docks ward the canon
            // troop numbers imply (class Javadoc); the ward is a many-compound district, the rest
            // off-map. Spawn order is fixed => ActorIds deterministic; the only RNG is
            // HouseholdFormer's named draws. Extra households cluster on each unit's confirmed-
            // walkable centre anchor (small offsets stay inside the same authored room).

            // ===================== OWNING-FAMILY MANSION (Reman-concrete, z:+0) =====================
            // The great house (40x60, ~8x a condo's floor): the owning family plus SIX
            // servant/dependent households — the wealth signal is the household COUNT one shell holds.
            // M1 owning family (wealthiest): a shop-owning head who works a courtyard stall by day
            // (commuter) + kin (one is the tracked ground mover).
            Actor mansionOwner = spawn(Shopkeeper.TYPE, MANSION_01, 0);
            Actor mansionSpouse = spawn(Serf.TYPE, MANSION_01, 0);
            Actor mansionHeir = spawn(Serf.TYPE, MANSION_01, 0);
            Actor mansionMover = spawn(Serf.TYPE, MANSION_01, 0);   // ground-floor tracked mover
            household(List.of(mansionOwner, mansionSpouse, mansionHeir, mansionMover));
            mansionOwner.setAnchorCell(worldCell(COURTYARD_STALL, 0));   // runs a stall (commuter)

            // M2 steward's family (three) · M3 cook's pair.
            Actor steward = spawn(Serf.TYPE, new int[]{24, 61}, 0);
            household(List.of(steward, spawn(Serf.TYPE, new int[]{24, 61}, 0),
                    spawn(Serf.TYPE, new int[]{24, 61}, 0)));
            Actor cook = spawn(Serf.TYPE, new int[]{30, 61}, 0);
            household(List.of(cook, spawn(Serf.TYPE, new int[]{30, 61}, 0)));

            // M4 groundskeeper household — an Animal Keeper (the §4.8 Keeper<->Animal invariant)
            // and a Serf apprentice (MENTOR, below), with the house watchdog sharing their Home.
            Actor houseKeeper = spawn(AnimalKeeper.TYPE, new int[]{24, 69}, 0);
            Actor houseKeeperMate = spawn(Serf.TYPE, new int[]{24, 69}, 0);
            household(List.of(houseKeeper, houseKeeperMate));
            Actor houseDog = spawn(AnimalActor.TYPE, new int[]{25, 69}, 0);
            houseDog.setOwnerId(houseKeeper.id());
            houseDog.setHomeId(houseKeeper.homeId());

            // M5 poor-relation pair · M6 laundry household (three) · M7 aged-retainer pair.
            Actor poorRel = spawn(Serf.TYPE, new int[]{30, 69}, 0);
            household(List.of(poorRel, spawn(Serf.TYPE, new int[]{30, 69}, 0)));
            Actor laundry = spawn(Serf.TYPE, new int[]{22, 65}, 0);
            household(List.of(laundry, spawn(Serf.TYPE, new int[]{22, 65}, 0),
                    spawn(Serf.TYPE, new int[]{22, 65}, 0)));
            Actor aged = spawn(Serf.TYPE, new int[]{32, 65}, 0);
            household(List.of(aged, spawn(Serf.TYPE, new int[]{32, 65}, 0)));

            // ===================== GROUND-FLOOR CONDOS (one household per unit, §2.5) ==============
            // C02 devout clergy household: the ward's ONE Priest (canon: exactly one) + TWO
            // Disciples (canon: 2-3), commuting to the gate-corridor alms post daily.
            Actor priest = spawn(PriestOfTheFlame.TYPE, CONDO_02, 0);
            Actor disciple1 = spawn(DiscipleOfTheFlame.TYPE, CONDO_02, 0);
            Actor disciple2 = spawn(DiscipleOfTheFlame.TYPE, CONDO_02, 0);
            household(List.of(priest, disciple1, disciple2));
            HouseholdFormer.bindMentorPairFree(priest, disciple1, relationships);   // §4.5 EMPLOYER
            priest.setAnchorCell(worldCell(ALMS_STATION, 0));
            disciple1.setAnchorCell(worldCell(ALMS_STATION, 0));
            disciple2.setAnchorCell(worldCell(ALMS_STATION, 0));

            // C03 farmer family (three) — work the courtyard atrium farm (serf.farmer commuters).
            Actor c03a = spawn(Serf.TYPE, CONDO_03, 0);
            Actor c03b = spawn(Serf.TYPE, CONDO_03, 0);
            Actor c03c = spawn(Serf.TYPE, CONDO_03, 0);
            household(List.of(c03a, c03b, c03c));
            makeFarmer(c03a);
            makeFarmer(c03b);
            makeFarmer(c03c);

            // C04 dock-labour pair — haul at the Eel-Pots quay (serf.laborer commuters).
            Actor c04a = spawn(Serf.TYPE, CONDO_04, 0);
            Actor c04b = spawn(Serf.TYPE, CONDO_04, 0);
            household(List.of(c04a, c04b));
            c04a.setAnchorCell(worldCell(EELPOTS, 0));
            c04b.setAnchorCell(worldCell(EELPOTS, 0));

            // C05 stallholder household — a second Shopkeeper working a courtyard stall + kin.
            Actor c05shop = spawn(Shopkeeper.TYPE, CONDO_05, 0);
            household(List.of(c05shop, spawn(Serf.TYPE, CONDO_05, 0)));
            c05shop.setAnchorCell(worldCell(COURTYARD_STALL, 0));

            // C06 farmer pair — also work the courtyard farm (serf.farmer commuters).
            Actor c06a = spawn(Serf.TYPE, CONDO_06, 0);
            Actor c06b = spawn(Serf.TYPE, CONDO_06, 0);
            household(List.of(c06a, c06b));
            makeFarmer(c06a);
            makeFarmer(c06b);

            // ===================== UPPER-FLOOR CONDOS (east wing, z:+1) ============================
            Actor c07a = spawn(Serf.TYPE, CONDO_07, 1);
            household(List.of(c07a, spawn(Serf.TYPE, CONDO_07, 1), spawn(Serf.TYPE, CONDO_07, 1),
                    spawn(Serf.TYPE, CONDO_07, 1)));
            Actor c08a = spawn(Serf.TYPE, CONDO_08, 1);
            household(List.of(c08a, spawn(Serf.TYPE, CONDO_08, 1)));
            Actor c09a = spawn(Serf.TYPE, CONDO_09, 1);
            household(List.of(c09a, spawn(Serf.TYPE, CONDO_09, 1), spawn(Serf.TYPE, CONDO_09, 1)));

            // ===================== ROOFTOP SLUM (cloth/leather huts, z:+2 — packed mass housing) ===
            // Hut 10: a Wastrel pair (one a secret cutpurse under streetlife cover) + a lone
            // destitute Serf who cannot afford a condo.
            Actor r10a = spawn(Wastrel.TYPE, ROOF_HUT_10, 2);
            household(List.of(r10a, spawn(Wastrel.TYPE, ROOF_HUT_10, 2)));
            assignJob(r10a, Job.Villain.Cutpurse.ID);
            household(List.of(spawn(Serf.TYPE, new int[]{87, 47}, 2)));

            // Hut 11: the Skyrunner household (secret compound-burglar under a rooftop-tenant cover)
            // + partner; and a second pair hiding a robber.
            Actor skyrunner = spawn(Wastrel.TYPE, ROOF_HUT_11, 2);
            Actor skyPartner = spawn(Wastrel.TYPE, ROOF_HUT_11, 2);
            assignJob(skyrunner, Job.Villain.Skyrunner.ID);
            household(List.of(skyrunner, skyPartner));
            Actor robber = spawn(Wastrel.TYPE, new int[]{101, 66}, 2);
            household(List.of(robber, spawn(Wastrel.TYPE, new int[]{101, 66}, 2)));
            assignJob(robber, Job.Villain.Robber.ID);

            // Hut 12: a Wastrel family (three), a lone cutpurse, and a destitute Serf.
            Actor r12a = spawn(Wastrel.TYPE, ROOF_HUT_12, 2);
            household(List.of(r12a, spawn(Wastrel.TYPE, ROOF_HUT_12, 2),
                    spawn(Wastrel.TYPE, ROOF_HUT_12, 2)));
            Actor cutpurse2 = spawn(Wastrel.TYPE, new int[]{89, 86}, 2);
            household(List.of(cutpurse2));
            assignJob(cutpurse2, Job.Villain.Cutpurse.ID);
            household(List.of(spawn(Serf.TYPE, new int[]{85, 82}, 2)));

            // ===================== STREET-FRONTAGE BUSINESSES (outside the compound walls) =========
            // Each proprietor lives over the shop (home == workplace, no commute). Each hired hand
            // lodges in a compound condo and commutes to the counter: home != anchor, so the
            // commute-aware pursueAtAnchor walks a daily round trip (§4.6/§4.2, §10.1).
            Actor netKeeper = spawn(Shopkeeper.TYPE, NETMENDERS, 0);
            soloHome(netKeeper);                                // over the shop
            Actor netStaff = spawn(Serf.TYPE, CONDO_03, 0);     // lodges in a compound condo
            netStaff.setHomeId(homes.addHome(worldCell(CONDO_03, 0)));
            netStaff.setAnchorCell(worldCell(NETMENDERS, 0));   // works the Netmenders counter
            hire(netKeeper, netStaff);                          // EMPLOYER edge

            Actor eelKeeper = spawn(Shopkeeper.TYPE, EELPOTS, 0);
            soloHome(eelKeeper);                                // over the shop
            Actor eelStaff = spawn(Serf.TYPE, CONDO_04, 0);     // lodges in a compound condo
            eelStaff.setHomeId(homes.addHome(worldCell(CONDO_04, 0)));
            eelStaff.setAnchorCell(worldCell(EELPOTS, 0));      // works the Eel-Pots counter
            hire(eelKeeper, eelStaff);

            // ===================== CIVIC / STREET TEXTURE ==========================================
            // Three Militia Watch on patrol — sleep at post (home == anchor): the ward's "enlisted"
            // (the "1" in the 1:10 heads-of-household ratio, class Javadoc).
            Actor watchGate = spawn(MilitiaWatch.TYPE, new int[]{71, 44}, 0);   // gate chokepoint
            soloHome(watchGate);
            soloHome(spawn(MilitiaWatch.TYPE, new int[]{48, 8}, 0));            // street frontage
            soloHome(spawn(MilitiaWatch.TYPE, new int[]{90, 25}, 0));           // Eel-Pots beat
            // Kennel-Row Animal Keeper with a pen and two beasts (Keeper<->Animal invariant).
            Actor keeper = spawn(AnimalKeeper.TYPE, new int[]{48, 58}, 0);
            Actor beastA = spawn(AnimalActor.TYPE, new int[]{49, 58}, 0);
            Actor beastB = spawn(AnimalActor.TYPE, new int[]{50, 58}, 0);
            beastA.setOwnerId(keeper.id());
            beastB.setOwnerId(keeper.id());
            int penHome = homes.addHome(keeper.cell());
            keeper.setHomeId(penHome);
            beastA.setHomeId(penHome);
            beastB.setHomeId(penHome);
            // Two Ferals scavenging the block (ownerless, roost at spawn).
            soloHome(spawn(FeralActor.TYPE, new int[]{98, 22}, 0));     // by the Eel-Pots' fish
            soloHome(spawn(FeralActor.TYPE, new int[]{30, 22}, 0));     // by the Netmenders

            // ===================== FLAVOUR RELATIONSHIP EDGES (deterministic, no RNG) ==============
            // Beyond the auto HOUSEHOLD cliques and business EMPLOYER edges, wire the remaining
            // RelationshipKinds so the graph exercises the full set (§11.3):
            // MENTOR — heir apprenticed to the head of house; the Keeper's mate learning the dogs;
            //          the Priest guiding both Disciples.
            relationships.addDirected(mansionOwner.id(), mansionHeir.id(), RelationshipKind.MENTOR);
            relationships.addDirected(houseKeeper.id(), houseKeeperMate.id(), RelationshipKind.MENTOR);
            relationships.addDirected(priest.id(), disciple1.id(), RelationshipKind.MENTOR);
            relationships.addDirected(priest.id(), disciple2.id(), RelationshipKind.MENTOR);
            // EMPLOYER — the great house employs its steward and its groundskeeper.
            relationships.addDirected(mansionOwner.id(), steward.id(), RelationshipKind.EMPLOYER);
            relationships.addDirected(mansionOwner.id(), houseKeeper.id(), RelationshipKind.EMPLOYER);
            // NEIGHBOR — adjacent Homes across the ring (mansion wing, condo stack, rooftop huts).
            relationships.addSymmetric(steward.id(), cook.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(poorRel.id(), laundry.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(priest.id(), c03a.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(c03a.id(), c04a.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(c05shop.id(), c06a.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(c07a.id(), c08a.id(), RelationshipKind.NEIGHBOR);
            relationships.addSymmetric(r10a.id(), r12a.id(), RelationshipKind.NEIGHBOR);
            // FRIEND — cross-household social bonds the courtyard / quay throw together.
            relationships.addSymmetric(c03a.id(), c06a.id(), RelationshipKind.FRIEND);     // fellow farmers
            relationships.addSymmetric(c04a.id(), eelStaff.id(), RelationshipKind.FRIEND);  // quay mates
            relationships.addSymmetric(netKeeper.id(), watchGate.id(), RelationshipKind.FRIEND);
            relationships.addSymmetric(aged.id(), laundry.id(), RelationshipKind.FRIEND);

            // ===================== Civic default jobs + presented-job cover (§10.4) ================
            // Every actor without an explicit job (the four villains + the five farmers already have
            // one) takes its type's defaultFor job.
            for (int i = 0; i < registry.size(); i++) {
                Actor actor = registry.get(i);
                if (actor.jobOrdinal() < 0) {
                    actor.setJobOrdinal((short) jobs.defaultOrdinalFor(actor.typeId()));
                }
            }

            // ===================== Starting inventory (placeholder ids + quantities, §11.2) ========
            for (int i = 0; i < registry.size(); i++) {
                giveStartingInventory(registry.get(i));
            }

            // ===================== Movers: displace + deplete REST so RETURN_HOME visibly fires ====
            // These spawn already home; nudging them off their home cell and dropping REST below LOW
            // makes RETURN_HOME (NEED band, leash-ignoring) win the argmax and walk them back over
            // many ticks — the movement the render/smoke proof observes. (Commuters/patrollers/
            // wanderers already move under their jobs, so only a few RETURN_HOME demos are needed.)
            trackedGroundMoverId = mansionMover.id();
            makeMover(mansionMover, 6, 4);          // z:+0, tracked
            makeMover(poorRel, 5, 3);               // z:+0
            makeMover(skyPartner, 4, 3);            // z:+2 rooftop
        }

        /** Sets one explicit (non-default) job by its raws id — the villain/farmer overrides. */
        private void assignJob(Actor actor, JobId jobId) {
            actor.setJobOrdinal((short) jobs.ordinalOf(jobId));
        }

        /** serf.farmer working the courtyard atrium farm: a commuter (anchor != home). */
        private void makeFarmer(Actor serf) {
            assignJob(serf, Job.Serf.Farmer.ID);
            serf.setAnchorCell(worldCell(COURTYARD_FARM, 0));
        }

        private Actor spawn(ActorTypeId type, int[] mapXY, int mapZ) {
            ActorTypeStats stats = typeStats.get(type);
            return registry.spawn(type, stats, worldCell(mapXY, mapZ));
        }

        /** One shared Home at the group leader's cell + a HOUSEHOLD clique, via the real former. */
        private void household(List<Actor> group) {
            HouseholdFormer.formHouseholds(group, homes, relationships, seed, cohesive(group.size()));
        }

        /** A single-occupant home (Watch at post, Feral roost) — a Home, no household edges. */
        private void soloHome(Actor actor) {
            actor.setHomeId(homes.addHome(actor.cell()));
        }

        /** One guaranteed EMPLOYER edge employer -> hire (staffCount forced to 1, §11.4 step 3). */
        private void hire(Actor employer, Actor staff) {
            HouseholdFormer.formEmployment(employer, List.of(staff), relationships, seed,
                    new HouseholdRaws(householdRaws.householdSizeWeights(), 1, 1, 0, 0, 0, 0));
        }

        /**
         * A household-size weight vector that forces {@link HouseholdFormer} to group all
         * {@code size} members into one Home (all weight on the exact size) — so each
         * authored dwelling unit is exactly one household, deterministically, while still
         * running through the real former's shared-Home + HOUSEHOLD-clique machinery.
         */
        private HouseholdRaws cohesive(int size) {
            int[] weights = new int[size];
            weights[size - 1] = 1;
            return new HouseholdRaws(weights, 1, 1, 0, 0, 0, 0);
        }

        private void makeMover(Actor actor, int dx, int dy) {
            int x = PackedPos.x(actor.cell());
            int y = PackedPos.y(actor.cell());
            int z = PackedPos.z(actor.cell());
            actor.setCell(PackedPos.pack(x + dx, y + dy, z));
            // Deplete REST well below LOW (3000) so RETURN_HOME scores above every JOB policy.
            actor.applyNeedDelta(Need.REST, -(actor.need(Need.REST) - 400));
            movers.add(actor.id());
        }

        private void giveStartingInventory(Actor actor) {
            String type = actor.typeId().key();
            switch (type) {
                case "serf" -> mint(actor, KIND_COIN, 2);
                case "shopkeeper" -> {
                    mint(actor, KIND_STOCK, 5);
                    mint(actor, KIND_COIN, 3);
                }
                case "priest_of_the_flame", "disciple_of_the_flame" -> mint(actor, KIND_ALMS_TOKEN, 4);
                case "wastrel" -> {
                    if (isSkyrunner(actor)) {
                        mint(actor, KIND_LOCKPICK, 1);
                        mint(actor, KIND_SCRAP, 1);
                    } else {
                        mint(actor, KIND_SCRAP, 1);
                    }
                }
                case "militia_watch" -> mint(actor, KIND_CUDGEL, 1);
                case "animal_keeper" -> mint(actor, KIND_FEED, 3);
                default -> { /* animals/ferals carry nothing */ }
            }
        }

        private boolean isSkyrunner(Actor actor) {
            return actor.jobOrdinal() >= 0
                    && jobs.get(actor.jobOrdinal()).id().equals(Job.Villain.Skyrunner.ID);
        }

        /** Mints a carried ItemsLite entry and links its id into the actor's inventory-lite. */
        private void mint(Actor actor, short kindId, int quantity) {
            int itemId = items.mint(kindId, actor.id(), actor.id(), Actor.NONE, (short) quantity);
            actor.addInventoryItem((short) itemId);
        }
    }
}
