package com.trojia.sim.actor;

/**
 * One {@link Need}'s raws-authored decay/recovery tuning (ACTORS-SPEC.md §3,
 * §6). {@code decayPerKilotick} drives the exact, drift-free integer decay of
 * §3.2; {@code recoverPerTick} models the event-driven "recovers +N per quiet
 * tick" needs (SAFETY, §3.1) — most needs set it to {@code 0} and rely on
 * decay alone. {@code lowBonus}/{@code critBonus} are carried for the §3.3
 * threshold→policy coupling a later, richer policy library reads; this
 * foundation's five-policy starter set does not yet consume them, but the
 * loader still enforces the band-jump invariant so the raws are correct from
 * day one.
 *
 * @param start             initial reserve, {@code [0, 10000]}
 * @param decayPerKilotick  integer units decayed per 1,000 ticks, {@code >= 0}
 * @param recoverPerTick    integer units recovered per tick, {@code >= 0}
 * @param lowBonus          score bonus once reserve {@code < LOW}
 * @param critBonus         score bonus once reserve {@code < CRITICAL}; must exceed {@code lowBonus}
 */
public record NeedConfig(int start, int decayPerKilotick, int recoverPerTick,
        int lowBonus, int critBonus) {

    public NeedConfig {
        if (start < 0 || start > NeedThresholds.MAX) {
            throw new IllegalArgumentException("start out of [0,10000]: " + start);
        }
        if (decayPerKilotick < 0) {
            throw new IllegalArgumentException("decayPerKilotick must be >= 0: " + decayPerKilotick);
        }
        if (recoverPerTick < 0) {
            throw new IllegalArgumentException("recoverPerTick must be >= 0: " + recoverPerTick);
        }
        if (lowBonus < 0 || critBonus < 0) {
            throw new IllegalArgumentException("bonuses must be >= 0");
        }
        if (critBonus > 0 && lowBonus > 0 && critBonus <= lowBonus) {
            throw new IllegalArgumentException(
                    "critBonus (" + critBonus + ") must exceed lowBonus (" + lowBonus + ")");
        }
    }
}
