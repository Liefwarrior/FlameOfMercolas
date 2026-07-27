package com.trojia.client.inspect;

import com.trojia.client.scenario.CompoundBlockPopulation;
import com.trojia.sim.actor.Actor;
import com.trojia.sim.actor.Need;
import com.trojia.sim.actor.StatusBit;

import org.junit.jupiter.api.Test;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint 6 motivation legibility: the clergy FAITH relabel (promised in {@code Need}'s
 * §3.1 javadoc since the vector shipped, unimplemented until now), the depleted-need
 * warning mark, and the district pulse line — the HUD-level census that makes a
 * district-wide DUTY collapse visible without clicking 638 souls. Headless, no GL.
 */
class MotivationLegibilityTest {

    private final CompoundBlockPopulation p = CompoundBlockPopulation.build(1234L);

    @Test
    void clergyDutyReadsFaithEveryoneElseKeepsDuty() {
        int duty = Need.DUTY.ordinal();
        assertEquals("FAITH", CharacterSheetText.needLabel(duty, "priest_of_the_flame"));
        assertEquals("FAITH", CharacterSheetText.needLabel(duty, "disciple_of_the_flame"));
        assertEquals("DUTY", CharacterSheetText.needLabel(duty, "serf"));
        assertEquals("HUNGER",
                CharacterSheetText.needLabel(Need.HUNGER.ordinal(), "priest_of_the_flame"));
    }

    @Test
    void aBottomedNeedCarriesTheWarningMark() {
        int duty = Need.DUTY.ordinal();
        assertEquals("DUTY !", CharacterSheetText.needRowLabel(duty, "serf", (short) 0));
        assertEquals("FAITH !",
                CharacterSheetText.needRowLabel(duty, "priest_of_the_flame", (short) 0));
        assertEquals("DUTY", CharacterSheetText.needRowLabel(duty, "serf", (short) 1));
    }

    @Test
    void districtPulseCountsTheLivingCensus() {
        String baseline = DistrictPulse.line(p.registry());
        assertTrue(baseline.startsWith("pulse: " + p.registry().size() + " souls"), baseline);
        int dutyOutBefore = count(baseline, "duty-out");
        int deadBefore = count(baseline, "dead");
        int heldBefore = count(baseline, "held");

        Actor soul = p.registry().get(2);
        Actor corpse = p.registry().get(3);
        Actor captive = p.registry().get(4);
        try {
            soul.applyNeedDelta(Need.DUTY, -20_000);
            corpse.setStatus(StatusBit.DEAD, true);
            captive.setStatus(StatusBit.HELD, true);
            String line = DistrictPulse.line(p.registry());
            assertEquals(dutyOutBefore + 1, count(line, "duty-out"), line);
            assertEquals(deadBefore + 1, count(line, "dead"), line);
            assertEquals(heldBefore + 1, count(line, "held"), line);

            // The dead leave every living census: a DEAD soul's bottomed DUTY stops
            // counting the moment it dies.
            soul.setStatus(StatusBit.DEAD, true);
            String afterDeath = DistrictPulse.line(p.registry());
            assertEquals(dutyOutBefore, count(afterDeath, "duty-out"), afterDeath);
            assertEquals(deadBefore + 2, count(afterDeath, "dead"), afterDeath);
        } finally {
            soul.setStatus(StatusBit.DEAD, false);
            soul.applyNeedDelta(Need.DUTY, 20_000);
            corpse.setStatus(StatusBit.DEAD, false);
            captive.setStatus(StatusBit.HELD, false);
        }
    }

    /** The integer following {@code token} in the pulse line. */
    private static int count(String line, String token) {
        Matcher m = Pattern.compile(Pattern.quote(token) + " (\\d+)").matcher(line);
        assertTrue(m.find(), "no '" + token + "' in: " + line);
        return Integer.parseInt(m.group(1));
    }
}
