package com.trojia.sim.actor;

/** Shared minimal, hand-built (non-raws) fixtures for actor-package unit tests. */
final class ActorTestFixtures {

    private ActorTestFixtures() {
    }

    /** A complete, valid {@link ActorTypeStats} for {@code typeId}, deference on, all bands populated. */
    static ActorTypeStats stats(ActorTypeId typeId) {
        return statsWithDefer(typeId, true);
    }

    static ActorTypeStats statsWithDefer(ActorTypeId typeId, boolean hasDefer) {
        return statsWithSpeedAndLeash(typeId, hasDefer, 1, 24);
    }

    /**
     * Like {@link #statsWithDefer}, but with a caller-chosen faction — the one field the
     * literacy gate reads ({@code SpellVerb.isLiterate} excludes {@code animals} and
     * {@code feral}), so a test can bake a body that genuinely cannot use a library.
     */
    static ActorTypeStats statsWithFaction(ActorTypeId typeId, String factionId) {
        return statsWithSpeedAndLeash(typeId, true, 1, 24, factionId);
    }

    /** Like {@link #statsWithDefer}, but with a caller-chosen speed/leash for movement tests. */
    static ActorTypeStats statsWithSpeedAndLeash(ActorTypeId typeId, boolean hasDefer,
            int speedTicksPerStep, int leashRadius) {
        return statsWithSpeedAndLeash(typeId, hasDefer, speedTicksPerStep, leashRadius, "test");
    }

    private static ActorTypeStats statsWithSpeedAndLeash(ActorTypeId typeId, boolean hasDefer,
            int speedTicksPerStep, int leashRadius, String factionId) {
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 700, 0, 250, 500);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 550, 0, 250, 500);
        needs[Need.COIN.ordinal()] = new NeedConfig(8000, 200, 0, 100, 200);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 2, 400, 800);
        needs[Need.DUTY.ordinal()] = new NeedConfig(9000, 900, 0, 300, 400);
        return new ActorTypeStats(typeId, "Test " + typeId, 'X', 0xFFFFFF, factionId,
                (short) 20, speedTicksPerStep, leashRadius, 4, needs,
                hasDefer, 980, 6, 940, 305, 305, 80, 12000, 24000, 10);
    }
}
