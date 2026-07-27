package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.ActorRegistry;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.ReasonCode;
import com.trojia.sim.actor.StatusBit;

/**
 * The district-level motivation readout (Sprint 6 legibility — the S6 diagnosis's biggest
 * blind spot: a district-wide DUTY collapse was invisible without clicking 638 souls one
 * at a time). One HUD line, recomputed live each frame from the registry:
 *
 * <pre>pulse: 692 souls  working 385  duty-out 88  starving 31  held 9  confined 86  dead 2</pre>
 *
 * <ul>
 *   <li><b>working</b> — souls whose last reason stamp is {@code JOB_GOAL} (on the job
 *       right now);</li>
 *   <li><b>duty-out</b> / <b>starving</b> — living souls whose DUTY / HUNGER reserve sits
 *       at 0 (each bottomed motivation is a dead scoring band — this line is exactly the
 *       acceptance surface for Eli's bugs 1 and 3);</li>
 *   <li><b>held</b> / <b>confined</b> — in custody / under shove-riot house arrest (the
 *       mid-shift idleness the soak measured);</li>
 *   <li><b>dead</b> — the resident dead (the roster never shrinks).</li>
 * </ul>
 *
 * GL-free, read-only, deterministic ascending scan — cheap at district scale (one pass
 * over ~700 actors per frame).
 */
public final class DistrictPulse {

    private DistrictPulse() {
    }

    /** The one-line district readout (see the class doc). */
    public static String line(ActorRegistry registry) {
        int working = 0;
        int dutyOut = 0;
        int starving = 0;
        int held = 0;
        int confined = 0;
        int dead = 0;
        for (int i = 0; i < registry.size(); i++) {
            Actor actor = registry.get(i);
            if (actor.hasStatus(StatusBit.DEAD) || actor.hasStatus(StatusBit.EXECUTED)) {
                dead++;
                continue; // the dead are inert: they idle in no living census
            }
            if (actor.lastReasonCode() == ReasonCode.JOB_GOAL) {
                working++;
            }
            if (actor.need(Need.DUTY) == 0) {
                dutyOut++;
            }
            if (actor.need(Need.HUNGER) == 0) {
                starving++;
            }
            if (actor.hasStatus(StatusBit.HELD)) {
                held++;
            }
            if (actor.hasStatus(StatusBit.HOUSE_ARREST)) {
                confined++;
            }
        }
        return "pulse: " + registry.size() + " souls  working " + working
                + "  duty-out " + dutyOut + "  starving " + starving
                + "  held " + held + "  confined " + confined + "  dead " + dead;
    }
}
