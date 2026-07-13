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
        NeedConfig[] needs = new NeedConfig[Need.COUNT];
        needs[Need.HUNGER.ordinal()] = new NeedConfig(9000, 700, 0, 250, 500);
        needs[Need.REST.ordinal()] = new NeedConfig(9000, 550, 0, 250, 500);
        needs[Need.COIN.ordinal()] = new NeedConfig(8000, 200, 0, 100, 200);
        needs[Need.SAFETY.ordinal()] = new NeedConfig(10000, 0, 2, 400, 800);
        needs[Need.DUTY.ordinal()] = new NeedConfig(9000, 900, 0, 300, 400);
        return new ActorTypeStats(typeId, "Test " + typeId, 'X', 0xFFFFFF, "test",
                (short) 20, 1, 24, 4, needs,
                hasDefer, 980, 6, 940, 305, 80, 12000, 24000, 10);
    }
}
