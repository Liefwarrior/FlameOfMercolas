package com.trojia.sim.actor;

import com.trojia.sim.actor.type.AnimalActor;
import com.trojia.sim.actor.type.CatActor;
import com.trojia.sim.actor.type.DiscipleOfTheFlame;
import com.trojia.sim.actor.type.FeralActor;
import com.trojia.sim.actor.type.MilitiaWatch;
import com.trojia.sim.actor.type.MouseActor;
import com.trojia.sim.actor.type.PriestOfTheFlame;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.actor.type.AnimalKeeper;
import com.trojia.sim.actor.type.Wastrel;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * The actor type registration list (ACTORS-SPEC.md §1.4's "register the class
 * in {@code ActorTypes}, one line, sorted by type id"). Nine Docks types
 * (§4.1-4.9), each a one-file thin subclass.
 *
 * <p>Add a type (§1.4 walkthrough): one new thin {@link Actor} subclass file +
 * one raws entry + one line here. No engine change.
 */
public final class ActorTypes {

    /** One type's binding: its id and a factory taking the assigned id/stats/spawn cell. */
    public record Registration(ActorTypeId id, ActorFactory factory) {
    }

    @FunctionalInterface
    public interface ActorFactory {
        Actor create(int id, ActorTypeStats stats, int cell);
    }

    public static final List<Registration> ALL = sorted(List.of(
            new Registration(MilitiaWatch.TYPE, MilitiaWatch::new),
            new Registration(Serf.TYPE, Serf::new),
            new Registration(Wastrel.TYPE, Wastrel::new),
            new Registration(PriestOfTheFlame.TYPE, PriestOfTheFlame::new),
            new Registration(DiscipleOfTheFlame.TYPE, DiscipleOfTheFlame::new),
            new Registration(Shopkeeper.TYPE, Shopkeeper::new),
            new Registration(AnimalKeeper.TYPE, AnimalKeeper::new),
            new Registration(AnimalActor.TYPE, AnimalActor::new),
            new Registration(FeralActor.TYPE, FeralActor::new),
            new Registration(CatActor.TYPE, CatActor::new),
            new Registration(MouseActor.TYPE, MouseActor::new)));

    private ActorTypes() {
    }

    public static Registration find(ActorTypeId id) {
        for (Registration reg : ALL) {
            if (reg.id().equals(id)) {
                return reg;
            }
        }
        throw new IllegalArgumentException("no registered actor type: " + id);
    }

    /** Every registered type id, sorted — the "known actor types" list the Job binder cross-checks. */
    public static List<ActorTypeId> allTypeIds() {
        List<ActorTypeId> ids = new ArrayList<>(ALL.size());
        for (Registration reg : ALL) {
            ids.add(reg.id());
        }
        return ids;
    }

    private static List<Registration> sorted(List<Registration> raw) {
        List<Registration> copy = new ArrayList<>(raw);
        copy.sort(Comparator.comparing(r -> r.id().key()));
        return List.copyOf(copy);
    }
}
