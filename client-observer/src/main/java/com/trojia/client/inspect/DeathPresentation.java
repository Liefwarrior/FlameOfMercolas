package com.trojia.client.inspect;

import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.StatusBit;

/**
 * The one shared "how does the client read a corpse" rule (Sprint 6 death, Eli's bug 7):
 * every presentation surface — sheet, nameplate, corpse rendering, verb targeting — asks
 * here whether a soul draws as dead, so the surfaces can never disagree. Also owns the
 * general status-bits-to-words decode (the sheet used to print raw hex — {@code status:
 * 0x100} told an observer nothing). GL-free, pure reads.
 */
public final class DeathPresentation {

    /** Status-bit names in bit order (mirrors {@link StatusBit}'s constants, bit 0 up). */
    private static final String[] BIT_NAMES = {
            "ON_FIRE", "WET", "DOWNED", "ORPHANED", "PANICKED", "ALERTED", "HELD",
            "MAIMED", "EXECUTED", "PLAYER_CONTROLLED", "MOVE_ALONG", "HOUSE_ARREST",
            "DEAD",
    };

    private DeathPresentation() {
    }

    /**
     * Whether {@code actor} presents as a corpse: {@link StatusBit#DEAD} (Sprint 6 —
     * hangings and terminal starvation both set it), or the legacy pre-death
     * {@link StatusBit#EXECUTED} alone (an old save's gibbeted body is still a body).
     */
    public static boolean isDead(Actor actor) {
        return actor.hasStatus(StatusBit.DEAD) || actor.hasStatus(StatusBit.EXECUTED);
    }

    /**
     * The status bits as words — {@code "DEAD, DOWNED, EXECUTED"}, or {@code "(none)"} —
     * replacing the sheet's old raw-hex print. Bit-ascending order (deterministic);
     * unknown high bits (a future append this table lags) print as {@code bit<N>} rather
     * than vanishing.
     */
    public static String statusWords(short bits) {
        if (bits == 0) {
            return "(none)";
        }
        StringBuilder out = new StringBuilder();
        for (int bit = 0; bit < 16; bit++) {
            if ((bits & (1 << bit)) == 0) {
                continue;
            }
            if (out.length() > 0) {
                out.append(", ");
            }
            out.append(bit < BIT_NAMES.length ? BIT_NAMES[bit] : ("bit" + bit));
        }
        return out.toString();
    }
}
