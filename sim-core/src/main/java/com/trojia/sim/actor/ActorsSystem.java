package com.trojia.sim.actor;

import com.trojia.sim.actor.job.Job;
import com.trojia.sim.actor.job.JobRegistry;
import com.trojia.sim.engine.SimulationSystem;
import com.trojia.sim.engine.SystemId;
import com.trojia.sim.engine.TickContext;
import com.trojia.sim.engine.TickPhase;
import com.trojia.sim.world.TileCursor;
import com.trojia.sim.world.Walkability;
import com.trojia.sim.world.World;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

/**
 * The ACTORS tick-phase system (ACTORS-SPEC.md §2.1, §2.8): decays needs,
 * selects and runs one policy per actor in ascending ActorId order, every
 * tick. Registries (actors, homes, relationships, items, jobs) are
 * constructor-injected — built by the spawner/{@link HouseholdFormer} bake
 * pass before the engine starts, exactly like {@code MacroWorld} precedent.
 *
 * <p>Persistence in this foundation milestone covers every {@link Actor}
 * field, the {@link Home} table and the {@link RelationshipEdge} table in one
 * straightforward primitive layout (documented simplification vs the spec's
 * exact §2.8 {@code ACTR} byte-for-byte record — the shape is faithful, the
 * wire format is this class's own).
 */
public final class ActorsSystem implements SimulationSystem {

    private final SystemId id = SystemId.of("actors", "ACTR");
    private final long worldSeed;
    private final ActorTypeStatsTable typeStats;
    private final JobRegistry jobs;
    private final ActorRegistry registry;
    private final HomeRegistry homes;
    private final RelationshipRegistry relationships;
    private final ItemsLiteRegistry items;
    /** The Royals ledger (Phase-0 economy F2); empty (no accounts) for the world-less bootstrap. */
    private final BankLedger bank;
    /**
     * The baked civic seams (Phase-2 Passes 9-10): arrest hold-cell + restricted zones (Phase 0)
     * plus the bank (vault/banker/queue), the multi-cell prison registry, and the payroll table.
     * {@link CivicFixtures#NONE} for the world-less bootstrap and economy-free tests.
     */
    private final CivicFixtures fixtures;
    /** Nullable — {@code null} for the world-less bootstrap ({@code ActorsDemoMain}). */
    private final World world;
    /** Reused flyweight cursor for {@code isWalkable} reads; {@code null} iff {@link #world} is. */
    private final TileCursor cursor;
    /** Per-actor per-tick draw-index counter (§2.2's "one counter per actor"); reset each tick. */
    private int[] drawCounters = new int[0];
    /**
     * The shared actor-actor occupancy index (the "only 2 to a cell" cap). Rebuilt from every
     * actor's cell at the top of each {@link #tick} — O(N), so it is robust to spawns, loads and
     * teleports that never routed through {@code onEnter} — then kept live as actors move.
     * Lazily sized to the current actor count on first tick.
     */
    private OccupancyIndex occupancy;

    /**
     * Closed-supply FOOD accounting (economy-loop pass): running totals of FOOD ever minted (larder
     * seed at bake + farm work-unit yields + periodic imports) and ever eaten (sunk). Pure
     * accounting for the conservation proof {@code minted == liveOfKind(FOOD) + eaten} — read by no
     * behavior, so they change no determinism property and ride no save (two fresh runs reproduce
     * them identically). {@code long} (purity-gate clean).
     */
    private long foodMinted;
    private long foodEaten;

    /** World-less constructor (test/headless convenience) — {@code isWalkable} always true. */
    public ActorsSystem(long worldSeed, ActorTypeStatsTable typeStats, JobRegistry jobs,
            ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items) {
        this(worldSeed, typeStats, jobs, registry, homes, relationships, items, new BankLedger(),
                null, CivicFixtures.NONE);
    }

    /**
     * World-aware constructor: {@code world} backs {@link ActorContext#isWalkable(int)} via a
     * reused {@link TileCursor} and {@link Walkability}. {@code world} may be {@code null}
     * (equivalent to the world-less constructor above). Empty ledger, no zones.
     */
    public ActorsSystem(long worldSeed, ActorTypeStatsTable typeStats, JobRegistry jobs,
            ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, World world) {
        this(worldSeed, typeStats, jobs, registry, homes, relationships, items, new BankLedger(),
                world, CivicFixtures.NONE);
    }

    /**
     * Phase-0-compatibility constructor: wires the {@code bank} ledger plus only the two Phase-0
     * justice seams — {@code arrestHoldCell} (ARREST-SPEC addendum) and {@code zones} (job/access
     * F3) — leaving the bank/prison/payroll fixtures unwired. Retained so existing call sites and
     * tests keep compiling; the live district uses the {@link CivicFixtures} constructor below.
     */
    public ActorsSystem(long worldSeed, ActorTypeStatsTable typeStats, JobRegistry jobs,
            ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, BankLedger bank, World world, int arrestHoldCell,
            RestrictedZoneTable zones) {
        this(worldSeed, typeStats, jobs, registry, homes, relationships, items, bank, world,
                CivicFixtures.ofJustice(arrestHoldCell, zones));
    }

    /**
     * Full constructor (Phase-2 Passes 9-10): wires the baked {@code bank} ledger and the whole
     * {@link CivicFixtures} bundle — arrest hold-cell + restricted zones, plus the bank vault/
     * banker/queue, the multi-cell prison registry, and the payroll table. {@link
     * CivicFixtures#NONE} means "fully unwired" (world-less bootstrap): custody holds in place, no
     * cell is restricted, no bank fixture resolves, and no wages are paid.
     */
    public ActorsSystem(long worldSeed, ActorTypeStatsTable typeStats, JobRegistry jobs,
            ActorRegistry registry, HomeRegistry homes, RelationshipRegistry relationships,
            ItemsLiteRegistry items, BankLedger bank, World world, CivicFixtures fixtures) {
        this.worldSeed = worldSeed;
        this.typeStats = typeStats;
        this.jobs = jobs;
        this.registry = registry;
        this.homes = homes;
        this.relationships = relationships;
        this.items = items;
        this.bank = bank;
        this.fixtures = fixtures;
        this.world = world;
        this.cursor = world == null ? null : world.cursor();
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

    public BankLedger bankAccounts() {
        return bank;
    }

    public RestrictedZoneTable restrictedZones() {
        return fixtures.zones();
    }

    /** Total FOOD ever minted (larder seed + farm yield + imports) — conservation-proof numerator. */
    public long foodMinted() {
        return foodMinted;
    }

    /** Total FOOD ever eaten/sunk — the conservation-proof sink count. */
    public long foodEaten() {
        return foodEaten;
    }

    /**
     * Records FOOD minted OUTSIDE the tick loop (the bake-time larder seed), so the harness's
     * conservation identity {@code minted == liveOfKind(FOOD) + eaten} accounts for every unit that
     * ever existed. Called once by the scenario bake; no effect on determinism (pure accounting).
     */
    public void recordFoodMintedAtBake(long n) {
        foodMinted += n;
    }

    @Override
    public SystemId id() {
        return id;
    }

    @Override
    public TickPhase phase() {
        return TickPhase.ACTORS;
    }

    @Override
    public void tick(TickContext context) {
        if (drawCounters.length < registry.size()) {
            drawCounters = new int[registry.size()];
        } else {
            java.util.Arrays.fill(drawCounters, 0, registry.size(), 0);
        }
        rebuildOccupancy();
        runPayroll(context.tick());
        runFoodImports(context.tick());
        registry.tickAll(new ActorContextImpl(context));
    }

    /**
     * The periodic FOOD provisioning (economy-loop pass): on an import tick, the quay restocks
     * every vendor shop's carried stock up to {@link FoodEconomy#SHOP_STOCK_CAP}, and the
     * provisioning ration tops every free commons cell and every guaranteed larder cell up to
     * {@link FoodEconomy#LARDER_CAP}. Each source is filled only to its cap (never beyond), so
     * supply is demand-driven and {@code liveOfKind(FOOD)} stays bounded rather than growing without
     * limit. Minting FOOD, never Royals, so the money invariant is untouched. Deterministic: fixed
     * cadence against the absolute tick (mirrors {@link #runPayroll}), ascending dense-index walks,
     * no draws. Every minted unit is accounted for the conservation proof.
     */
    private void runFoodImports(long tick) {
        FoodMarket market = fixtures.foodMarket();
        if (tick <= 0 || tick % FoodEconomy.IMPORT_PERIOD != 0
                || (market.vendorCount() == 0 && market.commonsCount() == 0
                        && market.larderCount() == 0)) {
            return;
        }
        for (int i = 0; i < market.vendorCount(); i++) {
            int shopId = market.vendorAt(i);
            int deficit = FoodEconomy.SHOP_STOCK_CAP
                    - items.countCarriedOfKind(shopId, ItemKinds.FOOD);
            if (deficit > 0) {
                foodMinted += items.addCarried(shopId, ItemKinds.FOOD, deficit);
            }
        }
        for (int i = 0; i < market.commonsCount(); i++) {
            foodMinted += topUpCell(market.commonsAt(i));
        }
        for (int i = 0; i < market.larderCount(); i++) {
            foodMinted += topUpCell(market.larderAt(i));
        }
    }

    /** Tops one cell's FOOD up to {@link FoodEconomy#LARDER_CAP}; returns the units minted. */
    private int topUpCell(int cell) {
        int deficit = FoodEconomy.LARDER_CAP - items.countOnCellOfKind(cell, ItemKinds.FOOD);
        return deficit > 0 ? items.addOnCell(cell, ItemKinds.FOOD, deficit) : 0;
    }

    /**
     * Live wages (Phase-2 STEP B, Pass 9): on a payday tick, transfer each worker's wage from the
     * finite employer pool to the worker's account, ascending actor id. A ledger transfer, never a
     * mint — an insufficient pool skips that wage ({@link BankLedger#transfer} returns {@code
     * false}), so {@link BankLedger#totalRoyals()} (and the vault COIN count) is invariant across
     * every payday. Deterministic: fixed cadence against the absolute tick, ascending-id dense-array
     * walk, no map/insertion-order iteration, no draws.
     */
    private void runPayroll(long tick) {
        Payroll payroll = fixtures.payroll();
        if (!payroll.isPayday(tick)) {
            return;
        }
        int employer = payroll.employerAccountId();
        for (int id = 0; id < registry.size(); id++) {
            long wage = payroll.wageForActor(id);
            if (wage > 0) {
                bank.transfer(employer, id, wage); // skipped (no-op) if the pool can't cover it
            }
        }
    }

    /**
     * Rebuilds the occupancy index from scratch (clear + one {@code add} per actor cell) before
     * the tick's moves run, so it always reflects true start-of-tick positions regardless of how
     * actors got there (spawn/load/teleport bypass {@code onEnter}). Lazily allocated, sized to
     * the actor count so the steady state needs no rehash.
     */
    private void rebuildOccupancy() {
        if (occupancy == null) {
            occupancy = new OccupancyIndex(registry.size());
        } else {
            occupancy.clear();
        }
        for (int i = 0; i < registry.size(); i++) {
            occupancy.add(registry.get(i).cell());
        }
    }

    @Override
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(registry.size());
        for (int i = 0; i < registry.size(); i++) {
            writeActor(out, registry.get(i));
        }
        out.writeInt(homes.size());
        for (int i = 0; i < homes.size(); i++) {
            out.writeInt(homes.get(i).homeCell());
        }
        out.writeInt(relationships.size());
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            out.writeInt(edge.fromId());
            out.writeInt(edge.toId());
            out.writeByte(edge.kind().ordinal());
        }
        out.writeInt(items.size());
        for (int i = 0; i < items.size(); i++) {
            ItemsLiteEntry entry = items.get(i);
            out.writeShort(entry.kindId());
            out.writeInt(entry.ownerActorId());
            out.writeInt(entry.locationCarriedBy());
            out.writeInt(entry.locationCell());
            out.writeInt(entry.quantity()); // int, not short (STEP A money-width fix): a vault
            out.writeInt(entry.accountId()); // COIN stack counts a whole district's Royals
        }
        // Recycling free-slot stack (LIFO push order) — genuine state, kept faithful across save.
        out.writeInt(items.freeSlotCount());
        for (int i = 0; i < items.freeSlotCount(); i++) {
            out.writeInt(items.freeSlotAt(i));
        }
        // The Royals ledger (Phase-0 economy F2), in canonical order after items (landmine E).
        out.writeInt(bank.accountCount());
        for (int i = 0; i < bank.accountCount(); i++) {
            out.writeLong(bank.balanceOf(i));
        }
    }

    private void writeActor(DataOutput out, Actor actor) throws IOException {
        out.writeInt(typeStats.ordinalOf(actor.typeId()));
        out.writeInt(actor.identity().trueId());
        out.writeInt(actor.identity().presentedId());
        out.writeInt(actor.cell());
        out.writeByte(actor.facing());
        for (Need need : Need.values()) {
            out.writeShort(actor.need(need));
        }
        out.writeShort(actor.hp());
        out.writeShort(actor.statusBits());
        out.writeShort(actor.downedTimer());
        out.writeByte(actor.policyOrdinal());
        out.writeByte(actor.targetKind().ordinal());
        out.writeInt(actor.targetKey());
        out.writeShort(actor.policyTimer());
        out.writeInt(actor.anchorCell());
        out.writeInt(actor.ownerId());
        out.writeInt(actor.homeId());
        out.writeShort(actor.jobOrdinal());
        out.writeByte(actor.goalState().ordinal());
        out.writeByte(actor.goalTargetKind().ordinal());
        out.writeInt(actor.goalTargetKey());
        out.writeShort(actor.goalProgress());
        out.writeInt(actor.goalCooldown());
        out.writeInt(actor.goalWorkTicks());
        out.writeLong(actor.heldUntilTick());
        out.writeByte(actor.offenseCount());
        out.writeInt(actor.assignedHoldCell()); // Phase-2 STEP C: per-prisoner cell (heldUntilTick triad)
        // Carried items are not written per-actor: ItemsLite (serialized above, keyed by carrier)
        // is the single source of truth for inventory — no parallel per-actor id list exists.
    }

    @Override
    public void load(DataInput in) throws IOException {
        int count = in.readInt();
        for (int i = 0; i < count; i++) {
            readActor(in);
        }
        int homeCount = in.readInt();
        for (int i = 0; i < homeCount; i++) {
            homes.addHome(in.readInt());
        }
        int edgeCount = in.readInt();
        for (int i = 0; i < edgeCount; i++) {
            int fromId = in.readInt();
            int toId = in.readInt();
            RelationshipKind kind = RelationshipKind.values()[in.readByte()];
            if (kind == RelationshipKind.HOUSEHOLD || kind == RelationshipKind.NEIGHBOR
                    || kind == RelationshipKind.FRIEND) {
                relationships.addSymmetric(fromId, toId, kind);
            } else {
                relationships.addDirected(fromId, toId, kind);
            }
        }
        int itemCount = in.readInt();
        for (int i = 0; i < itemCount; i++) {
            short kindId = in.readShort();
            int ownerActorId = in.readInt();
            int carriedBy = in.readInt();
            int cell = in.readInt();
            int quantity = in.readInt(); // int, not short (STEP A money-width fix)
            int accountId = in.readInt();
            items.mint(kindId, ownerActorId, carriedBy, cell, quantity, accountId);
        }
        int freeSlots = in.readInt();
        for (int i = 0; i < freeSlots; i++) {
            items.restoreFreeSlot(in.readInt());
        }
        int accountCount = in.readInt();
        for (int i = 0; i < accountCount; i++) {
            long balance = in.readLong();
            int accountId = bank.openAccount();
            bank.credit(accountId, balance);
        }
    }

    private void readActor(DataInput in) throws IOException {
        ActorTypeId typeId = typeStats.idAt(in.readInt());
        int trueId = in.readInt();
        int presentedId = in.readInt();
        int cell = in.readInt();
        byte facing = in.readByte();
        short[] needs = new short[Need.COUNT];
        for (int i = 0; i < Need.COUNT; i++) {
            needs[i] = in.readShort();
        }
        short hp = in.readShort();
        short statusBits = in.readShort();
        short downedTimer = in.readShort();
        byte policyOrdinal = in.readByte();
        TargetKind targetKind = TargetKind.values()[in.readByte()];
        int targetKey = in.readInt();
        short policyTimer = in.readShort();
        int anchorCell = in.readInt();
        int ownerId = in.readInt();
        int homeId = in.readInt();
        short jobOrdinal = in.readShort();
        GoalState goalState = GoalState.values()[in.readByte()];
        TargetKind goalTargetKind = TargetKind.values()[in.readByte()];
        int goalTargetKey = in.readInt();
        short goalProgress = in.readShort();
        int goalCooldown = in.readInt();
        int goalWorkTicks = in.readInt();
        long heldUntilTick = in.readLong();
        byte offenseCount = in.readByte();
        int assignedHoldCell = in.readInt(); // Phase-2 STEP C: per-prisoner cell

        Actor actor = registry.spawn(typeId, typeStats.get(typeId), cell);
        actor.setIdentity(new Persona(trueId, presentedId));
        actor.setFacing(facing);
        for (Need need : Need.values()) {
            actor.applyNeedDelta(need, needs[need.ordinal()] - actor.need(need));
        }
        actor.setHp(hp);
        actor.setDownedTimer(downedTimer);
        actor.setStatusBits(statusBits);
        // Thaw repair (PLAY-MODE-SPEC.md §6, the same shape as the goalTarget-at-thaw repair):
        // a fresh app launch never has a live human reattached to a specific actor. Without
        // this, an actor saved mid-Play-mode would permanently win selectIndex (score 2000 > 0)
        // with act() doing nothing useful (no target ever arrives again) — frozen forever.
        actor.setStatus(StatusBit.PLAYER_CONTROLLED, false);
        actor.setPolicyOrdinal(policyOrdinal);
        actor.setTarget(targetKind, targetKey);
        actor.setPolicyTimer(policyTimer);
        actor.setAnchorCell(anchorCell);
        actor.setOwnerId(ownerId);
        actor.setHomeId(homeId);
        actor.setJobOrdinal(jobOrdinal);
        actor.setGoalState(goalState);
        actor.setGoalTarget(goalTargetKind, goalTargetKey);
        actor.setGoalProgress(goalProgress);
        actor.setGoalCooldown(goalCooldown);
        actor.setGoalWorkTicks(goalWorkTicks);
        actor.setHeldUntilTick(heldUntilTick);
        actor.setOffenseCount(offenseCount);
        actor.setAssignedHoldCell(assignedHoldCell);
    }

    @Override
    public void hashInto(WorldHasher.Sink sink) {
        sink.putInt(registry.size());
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            sink.putInt(typeStats.ordinalOf(actor.typeId()));
            sink.putInt(actor.cell());
            for (Need need : Need.values()) {
                sink.putShort(actor.need(need));
            }
            sink.putShort(actor.hp());
            sink.putShort(actor.statusBits());
            sink.putInt(actor.homeId());
            sink.putShort(actor.jobOrdinal());
            sink.putByte(actor.goalState().ordinal());
            sink.putShort(actor.goalProgress());
            // Phase-2 STEP C: the per-prisoner assigned cell (landmine F — otherwise a divergence
            // isolated to cell assignment, e.g. two prisoners colliding on one cell, slips the
            // twin-run check). heldUntilTick/offenseCount remain out; the cell is the state the
            // multi-cell pass adds and must not be able to desync silently.
            sink.putInt(actor.assignedHoldCell());
        }
        sink.putInt(homes.size());
        for (int i = 0; i < homes.size(); i++) {
            sink.putInt(homes.get(i).homeCell());
        }
        sink.putInt(relationships.size());
        for (int i = 0; i < relationships.size(); i++) {
            RelationshipEdge edge = relationships.get(i);
            sink.putInt(edge.fromId());
            sink.putInt(edge.toId());
            sink.putByte(edge.kind().ordinal());
        }
        // Ledger balances (Phase-0 economy F2): the hash previously omitted every money-ish scalar
        // (landmine F), so a divergence isolated to a balance would have slipped past the twin-run
        // check. Hashing the ledger closes that gap — a money-only desync now fails determinism.
        sink.putInt(bank.accountCount());
        for (int i = 0; i < bank.accountCount(); i++) {
            sink.putLong(bank.balanceOf(i));
        }
    }

    /** The per-tick {@link ActorContext}, bound to this system's registries and named draws. */
    private final class ActorContextImpl implements ActorContext {

        private final TickContext ctx;

        ActorContextImpl(TickContext ctx) {
            this.ctx = ctx;
        }

        @Override
        public long tick() {
            return ctx.tick();
        }

        @Override
        public long worldSeed() {
            return worldSeed;
        }

        @Override
        public ActorRegistry registry() {
            return registry;
        }

        @Override
        public HomeRegistry homes() {
            return homes;
        }

        @Override
        public RelationshipRegistry relationships() {
            return relationships;
        }

        @Override
        public ItemsLiteRegistry items() {
            return items;
        }

        @Override
        public BankLedger bankAccounts() {
            return bank;
        }

        @Override
        public RestrictedZoneTable restrictedZones() {
            return fixtures.zones();
        }

        @Override
        public JobRegistry jobs() {
            return jobs;
        }

        @Override
        public long draw(ActorRngStream stream, int actorId, int drawIndex) {
            return NamedDraws.draw(stream, worldSeed, ctx.tick(), actorId, drawIndex);
        }

        @Override
        public int nextDrawIndex(int actorId) {
            return drawCounters[actorId]++;
        }

        @Override
        public int wielderCell() {
            int wielderId = wielderId();
            return wielderId == Actor.NONE ? Actor.NONE : registry.get(wielderId).cell();
        }

        @Override
        public int wielderId() {
            for (int i = 0; i < registry.size(); i++) {
                Actor actor = registry.get(i);
                if (actor.jobOrdinal() >= 0) {
                    Job job = jobs.get(actor.jobOrdinal());
                    if (job instanceof Job.FlameOfMerc) {
                        return actor.identity().presentedId() == actor.id() ? actor.id() : Actor.NONE;
                    }
                }
            }
            return Actor.NONE;
        }

        @Override
        public boolean isWalkable(int cell) {
            return cursor == null || Walkability.isWalkable(cursor.moveTo(cell));
        }

        @Override
        public Actor.OccupancyQuery occupancy() {
            return OCCUPANCY_VIEW;
        }

        @Override
        public int arrestHoldCell() {
            return fixtures.arrestHoldCell();
        }

        @Override
        public int vaultChestCell() {
            return fixtures.vaultChestCell();
        }

        @Override
        public int bankerCell() {
            return fixtures.bankerCell();
        }

        @Override
        public BankQueue bankQueue() {
            return fixtures.bankQueue();
        }

        @Override
        public PrisonCellRegistry prisonCells() {
            return fixtures.prisonCells();
        }

        @Override
        public FoodMarket foodMarket() {
            return fixtures.foodMarket();
        }

        @Override
        public void recordFoodMinted(int n) {
            foodMinted += n;
        }

        @Override
        public void recordFoodEaten(int n) {
            foodEaten += n;
        }
    }

    /**
     * The per-system occupancy view handed to movers: {@code occupantsAt} reads the shared index
     * and {@code onEnter} keeps it live (remove the vacated cell, add the entered one). Stateless
     * over {@link #occupancy}, so one instance is reused across ticks.
     */
    private final Actor.OccupancyQuery OCCUPANCY_VIEW = new Actor.OccupancyQuery() {
        @Override
        public int occupantsAt(int cell) {
            return occupancy.count(cell);
        }

        @Override
        public void onEnter(int fromCell, int toCell) {
            occupancy.remove(fromCell);
            occupancy.add(toCell);
        }
    };
}
