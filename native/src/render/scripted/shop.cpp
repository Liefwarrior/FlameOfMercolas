// Scripted capture drivers: shop.
#include "scripted.hpp"

namespace granadad::render {

namespace {

/// SHOPS. Puts the body before `notableId` while they are working their
/// counter: the clock is walked an hour at a time until their day has them
/// at their post and some stand tile answers them by the talk key's own rule,
/// then the body is PLACED there (runStreetLine's one placement) facing them,
/// and the real interact opens the talk. True when it opened on them.
bool talkToKeeperAtCounter(Session& session, std::string_view notableId) {
    for (int hourStep = 0; hourStep < 24; ++hourStep) {
        if (hourStep > 0) {
            session.skipToHour((session.timeOfDay() / 3600 + 1) % 24);
            session.stepMany(sim::MoveInput{}, 1);
        }
        const sim::WardActor* keeper = nullptr;
        for (const sim::WardActor& actor : session.people().actors()) {
            if (actor.visible() && session.people().identity(actor.id).notableId == notableId) {
                keeper = &actor;
                break;
            }
        }
        if (keeper == nullptr || !session.wardAtCounterNow(*keeper)) {
            continue;
        }
        for (std::int32_t oy = -kStreetReachTiles; oy <= kStreetReachTiles; ++oy) {
            for (std::int32_t ox = -kStreetReachTiles; ox <= kStreetReachTiles; ++ox) {
                const std::int32_t sx = keeper->x + ox;
                const std::int32_t sy = keeper->y + oy;
                if ((ox == 0 && oy == 0) || !session.tiles().standable(sx, sy, keeper->band)) {
                    continue;
                }
                const sim::Angle look = cardinalToward(keeper->x - sx, keeper->y - sy);
                const sim::WardActor* addressed = addresseeFrom(session, sx, sy, keeper->band, look);
                if (addressed == nullptr || addressed->id != keeper->id) {
                    continue;
                }
                const std::int32_t keeperId = keeper->id;
                session.placeBodyAt(sx, sy, keeper->band);
                session.body().setYaw(look);
                session.stepMany(sim::MoveInput{}, 1);
                session.interact();
                return session.talking() && session.wardTalkingTo() == keeperId;
            }
        }
    }
    return false;
}

/// SHOPS. Chooses the first topic of `kind` (and `arg`, when given) on the open
/// list, the way a number key would. True when one was there.
bool chooseFirst(Session& session, sim::TopicKind kind, std::string_view arg = {}) {
    const std::vector<sim::Topic>& topics = session.tavern().dialogue().topics();
    for (std::size_t i = 0; i < topics.size(); ++i) {
        if (topics[i].kind == kind && (arg.empty() || topics[i].arg == arg)) {
            session.chooseTopic(i);
            return true;
        }
    }
    return false;
}

}  // namespace

ShopLineResult runShopLine(Session& session, const std::string& end) {
    ShopLineResult out;
    constexpr std::string_view kWant = "lantern";
    const std::int32_t lantern = session.tavern().items().indexOf(kWant);
    if (lantern < 0) {
        return out;
    }
    out.coinStart = session.tavern().playerCoin();

    // 1. BRANN, AT HIS COUNTER, AND HIS SHELF.
    out.chandler = talkToKeeperAtCounter(session, "brann");
    if (!out.chandler) {
        return out;
    }
    const std::int32_t brannId = session.wardTalkingTo();
    out.wares = chooseFirst(session, sim::TopicKind::Wares);
    std::int32_t price = 0;
    for (const sim::Topic& topic : session.tavern().dialogue().topics()) {
        if (topic.kind == sim::TopicKind::BuyItem && topic.arg == kWant) {
            price = topic.payload;
        }
    }
    out.wares = out.wares && price > 0;
    out.price = price;
    if (!out.wares || end == "wares") {
        return out;
    }

    // 2. THE PURSE -- capture plumbing, said out loud in the summary: a fresh
    // character's purse is whatever the biography gave, and this line is about
    // the counter, not the biography.
    if (session.tavern().playerCoin() < price) {
        out.granted = price - session.tavern().playerCoin();
        session.tavern().setPlayerCoin(price);
    }

    // 3. BUY IT. The Kit, the purse and Brann's purse, before and after.
    const std::int32_t carried = session.tavern().kit().count(lantern);
    const std::int32_t coin = session.tavern().playerCoin();
    const std::int32_t brannCoin = session.people().byId(brannId)->coin;
    (void)chooseFirst(session, sim::TopicKind::BuyItem, kWant);
    out.boughtLine = session.lastMessage();
    out.bought = session.tavern().kit().count(lantern) == carried + 1 &&
                 session.tavern().playerCoin() == coin - price &&
                 session.people().byId(brannId)->coin == brannCoin + price;
    if (!out.bought || end == "bought") {
        session.closeConversation();
        out.coinEnd = session.tavern().playerCoin();
        return out;
    }
    session.closeConversation();

    // 4. FENNER, AT HIS PAWN, AND WHAT HE WOULD TAKE.
    out.pawnbroker = talkToKeeperAtCounter(session, "fenner");
    if (!out.pawnbroker) {
        out.coinEnd = session.tavern().playerCoin();
        return out;
    }
    const std::int32_t fennerId = session.wardTalkingTo();
    (void)chooseFirst(session, sim::TopicKind::Pawn);
    std::int32_t offer = 0;
    for (const sim::Topic& topic : session.tavern().dialogue().topics()) {
        if (topic.kind == sim::TopicKind::SellItem && topic.arg == kWant) {
            offer = topic.payload;
        }
    }
    out.pawnList = offer > 0;
    out.offer = offer;
    if (!out.pawnList || end == "pawn") {
        out.coinEnd = session.tavern().playerCoin();
        return out;
    }

    // 5. SELL IT BACK.
    const std::int32_t held = session.tavern().kit().count(lantern);
    const std::int32_t before = session.tavern().playerCoin();
    const std::int32_t fennerCoin = session.people().byId(fennerId)->coin;
    (void)chooseFirst(session, sim::TopicKind::SellItem, kWant);
    out.soldLine = session.lastMessage();
    out.sold = session.tavern().kit().count(lantern) == held - 1 &&
               session.tavern().playerCoin() == before + offer &&
               session.people().byId(fennerId)->coin == fennerCoin - offer;
    out.coinEnd = session.tavern().playerCoin();
    // Left talking: Fenner's list, one lantern lighter, is the shutter.
    return out;
}

}  // namespace granadad::render
