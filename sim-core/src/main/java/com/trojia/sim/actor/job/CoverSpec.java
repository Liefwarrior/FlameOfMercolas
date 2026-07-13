package com.trojia.sim.actor.job;

import com.trojia.sim.actor.ActorTypeId;

/**
 * The presented cover for a secret {@link Job.Villain} job (ACTORS-SPEC.md
 * §10.2, §10.4): every villain job carries one. The Actor's PRESENTED job is
 * {@link #presentedJob()}, derived from this spec, never stored — the same
 * single-source-of-truth argument the addendum uses for owned-animal lists
 * and presented identity.
 *
 * @param actorType     the host civic actor type this cover rides
 * @param presentedJob  the non-secret job id the actor presents as holding
 */
public record CoverSpec(ActorTypeId actorType, JobId presentedJob) {
}
