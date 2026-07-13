package com.trojia.sim.actor.job;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorContext;

/**
 * The Job taxonomy — DECISIONS.md "Goals & Jobs" ruling, ACTORS-SPEC.md §10.
 * Nested static classes give type-safe signatures: a method can demand
 * {@code Job.Villain} and the compiler enforces it. Every class in the
 * taxonomy is nested here (one file, the taxonomy is the table of contents,
 * §10.2) — depth {@code Job -> family -> leaf} (depth 3, a SEPARATE guard from
 * the Actor hierarchy's depth-2 rule, §10.9 friction 3).
 *
 * <p>Jobs are stateless singletons: one bound instance per leaf class,
 * immutable {@link JobParams} injected by {@link JobBinder} from
 * {@code jobs.json}, zero mutable fields. All per-actor goal state
 * ({@code jobOrdinal}, {@code goalState}, target, progress, cooldown) lives on
 * {@link Actor} — the base grows, jobs don't (§10.1).
 *
 * <p>Relationship (ruled): Actor subtype = what you ARE; Job = WHY you're on
 * screen. Add-a-job walkthrough (§10.2): one new nested leaf class + one
 * {@code jobs.json} entry — no engine change, no new verbs.
 */
public sealed abstract class Job {

    private final JobId id;
    private final JobParams params;

    Job(JobId id, JobParams params) {
        this.id = id;
        this.params = params;
    }

    /** This job's dotted, bound identity — must equal its raws {@code id}. */
    public final JobId id() {
        return id;
    }

    /** The raws-bound, immutable goal parameters (never mutated after bind). */
    public final JobParams params() {
        return params;
    }

    /** The goal kind this job grants (legibility only, §10.5). */
    public final GoalKind goalKind() {
        return params.goalKind();
    }

    /** SELECT step (§10.1): pick a target deterministically; may draw named RNG. */
    public abstract void selectTarget(Actor self, ActorContext ctx);

    /** PURSUE step (§10.1): one tick of behavior via shared verbs only. */
    public abstract void pursue(Actor self, ActorContext ctx);

    /** COMPLETE check (§10.1): pure. */
    public abstract boolean isComplete(Actor self, ActorContext ctx);

    // ================== taxonomy (families abstract, leaves final) ==================

    /** The Docks laboring commoner (ACTORS-SPEC.md §4.2). */
    public static sealed abstract class Serf extends Job {

        Serf(JobId id, JobParams params) {
            super(id, params);
        }

        /** Tends a garden plot cycle (placeholder default texture, §10.6). */
        public static final class Farmer extends Serf {
            public static final JobId ID = JobId.of("serf.farmer");

            public Farmer(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }

        /** Haul/gut/porter quota at the work anchor — the civic default for Serf. */
        public static final class Laborer extends Serf {
            public static final JobId ID = JobId.of("serf.laborer");

            public Laborer(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /** Beggars/urchins/petty-thief texture (ACTORS-SPEC.md §4.3). */
    public static sealed abstract class Wastrel extends Job {

        Wastrel(JobId id, JobParams params) {
            super(id, params);
        }

        /** Beg circuit + scavenge sweep — the civic default for Wastrel. */
        public static final class Streetlife extends Wastrel {
            public static final JobId ID = JobId.of("wastrel.streetlife");

            public Streetlife(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /**
     * Secret jobs riding a host civic type (ACTORS-SPEC.md §10.2, §10.4): the
     * type-safe signature "EVERY villain job has a cover".
     */
    public static sealed abstract class Villain extends Job {

        private final CoverSpec cover;

        Villain(JobId id, JobParams params, CoverSpec cover) {
            super(id, params);
            this.cover = cover;
        }

        /** The presented cover this secret job rides — every Villain job has one. */
        public final CoverSpec cover() {
            return cover;
        }

        /** Waylays travelers on a route (bound, weight 0 in MVP civic tables). */
        public static final class Robber extends Villain {
            public static final JobId ID = JobId.of("villain.robber");

            public Robber(JobParams params, CoverSpec cover) {
                super(ID, params, cover);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }

        /** Lifts purses; presents as a Wastrel (§10.4's ~1-in-10 civic texture). */
        public static final class Cutpurse extends Villain {
            public static final JobId ID = JobId.of("villain.cutpurse");

            public Cutpurse(JobParams params, CoverSpec cover) {
                super(ID, params, cover);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }

        /**
         * Granadad's compound burglar/assassin (DECISIONS.md "Trojian housing:
         * Compounds"): breaks in through a compound ceiling and flees across the
         * rooftop-slum layer, so it presents as an ordinary rooftop tenant (a
         * Wastrel on {@code wastrel.streetlife} cover). Same shared pursue-at-anchor
         * cycle as the other MVP villains — its {@code BURGLE_ROOST} goal kind is
         * legibility only in this foundation milestone; a rooftop-scoped waylay/lift
         * behavior is a later extension that swaps this leaf's delegate without
         * touching the taxonomy.
         */
        public static final class Skyrunner extends Villain {
            public static final JobId ID = JobId.of("villain.skyrunner");

            public Skyrunner(JobParams params, CoverSpec cover) {
                super(ID, params, cover);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /**
     * The Wielder's job — the PC seam (ACTORS-SPEC.md §10.4). Reserved for
     * Gabri/Play-mode {@code actAs()}; appears in no civic {@code assign}/
     * {@code defaultFor} table, but is bound like every other job (the 1:1
     * rule has no exceptions). Never auto-pursues: the player drives it.
     */
    public static final class FlameOfMerc extends Job {
        public static final JobId ID = JobId.of("flame_of_merc");

        public FlameOfMerc(JobParams params) {
            super(ID, params);
        }

        @Override
        public void selectTarget(Actor self, ActorContext ctx) {
            // PC seam: no autonomous target selection.
        }

        @Override
        public void pursue(Actor self, ActorContext ctx) {
            // PC seam: no autonomous pursuit: the player drives this job.
        }

        @Override
        public boolean isComplete(Actor self, ActorContext ctx) {
            return false;
        }
    }

    /** The law pillar's face (ACTORS-SPEC.md §4.1). */
    public static sealed abstract class Watch extends Job {

        Watch(JobId id, JobParams params) {
            super(id, params);
        }

        /** Walks the waypoint loop — the civic default for Militia Watch. */
        public static final class Patrol extends Watch {
            public static final JobId ID = JobId.of("watch.patrol");

            public Patrol(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /** Priest/Disciple of the Flame (ACTORS-SPEC.md §4.4-4.5). */
    public static sealed abstract class Clergy extends Job {

        Clergy(JobId id, JobParams params) {
            super(id, params);
        }

        /** Alms-station cycle — the civic default for Priest of the Flame. */
        public static final class Shepherd extends Clergy {
            public static final JobId ID = JobId.of("clergy.shepherd");

            public Shepherd(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }

        /** Fetch-and-carry runs for the assigned Priest — the civic default for Disciple. */
        public static final class Acolyte extends Clergy {
            public static final JobId ID = JobId.of("clergy.acolyte");

            public Acolyte(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /** Stall/shop keepers of the Docks set (ACTORS-SPEC.md §4.6). */
    public static sealed abstract class Trade extends Job {

        Trade(JobId id, JobParams params) {
            super(id, params);
        }

        /** Open/vend/restock/shutter cycle — the civic default for Shopkeeper. */
        public static final class Stallkeep extends Trade {
            public static final JobId ID = JobId.of("trade.stallkeep");

            public Stallkeep(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /** Owns/works animals (ACTORS-SPEC.md §4.7). */
    public static sealed abstract class Husbandry extends Job {

        Husbandry(JobId id, JobParams params) {
            super(id, params);
        }

        /** Tend/feed/work the owned beasts — the civic default for Animal Keeper. */
        public static final class Keeper extends Husbandry {
            public static final JobId ID = JobId.of("husbandry.keeper");

            public Keeper(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }

    /** Owned Animal and ownerless Feral (ACTORS-SPEC.md §4.8-4.9). */
    public static sealed abstract class Beast extends Job {

        Beast(JobId id, JobParams params) {
            super(id, params);
        }

        /**
         * Degenerate goal (graze/stay-near-owner) — satisfies the
         * every-actor-has-a-job invariant without pretending animals have
         * ambitions (invention, §10.6) — the civic default for Animal.
         */
        public static final class Chattel extends Beast {
            public static final JobId ID = JobId.of("beast.chattel");

            public Chattel(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }

        /**
         * Degenerate scavenge-circuit goal — the every-actor invariant holds
         * even for a rat (invention, Q7 addendum §4.9) — the civic default
         * for Feral.
         */
        public static final class Feral extends Beast {
            public static final JobId ID = JobId.of("beast.feral");

            public Feral(JobParams params) {
                super(ID, params);
            }

            @Override
            public void selectTarget(Actor self, ActorContext ctx) {
                JobBehaviors.selectAnchorTarget(self, ctx);
            }

            @Override
            public void pursue(Actor self, ActorContext ctx) {
                JobBehaviors.pursueAtAnchor(self, ctx, params());
            }

            @Override
            public boolean isComplete(Actor self, ActorContext ctx) {
                return JobBehaviors.isCompleteAtUnits(self, params());
            }
        }
    }
}
