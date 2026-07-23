package com.trojia.sim.actor;

import com.trojia.sim.progression.AttributeCalculator;
import com.trojia.sim.progression.AttributeId;
import com.trojia.sim.progression.SkillId;
import com.trojia.sim.progression.SkillLevelledEvent;
import com.trojia.sim.progression.SkillRegistry;
import com.trojia.sim.progression.SkillTrack;
import com.trojia.sim.world.io.WorldHasher;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * The per-actor {@link SkillTrack} side table (Sprint 1 "the character sheet comes alive"):
 * dense, {@code index == ActorId} — exactly the Home/Relationship side-table convention, and
 * exactly the wiring {@code SkillTrack}'s own javadoc prescribed. This is the "connect to
 * Actor" pass: the dependency points from the actor package INTO the progression package,
 * never the reverse.
 *
 * <p><b>Award routing.</b> Sim outcomes that already resolve today call {@link #award} at
 * their resolution point: push contests ({@code PushMechanics} — open_hand for the pusher,
 * grit for the shovee), scavenging ({@code SeekFoodPolicy} — streetwise), and the played
 * actor's rooftop running ({@code PlayerControlPolicy} — skyrunning). Combat skills stay
 * honestly at level 0: no fake XP source exists for them yet. Every
 * {@link SkillLevelledEvent} lands in the {@link SkillLevelLog} ring — the client-readable
 * seam. Attribute recomputation is stateless per PROGRESSION-SPEC &sect;5 ("recomputed
 * live"): {@link #attribute} computes from current levels at read time, so a level-up needs
 * no banked-value invalidation — the recompute IS the read.
 *
 * <p><b>Wiring.</b> {@link #UNWIRED} is the degraded default (world-less bootstrap, test
 * doubles, and every pre-existing {@code ActorsSystem} constructor): awards no-op, every
 * level reads 0, every attribute reads the base 10, and the serialized chunk is an empty
 * frame — so unwired runs are byte-identical to the pre-progression era. The live docks
 * scenario wires a real instance from the committed {@code content/raws/skills/skills.json}.
 *
 * <p><b>Determinism.</b> Tracks materialize lazily on first award, in event order — a pure
 * function of the deterministic event stream, so twin runs materialize identical table
 * shapes. State is the persisted triad (serialize/load/hashInto) inside the
 * {@code ActorsSystem} chunk; the skill count is a framing guard on load (a raws/save
 * mismatch fails loudly instead of desyncing silently).
 */
public final class SkillTrackRegistry {

    /** Ring capacity of the level log — plenty for an observer session's narration. */
    private static final int LEVEL_LOG_CAPACITY = 256;

    /** The degraded no-op instance: no skills, no awards, level 0 / attribute 10 reads. */
    public static final SkillTrackRegistry UNWIRED = new SkillTrackRegistry();

    // Well-known raws keys this sprint's award sites route to (resolved once at wiring).
    static final String KEY_OPEN_HAND = "open_hand";
    static final String KEY_GRIT = "grit";
    static final String KEY_STREETWISE = "streetwise";
    static final String KEY_SKYRUNNING = "skyrunning";

    /** The boot-built skill universe, or {@code null} when unwired. */
    private final SkillRegistry skills;
    /** Dense tracks, {@code index == actorId}; {@code null} slots = never awarded (level 0). */
    private final List<SkillTrack> tracks = new ArrayList<>();
    private final SkillLevelLog levelLog;

    private final int openHand;
    private final int grit;
    private final int streetwise;
    private final int skyrunning;

    private SkillTrackRegistry() {
        this.skills = null;
        this.levelLog = SkillLevelLog.EMPTY;
        this.openHand = Actor.NONE;
        this.grit = Actor.NONE;
        this.streetwise = Actor.NONE;
        this.skyrunning = Actor.NONE;
    }

    /**
     * Wires the live table against a boot-built registry (the committed 16-skill raws in
     * production; any registry in tests). Well-known award skills missing from the registry
     * resolve to "absent" and their award sites degrade to no-ops rather than throwing.
     */
    public SkillTrackRegistry(SkillRegistry skills) {
        this.skills = java.util.Objects.requireNonNull(skills, "skills");
        this.levelLog = new SkillLevelLog(LEVEL_LOG_CAPACITY);
        this.openHand = rawOf(skills, KEY_OPEN_HAND);
        this.grit = rawOf(skills, KEY_GRIT);
        this.streetwise = rawOf(skills, KEY_STREETWISE);
        this.skyrunning = rawOf(skills, KEY_SKYRUNNING);
    }

    private static int rawOf(SkillRegistry skills, String key) {
        return skills.contains(key) ? skills.id(key).raw() : Actor.NONE;
    }

    /** Whether a real skill universe is wired (awards live, levels meaningful). */
    public boolean isWired() {
        return skills != null;
    }

    /** The wired skill universe. {@code null} when {@link #UNWIRED}. */
    public SkillRegistry skills() {
        return skills;
    }

    /** The level-up ring — the client-readable seam. {@link SkillLevelLog#EMPTY} when unwired. */
    public SkillLevelLog levelLog() {
        return levelLog;
    }

    // ---- well-known skill raw ids ({@link Actor#NONE} when unwired/absent) ----

    public int openHandRaw() {
        return openHand;
    }

    public int gritRaw() {
        return grit;
    }

    public int streetwiseRaw() {
        return streetwise;
    }

    public int skyrunningRaw() {
        return skyrunning;
    }

    /**
     * An actor's current level in a skill (by raw registry index), {@code 0..100}. Reads 0
     * when unwired, when the skill is absent ({@link Actor#NONE}), or when the actor has
     * never earned any XP (no track materialized).
     */
    public int level(int actorId, int skillRaw) {
        if (skills == null || skillRaw == Actor.NONE
                || actorId < 0 || actorId >= tracks.size()) {
            return 0;
        }
        SkillTrack track = tracks.get(actorId);
        return track == null ? 0 : track.levelRaw(skillRaw);
    }

    /**
     * An actor's derived attribute (PROGRESSION-SPEC &sect;5), computed LIVE from its
     * current levels via {@link AttributeCalculator#compute} — never banked, so every
     * {@link SkillLevelledEvent} is "recomputed" by construction. The skill-less base 10
     * when unwired or trackless.
     */
    public int attribute(int actorId, AttributeId attributeId) {
        if (skills == null || actorId < 0 || actorId >= tracks.size()
                || tracks.get(actorId) == null) {
            return 10; // the §5 base: 10 + (weightedSum >> 8) with all levels 0
        }
        return AttributeCalculator.compute(attributeId, skills, tracks.get(actorId));
    }

    /**
     * Awards use-XP for one qualifying outcome ({@link SkillTrack#awardXp}) and records
     * every resulting level-up in the {@link #levelLog}. No-op when unwired or the skill is
     * absent — award sites never need to guard. Track materialization is lazy and
     * event-ordered (deterministic).
     *
     * @param actorId    the earning actor (the TRUE doer — XP always lands on the body that
     *                   did the deed, disguised or not; only SOCIAL reads key on presentedId)
     * @param skillRaw   the skill's raw registry index (a well-known id above)
     * @param baseCp     the §3.1 base award in cp
     * @param contextKey the §3.3 satiation context discriminator
     * @param tick       the current simulation tick
     */
    public void award(int actorId, int skillRaw, int baseCp, long contextKey, long tick) {
        if (skills == null || skillRaw == Actor.NONE || actorId < 0) {
            return;
        }
        SkillTrack track = trackOf(actorId);
        List<SkillLevelledEvent> events =
                track.awardXp(SkillId.of(skillRaw), baseCp, contextKey, tick);
        for (int i = 0; i < events.size(); i++) {
            SkillLevelledEvent event = events.get(i);
            levelLog.record(tick, actorId, event.skillRaw(), event.newLevel());
        }
    }

    /** The actor's track, materializing (and padding the dense list) on demand. */
    private SkillTrack trackOf(int actorId) {
        while (tracks.size() <= actorId) {
            tracks.add(null);
        }
        SkillTrack track = tracks.get(actorId);
        if (track == null) {
            track = new SkillTrack(actorId, skills);
            tracks.set(actorId, track);
        }
        return track;
    }

    // ======================================================================
    // The persisted triad (rides the ActorsSystem TROJSAV chunk after ShoveLog)
    // ======================================================================

    /**
     * Serializes the whole table in canonical order: the skill-count frame guard, then the
     * dense track list (a presence byte per slot — {@code null} slots stay {@code null}
     * across the round trip, keeping materialization order irrelevant to the bytes), then
     * the level-log ring.
     */
    public void serialize(DataOutput out) throws IOException {
        out.writeInt(skills == null ? 0 : skills.size());
        out.writeInt(tracks.size());
        for (int i = 0; i < tracks.size(); i++) {
            SkillTrack track = tracks.get(i);
            out.writeBoolean(track != null);
            if (track != null) {
                track.writeTo(out);
            }
        }
        levelLog.serialize(out);
    }

    /**
     * Loads what {@link #serialize} wrote into this fresh table. The wired skill count must
     * match the serialized frame — loading progression state against different skill raws is
     * a config error and fails loudly here rather than desyncing downstream.
     */
    public void load(DataInput in) throws IOException {
        int skillCount = in.readInt();
        int wired = skills == null ? 0 : skills.size();
        if (skillCount != wired) {
            throw new IOException("skill-track frame mismatch: serialized skillCount="
                    + skillCount + " but the loading system wires " + wired
                    + " (same raws must be booted before load)");
        }
        int trackCount = in.readInt();
        for (int i = 0; i < trackCount; i++) {
            if (in.readBoolean()) {
                SkillTrack track = trackOf(i);
                track.readFrom(in);
            } else {
                while (tracks.size() <= i) {
                    tracks.add(null);
                }
            }
        }
        levelLog.load(in);
    }

    /** Hashes the exact state {@link #serialize} writes, in the same canonical order. */
    public void hashInto(WorldHasher.Sink sink) {
        sink.putInt(skills == null ? 0 : skills.size());
        sink.putInt(tracks.size());
        for (int i = 0; i < tracks.size(); i++) {
            SkillTrack track = tracks.get(i);
            sink.putByte(track == null ? 0 : 1);
            if (track != null) {
                track.hashInto(sink);
            }
        }
        levelLog.hashInto(sink);
    }
}
