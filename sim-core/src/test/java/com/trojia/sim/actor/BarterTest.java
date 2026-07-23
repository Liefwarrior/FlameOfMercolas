package com.trojia.sim.actor;

import com.trojia.sim.actor.faction.FactionRawsLoader;
import com.trojia.sim.actor.faction.FactionRegistry;
import com.trojia.sim.actor.type.Serf;
import com.trojia.sim.actor.type.Shopkeeper;
import com.trojia.sim.progression.SkillRawsLoader;
import com.trojia.sim.world.PackedPos;

import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Sprint-2 disposition-gated barter (the DoD fixtures): the flat FOOD_PRICE is replaced by
 * a price that reads the buyer's PRESENTED standings (merchants discount, watch surcharge,
 * hostile refusal), the counter relationship, and the true body's streetwise haggling —
 * including the disguise flip: the same body pays the PRESENTED identity's price.
 */
final class BarterTest {

    private static final FactionRegistry FACTIONS = FactionRawsLoader.load(committedRawsRoot());

    private static Path committedRawsRoot() {
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("content").resolve("raws");
            if (Files.isDirectory(candidate)) {
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new IllegalStateException("content/raws not found above "
                + Path.of("").toAbsolutePath());
    }

    /** Ctx double with a WIRED standing ledger (and optionally wired tracks). */
    private static final class BarterContext extends NoOpActorContext {
        private final FactionStandings standings;
        private final SkillTrackRegistry tracks;

        BarterContext(ActorRegistry registry, FactionStandings standings,
                SkillTrackRegistry tracks) {
            super(registry);
            this.standings = standings;
            this.tracks = tracks;
        }

        @Override
        public FactionStandings factionStandings() {
            return standings;
        }

        @Override
        public SkillTrackRegistry skillTracks() {
            return tracks;
        }
    }

    private record Stage(ActorRegistry registry, BarterContext ctx, Actor buyer, Actor shop) {
    }

    private static Stage stage(SkillTrackRegistry tracks) {
        ActorRegistry registry = new ActorRegistry();
        BarterContext ctx = new BarterContext(registry,
                new FactionStandings(FACTIONS), tracks);
        Actor buyer = registry.spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(10, 10, 11));
        Actor shop = registry.spawn(Shopkeeper.TYPE, ActorTestFixtures.stats(Shopkeeper.TYPE),
                PackedPos.pack(12, 10, 11));
        return new Stage(registry, ctx, buyer, shop);
    }

    @Test
    void aCleanStrangerPaysExactlyThePreSprintFlatPrice() {
        Stage s = stage(SkillTrackRegistry.UNWIRED);
        Barter.Quote quote = Barter.quoteFor(s.buyer(), s.ctx());
        assertEquals(FoodEconomy.FOOD_PRICE,
                Barter.priceAt(quote, s.shop().id(), s.ctx().relationships()),
                "neutral standings, no ties, no haggle -> the S1 baseline price, exactly");
        assertEquals(FoodEconomy.FOOD_PRICE, Barter.floorPriceFor(quote),
                "the broke threshold stays the S1 baseline for a neutral buyer");
    }

    @Test
    void merchantsStandingDiscountsAndTheWatchSurchargeRaises() {
        Stage s = stage(SkillTrackRegistry.UNWIRED);
        int buyer = s.buyer().identity().presentedId();
        s.ctx().factionStandings().adjust(buyer, FACTIONS.rawId("merchants"), 100);
        assertEquals(1, Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships()), "a +100 regular pays near cost (5 - 4)");

        // The crime-spree DoD: an arrest + a fine (-30 watch) visibly raises the SAME
        // actor's price at the SAME counter.
        s.ctx().factionStandings().adjust(buyer, FACTIONS.rawId("merchants"), -100);
        long before = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        s.ctx().factionStandings().onArrest(buyer);
        s.ctx().factionStandings().onFine(buyer);
        long after = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        assertEquals(5, before);
        assertEquals(6, after, "watch -30 -> +1 surcharge: the streets now price the record");
        assertTrue(after > before, "a crime spree must cost at the counter too");
    }

    @Test
    void hostileWatchStandingIsRefusedEverywhereAndFallsToTheScavengeChain() {
        Stage s = stage(SkillTrackRegistry.UNWIRED);
        int buyer = s.buyer().identity().presentedId();
        s.ctx().factionStandings().adjust(buyer, FACTIONS.rawId("watch"),
                Barter.REFUSAL_WATCH_STANDING);
        Barter.Quote quote = Barter.quoteFor(s.buyer(), s.ctx());
        assertTrue(quote.refusedEverywhere());
        assertEquals(Barter.REFUSED,
                Barter.priceAt(quote, s.shop().id(), s.ctx().relationships()));
        assertEquals(Long.MAX_VALUE, Barter.floorPriceFor(quote),
                "refused everywhere reads as broke -> the commons/scavenge chain opens");
    }

    @Test
    void kinAndFriendsGetTheKitchenPriceAtTheirOwnCounterOnly() {
        Stage s = stage(SkillTrackRegistry.UNWIRED);
        s.ctx().relationships().addSymmetric(s.buyer().id(), s.shop().id(),
                RelationshipKind.FRIEND);
        Barter.Quote quote = Barter.quoteFor(s.buyer(), s.ctx());
        assertEquals(FoodEconomy.FOOD_PRICE - 1,
                Barter.priceAt(quote, s.shop().id(), s.ctx().relationships()));
        // A third, unrelated counter charges the plain personal price.
        Actor otherShop = s.registry().spawn(Shopkeeper.TYPE,
                ActorTestFixtures.stats(Shopkeeper.TYPE), PackedPos.pack(20, 10, 11));
        assertEquals(FoodEconomy.FOOD_PRICE,
                Barter.priceAt(quote, otherShop.id(), s.ctx().relationships()));
    }

    @Test
    void streetwiseHagglingLowersThePriceForTheTrueBody() {
        SkillTrackRegistry tracks = new SkillTrackRegistry(SkillRawsLoader.load(committedRawsRoot()));
        Stage s = stage(tracks);
        long before = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        pumpSkill(tracks, s.buyer().id(), tracks.streetwiseRaw(), 25);
        long after = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        assertEquals(FoodEconomy.FOOD_PRICE, before);
        assertEquals(FoodEconomy.FOOD_PRICE - 1, after,
                "streetwise 25 haggles one Royal off (the raws' black-market-pricing cover)");
    }

    @Test
    void aDisguisedBuyerPaysThePresentedIdentitysPrice() {
        Stage s = stage(SkillTrackRegistry.UNWIRED);
        // A notorious second citizen: watch -40 -> +2 surcharge.
        Actor notorious = s.registry().spawn(Serf.TYPE, ActorTestFixtures.stats(Serf.TYPE),
                PackedPos.pack(30, 10, 11));
        s.ctx().factionStandings().adjust(notorious.identity().presentedId(),
                FACTIONS.rawId("watch"), -40);

        long ownPrice = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        s.buyer().setActAs(notorious.id());
        long disguisedPrice = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());
        s.buyer().setActAs(s.buyer().id());
        long droppedPrice = Barter.priceAt(Barter.quoteFor(s.buyer(), s.ctx()), s.shop().id(),
                s.ctx().relationships());

        assertEquals(5, ownPrice);
        assertEquals(7, disguisedPrice,
                "the counter prices the FACE presented, not the body (the Persona rule)");
        assertEquals(5, droppedPrice, "dropping the disguise restores the own price");
    }

    /** Awards use-XP with fresh contexts until the skill reaches {@code targetLevel}. */
    private static void pumpSkill(SkillTrackRegistry tracks, int actorId, int skillRaw,
            int targetLevel) {
        long context = 1_000;
        long tick = 1;
        while (tracks.level(actorId, skillRaw) < targetLevel) {
            tracks.award(actorId, skillRaw, 10_000, context++, tick++);
            if (context > 100_000) {
                throw new IllegalStateException("skill pump did not converge");
            }
        }
    }
}
