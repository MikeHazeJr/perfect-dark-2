/**
 * connectcode.c -- Encode/decode IP as a memorable 4-part sentence.
 *
 * 4 bytes (IPv4) encoded as a natural sentence:
 *   [adjective] [creature/object] [action phrase] [place]
 *
 * Examples:
 *   "fat vampire running to the park"
 *   "tiny cheese skipping around space"
 *   "heroic flea hiding in a mall"
 *
 * Each slot maps to one byte (256 entries per category).
 * Port is assumed as CONNECT_DEFAULT_PORT (27100).
 * Case-insensitive decode. The decoder strips articles (the, a, an)
 * and matches only the core words.
 */

#include "connectcode.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ========================================================================
 * Slot 0: Adjectives (256) — descriptive qualities
 * ======================================================================== */

static const char *s_Adjectives[256] = {
    "fat","thin","tall","short","big","small","old","young",
    "fast","slow","hot","cold","dark","bright","loud","quiet",
    "happy","sad","angry","calm","brave","shy","wild","tame",
    "sneaky","lazy","busy","lucky","dusty","rusty","shiny","fuzzy",
    "golden","silver","red","blue","green","purple","orange","pink",
    "frozen","burning","flying","falling","hidden","secret","ancient","modern",
    "magic","cosmic","tiny","giant","crimson","cobalt","emerald","violet",
    "hollow","solid","heavy","light","smooth","rough","sharp","blunt",
    "mighty","humble","noble","wicked","jolly","grim","fierce","gentle",
    "silent","roaring","dancing","sleeping","hungry","thirsty","tired","alert",
    "clever","foolish","wise","silly","proud","modest","bold","timid",
    "swift","steady","dizzy","foggy","stormy","sunny","cloudy","misty",
    "spicy","sweet","salty","bitter","tangy","bland","rich","plain",
    "fluffy","scaly","spotted","striped","curly","twisted","bent","flat",
    "electric","magnetic","atomic","solar","lunar","galactic","orbital","nano",
    "perfect","double","special","phantom","shadow","crystal","velvet","iron",
    "plastic","wooden","marble","glass","leather","steel","copper","brass",
    "northern","southern","eastern","western","central","coastal","mountain","swampy",
    "arctic","tropical","desert","volcanic","oceanic","glacial","frosty","steamy",
    "musical","poetic","dramatic","comic","tragic","epic","lyrical","funky",
    "digital","virtual","cyber","quantum","mega","ultra","super","hyper",
    "royal","sacred","cursed","blessed","haunted","enchanted","mundane","exotic",
    "radical","extreme","supreme","ultimate","absolute","infinite","eternal","mortal",
    "scarlet","azure","ivory","ebony","amber","jade","coral","chrome",
    "rugged","polished","matte","glossy","frosted","tinted","stained","pure",
    "fragile","sturdy","tough","brittle","elastic","rigid","flexible","dense",
    "luminous","radiant","glowing","dim","faint","vivid","pale","deep",
    "rotten","fresh","stale","ripe","raw","mature","vintage","antique",
    "spectral","ghostly","ethereal","primal","feral","savage","regal","noble",
    "volatile","stable","reactive","inert","active","dormant","kinetic","static",
    "heroic","villainous","legendary","mythic","infamous","unknown","famous","obscure",
    "grumpy","cheerful","nervous","relaxed","frantic","peaceful","chaotic","orderly",
};

/* ========================================================================
 * Slot 1: Nouns — creatures, monsters, objects (256)
 * ======================================================================== */

static const char *s_Nouns[256] = {
    "flea","ant","bee","wasp","moth","slug","worm","snail",
    "spider","beetle","mantis","cricket","roach","tick","mite","fly",
    "rabbit","squirrel","hamster","mouse","rat","fox","wolf","bear",
    "falcon","eagle","raven","parrot","owl","crow","duck","goose",
    "shark","whale","dolphin","squid","lobster","crab","eel","jellyfish",
    "dragon","phoenix","griffin","unicorn","goblin","troll","ogre","fairy",
    "vampire","werewolf","zombie","ghost","skeleton","mummy","witch","demon",
    "robot","cyborg","alien","clone","android","golem","mutant","ninja",
    "cheese","pizza","taco","waffle","burger","pretzel","donut","cookie",
    "pickle","muffin","sausage","pancake","noodle","bagel","biscuit","cupcake",
    "hammer","sword","shield","arrow","cannon","bomb","axe","spear",
    "crystal","diamond","ruby","pearl","emerald","sapphire","topaz","opal",
    "wizard","knight","pirate","cowboy","clown","chef","doctor","astronaut",
    "monkey","penguin","panda","koala","sloth","otter","walrus","hippo",
    "tiger","lion","panther","jaguar","cobra","viper","gator","python",
    "cactus","mushroom","pumpkin","turnip","potato","onion","pepper","banana",
    "rocket","satellite","comet","meteor","asteroid","planet","star","moon",
    "guitar","piano","trumpet","violin","banjo","drum","flute","harp",
    "tornado","blizzard","tsunami","volcano","earthquake","avalanche","cyclone","monsoon",
    "compass","anchor","lantern","telescope","hourglass","sundial","bell","whistle",
    "kitten","puppy","duckling","piglet","calf","foal","lamb","chick",
    "blimp","balloon","kite","glider","submarine","canoe","raft","sled",
    "statue","gargoyle","scarecrow","mannequin","puppet","doll","teddy","mask",
    "sandwich","burrito","dumpling","croissant","strudel","brownie","truffle","popcorn",
    "toaster","blender","spatula","bucket","mop","broom","wrench","plunger",
    "parrot","flamingo","toucan","pelican","stork","heron","finch","sparrow",
    "centaur","minotaur","sphinx","hydra","kraken","chimera","basilisk","djinn",
    "laptop","phone","tablet","modem","printer","camera","speaker","headset",
    "marble","pebble","boulder","brick","plank","rope","chain","wire",
    "candle","torch","matchstick","firecracker","sparkler","flare","beacon","lantern",
    "acorn","pinecone","seashell","feather","fossil","bone","tusk","horn",
    "trophy","medal","crown","scepter","chalice","amulet","scroll","tome",
};

/* ========================================================================
 * Slot 2: Action phrases — verb + preposition (256)
 * ======================================================================== */

static const char *s_Actions[256] = {
    "running to","walking to","sprinting to","jogging to",
    "skipping to","crawling to","rolling to","tumbling to",
    "flying to","gliding to","soaring to","floating to",
    "driving to","riding to","sailing to","paddling to",
    "running from","walking from","sprinting from","fleeing from",
    "escaping from","hiding from","retreating from","drifting from",
    "flying from","falling from","jumping from","diving from",
    "driving from","riding from","sailing from","racing from",
    "running around","walking around","dancing around","spinning around",
    "skipping around","sneaking around","lurking around","wandering around",
    "flying around","circling around","orbiting around","buzzing around",
    "driving around","cruising around","skating around","rolling around",
    "hiding in","sleeping in","cooking in","singing in",
    "dancing in","reading in","swimming in","playing in",
    "sitting in","standing in","waiting in","working in",
    "fighting in","training in","studying in","painting in",
    "hiding near","standing near","camping near","lurking near",
    "sleeping near","fishing near","parking near","sitting near",
    "singing near","dancing near","juggling near","napping near",
    "smoking near","eating near","reading near","waiting near",
    "running through","walking through","charging through","crashing through",
    "sneaking through","tiptoeing through","marching through","stomping through",
    "flying through","blasting through","zooming through","drifting through",
    "driving through","racing through","sliding through","tunneling through",
    "jumping over","flying over","leaping over","vaulting over",
    "climbing over","hopping over","soaring over","gliding over",
    "stepping over","tripping over","tumbling over","rolling over",
    "floating over","hovering over","diving over","skipping over",
    "hiding behind","standing behind","crouching behind","peeking behind",
    "sleeping behind","waiting behind","lurking behind","camping behind",
    "parking behind","sitting behind","leaning behind","ducking behind",
    "sneaking behind","tiptoeing behind","creeping behind","sliding behind",
    "sitting on","standing on","dancing on","jumping on",
    "sleeping on","camping on","surfing on","skating on",
    "bouncing on","balancing on","climbing on","landing on",
    "riding on","perching on","nesting on","resting on",
    "crawling under","hiding under","sleeping under","digging under",
    "sliding under","rolling under","squeezing under","ducking under",
    "tunneling under","burrowing under","nesting under","camping under",
    "swimming under","diving under","sinking under","floating under",
    "running toward","walking toward","charging toward","marching toward",
    "flying toward","gliding toward","drifting toward","floating toward",
    "driving toward","racing toward","steering toward","heading toward",
    "sneaking toward","tiptoeing toward","creeping toward","inching toward",
    "running past","walking past","sprinting past","jogging past",
    "flying past","zooming past","blasting past","streaking past",
    "driving past","cruising past","racing past","drifting past",
    "sneaking past","tiptoeing past","creeping past","sliding past",
    "running beside","walking beside","sitting beside","standing beside",
    "flying beside","floating beside","hovering beside","gliding beside",
    "driving beside","riding beside","skating beside","rolling beside",
    "dancing beside","singing beside","sleeping beside","waiting beside",
    "teleporting to","warping to","beaming to","phasing to",
    "materializing at","appearing at","arriving at","landing at",
    "departing from","vanishing from","fading from","dissolving from",
    "emerging from","erupting from","launching from","blasting from",
    "marching toward","dashing toward","stampeding toward","charging toward",
    "bouncing toward","leaping toward","sprinting toward","diving toward",
    "tumbling around","cartwheeling around","pirouetting around","moonwalking to",
    "materializing at","manifesting at","disappearing from","evaporating from",
    "melting from","crumbling from","parachuting into","cannonballing into",
    "belly-flopping into","tightrope-walking over","parasailing over","hang-gliding over",
    "burrowing through","phasing through","warping through","catapulting toward",
    "somersaulting over","backflipping over","belly-sliding to","barrel-rolling to",
};

/* ========================================================================
 * Slot 3: Places (256) — locations, with optional articles
 * ======================================================================== */

static const char *s_Places[256] = {
    "the park","the mall","the beach","the zoo",
    "the gym","the pool","the library","the museum",
    "the airport","the station","the harbor","the dock",
    "the market","the plaza","the arcade","the theater",
    "a castle","a tower","a bridge","a temple",
    "a fortress","a palace","a cabin","a bunker",
    "a mansion","a cottage","a villa","a lodge",
    "a chapel","a cathedral","a monastery","a lighthouse",
    "the moon","the sun","mars","jupiter",
    "saturn","neptune","pluto","venus",
    "space","orbit","the void","the cosmos",
    "a nebula","a galaxy","a black hole","the stars",
    "the forest","the jungle","the swamp","the desert",
    "the tundra","the savanna","the prairie","the meadow",
    "a volcano","a canyon","a glacier","a waterfall",
    "a mountain","a valley","a cave","an island",
    "downtown","uptown","the suburbs","the highway",
    "the alley","the rooftop","the basement","the attic",
    "the sewer","the tunnel","the bridge","the overpass",
    "a parking lot","a junkyard","a warehouse","a factory",
    "the ocean","the river","the lake","the pond",
    "the reef","the trench","the shore","the pier",
    "a lagoon","a stream","a creek","a bay",
    "the rapids","the falls","the delta","the marsh",
    "school","church","the hospital","the courthouse",
    "the bank","the office","the lab","the studio",
    "a diner","a cafe","a pub","a nightclub",
    "a bakery","a pizzeria","a taco stand","a food truck",
    "the circus","the carnival","the fair","the rodeo",
    "the arena","the stadium","the colosseum","the ring",
    "a racetrack","a skatepark","a bowling alley","a golf course",
    "the playground","the sandbox","the treehouse","the fort",
    "the north pole","the south pole","the equator","the tropics",
    "antarctica","the arctic","the sahara","the amazon",
    "tokyo","paris","london","new york",
    "chicago","mumbai","cairo","sydney",
    "the white house","the pentagon","area 51","the kremlin",
    "buckingham palace","the colosseum","the pyramids","stonehenge",
    "mount everest","the grand canyon","niagara falls","the bermuda triangle",
    "atlantis","el dorado","shangri-la","narnia",
    "hogwarts","mordor","gotham","wakanda",
    "the matrix","the upside down","jurassic park","bikini bottom",
    "minecraft","the metaverse","the cloud","cyberspace",
    "the simulation","the holodeck","the danger zone","the twilight zone",
    "nowhere","everywhere","somewhere","the unknown",
    "the future","the past","another dimension","a parallel universe",
    "a dream","a nightmare","reality","the afterlife",
    "the beginning","the end","the middle","the edge",
    "a volcano","a glacier","a desert","an oasis",
    "a swamp","a jungle","a rainforest","a savanna",
    "the pentagon","the louvre","the colosseum","the kremlin",
    "wall street","broadway","bourbon street","rodeo drive",
    "a spaceship","a submarine","a helicopter","a hot air balloon",
    "a treehouse","an igloo","a teepee","a yurt",
    "a dungeon","a labyrinth","a maze","a catacomb",
    "a roller coaster","a ferris wheel","a carousel","a waterslide",
    "the batcave","the death star","rivendell","the shire",
    "sesame street","neverland","oz","wonderland",
    "a dumpster","a phone booth","an elevator","a closet",
    "a rooftop","a balcony","a fire escape","a windowsill",
    "a highway","a roundabout","a dead end","a crossroads",
    "the bermuda triangle","roswell","loch ness","shangri-la",
    "a cornfield","a vineyard","an orchard","a greenhouse",
    "a parking garage","a bus stop","a train station","a laundromat",
};

/* ========================================================================
 * Slot pointers: [adjective] [creature] [action] [place]
 * ======================================================================== */

static const char **s_SlotWords[4] = {
    s_Adjectives, s_Nouns, s_Actions, s_Places
};

/* ========================================================================
 * Encode: IP (network byte order) -> 4-part sentence
 * Port assumed as CONNECT_DEFAULT_PORT.
 * ======================================================================== */

s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize)
{
    if (!buf || bufsize < 32) return -1;

    u8 bytes[4];
    bytes[0] = (ip >> 0)  & 0xFF;
    bytes[1] = (ip >> 8)  & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;

    return snprintf(buf, bufsize, "%s %s %s %s",
        s_Adjectives[bytes[0]],
        s_Nouns[bytes[1]],
        s_Actions[bytes[2]],
        s_Places[bytes[3]]);
}

/* ========================================================================
 * Decode: sentence -> IP
 *
 * Strategy: try to match each slot's word list against the input.
 * For multi-word slots (actions, places), we greedily match the longest
 * phrase that starts at the current position.
 * Articles (the, a, an) are part of the place strings, so they match naturally.
 * ======================================================================== */

static s32 matchSlot(const char *input, s32 slot, s32 *chars_consumed)
{
    const char **list = s_SlotWords[slot];

    /* Try longest match first (multi-word phrases like "running to" or "the park") */
    s32 bestIdx = -1;
    s32 bestLen = 0;

    for (s32 i = 0; i < 256; i++) {
        const char *phrase = list[i];
        s32 plen = (s32)strlen(phrase);

        /* Case-insensitive prefix match */
        s32 match = 1;
        for (s32 j = 0; j < plen; j++) {
            if (!input[j]) { match = 0; break; }
            if (tolower((unsigned char)input[j]) != tolower((unsigned char)phrase[j])) {
                match = 0;
                break;
            }
        }

        /* Must be followed by end-of-string or a separator */
        if (match && plen > bestLen) {
            char next = input[plen];
            if (next == '\0' || isspace((unsigned char)next) ||
                next == '-' || next == '.' || next == ',') {
                bestIdx = i;
                bestLen = plen;
            }
        }
    }

    if (bestIdx >= 0) {
        *chars_consumed = bestLen;
        return bestIdx;
    }

    return -1;
}

s32 connectCodeDecode(const char *code, u32 *outIp)
{
    if (!code || !outIp) return -1;

    u8 bytes[4];
    const char *p = code;

    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p)) p++;

    for (s32 slot = 0; slot < 4; slot++) {
        /* Skip separators between slots */
        while (*p && (isspace((unsigned char)*p) || *p == '-' || *p == '.' || *p == ',')) p++;

        if (!*p) return -1;

        s32 consumed = 0;
        s32 idx = matchSlot(p, slot, &consumed);
        if (idx < 0) return -1;

        bytes[slot] = (u8)idx;
        p += consumed;
    }

    *outIp = (u32)bytes[0] | ((u32)bytes[1] << 8) |
              ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);

    return 0;
}
