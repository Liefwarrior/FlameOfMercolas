package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorRawsValidationException;
import com.trojia.sim.actor.ActorTypeId;
import com.trojia.sim.progression.SkillRegistry;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The startup binder (ACTORS-SPEC.md §10.2, §10.3): matches every registered
 * {@link Job} leaf ({@link Jobs#ALL}) against {@code jobs.json} entries
 * <strong>1:1, fail-fast in both directions</strong> — an unbound leaf class
 * and an unmatched json entry are both boot errors, never silent defaults.
 * Also enforces the every-actor-has-a-job invariant (§10.3 item 8) and the
 * secret/cover shape rule (§10.3 item 9).
 */
public final class JobBinder {

    private JobBinder() {
    }

    /**
     * Binds {@code jobsJsonFile} against {@link Jobs#ALL}, cross-checked
     * against {@code knownActorTypes} (every civic actor type the caller has
     * registered) — the legacy skill-less overload: every {@code trainsSkill}
     * key stays UNRESOLVED ({@link JobParams#TRAINS_NOTHING}, training
     * dormant), matching the callers that also leave
     * {@code SkillTrackRegistry.UNWIRED} in place (world-less bootstraps, the
     * compound demo) where awards would no-op anyway.
     *
     * @throws ActorRawsValidationException on any 1:1, shape or coverage violation
     */
    public static JobRegistry bind(Path jobsJsonFile, List<ActorTypeId> knownActorTypes) {
        return bind(jobsJsonFile, knownActorTypes, null);
    }

    /**
     * Binds {@code jobsJsonFile} with the Sprint-5 training pair RESOLVED against the
     * boot-built skill universe: each {@code trainsSkill} key becomes a raw registry index on
     * the bound {@link JobParams}, and an unknown key is a LOUD bind failure (the frame-guard
     * discipline — a jobs.json/skills.json drift never ships as a silent no-train).
     *
     * @param skills the boot-built skill universe; {@code null} degrades to the legacy
     *               skill-less bind (training dormant)
     * @throws ActorRawsValidationException on any 1:1/shape/coverage violation or an
     *                                      unresolvable {@code trainsSkill} key
     */
    public static JobRegistry bind(Path jobsJsonFile, List<ActorTypeId> knownActorTypes,
            SkillRegistry skills) {
        List<JobRaw> raws = JobRawsLoader.load(jobsJsonFile);
        return bind(raws, knownActorTypes, skills);
    }

    static JobRegistry bind(List<JobRaw> raws, List<ActorTypeId> knownActorTypes) {
        return bind(raws, knownActorTypes, null);
    }

    static JobRegistry bind(List<JobRaw> raws, List<ActorTypeId> knownActorTypes,
            SkillRegistry skills) {
        String file = raws.isEmpty() ? "jobs.json" : raws.get(0).file();

        // Direction 1: no duplicate ids.
        for (int i = 0; i < raws.size(); i++) {
            for (int j = i + 1; j < raws.size(); j++) {
                if (raws.get(i).id().equals(raws.get(j).id())) {
                    throw new ActorRawsValidationException(file, "id",
                            "duplicate job id \"" + raws.get(i).id() + "\"");
                }
            }
        }

        // Direction 2: every json id must match a registered leaf ("unknown class").
        for (JobRaw raw : raws) {
            if (findRegistration(raw.id()) == null) {
                throw new ActorRawsValidationException(raw.file(), "id",
                        "job id \"" + raw.id() + "\" has no registered Job leaf class "
                                + "(Jobs.ALL) — either the id is misspelled or the class "
                                + "was never added");
            }
        }

        // Direction 3: every registered leaf must have a json entry ("missing json").
        for (Jobs.Registration reg : Jobs.ALL) {
            if (findRaw(raws, reg.id()) == null) {
                throw new ActorRawsValidationException(file, "id",
                        "registered Job leaf \"" + reg.id()
                                + "\" has no jobs.json entry — every bound class needs data");
            }
        }

        // Cross-check: assign/defaultFor actor type ids are real.
        for (JobRaw raw : raws) {
            for (JobRaw.AssignWeight aw : raw.assign()) {
                requireKnownType(raw.file(), "assign", aw.actorType(), knownActorTypes);
            }
            for (ActorTypeId type : raw.defaultFor()) {
                requireKnownType(raw.file(), "defaultFor", type, knownActorTypes);
            }
        }

        // Every-actor-has-a-job: each known type in exactly one entry's defaultFor.
        List<JobRegistry.DefaultJob> defaults = new ArrayList<>();
        for (ActorTypeId type : knownActorTypes) {
            JobRaw owner = null;
            for (JobRaw raw : raws) {
                if (raw.defaultFor().contains(type)) {
                    if (owner != null) {
                        throw new ActorRawsValidationException(file, "defaultFor",
                                "actor type \"" + type + "\" is the default for both \""
                                        + owner.id() + "\" and \"" + raw.id() + "\"");
                    }
                    owner = raw;
                }
            }
            if (owner == null) {
                throw new ActorRawsValidationException(file, "defaultFor",
                        "actor type \"" + type + "\" has no default job "
                                + "(every actor type needs exactly one)");
            }
        }

        // Secret shape: secret => cover present & consistent; non-secret => no cover.
        for (JobRaw raw : raws) {
            if (raw.secret()) {
                if (raw.cover() == null) {
                    throw new ActorRawsValidationException(raw.file(), "cover",
                            "secret job \"" + raw.id() + "\" must declare a cover block");
                }
                CoverSpec cover = raw.cover();
                boolean hostAssigned = raw.assign().stream()
                        .anyMatch(aw -> aw.actorType().equals(cover.actorType()));
                if (!hostAssigned) {
                    throw new ActorRawsValidationException(raw.file(), "cover.actorType",
                            "cover actor type \"" + cover.actorType()
                                    + "\" must be named in this job's own assign table");
                }
                JobRaw presented = findRaw(raws, cover.presentedJob());
                if (presented == null) {
                    throw new ActorRawsValidationException(raw.file(), "cover.presentedJob",
                            "cover.presentedJob \"" + cover.presentedJob()
                                    + "\" is not a bound job id");
                }
                if (presented.secret()) {
                    throw new ActorRawsValidationException(raw.file(), "cover.presentedJob",
                            "cover.presentedJob \"" + cover.presentedJob()
                                    + "\" must not itself be secret");
                }
                boolean coverPlausible = presented.assign().stream()
                        .anyMatch(aw -> aw.actorType().equals(cover.actorType()))
                        || presented.defaultFor().contains(cover.actorType());
                if (!coverPlausible) {
                    throw new ActorRawsValidationException(raw.file(), "cover.presentedJob",
                            "cover.presentedJob \"" + cover.presentedJob()
                                    + "\" is not assignable to actor type \""
                                    + cover.actorType() + "\"");
                }
            } else if (raw.cover() != null) {
                throw new ActorRawsValidationException(raw.file(), "cover",
                        "job \"" + raw.id() + "\" declares secret: false but has a cover block");
            }
        }

        // Build: instantiate every leaf from its matched raw, resolving the training pair
        // against the skill universe where one is wired (unknown key = loud bind failure).
        List<Job> jobs = new ArrayList<>(raws.size());
        for (JobRaw raw : raws) {
            Jobs.Registration reg = findRegistration(raw.id());
            int trainSkillRaw = JobParams.TRAINS_NOTHING;
            int trainCp = 0;
            if (raw.trainsSkill() != null && skills != null) {
                if (!skills.contains(raw.trainsSkill())) {
                    throw new ActorRawsValidationException(raw.file(), "trainsSkill",
                            "job \"" + raw.id() + "\" trains unknown skill \""
                                    + raw.trainsSkill() + "\" (no such id in skills.json)");
                }
                trainSkillRaw = skills.id(raw.trainsSkill()).raw();
                trainCp = raw.trainCp();
            }
            JobParams params = new JobParams(raw.goalKind(), raw.priority(), raw.rhythmStart(),
                    raw.rhythmEnd(), raw.rhythmBonus(), raw.workTicksPerUnit(),
                    raw.unitsToComplete(), raw.renewMode(), raw.cooldownTicks(),
                    trainSkillRaw, trainCp);
            jobs.add(reg.factory().create(params, raw.cover()));
        }
        JobRegistry unresolved = JobRegistry.of(jobs, List.of());
        for (ActorTypeId type : knownActorTypes) {
            JobRaw owner = null;
            for (JobRaw raw : raws) {
                if (raw.defaultFor().contains(type)) {
                    owner = raw;
                    break;
                }
            }
            defaults.add(new JobRegistry.DefaultJob(type, unresolved.ordinalOf(owner.id())));
        }
        return JobRegistry.of(jobs, defaults);
    }

    private static void requireKnownType(String file, String field, ActorTypeId type,
            List<ActorTypeId> known) {
        if (!known.contains(type)) {
            throw new ActorRawsValidationException(file, field,
                    "actor type \"" + type + "\" is not a registered actor type");
        }
    }

    private static Jobs.Registration findRegistration(JobId id) {
        for (Jobs.Registration reg : Jobs.ALL) {
            if (reg.id().equals(id)) {
                return reg;
            }
        }
        return null;
    }

    private static JobRaw findRaw(List<JobRaw> raws, JobId id) {
        for (JobRaw raw : raws) {
            if (raw.id().equals(id)) {
                return raw;
            }
        }
        return null;
    }
}
