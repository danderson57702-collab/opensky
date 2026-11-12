"""The region's creatures and hostile people, for combat: wolves, bears, skeevers, draugr,
skeletons, giants, mammoths... and bandits (Skyrim's encounter NPCs).

A creature kind comes from its race record: its skeleton (ANAM), skin (WNAM: the body's
armour addons), behaviour project (root motion, for how fast its walk and run carry it) and
the clips in its animations folder (Skyrim's names differ per creature, so each role -
idle, walk, run, attacks, recoil, death - is matched by name). Kinds on the same skeleton
share one clip set. Their numbers are the encounter NPC's: health (DNAM + the ACBS offset),
aggression and confidence (AIDT), the race's unarmed damage and reach; what's on the body
when it dies is its death item and inventory, as a container.

People (bandits, necromancers, vampires...) are built like the player's character: a donor's
FaceGen head (encounter NPCs are generated in the game, so they have none in the files) on
the race's body in the encounter's outfit, the weapon from its inventory in hand; they use
the characters' clip sets.

Spawn points are the placed actors (outdoors in the region and in its interiors) whose base
- or a leveled list's entry at about LEVEL - is one of the kinds.

  PACK_DIR/creatures/<kind>.sknpc  skeleton + batches (npcs.write_sknpc)
  PACK_DIR/creatures/<set>.skanim  a skeleton's clips (animations._write_set): idle, combat,
                                   walk, trot, run, attack, attack2, power, recoil, death, block
  PACK_DIR/creatures/creatures.txt '|'-separated lines:
    K|kind|title|health|damage|reach m|aggression|confidence|height m|radius m|clip set|flags|
      weapon kind|corpse container|armour|power mult|bite mult|bite poison/s|bite poison s|
      spit poison/s|spit poison s|scale|voice type (people: their grunts, struck and dying; -)|
      level mult (thousandths of the player's; 0: fixed)|level (fixed)|least level|most level (0: any)|
      health at level 0|health a level  (Skyrim's leveling: the engine works its level out as it wakes)
      clip set: a .skanim stem, or @male / @female (the characters'); flags: 1 a person,
      2 an archer, 4 undead, 8 no death clip (it falls over), 16 lives in water, 32 upright
      (two legs: falls over as people do), 64 a dragon (flies: G line), 128 remains (not spawned);
      aggression: 0 unaggressive .. 3 frenzied (Skyrim's AIDT); confidence: 0 cowardly .. 4
      foolhardy; weapon kind: Skyrim's animation type (0 none); power mult: its strongest
      attack's damage multiplier (people 2); bite: the attacks with a poison spell (their
      multiplier, the poison); spit: a ranged poison (0 0: none) - Skyrim's race attack data;
      scale: drawn this much bigger than its clip set's skeleton (height, radius, reach are)
    S|kind|x|y|z|heading|place|flags|level mult|level|least|most|health at level 0|health a level
      a spawn point (engine space; place: -1 Tamriel, an interior's index; flags: 1 lying in
      ambush (draugr in their alcoves), 2 a dragon's lair (it waits perched)); its NPC's leveling
      (as K's: the engine levels it as it wakes)
    U|kind|shouts: 1 Unrelenting Force, 2 Frost Breath, 4 Fire Breath, 8 Disarm (a draugr boss's)
    G|kind|breath element (1 fire, 2 frost)|breath damage a second|level|bones kind
      a dragon (K flags 64): Skyrim's breath shout, its level (random dragons are picked by
      the player's), the kind drawn where it died (its skeleton; K flags 128: never spawned)
"""
import math
import os
import struct
import companions
import college
import thieves
from collections import Counter

import numpy as np

import animations
import crime
import hkx
from bsa import BSA
from esm import RECORD_INITIALLY_DISABLED
from extract_textures import SKYRIM_DATA, find
from meshes import texture_pack_name
from nif import NIF
from npcs import ACT_NONE, VERTEX_DTYPE, write_sknpc
from skin import Skeleton, pose_shape, skinned_shapes

UNITS_PER_METER = 70.0
C = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], np.float64)  # Skyrim -> engine axes
LEVEL = 10  # leveled lists are read at about this player level
MAX_BONES = 64
PLAYABLE = ("NordRace", "ImperialRace", "BretonRace", "RedguardRace", "HighElfRace", "DarkElfRace", "WoodElfRace",
            "OrcRace", "KhajiitRace", "ArgonianRace")
# Creatures converted: race -> kind name (others' spawn points are left out). Skins that
# differ (an ice wolf) become their own kinds on the same clip set.
CREATURE_RACES = {
    "WolfRace": "wolf", "SkeeverRace": "skeever", "SkeeverWhiteRace": "skeever_white", "MudcrabRace": "mudcrab",
    "BearBlackRace": "bear_black", "BearBrownRace": "bear_brown", "BearSnowRace": "bear_snow",
    "SabreCatRace": "sabrecat", "SabreCatSnowyRace": "sabrecat_snow", "TrollRace": "troll",
    "TrollFrostRace": "troll_frost", "FrostbiteSpiderRace": "spider", "FrostbiteSpiderRaceLarge": "spider_large",
    "FrostbiteSpiderRaceGiant": "spider_giant", "DraugrRace": "draugr", "SkeletonRace": "skeleton",
    "GiantRace": "giant", "MammothRace": "mammoth", "FoxRace": "fox", "DeerRace": "deer", "ElkRace": "elk",
    "GoatRace": "goat", "HareRace": "hare", "CowRace": "cow", "ChickenRace": "chicken", "HorkerRace": "horker",
    "SprigganRace": "spriggan", "IceWraithRace": "icewraith", "FalmerRace": "falmer", "ChaurusRace": "chaurus",
    "SlaughterfishRace": "slaughterfish", "DogRace": "dog", "DragonRace": "dragon",
    "DwarvenSpiderRace": "dwarven_spider", "DwarvenSphereRace": "dwarven_sphere",
    "DwarvenCenturionRace": "dwarven_centurion", "DragonPriestRace": "dragon_priest",
    "HorseRace": "horse", "CartHorseRace": "horse_cart", "AlduinRace": "alduin",
    # (Conjuration's: what its spells summon - spells.txt's summons name these kinds)
    "AtronachFlameRace": "atronach_flame", "AtronachFrostRace": "atronach_frost", "AtronachStormRace": "atronach_storm",
    "DLC1BlackSkeletonRace": "boneman", "DLC1SoulCairnSkeletonNecroRace": "mistman",
    "DLC1SoulCairnSkeletonArmorRace": "wrathman", "DLC2SeekerRace": "seeker", "DLC2AshSpawnRace": "ashspawn",
    "dlc2AshGuardianRace": "ashguardian",
    # (29 Oct, the whole map: the rest of the base game's - their own sets: the Hagraven, the werewolf, the
    # Wispmother and her shades, the witchlight; the rest on a kind's skeleton already: the necromancers'
    # and the coffins' skeletons, Meeko and Barbas, the Companions' spirit wolves, the White Stag, the
    # Spriggan Matron and swarm, the Chaurus Reaper, Whiterun's giant, a farm's goat)
    "HagravenRace": "hagraven", "WerewolfBeastRace": "werewolf", "WispRace": "wisp", "WispShadeRace": "wispshade",
    "WitchlightRace": "witchlight", "SkeletonNecroRace": "skeleton", "SkeletonNecroPriestRace": "skeleton",
    "RigidSkeletonRace": "skeleton", "DogCompanionRace": "dog", "MG07DogRace": "dog", "DA03BarbasDogRace": "dog",
    "C06WolfSpiritRace": "wolf_spirit", "WhiteStagRace": "white_stag", "SprigganMatronRace": "spriggan_matron",
    "SprigganSwarmRace": "spriggan_swarm", "ChaurusReaperRace": "chaurus_reaper",
    "C00GiantOutsideWhiterunRace": "giant", "GoatDomesticsRace": "goat",
    "MagicAnomalyRace": "icewraith",  # (Containment's anomalies: on the Ice Wraith's skeleton, as Skyrim's)
}
# Dragons: their own clips (Skyrim's flight, landing, crash, breath...); the ones met at
# random (Skyrim's LCharDragon by level: Dragon, Blood, Frost, Elder, Ancient); the bones left
# when one dies (its skin burns away from them); lair dragons placed for quests left out
DRAGON_RACES = ("DragonRace", "AlduinRace", "UndeadDragonRace")
DRAGON_ROLES = {
    "idle": ("special_perchidle",), "combat": ("ground_combatidle",), "walk": ("mtforwardground",),
    "run": ("mtforwardground",), "attack": ("ground_bite",), "attack2": ("ground_tailwhip",),
    "power": ("ground_wingswipeleft",), "recoil": ("ground_injured",), "death": ("mtdeathintro",),
    "glide": ("mtforward_glide",), "glide_l": ("mtforward_glidebankleft",), "glide_r": ("mtforward_glidebankright",),
    "glide_hurt": ("mtforward_glide_hurt",), "flap": ("mtfastforward_flap",), "hover": ("mtidle_hover",),
    "takeoff": ("mttakeoffvertical",), "land": ("mtlandvertical",), "crash": ("mtland_crashlong",),
    "crash_up": ("mtland_crashlonggetup",), "roar": ("mtflight_roar",), "air_hit": ("mtflight_hit",),
    "inhale": ("ground_inhale",), "breathe": ("ground_exhalefirebreath",), "fly_inhale": ("shout_flightinhale",),
    "fly_breathe": ("shout_flightexhaleflame",), "hover_inhale": ("shout_hoverinhale",),
    "hover_breathe": ("shout_hoverexhaleflame",), "turn_l": ("mtturnleftground45",),
    "turn_r": ("mtturnrightground45",),
    # (dead: Skyrim's death intro hands the body to its ragdoll, which lays it down - its remains,
    # the bones, lie so; without one, its wounded pose lying on the ground: head and wings down)
    "dead": ("ground_bleedoutidle",),
}
DRAGON_DROP_FIRST = ("toe", "lip", "brow", "tongue", "magic", "hub")  # (84 bones: wings, tail and jaw kept)
DRAGON_NPCS = ("EncDragon01Fire", "EncDragon02Fire", "EncDragon03Frost", "EncDragon04Fire", "EncDragon05Frost")
OPENING_NPCS = ("MQ101Alduin", "MQ101CartHorse")
DRAGON_BONES_NIF = "meshes\\actors\\dragon\\character assets\\decaydragon.nif"
DRAGON_QUEST_PREFIXES = ("mq", "paarthurnax")
DRAGON_REACH_M = 7.0
UNDEAD = ("DraugrRace", "SkeletonRace")
# How they take to the player (Skyrim's factions: predators and the undead attack on sight,
# giants and mammoths defend themselves, prey runs): the engine's aggression 2 / 1 / 0
PREY = ("FoxRace", "DeerRace", "ElkRace", "GoatRace", "HareRace", "CowRace", "ChickenRace")
NEUTRAL = ("GiantRace", "MammothRace", "HorkerRace", "DogRace", "SprigganRace", "HorseRace", "CartHorseRace")
MAX_REACH_M = 3.5
WATER = ("SlaughterfishRace",)
# Clip roles: candidate file stems in each creature's animations folder, first found wins
ROLES = {
    # (the Storm Atronach - and the Ash Guardian on its skeleton - floats: "rootf" is its way forward;
    # the Frost Atronach's are walkf / runf; the Seeker's mt_forward / mt_fastforward, its blows its casts)
    "idle": ("mt_idle", "mainidle", "mtidle_standing", "idle", "h2hidle", "1hmidle", "idle_body1", "combatidle", "mtidle",
             "ww_idlebase"),  # (the werewolf's)
    "combat": ("combatidle", "idlecombat1", "h2hidle", "1hmidle", "combatidle1", "mt_idle", "mainidle", "idle"),
    "walk": ("walkforward", "forward_walk", "mtwalkforward", "1hmforward", "combatwalkforward", "walkforward_wolf",
             "walkf", "mt_forward", "rootf",
             # (the Chaurus, the Slaughterfish swimming, the Ice Wraith's glide, a Dragon Priest's float, Dwemer)
             "forwardwalk", "swim_forward", "runf", "mt_walk_f", "mtforward", "mtwalkforward", "forward_walk"),
    "trot": ("trotforward", "1hmfastforward", "trotforward_wolf"),
    "run": ("runforward", "forward_run", "mtrunforward", "1hmfastforward", "runforwardgrowl", "swimforward",
            "runf", "mt_fastforward", "rootf", "forwardrun", "swim_fastforward", "mt_run_f", "mtfastforward",
            "mtrunforward", "forward_run", "ww_runforward", "combat_runf"),  # (the werewolf's, the wisp's)
    "attack": ("attack1", "attack_bitelow", "attack_leftchop", "h2hattacklefta", "1hmattacka", "attack_combochop",
               "attack_swipe", "attack_l1", "magic_rcastff",
               # (Falmer, Chaurus, Slaughterfish, the Ice Wraith, Dwemer spheres and centurions, a Dragon Priest's spell)
               "1hm_attack1", "attackrbite", "attacklcross", "attack_new", "h2h_attackslash", "h2hattackleftchop",
               "attackrightstab", "mag_rh_ff_r_release", "attackl", "ww_attackleft"),  # (the Hagraven's, the werewolf's)
    "attack2": ("attack2", "attack_bitehigh", "attack_rightchop", "attackleft1", "h2hattackrighta", "1hmattackf",
                "attackleft_side", "attack1x2", "attack_r1", "magic_lcastff",
                "1hm_attack2", "attackforwardbite", "attackrcross", "attack_grounddive", "h2h_attackchop",
                "h2hattackleftslash", "attack_combolow", "mag_lh_ff_r_release", "attackr", "ww_attackright"),
    "power": ("attackpowerforward", "attack_forwardjump", "attack_forwardjunp", "h2hattackforwardpowerrush",
              "forwardpowerattack", "1hmattackpowerchop", "attack_stomp", "attackpowerstanding", "powerattack_f",
              "powerattack_l1", "1hm_forwardpowerattack1", "attackpowerbite", "attack_power",
              "h2h_attackforwardpowerchop", "h2hattackforwardpower", "ww_attackleftpowercombo"),
    "recoil": ("recoil", "recoil_bite", "staggerbacksmall", "stagger_small", "h2hrecoillefta", "1hmrecoil",
               "recoillchop", "stagger_back", "recoil_jump", "recoill", "recoil_l1", "mt_staggerlight"),
    "death": ("death", "deathpose", "mt_death", "spinningdeath", "ww_deathtemp"),  # (the wisp's, the werewolf's)
    "block": ("1hmweaponblock",),
    "sprint": ("sprintforward",),  # (a horse's gallop)
    # (the dead lying in wait: in a sarcophagus, an alcove, sat on a throne - and getting out)
    "sarc_idle": ("specialidle_sarcophagusidle",), "sarc_exit": ("specialidle_sarcophagusexit",),
    "alcove_idle": ("alcove_idle",),  # (standing in a wall's alcove, as Skyrim's CreatureAlcoveMarker has them)
    "alcove_exit": ("alcove_wake",),  # (... stepping out of it)
    # (lying in a burial shelf - CreatureAlcoveBgMarker - or a Dragon Priest's coffin: flat, and up and out sideways)
    "lay_idle": ("specialidle_alcovelayingidle",), "lay_exit": ("specialidle_alcovelayingexit",),
    "sit_idle": ("specialidle_sitidle",), "sit_exit": ("specialidle_sitexit",),
    "shout_in": ("shoutinhale",), "shout": ("shoutexhale",),
}
LOCOMOTION = ("walk", "trot", "run", "sprint")
# The horses the stables sell (Skyrim's player horses: saddled, waiting by the stables), by the
# stablemaster who sells them
PLAYER_HORSES = (("WhiterunPlayerHorse", "SkulvarSableHilt", ("WhiterunStablesHorseMarker", "WhiterunStablesExteriorMarker")),)
# People: the encounter NPCs that fight the player (by name)
ARCH_SUMMON = 18   # (MGEF archetype: Summon Creature - its associated form the NPC)
SUMMON_BY_WORD = {}  # {a word of the effect's editor id: kind name} (SCRIPT_SUMMONS)
SCRIPT_SUMMONS = (("ashguardian", "DLC2SummonAshGuardian"),)
SUMMON_KINDS = {}  # {NPC global id: creature kind name}, filled by export (perks.py's spells read it)
OPENING_KEEP = "helgenkeep01"
OPENING_BODY = "mq101corpsesons01"  # (Gunjar: dead in the keep's first room, his gear yours - with Ralof)
# The keep's soldiers (Skyrim's opening: the other side's, hostile while you escape)
OPENING_PEOPLE = ("Imperial Soldier", "Stormcloak Soldier", "Imperial Captain", "Imperial Torturer", "Torturer",
                  "Torturer's Assistant", "Stormcloak Archer", "Stormcloak", "Gunjar", "Dremora Lord", "Dremora")
# ("Arvel the Swift": Bleak Falls Barrow's thief - the Golden Claw on him)
HOSTILE_PEOPLE = ("Arvel the Swift", "Bandit", "Bandit Outlaw", "Bandit Thug", "Bandit Highwayman", "Bandit Plunderer", "Bandit Marauder",
                  "Bandit Chief", "Marauder", "Silver Hand", "Novice Necromancer", "Apprentice Necromancer",
                  "Novice Fire Mage", "Novice Conjurer", "Vampire Fledgling", "Vampire", "Master Vampire",
                  "Vampire's Thrall", "Poacher", "Witch", "Hagraven", "Forsworn")
# Skeletons that stand on two legs (they fall over as people do: onto the back or the face)
UPRIGHT_SETS = {"draugr", "draugrskeleton", "giant", "troll", "spriggan", "falmer", "hagraven", "werewolf",
                "dwarvensteamcenturion", "dragonpriest"}
HARMLESS_RACES = ("HareRace", "ChickenRace")  # (no attack clips: they flee)
WEAPON_STAFF = 8
NPC_HEALTH_LEVEL_BONUS = 5.0  # (Skyrim.esm fNPCHealthLevelBonus)
MIN_DEATH_CLIP_S = 0.3  # (shorter "death" clips are poses for the ragdoll)
HUMANOID_DAMAGE = 4.0  # (people's fists; their weapons' own damage when armed)
F_GUARD = 256  # (a kind's flag: a hold's guard - engine/src/creatures.c)
# Diplomatic Immunity's Thalmor (MQ201): placed at the Embassy (its hall, Solar, barracks, dungeon
# and courtyard - by their base's editor id), at peace till you leave the party (Skyrim's MQ201
# SetEnemy ThalmorCombatFaction/PlayerFaction, as Malborn takes you out) - spawn side 4; the party
# hall's own guards stay at peace (6); the two the escape scene sends down to the dungeon with the
# trapdoor's keys (MQ201 EscapeSceneGuard1/2: MQ201TortureChamberGuard, Skyrim's EscapeKey1/2) - 5
THALMOR_BASES = ("mq201partyguard", "mq201torturechamberguard", "mq201thalmor", "thalmorembassyguard", "lvlthalmor")
SIDE_THALMOR, SIDE_ESCAPE, SIDE_PARTY = 4, 5, 6
ESCAPE_GUARD = "MQ201TortureChamberGuard"
ESCAPE_DOOR = "ThalmorEmbassyGuardsToDungeonDoor"  # (the Solar's stairs down: where they come in)


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


class Records:
    """Encounter NPCs as Skyrim reads them: leveled lists rolled, templates' traits."""

    def __init__(self, lo, strings):
        self.lo, self.strings = lo, strings
        self.npcs = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("NPC_")}
        self.lvln = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("LVLN")}
        self.races = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("RACE")}

    def candidates(self, gid, depth=0):
        """The NPC_ records a placed actor's base can turn out as at about LEVEL."""
        if gid in self.npcs:
            return [gid]
        if gid not in self.lvln or depth > 6:
            return []
        rec, out, best = self.lvln[gid], [], -1
        entries = [(struct.unpack_from("<H", v)[0], self.lo.global_id(rec.plugin, _u32(v, 4)))
                   for t, v in rec.subrecords() if t == "LVLO" and len(v) >= 8]
        reached = [lv for lv, _g in entries if lv <= LEVEL]
        if not reached:
            reached = [min(lv for lv, _g in entries)] if entries else []
        for lv, g in entries:
            if lv in reached:
                out += self.candidates(g, depth + 1)
        return out

    seed = 0  # (which of a leveled template's entries: the spawn's, so bandits vary)

    def template(self, npc, flag, depth=0):
        """The record a trait comes from: the NPC, or its template's when the template flag says."""
        acbs = npc.sub("ACBS")
        tflags = _u16(acbs, 18) if acbs and len(acbs) >= 20 else 0
        tplt = npc.sub("TPLT")
        if not (tflags & flag) or not tplt or depth > 6:
            return npc
        cands = self.candidates(self.lo.global_id(npc.plugin, _u32(tplt)))
        return self.template(self.npcs[cands[(self.seed >> depth) % len(cands)]], flag, depth + 1) if cands else npc

    def race(self, npc):
        t = self.template(npc, 0x1)
        rn = t.sub("RNAM")
        return self.races.get(self.lo.global_id(t.plugin, _u32(rn))) if rn else None

    def female(self, npc):
        acbs = self.template(npc, 0x1).sub("ACBS")
        return bool(acbs and _u32(acbs) & 1)

    def name(self, npc, depth=0):
        """Its name: the record its race comes from first (the same leveled template entry, so a
        wolf is never named after a skeever beside it in the list), else its own, else its
        template's."""
        for rec in (self.template(npc, 0x1), npc) if depth == 0 else (npc,):
            f = rec.sub("FULL")
            if f and len(f) == 4:
                return self.strings.get(rec.plugin, _u32(f), "strings") or rec.editor_id()
        tplt = npc.sub("TPLT")
        cands = self.candidates(self.lo.global_id(npc.plugin, _u32(tplt))) if tplt and depth < 6 else []
        return self.name(self.npcs[cands[(self.seed >> depth) % len(cands)]], depth + 1) if cands else npc.editor_id()

    def level(self, npc):
        """Its level at about LEVEL (a player-scaled one: its multiplier within its range)."""
        acbs = self.template(npc, 0x2).sub("ACBS")
        if not acbs or len(acbs) < 14:
            return 1
        flags, lv, lo_, hi = _u32(acbs), struct.unpack_from("<h", acbs, 8)[0], _u16(acbs, 10), _u16(acbs, 12)
        if flags & 0x80:  # (PC level mult: thousandths)
            lv = int(round(lv / 1000.0 * LEVEL))
            lv = max(lo_, min(hi if hi else 81, lv))
        return max(lv, 1)

    def leveling(self, npc, race, person=False):
        """How it levels with the player (Skyrim's ACBS): (level multiplier in thousandths - 0: a
        fixed level -, that level, its least, its most, health at level 0, health a level)."""
        acbs = self.template(npc, 0x2).sub("ACBS")
        if not acbs or len(acbs) < 14:
            return 0, 1, 1, 0, self.health(npc, race, person), 0.0
        flags, lv, lo_, hi = _u32(acbs), struct.unpack_from("<h", acbs, 8)[0], _u16(acbs, 10), _u16(acbs, 12)
        mult = lv if flags & 0x80 else 0
        if person:  # (people: the race's, the offset, and Skyrim's fNPCHealthLevelBonus a level)
            off = struct.unpack_from("<h", acbs, 20)[0] if len(acbs) >= 22 else 0
            start = struct.unpack_from("<f", race.sub("DATA"), 36)[0] if race is not None and race.sub("DATA") else 50.0
            return mult, max(lv, 1) if not mult else 1, lo_, hi, start + off, NPC_HEALTH_LEVEL_BONUS
        return mult, max(lv, 1) if not mult else 1, lo_, hi, self.health(npc, race, person), 0.0

    def weapon_skill(self, npc, wkind):
        """Its skill with the weapon it carries (DNAM: one-handed, two-handed, archery)."""
        d = self.template(npc, 0x2).sub("DNAM")
        k = 2 if wkind in (7, 9) else 1 if wkind in (5, 6) else 0
        return d[k] if d and len(d) > k else 15

    def health(self, npc, race, person=False):
        """Skyrim's: the NPC's health (DNAM) and offset; a person also starts from the race's
        and gains with level (their class's health share, about 4 a level)."""
        t = self.template(npc, 0x2)  # (stats)
        d, acbs = t.sub("DNAM"), t.sub("ACBS")
        h = _u16(d, 36) if d and len(d) >= 38 else 0
        autocalc = bool(acbs and len(acbs) >= 4 and _u32(acbs) & 0x10)
        if not autocalc:  # (an auto-calculated NPC's DNAM health has its offset in it already - the draugr's: 50 + 10 a
            # level, a Restless Draugr's 150, an Overlord's 210; adding it again made them 200 and 310, a Wight Lord 790 for 490)
            h += struct.unpack_from("<h", acbs, 20)[0] if acbs and len(acbs) >= 22 else 0
        start = struct.unpack_from("<f", race.sub("DATA"), 36)[0] if race is not None and race.sub("DATA") else 50.0
        if person:  # (their DNAM is the class's at some other level: the race's and 5 a level instead)
            off = struct.unpack_from("<h", acbs, 20)[0] if acbs and len(acbs) >= 22 else 0
            h = start + 5 * self.level(npc) + off
        elif h <= 0:
            h = start
        return max(h, 5)

    def ai(self, npc):
        a = self.template(npc, 0x10).sub("AIDT")  # (AI data)
        return (a[0], a[1]) if a and len(a) >= 2 else (1, 2)

    def inventory(self, npc):
        """(form gid, count) of its inventory (CNTO; a template's when it says so)."""
        t = self.template(npc, 0x100)
        return [(self.lo.global_id(t.plugin, _u32(v)), struct.unpack_from("<i", v, 4)[0])
                for k, v in t.subrecords() if k == "CNTO" and len(v) >= 8]

    def outfit(self, npc):
        t = self.template(npc, 0x100)
        d = t.sub("DOFT")
        return self.lo.record(self.lo.global_id(t.plugin, _u32(d)), types=("OTFT",)) if d else None

    def death_item(self, npc):
        t = self.template(npc, 0x1)
        i = t.sub("INAM")
        return self.lo.global_id(t.plugin, _u32(i)) if i else None

    def all_items(self, gid, types, depth=0):
        """first_item's, but every entry of a "use all" leveled list (a soldier's whole set:
        cuirass, boots, gauntlets, helmet - not the first of them alone)."""
        rec = self.lo.record(gid, types=types + ("LVLI",))
        if rec is None or depth > 6:
            return []
        lvlf = rec.sub("LVLF") if rec.type == "LVLI" else None
        if rec.type != "LVLI":
            return [rec] if rec.type in types else []
        if not (lvlf and lvlf[0] & 0x04):  # (LVLF use all: not - one entry, at about LEVEL; it whole, a set
            # itself - the Thalmor's: the best armour set for the level, every piece of it, not its boots alone)
            entries = sorted(((struct.unpack_from("<H", v)[0], self.lo.global_id(rec.plugin, _u32(v, 4)))
                              for t, v in rec.subrecords() if t == "LVLO" and len(v) >= 8), key=lambda e: -e[0])
            for lv, g in [e for e in entries if e[0] <= LEVEL] + entries:
                got = self.all_items(g, types, depth + 1)
                if got:
                    return got
            return []
        out = []
        for t, v in rec.subrecords():
            if t == "LVLO" and len(v) >= 8:
                out += self.all_items(self.lo.global_id(rec.plugin, _u32(v, 4)), types, depth + 1)
        return out

    def first_item(self, gid, types, depth=0):
        """An item of the types from a form or (its entries at about LEVEL of) a leveled list."""
        rec = self.lo.record(gid, types=types + ("LVLI",))
        if rec is None or depth > 6:
            return None
        if rec.type != "LVLI":
            return rec if rec.type in types else None
        entries = [(struct.unpack_from("<H", v)[0], self.lo.global_id(rec.plugin, _u32(v, 4)))
                   for t, v in rec.subrecords() if t == "LVLO" and len(v) >= 8]
        entries.sort(key=lambda e: -e[0])
        for lv, g in entries:  # (the best it would carry at about LEVEL)
            if lv <= LEVEL:
                found = self.first_item(g, types, depth + 1)
                if found is not None:
                    return found
        for lv, g in entries:
            found = self.first_item(g, types, depth + 1)
            if found is not None:
                return found
        return None


def _path(b):
    return b.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")


def _race_model(race, kind_sub):
    """A race's male skeleton (ANAM after MNAM) or behaviour project (MODL after NAM3, MNAM)."""
    subs = list(race.subrecords())
    if kind_sub == "skeleton":
        for i, (t, v) in enumerate(subs):
            if t == "ANAM":
                return _path(v)
    in_nam3 = False
    for t, v in subs:
        if t == "NAM3":
            in_nam3 = True
        elif in_nam3 and t == "MODL":
            return _path(v)
    return None


class Kinds:
    def __init__(self, lo, models, people, records, anims, items):
        self.lo, self.models, self.people, self.r, self.anims, self.items = lo, models, people, records, anims, items
        self.kinds = {}       # key -> kind dict
        self.sets = {}        # clip set stem -> (anim dir, hkx path, skeleton nif, roles, kept bones, project)
        self.stats = Counter()
        self.anim_files = {}  # dir -> {stem: path}
        self.donors = None

    # --- creatures ---------------------------------------------------------------------

    def _anim_dir_files(self, d):
        if d not in self.anim_files:
            self.anim_files[d] = {n.rsplit("\\", 1)[1][:-4].lower(): n for n in self.anims.list(d[:-1]) if n.lower().endswith(".hkx")
                                  and n.lower().startswith(d)}
        return self.anim_files[d]

    def _clip_set(self, race):
        skel_nif = _race_model(race, "skeleton")
        project = _race_model(race, "project")
        if not skel_nif or not project:
            return None
        skel_nif = "meshes\\" + skel_nif
        skel_dir = skel_nif.rsplit("\\", 1)[0] + "\\"
        base = skel_dir.rstrip("\\").rsplit("\\", 1)[0] + "\\"
        anim_dir = base + "animations\\"
        stem = project.rsplit("\\", 1)[1][:-4].lower()
        key = stem.replace("project", "") or stem
        if key in self.sets:
            return key
        hk_path = next((p for p in (skel_dir + skel_nif.rsplit("\\", 1)[1][:-4] + ".hkx", skel_dir + "skeleton.hkx")
                        if p in self.anims.entries), None)
        if hk_path is None:
            self.stats["no skeleton.hkx"] += 1
            return None
        files = self._anim_dir_files(anim_dir)
        roles = {}
        dragon = race.editor_id() in DRAGON_RACES
        for role, cands in (DRAGON_ROLES if dragon else ROLES).items():
            hit = next((files[c] for c in cands if c in files), None)
            if hit:
                roles[role] = hit
        # (a "death" that's only a pose - a giant's deathpose: two frames, standing, Skyrim's
        # ragdoll takes it from there - isn't a fall: the engine tips the body over instead)
        if "death" in roles and hkx.Animation(self.anims.read(roles["death"])).duration < MIN_DEATH_CLIP_S:
            del roles["death"]
        if all(r in roles for r in ("idle", "walk", "run")) and not any(r in roles for r in ("attack", "attack2")) and \
                race.editor_id() in HARMLESS_RACES:  # (a hare, a chicken: no blows - they run)
            roles["attack"] = roles["idle"]
        if "walk" in roles or "run" in roles:  # (only runs - the werewolf - or only a walk - a witchlight's float)
            roles.setdefault("walk", roles.get("run")), roles.setdefault("run", roles.get("walk"))
        if not all(r in roles for r in ("idle", "walk", "run")) or not any(r in roles for r in ("attack", "attack2")):
            self.stats["clips missing: " + key] += 1
            return None
        for r in ("attack", "attack2"):
            roles.setdefault(r, roles.get("attack2" if r == "attack" else "attack"))
        roles.setdefault("combat", roles["idle"])
        roles.setdefault("trot", roles["run"])
        roles.setdefault("recoil", roles["idle"])
        roles.setdefault("power", roles["attack"])
        try:
            rest_nif = NIF(find(self.models.mesh_archives, skel_nif))
        except (KeyError, ValueError):
            self.stats["no skeleton.nif"] += 1
            return None
        hk = hkx.Skeleton(self.anims.read(hk_path))
        world = Skeleton(rest_nif).world()
        kept = [b for b in hk.bones if b in world]
        if len(kept) > MAX_BONES:  # (fingers, toes and tail tips go first; a dragon's wings are its fingers)
            words = DRAGON_DROP_FIRST if dragon else ("finger", "toe", "tail", "jaw", "eye", "tongue", "pony", "brow", "lip")
            minor = [b for b in kept if any(w in b.lower() for w in words)]
            for b in reversed(minor):
                if len(kept) <= MAX_BONES:
                    break
                kept.remove(b)
            kept = kept[:MAX_BONES]
        self.sets[key] = {"anim_dir": anim_dir, "hk": hk_path, "nif": skel_nif, "rest": rest_nif, "roles": roles,
                          "kept": kept, "hkobj": hk, "world": world, "project": stem, "dragon": dragon}
        return key

    def _skin_batches(self, set_key, paths, attach=()):
        """Skinned shapes of the models (body parts) on a clip set's skeleton, bind pose; attach:
        [(nif path, node)] rigid models on a node (weapons)."""
        s = self.sets[set_key]
        world, kept, hk = s["world"], s["kept"], s["hkobj"]
        index = {b: i for i, b in enumerate(kept)}

        lower = {b.lower(): b for b in hk.bones}  # (the model's "WEAPON" is the skeleton's "Weapon")

        def kept_for(name):
            name = lower.get(name.lower(), name)
            i = hk.bones.index(name) if name in hk.bones else -1
            while i >= 0 and hk.bones[i] not in index:
                i = hk.parents[i]
            return index[hk.bones[i]] if i >= 0 else 0
        out = []
        for path in paths:
            nif = self.people.builder._nif(path)
            if nif is None:
                continue
            for sh in skinned_shapes(nif):
                if not sh.texture:
                    continue
                pos, nrm = pose_shape(sh, world)
                if not np.isfinite(pos).all():
                    continue
                mapped = np.array([kept_for(n) for n in sh.bone_names], np.int32)[sh.bones]
                out.append(self._batch(pos, nrm, sh, mapped, len(kept)))
        for path, node in attach:
            nif = self.people.builder._nif(path)
            if nif is None or node not in world:
                continue
            m = world[node]
            if not np.isfinite(m).all() or np.abs(m[:3, 3]).max() > 1e4:
                continue
            bone = kept_for(node)
            for sh in nif.shapes():
                if not sh.texture:
                    continue
                pos = sh.positions.astype(np.float64) @ m[:3, :3].T + m[:3, 3]
                if not np.isfinite(pos).all():
                    continue
                nrm = sh.normals.astype(np.float64) @ m[:3, :3].T if sh.normals is not None else None
                mapped = np.full((len(pos), 4), bone, np.int32)
                sh.weights = np.tile(np.array([[1.0, 0, 0, 0]]), (len(pos), 1))
                out.append(self._batch(pos, nrm, sh, mapped, len(kept)))
        return [b for b in out if b is not None]

    def _batch(self, pos, nrm, s, mapped, nbones):
        dense = np.zeros((len(pos), max(nbones, 3)))  # (at least three: a witchlight's skeleton keeps fewer bones)
        np.add.at(dense, (np.repeat(np.arange(len(pos)), mapped.shape[1]), mapped.reshape(-1)),
                  s.weights[:, :mapped.shape[1]].reshape(-1))
        order = np.argsort(-dense, axis=1)
        rows = np.arange(len(pos))
        b0, b1, b2 = order[:, 0], order[:, 1], order[:, 2]
        top = np.stack([dense[rows, b0], dense[rows, b1], dense[rows, b2]], 1)
        total = np.maximum(top.sum(1, keepdims=True), 1e-9)
        wts = np.where(top.sum(1, keepdims=True) > 0, top / total, np.array([[1.0, 0.0, 0.0]]))
        v = np.zeros(len(pos), VERTEX_DTYPE)
        v["pos"] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1) / UNITS_PER_METER
        v["uv"] = np.nan_to_num(s.uvs)
        v["n"] = np.clip(np.round(np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1) * 127), -127, 127) \
            if nrm is not None else (0, 127, 0)
        v["col"] = 255
        v["bone"][:, 0], v["bone"][:, 1] = b0, np.where(wts[:, 1] > 0, b1, b0)
        v["bone2"] = np.where(wts[:, 2] > 0, b2, b0)
        w0 = np.clip(np.round(wts[:, 0] * 255), 0, 255)
        v["w"][:, 0], v["w"][:, 1] = w0, np.clip(np.round(wts[:, 1] * 255), 0, 255 - w0)
        if len(s.triangles) == 0 or len(pos) > 0xFFFF:
            return None
        cut = int(s.alpha_test) if int(s.alpha_test) > 160 else 0  # (a high cut of Skyrim's: baked in - see textures.convert)
        tex = texture_pack_name(s.texture) + ("_a" if s.alpha_test else "") + (f"_c{cut}" if cut else "")
        self.models._convert_texture({"texture": tex, "source": s.texture, "flags": 1 if s.alpha_test else 0, "cut": cut})
        return (tex, s.alpha_test, v, s.triangles.astype("<u2").reshape(-1), 0)

    def _weapon(self, npc):
        """(WEAP record, its model path) of the weapon it would carry, or None."""
        staff = None  # (a staff is cast, not swung: a blade or a bow first, as Skyrim's skeletons fight)
        for gid, _n in self.r.inventory(npc):
            w = self.r.first_item(gid, ("WEAP",))
            if w is not None and w.sub("MODL"):
                if w.sub("DNAM") and w.sub("DNAM")[0] == WEAPON_STAFF:
                    staff = staff or (w, "meshes\\" + _path(w.sub("MODL")))
                    continue
                return w, "meshes\\" + _path(w.sub("MODL"))
        return staff

    def _corpse(self, title, npc, outfit_items=()):
        """What its body holds (its death item, inventory and outfit) as a container index."""
        if self.items is None:
            return -1
        entries = []
        di = self.r.death_item(npc)
        if di:
            e = self.items.entry(di)
            if e is not None:
                entries.append((e[0], e[1], 1))
        for gid, count in self.r.inventory(npc):
            e = self.items.entry(gid)
            if e is not None and count > 0:
                entries.append((e[0], e[1], count))
        for rec in outfit_items:
            idx = self.items.item_for(self.lo.global_id(rec.plugin, rec.form_id), rec)
            if idx is not None:
                entries.append(("I", idx, 1))
        idx = len(self.items.containers)
        self.items.containers.append((None, npc, entries))
        self.items.container_names = getattr(self.items, "container_names", {})
        self.items.container_names[idx] = title
        return idx

    def _spell_poison(self, owner, spell_id):
        """A poison spell's (damage a second, seconds): its strongest effect's magnitude and duration."""
        spell = self.lo.record(self.lo.global_id(owner.plugin, spell_id), types=("SPEL",)) if spell_id else None
        best = (0.0, 0.0)
        subs = list(spell.subrecords()) if spell is not None else []
        for i, (t, v) in enumerate(subs):
            if t == "EFID" and i + 1 < len(subs) and subs[i + 1][0] == "EFIT" and len(subs[i + 1][1]) >= 12:
                mag, _area, dur = struct.unpack_from("<fII", subs[i + 1][1])
                if mag > best[0]:
                    best = (float(mag), float(dur))
        return best

    def _race_attacks(self, race):
        """Skyrim's attack data for a creature race (ATKD): its strongest attack's damage
        multiplier (ours its power attack), its bite (the attacks carrying a poison spell: their
        multiplier, the poison's damage a second and seconds), and a spit (a race spell
        named *Spit*: aimed, the poison's damage a second and seconds)."""
        power, bite, bite_poison = 1.0, 1.0, (0.0, 0.0)
        subs = list(race.subrecords())
        for i, (t, v) in enumerate(subs):
            if t != "ATKD" or len(v) < 12:
                continue
            mult, chance, spell = struct.unpack_from("<ffI", v)
            event = subs[i + 1][1].rstrip(b"\0").decode("cp1252", "replace").lower() \
                if i + 1 < len(subs) and subs[i + 1][0] == "ATKE" else ""
            if "bash" in event:
                continue
            power = max(power, mult)
            poison = self._spell_poison(race, spell) if spell else (0.0, 0.0)
            if poison[1] > 0.0:  # (a poison: over seconds; not a stagger and the like)
                bite = max(bite, mult)
                bite_poison = max(bite_poison, poison)
        spit = (0.0, 0.0)
        for t, v in subs:
            if t == "SPLO" and len(v) == 4:
                sp = self.lo.record(self.lo.global_id(race.plugin, _u32(v)), types=("SPEL",))
                poison = self._spell_poison(race, _u32(v)) if sp is not None else (0.0, 0.0)
                if "spit" in (sp.editor_id() or "").lower() if sp is not None else False:
                    spit = max(spit, poison if poison[1] > 0.0 else (0.0, 0.0))
        return power, bite, bite_poison, spit

    def _damage_perks(self, npc):
        """Its perks' multiplier on its own blows (Skyrim's Mod Attack Damage entry point, 35,
        multiplying: crWoundedSpider's 0.13 - Bleak Falls Barrow's spider, wounded)."""
        mult = 1.0
        for rec in (npc, self.r.template(npc, 0x8)):
            for k, v in rec.subrecords():
                if k != "PRKR" or len(v) < 4:
                    continue
                perk = self.lo.record(self.lo.global_id(rec.plugin, _u32(v)), types=("PERK",))
                if perk is None:
                    continue
                entry, conds = None, 0
                for t, d in perk.subrecords():
                    if t == "DATA" and len(d) == 3:
                        entry, conds = (d[0], d[1]), 0
                    elif t == "CTDA":
                        conds += 1
                    elif t == "EPFD" and len(d) == 4 and entry == (35, 3) and not conds:
                        # (only those that always apply: crExtraDamage02's doubling is on conditions - not
                        # on you - and taken whole it had every bandit hitting twice as hard)
                        mult *= struct.unpack("<f", d)[0]
                        self.stats["damage perks"] += 1
            if rec is npc and npc.sub("PRKR"):
                break
        return mult

    def creature(self, npc_gid):
        npc = self.r.npcs[npc_gid]
        race = self.r.race(npc)
        if race is None or race.editor_id() not in CREATURE_RACES:
            return None
        skin_ref = self.r.template(npc, 0x1).sub("WNAM")
        skin_owner = self.r.template(npc, 0x1) if skin_ref else race
        skin_ref = skin_ref or race.sub("WNAM")
        skin_gid = self.lo.global_id(skin_owner.plugin, _u32(skin_ref)) if skin_ref else 0
        title = self.r.name(npc)
        weapon = self._weapon(npc)  # (one kind per weapon: a skeleton with a sword, one with a bow...)
        dragon_race = race.editor_id() in DRAGON_RACES
        if dragon_race:  # (each of Skyrim's dragons its own: Blood, Frost, Elder... by its own name and level)
            full = npc.sub("FULL")
            title = (self.r.strings.get(npc.plugin, _u32(full), "strings") if full and len(full) == 4 else None) or title
        damage_mult = self._damage_perks(npc)
        shouts = self._shouts(npc) if not dragon_race else 0
        key = (race.editor_id(), skin_gid, title, weapon[1] if weapon is not None else None, round(damage_mult, 3),
               shouts) + \
            ((npc.editor_id(),) if dragon_race else ())
        if key in self.kinds:
            return self.kinds[key]["name"]
        set_key = self._clip_set(race)
        if set_key is None:
            return None
        base = CREATURE_RACES[race.editor_id()]
        name = base
        n = 2
        while any(k["name"] == name for k in self.kinds.values()):
            name, n = f"{base}{n}", n + 1
        race_id = self.lo.global_id(race.plugin, race.form_id)
        skin = self.lo.record(skin_gid, types=("ARMO",)) if skin_gid else None
        paths = [p for _sl, p in self.people.builder._armor_addons(skin, race_id, False)] if skin is not None else []
        attach = []
        if weapon is not None:
            attach.append((weapon[1], "WEAPON"))
        batches = self._skin_batches(set_key, paths, attach)
        if not batches:
            self.stats["no body: " + name] += 1
            return None
        s = self.sets[set_key]
        world, kept = s["world"], s["kept"]
        parents = []
        hk = s["hkobj"]
        for b in kept:
            p = hk.parents[hk.bones.index(b)]
            while p >= 0 and hk.bones[p] not in kept:
                p = hk.parents[p]
            parents.append(kept.index(hk.bones[p]) if p >= 0 else -1)
        joints = [tuple(C @ world[b][:3, 3] / UNITS_PER_METER) for b in kept]
        home = (0, (0.0, 0.0, 0.0), 0.0, 0.0, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, 0)
        write_sknpc(os.path.join(self.out_dir, name + ".sknpc"), name, (parents, joints, False), [], home, batches, {})
        verts = np.concatenate([b[2]["pos"] for b in batches])
        height = float(verts[:, 1].max() - verts[:, 1].min())
        radius = float(max(np.ptp(verts[:, 0]), np.ptp(verts[:, 2])) * 0.35)
        d = race.sub("DATA")
        damage = struct.unpack_from("<f", d, 96)[0] if d and len(d) >= 104 else 5.0
        reach = struct.unpack_from("<f", d, 100)[0] / UNITS_PER_METER if d and len(d) >= 104 else 1.5
        if weapon is not None and weapon[0].sub("DATA") and len(weapon[0].sub("DATA")) >= 10:
            wk = weapon[0].sub("DNAM")[0] if weapon[0].sub("DNAM") else 1  # (Skyrim's: its weapon, by its skill)
            damage = max(damage, float(_u16(weapon[0].sub("DATA"), 8)) * (1.0 + self.r.weapon_skill(npc, wk) / 200.0))
        damage *= damage_mult  # (its own perks: Bleak Falls' wounded spider bites at 13%)
        _aggression, confidence = self.r.ai(npc)
        aggression = 0 if race.editor_id() in PREY else 1 if race.editor_id() in NEUTRAL else 2
        flags = (4 if race.editor_id() in UNDEAD else 0) | (8 if "death" not in s["roles"] else 0) | \
            (16 if race.editor_id() in WATER else 0) | (32 if set_key in UPRIGHT_SETS else 0) | \
            (64 if race.editor_id() in DRAGON_RACES else 0)
        wkind = weapon[0].sub("DNAM")[0] if weapon is not None and weapon[0].sub("DNAM") else 0
        power, bite, bite_poison, spit = self._race_attacks(race)
        # Its size: the race's height (a giant frostbite spider's 1.9, a small one's 0.75) by
        # the NPC's own (NAM6); its numbers in metres scale with it
        rdata = race.sub("DATA")
        female = bool(npc.sub("ACBS") and _u32(npc.sub("ACBS")) & 1)
        scale = struct.unpack_from("<f", rdata, 20 if female else 16)[0] if rdata and len(rdata) >= 24 else 1.0
        nam6 = self.r.template(npc, 0x1).sub("NAM6") or npc.sub("NAM6")
        scale *= struct.unpack_from("<f", nam6)[0] if nam6 and len(nam6) >= 4 else 1.0
        scale = min(max(scale, 0.25), 4.0)
        height, radius, reach = height * scale, radius * scale, reach * scale
        dragon = None
        if race.editor_id() in DRAGON_RACES:  # (its breath, bite, level and the bones it leaves)
            element, breath, extra = self._dragon_spells(npc)
            damage += extra
            reach = DRAGON_REACH_M
            dragon = (element, breath, self.r.level(npc), self._dragon_bones(set_key))
        self.kinds[key] = {"name": name, "title": title, "health": self.r.health(npc, race), "damage": damage,
                           "dragon": dragon,
                           "reach": min(max(reach, 0.8), DRAGON_REACH_M if dragon else MAX_REACH_M), "aggression": aggression, "confidence": confidence,
                           "height": height, "radius": max(radius, 0.2), "set": set_key, "flags": flags,
                           "weapon": wkind, "corpse": self._corpse(title, npc), "armor": 0,
                           "power": power, "bite": bite, "bite_poison": bite_poison, "spit": spit, "scale": scale,
                           "leveling": self.r.leveling(npc, race), "shouts": shouts}
        self.stats["creature kinds"] += 1
        return name

    def _same_one(self, npc):
        """Its spell list's record is the same kind of actor as it is (by name: an Arch Necromancer's
        spells for an Arch Necromancer - not a Bandit Mage's for a Bandit Thug of the same leveled list)."""
        return self.r.name(self.r.template(npc, 0x8), 1) == self.r.name(npc)

    def _mage(self, npc):
        """A mage's spells (its spell list's, through its leveled lists at about LEVEL): its strongest
        aimed Destruction spell - (element 1 fire 2 frost 3 shock, damage, area m) - the creature its
        conjuration calls (the NPC's global id, 0 none) and its own healing (magnitude)."""
        rec = self.r.template(npc, 0x8)
        spells = []

        def take(form, plugin, depth=0):
            r = self.lo.record(self.lo.global_id(plugin, form), types=("SPEL", "LVSP"))
            if r is None or depth > 4:
                return
            if r.type == "SPEL":
                spells.append(r)
                return
            best = None
            for t, v in r.subrecords():  # (a leveled list: its highest entry within reach)
                if t == "LVLO" and len(v) >= 8 and struct.unpack_from("<H", v)[0] <= LEVEL:
                    best = _u32(v, 4)
            if best is not None:
                take(best, r.plugin, depth + 1)
        for k, v in rec.subrecords():
            if k == "SPLO" and len(v) == 4:
                take(_u32(v), rec.plugin)
        bolt, summon, heal = (0, 0.0, 0.0), 0, 0.0
        for sp in spells:
            mag, area, mgef, first = 0.0, 0, None, True
            for t, v in sp.subrecords():
                if t == "EFID" and len(v) == 4:
                    mgef = self.lo.record(self.lo.global_id(sp.plugin, _u32(v)), types=("MGEF",))
                elif t == "EFIT" and len(v) >= 12 and mgef is not None and first:
                    first = False  # (its own effect: the first - those after are perks' extras, Disintegrate's 200)
                    if (mgef.editor_id() or "").startswith("Perk"):
                        continue
                    mag, area = struct.unpack_from("<fI", v)
                    d = mgef.sub("DATA")
                    if not d or len(d) < 88:
                        continue
                    resist, arch, av, delivery = struct.unpack_from("<i", d, 16)[0], _u32(d, 64), struct.unpack_from("<i", d, 68)[0], _u32(d, 84)
                    element = {41: 1, 43: 2, 42: 3}.get(resist, 0)
                    if element and delivery == 2 and mag > bolt[1]:  # (aimed: a bolt, a stream, a ball)
                        bolt = (element, mag, area * 0.3048)
                    elif arch == ARCH_SUMMON and _u32(d, 8) and not summon:
                        summon = self.lo.global_id(mgef.plugin, _u32(d, 8))
                    elif arch == 0 and av == 24 and delivery == 0 and not (_u32(d) & 0x5):  # (Health restored on itself: not hostile, not detrimental)
                        heal = max(heal, mag)
        return bolt, summon, heal

    def _shouts(self, npc):
        """The shouts it knows (its spell list's SHOU: Skyrim's draugr bosses - Unrelenting Force,
        Frost Breath, Disarm): 1 force, 2 frost, 4 fire, 8 disarm."""
        mask = 0
        rec = self.r.template(npc, 0x8)  # (its template's when it uses its spell list)
        for k, v in rec.subrecords():
            if k != "SPLO" or len(v) < 4:
                continue
            s = self.lo.record(self.lo.global_id(rec.plugin, _u32(v)), types=("SHOU",))
            eid = (s.editor_id() or "").lower() if s is not None else ""
            mask |= 1 if "unrelentingforce" in eid else 2 if "frostbreath" in eid else 4 if "firebreath" in eid else \
                8 if "disarm" in eid else 0
        if mask:
            self.stats["kinds that shout"] += 1
        return mask

    def _dragon_spells(self, npc):
        """A dragon's breath - its element (1 fire, 2 frost) and damage a second (its breath
        shout's spell) - and the bite damage its crDragonUnarmedDamage adds."""
        element, breath, extra = 1, 20.0, 0.0
        for t, v in npc.subrecords():
            if t != "SPLO" or len(v) != 4:
                continue
            rec = self.lo.record(self.lo.global_id(npc.plugin, _u32(v)), types=("SPEL", "SHOU"))
            eid = (rec.editor_id() or "").lower() if rec is not None else ""
            if rec is not None and rec.type == "SPEL" and "unarmeddamage" in eid:
                for tt, vv in rec.subrecords():
                    if tt == "EFIT" and len(vv) >= 12:
                        extra = struct.unpack_from("<f", vv)[0]
                        break
            if rec is not None and rec.type == "SHOU" and "breath" in eid:
                element = 2 if "frost" in eid else 1
                for tt, vv in rec.subrecords():  # (its first word's spell)
                    if tt == "SNAM" and len(vv) >= 8:
                        sp = self.lo.record(self.lo.global_id(rec.plugin, _u32(vv, 4)), types=("SPEL",))
                        for t2, v2 in (sp.subrecords() if sp is not None else ()):
                            if t2 == "EFIT" and len(v2) >= 12:
                                breath = max(struct.unpack_from("<f", v2)[0], 1.0)
                                break
                        break
        return element, breath, extra

    def _dragon_bones(self, set_key):
        """The kind drawn where a dragon died: its skeleton, bare (Skyrim's decaydragon.nif)."""
        name = "dragon_bones"
        if any(k["name"] == name for k in self.kinds.values()):
            return name
        batches = self._skin_batches(set_key, [DRAGON_BONES_NIF])
        if not batches:
            self.stats["no dragon bones"] += 1
            return "-"
        s = self.sets[set_key]
        world, kept, hk = s["world"], s["kept"], s["hkobj"]
        parents = []
        for b in kept:
            p = hk.parents[hk.bones.index(b)]
            while p >= 0 and hk.bones[p] not in kept:
                p = hk.parents[p]
            parents.append(kept.index(hk.bones[p]) if p >= 0 else -1)
        joints = [tuple(C @ world[b][:3, 3] / UNITS_PER_METER) for b in kept]
        home = (0, (0.0, 0.0, 0.0), 0.0, 0.0, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, 0)
        write_sknpc(os.path.join(self.out_dir, name + ".sknpc"), name, (parents, joints, False), [], home, batches, {})
        self.kinds[("dragon bones",)] = {"name": name, "title": "Dragon Skeleton", "health": 1, "damage": 0, "reach": 1.0,
                                         "aggression": 0, "confidence": 0, "height": 3.0, "radius": 3.0, "set": set_key,
                                         "flags": 8 | 128, "weapon": 0, "corpse": -1, "armor": 0, "dragon": None}
        return name

    # --- people ------------------------------------------------------------------------

    def _donor_faces(self):
        """(race eid, female) -> [donor NPC (record, gid)]: unique NPCs with FaceGen heads."""
        if self.donors is None:
            facegen = set()
            for a in self.models.mesh_archives:
                facegen.update(a.list("meshes\\actors\\character\\facegendata\\facegeom"))
            self.donors = {}
            for gid, npc in self.r.npcs.items():
                if npc.sub("TPLT") or not npc.sub("RNAM"):
                    continue
                plugin = self.lo.plugins[gid >> 24].name.lower()
                head = f"meshes\\actors\\character\\facegendata\\facegeom\\{plugin}\\{gid & 0xFFFFFF:08x}.nif"
                if head not in facegen:
                    continue
                race = self.r.races.get(self.lo.global_id(npc.plugin, _u32(npc.sub("RNAM"))))
                if race is None or race.editor_id() not in PLAYABLE:
                    continue
                key = (race.editor_id(), self.r.female(npc))
                if len(self.donors.setdefault(key, [])) < 12:
                    self.donors[key].append((npc, gid))
        return self.donors

    def person(self, npc_gid, opening=False, hold=None, quest=False):
        """hold: a guard's (its crime faction's editor id) - one of Skyrim's hold guards, at
        peace till you break the law (the engine's crime.c)."""
        npc = self.r.npcs[npc_gid]
        title = self.r.name(npc) if hold is None else hold[len("CrimeFaction"):] + " Guard"
        race = self.r.race(npc)
        dremora = opening and race is not None and race.editor_id() not in PLAYABLE and \
            (race.editor_id() == "DremoraRace" or (title or "").lower().startswith("tortur"))
        # (Conjuration's Dremora Lord; Helgen Keep's Torturer - of a race of his own, a Nord's head here)
        if race is None or (race.editor_id() not in PLAYABLE and not dremora) or (
                title not in HOSTILE_PEOPLE and hold is None and not quest and not (opening and (title in OPENING_PEOPLE or dremora))):
            if opening:
                self.stats[f"opening skipped: {title}"] += 1
            return None
        female = self.r.female(npc)
        weapon = self._weapon(npc)
        wkind = weapon[0].sub("DNAM")[0] if weapon is not None and weapon[0].sub("DNAM") else 0
        style = 2 if wkind in (7, 9) else 1 if wkind >= 5 else 0 if wkind else 3
        key = ("person", title, race.editor_id(), female, style, hold, quest)  # (one look per kind of fighter)
        if key in self.kinds:
            return self.kinds[key]["name"]
        donors = self._donor_faces().get((race.editor_id(), female)) or self._donor_faces().get((race.editor_id(), not female))
        if not donors and dremora:  # (no FaceGen head of its own kind in the pack: a Nord's under the helm)
            donors = self._donor_faces().get(("NordRace", female)) or self._donor_faces().get(("NordRace", not female))
        if not donors:
            self.stats["no donor face"] += 1
            return None
        donor, donor_gid = donors[(npc_gid * 2654435761 >> 8) % len(donors)]
        b = self.people.builder
        race_id = self.lo.global_id(race.plugin, race.form_id)
        head = b.parts(donor, donor_gid)
        if head is None:
            return None
        # The encounter's outfit (its leveled lists at about LEVEL), the skin where it's bare
        worn, covered, outfit_items = [], 0, []
        outfit = self.r.outfit(npc)
        inam = (outfit.sub("INAM") or b"") if outfit is not None else b""
        for armo in [a for i in range(len(inam) // 4)
                     for a in self.r.all_items(self.lo.global_id(outfit.plugin, _u32(inam, i * 4)), ("ARMO",))]:
            slots = b._armor_slots(armo)
            if slots & covered:
                continue
            addons = b._armor_addons(armo, race_id, female)
            if addons:
                worn += [p for _s, p in addons]
                covered |= slots
                outfit_items.append(armo)
        skin = self.lo.record(self.lo.global_id(race.plugin, _u32(race.sub("WNAM"))), types=("ARMO",)) \
            if race.sub("WNAM") else None
        body = [p for sl, p in (b._armor_addons(skin, race_id, female) if skin else []) if sl & 0x8C and not sl & covered]
        plan = [(head[2][0][0], bool(covered & 0x3), 0)] + [(p, False, 0) for p in body + worn]
        skeleton, batches = self.people._batches(donor, donor_gid, (female, race_id, plan))
        if batches is None:
            return None
        if weapon is not None:  # (in the right hand; a bow in the left, as Skyrim holds it)
            batches += _rigid_batches(self.people, female, weapon[1], "SHIELD" if wkind == 7 else "WEAPON")
        write_sknpc(os.path.join(self.out_dir, f"person_{len(self.kinds)}.sknpc"), npc.editor_id()[:31], skeleton, [],
                    (0, (0.0, 0.0, 0.0), 0.0, 0.0, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, 0), batches, {})
        name = f"person_{len(self.kinds)}"
        armor = sum(_u32(a.sub("DNAM")) // 100 for a in outfit_items if a.sub("DNAM"))
        damage = float(_u16(weapon[0].sub("DATA"), 8)) * (1.0 + self.r.weapon_skill(npc, wkind) / 200.0) \
            if weapon is not None and weapon[0].sub("DATA") else HUMANOID_DAMAGE
        _aggression, confidence = self.r.ai(npc)
        aggression = 1 if quest else 2  # (a quest's: at peace till its side turns - engine/src/creatures.c)
        vtck = self.r.template(npc, 0x1).sub("VTCK") or npc.sub("VTCK")
        vt = self.lo.record(self.lo.global_id(self.r.template(npc, 0x1).plugin, _u32(vtck)), types=("VTYP",)) if vtck else None
        voice = (vt.editor_id() or "").lower() if vt is not None else ("femaleeventoned" if female else "maleeventoned")
        self.kinds[key] = {"name": name, "title": title, "health": self.r.health(npc, race, True), "damage": damage,
                           "voice": voice, "leveling": self.r.leveling(npc, race, True),
                           "reach": 2.0 if wkind >= 5 else 1.6, "aggression": max(aggression, 1) if hold is None else 0,
                           "confidence": confidence, "height": 1.8, "radius": 0.35,
                           "set": "@female" if female else "@male",
                           "flags": 1 | (2 if wkind in (7, 9) else 0) | (F_GUARD if hold else 0),
                           "weapon": wkind, "corpse": self._corpse(title, npc, outfit_items), "armor": armor, "hold": hold,
                           "mage": self._mage(npc) if self._same_one(npc)
                           else ((0, 0.0, 0.0), 0, 0.0)}
        # (a mage's spells only from the same record as its name and race: a leveled list may pick a mage for
        # the spells and a thug for the rest - Skyrim's would be the one actor)
        if self.kinds[key]["mage"][0][0] and wkind in (0, 2):  # (a mage - no weapon, or a dagger: it casts from afar)
            self.kinds[key]["flags"] |= 2
        self.stats["guard kinds" if hold else "people kinds"] += 1
        return name

    # --- output ------------------------------------------------------------------------

    def write_sets(self):
        for key, s in self.sets.items():
            try:
                motion = animations._root_motion(self.anims, s["project"])
            except KeyError:  # (no animation data for its project - the Seeker's: the engine's own pace for it)
                motion = {}
            speeds = {}
            for role in LOCOMOTION:
                path = s["roles"].get(role)
                stem = path.rsplit("\\", 1)[1][:-4].lower() if path else None
                keys = motion.get(stem) if stem else None
                if keys is None and stem:  # (the generator named a little differently)
                    keys = next((v for k, v in motion.items() if stem in k.replace(" ", "")), None)
                if keys and keys[-1][0] > 0:
                    speeds[role] = (0.0, -keys[-1][2] * UNITS_PER_METER / keys[-1][0])
            animations._write_set(os.path.join(self.out_dir, key + ".skanim"), self.anims, self.anims.read(s["hk"]),
                                  s["rest"], {r: [p] for r, p in s["roles"].items()}, bones=s["kept"], speeds=speeds,
                                  unpaced=tuple(s["roles"]))
            self.stats["clip sets"] += 1


def _rigid_batches(people, female, path, node):
    """A model held on a node of the characters' skeleton (bind pose), as the torch is."""
    world, kept, _parents, _joints, kept_for = people._skeleton(female)
    nif = people.builder._nif(path)
    if nif is None or node not in world:
        return []
    m, bone, out = world[node], kept_for(node), []
    for s in nif.shapes():
        if not s.texture:
            continue
        pos = s.positions.astype(np.float64) @ m[:3, :3].T + m[:3, 3]
        v = np.zeros(len(pos), VERTEX_DTYPE)
        v["pos"] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1) / UNITS_PER_METER
        v["uv"] = np.nan_to_num(s.uvs)
        if s.normals is not None:
            nrm = s.normals.astype(np.float64) @ m[:3, :3].T
            v["n"] = np.clip(np.round(np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1) * 127), -127, 127)
        else:
            v["n"] = (0, 127, 0)
        v["col"] = 255
        v["bone"][:, 0] = v["bone"][:, 1] = v["bone2"] = bone
        v["w"][:, 0] = 255
        cut = int(s.alpha_test) if int(s.alpha_test) > 160 else 0
        tex = texture_pack_name(s.texture) + ("_a" if s.alpha_test else "") + (f"_c{cut}" if cut else "")
        people.models._convert_texture({"texture": tex, "source": s.texture, "flags": 1 if s.alpha_test else 0, "cut": cut})
        out.append((tex, s.alpha_test, v, s.triangles.astype("<u2").reshape(-1), 0))
    return out


POST_MAX_M = 150.0  # (a garrison's outside posts: this far from where they're placed at most)


def px0(r):
    return struct.unpack_from("<f", r.sub("DATA"), 0)[0]


def py0(r):
    return struct.unpack_from("<f", r.sub("DATA"), 4)[0]


def export(lo, models, cells, pack, bounds, people, strings, inside=None, interior_index=None):
    """people: npcs.Exporter (its builder and skeletons); inside / interior_index: the
    interiors exporter and its {cell gid: index} (their spawn points too)."""
    out_dir = os.path.join(pack, "creatures")
    os.makedirs(out_dir, exist_ok=True)
    try:  # (the last export's spawn points: their order kept - _keep_spawn_order)
        prev_spawns = [l for l in open(os.path.join(out_dir, "creatures.txt"), encoding="cp1252") if l.startswith("S|")]
    except OSError:
        prev_spawns = None
    for old in os.listdir(out_dir):
        os.remove(os.path.join(out_dir, old))
    anims = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Animations.bsa"))
    records = Records(lo, strings)
    kinds = Kinds(lo, models, people, records, anims, people.items)
    kinds.out_dir = out_dir
    spawns = []
    crime_holds = crime.crime_factions(lo)
    guard_voices, guard_kind_names = set(), set()

    def consider(r, place, side=0, body=False):
        # (body: one of Skyrim's placed dead - Gunjar in Helgen Keep - lying there to be searched)
        if r.type != "ACHR" or not r.sub("NAME") or not r.sub("DATA") or (r.flags & 0x200 and not body):  # (dead ones lie still)
            return
        base = lo.global_id(r.plugin, _u32(r.sub("NAME")))
        if any(records.race(records.npcs[g]) is not None and records.race(records.npcs[g]).editor_id() in DRAGON_RACES
               for g in records.candidates(base)):
            return  # (dragons: their lairs, below)
        b = records.npcs.get(base) or records.lvln.get(base)
        eid = b.editor_id().lower() if b is not None else ""
        if ("corpse" in eid or "dead" in eid) and not body:
            return
        cands = records.candidates(base)
        if not cands:
            return
        seed = lo.global_id(r.plugin, r.form_id) * 2654435761 >> 8
        pick = cands[seed % len(cands)]
        records.seed = seed
        if place == keep_index and not side and not eid.startswith("mq101"):
            side = 3  # (Helgen Keep's bandits: Skyrim's once Unbound's done - not while you escape)
        name = kinds.creature(pick) or kinds.person(pick, opening=bool(side in (1, 2)))
        if name is None and side == 0 and crime.is_guard(records, records.npcs[pick]):
            g = crime.crime_faction_of(records, records.npcs[pick], crime_holds)
            name = kinds.person(pick, hold=crime_holds[g].editor_id()) if g is not None else None
            guard_voices.add(next((k.get("voice") for k in kinds.kinds.values() if k["name"] == name), None))
            guard_kind_names.add(name)
        if name is None and side == 0 and eid.startswith(THALMOR_BASES):
            name = kinds.person(pick, quest=True)
            side = SIDE_PARTY if eid.startswith("mq201partyguard") else SIDE_THALMOR
            kinds.stats["Thalmor at the Embassy"] += name is not None
        if name is None:
            return
        px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
        # At its post: a fort's garrison stands together outside, each linked to their post - in
        # its halls, its prison, on its walls - and Skyrim's packages put them there (Fort
        # Greymoor's bandits were all fifteen in a heap by the gate, none inside)
        post = linked_post(r, place)
        if post is not None:
            place, (px, py, pz, rz) = post
            kinds.stats["at their linked posts"] += 1
        ambush = 4 if body else int("ambush" in eid)
        if ambush == 1:  # (how it lies in wait: Skyrim's linked furniture - a coffin, an alcove, a throne)
            ambush, at = ambush_kind(r, place)
            if at is not None:
                px, py, pz, rz = at
        web = web_furniture(records.npcs[pick], r)
        if web is not None:  # (Arvel: hung in the spider's web - Skyrim's package sits him in it)
            place, (px, py, pz, rz) = web
            ambush = 5
        # (its own leveling: this NPC's - one kind's spawns can be leveled differently, as Bleak
        # Falls' spider, 1.2x the player's level from 5 to 14)
        npc = records.npcs[pick]
        race = records.race(npc)
        person = name.startswith("person_")
        gid_ref = lo.global_id(r.plugin, r.form_id)
        quest_ref = gid_ref in companions.QUEST_REFS | college.QUEST_REFS | thieves.QUEST_REFS  # (one a quest finds by its reference: Morokei)
        mult, lv, lo_, hi, h0, hper = records.leveling(npc, race, person)
        spawns.append(f"S|{name}|{px / UNITS_PER_METER:.2f}|{pz / UNITS_PER_METER:.2f}|{-py / UNITS_PER_METER:.2f}|"
                      f"{-rz:.3f}|{place}|{ambush}|{mult}|{lv}|{lo_}|{hi}|{h0:g}|{hper:g}" +
                      (f"|{side}" if side or quest_ref else "") +
                      (f"|{gid_ref:x}" if side >= SIDE_THALMOR or quest_ref else ""))
        if name in guard_kind_names:  # (a guard's patrol: its linked markers, one to the next)
            route = patrol_route(r, place)
            if route:
                spawns.append("R|" + ";".join(f"{x / UNITS_PER_METER:.2f},{-y / UNITS_PER_METER:.2f}" for x, y in route))
                kinds.stats["guards on patrol"] += 1

    # Where every placed reference is, for the linked posts: interiors' (exported ones: their
    # index) and the world's
    ref_at = {}
    if inside is not None and interior_index:
        for cg, idx in interior_index.items():
            for rr in inside.inner[cg]["refs"]:
                ref_at[lo.global_id(rr.plugin, rr.form_id)] = (idx, rr)
    for (_x, _y), cell in cells.items():
        for rr in cell["refs"]:
            ref_at.setdefault(lo.global_id(rr.plugin, rr.form_id), (-1, rr))
    for n, (_prefix, (wcells, keys)) in enumerate(getattr(inside, "cities", {}).items()):
        for key in keys:
            for rr in wcells[key]["refs"]:
                ref_at.setdefault(lo.global_id(rr.plugin, rr.form_id), (-2 - n, rr))

    def ambush_kind(r, place):
        """(spawn flags, (x, y, z, rz) of the furniture or None): 6 in a sarcophagus (a coffin),
        7 standing in a wall's alcove, 8 sat on a throne, 9 lying in a burial shelf or a coffin;
        1 standing in wait."""
        for t, v in r.subrecords():
            if t != "XLKR" or len(v) < 4:
                continue
            target = ref_at.get(lo.global_id(r.plugin, _u32(v, 4 if len(v) >= 8 else 0)))
            if target is None or target[0] != place or not target[1].sub("NAME"):
                continue
            base = lo.record(lo.global_id(target[1].plugin, _u32(target[1].sub("NAME"))), types=("FURN",))
            beid = (base.editor_id() or "").lower() if base is not None else ""
            # (9: lying - a burial shelf's, entered from the side, or a Dragon Priest's coffin; the
            # upright sarcophagus's clip had the overlord stand out of a lying coffin, snagging)
            kind = 9 if ("alcovebg" in beid or "dragonpriestcoffin" in beid) else \
                6 if ("coffin" in beid or "sarcophag" in beid) else 7 if "alcove" in beid else \
                8 if ("throne" in beid or "chair" in beid) else 0
            if kind and target[1].sub("DATA"):
                tx, ty, tz, _a, _b, trz = struct.unpack_from("<6f", target[1].sub("DATA"))
                kinds.stats[f"ambush in furniture {kind}"] += 1
                return kind, (tx, ty, tz, trz)
        return 1, None

    def patrol_route(r, place):
        """A guard's patrol: from its linked reference (Skyrim's patrol packages walk the linked
        ref and on along each marker's own link) - [(x, y)] in Skyrim units, 2 or more, or None."""
        pts, seen, cur = [], set(), r
        for _ in range(24):
            nxt = None
            for t, v in cur.subrecords():
                if t == "XLKR" and len(v) >= 4 and (len(v) < 8 or _u32(v) == 0):  # (the plain link, no keyword)
                    nxt = lo.global_id(cur.plugin, _u32(v, 4 if len(v) >= 8 else 0))
                    break
            if nxt is None or nxt in seen or nxt not in ref_at or ref_at[nxt][0] != place:
                break
            seen.add(nxt)
            cur = ref_at[nxt][1]
            if not cur.sub("DATA") or cur.type == "ACHR":
                break
            x, y = struct.unpack_from("<2f", cur.sub("DATA"))
            pts.append((x, y))
        return pts if len(pts) >= 2 else None

    def web_furniture(npc, r):
        """(place, (x, y, z, rz)) of the web an NPC's package holds it in (WebFurnitureMarker:
        Arvel the Swift, caught by Bleak Falls Barrow's spider), or None."""
        for t, v in npc.subrecords():
            if t != "PKID":
                continue
            pack = lo.record(lo.global_id(npc.plugin, _u32(v)), types=("PACK",))
            if pack is None or "web" not in (pack.editor_id() or "").lower():
                continue
            ptda = pack.sub("PTDA")
            if not ptda or len(ptda) < 8 or _u32(ptda) not in (0, 3):  # (0: a reference; 3: its linked one)
                continue
            if _u32(ptda) == 3:
                link = next((v for t, v in r.subrecords() if t == "XLKR" and len(v) >= 4), None)
                gid = lo.global_id(r.plugin, _u32(link, 4 if len(link) >= 8 else 0)) if link else None
            else:
                gid = lo.global_id(pack.plugin, _u32(ptda, 4))
            target = ref_at.get(gid) if gid is not None else None
            if target is not None and target[1].sub("DATA"):
                tx, ty, tz, _a, _b, trz = struct.unpack_from("<6f", target[1].sub("DATA"))
                return target[0], (tx, ty, tz, trz)
        return None

    def linked_post(r, place):
        """(place, (x, y, z, rz)) of the post an actor's linked to, if it stands elsewhere: another
        place (inside), or - a garrison's (enable-parented) - up to POST_MAX_M away outside."""
        for t, v in r.subrecords():
            if t != "XLKR" or len(v) < 4:
                continue
            target = ref_at.get(lo.global_id(r.plugin, _u32(v, 4 if len(v) >= 8 else 0)))
            if target is None or not target[1].sub("DATA"):
                continue
            tplace, tr = target
            tx, ty, tz, _a, _b, trz = struct.unpack_from("<6f", tr.sub("DATA"))
            if tplace != place and tplace >= 0:
                return tplace, (tx, ty, tz, trz)
            if tplace == place and r.sub("XESP") and math.hypot(tx - px0(r), ty - py0(r)) < POST_MAX_M * UNITS_PER_METER:
                return tplace, (tx, ty, tz, trz)
        return None

    keep_index, keep_gid = None, None
    if inside is not None and interior_index:
        for cg, idx in interior_index.items():
            c = inside.inner[cg].get("cell")
            if c is not None and (c.editor_id() or "").lower() == OPENING_KEEP:
                keep_index, keep_gid = idx, cg
    min_x, min_y, max_x, max_y = bounds
    for (x, y), cell in cells.items():
        if min_x <= x <= max_x and min_y <= y <= max_y:
            for r in cell["refs"]:
                consider(r, -1)
    if inside is not None and interior_index:
        for cg, idx in interior_index.items():
            for r in inside.inner[cg]["refs"]:
                consider(r, idx)
    # The escape scene's two guards (Skyrim creates them at Elenwen's office and sends them down
    # the Solar's stairs with the trapdoor's keys): at the stairs' foot in the dungeon
    if inside is not None and interior_index:
        esc = next((g for g, r in records.npcs.items() if r.editor_id() == ESCAPE_GUARD), None)
        for cg, idx in interior_index.items():
            for rr in inside.inner[cg]["refs"]:
                t = rr.sub("XTEL")
                if esc is None or (rr.editor_id() or "") != ESCAPE_DOOR or not t:
                    continue
                dest = ref_at.get(lo.global_id(rr.plugin, _u32(t)))
                cands = records.candidates(esc)
                if dest is None or not cands:
                    continue
                dx, dy, dz, _a, _b, drz = struct.unpack_from("<6f", t, 4)
                for n in range(2):
                    records.seed = n
                    pick = cands[n % len(cands)]
                    name = kinds.person(pick, quest=True)
                    if name is None:
                        continue
                    npc = records.npcs[pick]
                    mult, lv, lo_, hi, h0, hper = records.leveling(npc, records.race(npc), True)
                    side_m = (n - 0.5) * 1.6 * UNITS_PER_METER  # (side by side)
                    sx, sy = dx + math.cos(drz) * side_m, dy - math.sin(drz) * side_m
                    spawns.append(f"S|{name}|{sx / UNITS_PER_METER:.2f}|{dz / UNITS_PER_METER:.2f}|{-sy / UNITS_PER_METER:.2f}|"
                                  f"{-drz:.3f}|{dest[0]}|0|{mult}|{lv}|{lo_}|{hi}|{h0:g}|{hper:g}|{SIDE_ESCAPE}|0")
                    kinds.stats["the escape scene's guards"] += 1
    # What Conjuration summons (a summon effect's creature: atronachs, the familiar, a Dremora
    # Lord, the Soul Cairn's dead...): their kinds, by the NPC (SUMMON_KINDS, for the spells)
    SUMMON_KINDS.clear()
    for p in lo.plugins:
        for m in p.records_of_type("MGEF"):
            d = m.sub("DATA")
            if not d or len(d) < 72 or _u32(d, 64) != ARCH_SUMMON or not _u32(d, 8):
                continue
            g = lo.global_id(m.plugin, _u32(d, 8))
            if g in SUMMON_KINDS or g not in records.npcs:
                continue
            name = kinds.creature(g) or kinds.person(g, opening=True) or kinds.person(g, quest=True)
            if name:  # (people summoned too - Call of Valor's heroes of Sovngarde, as a quest's: they were left out)
                SUMMON_KINDS[g] = name
            else:
                kinds.stats[f"summon not made: {records.npcs[g].editor_id()}"] += 1
    for eid in college.QUEST_KINDS + thieves.QUEST_KINDS:  # (made though nothing places them: a quest does - Containment's anomalies, Ancano)
        for g, r in records.npcs.items():
            if r.editor_id() == eid and not (kinds.creature(g) or kinds.person(g, quest=True)):
                kinds.stats[f"quest kind not made: {eid}"] += 1
    SUMMON_BY_WORD.clear()
    for word, eid in SCRIPT_SUMMONS:  # (summoned by a script, no creature on the effect: by its NPC)
        for g, r in records.npcs.items():
            if r.editor_id() == eid:
                name = kinds.creature(g) or kinds.person(g, opening=True)
                if name:
                    SUMMON_BY_WORD[word] = name
    # Helgen Keep's soldiers in Skyrim's opening: there disabled, its scripts enabling the side
    # you didn't choose to come for you (the Imperials with Ralof, the Stormcloaks with Hadvar;
    # the torturer with Ralof too) - here marked with the side (the engine wakes the one)
    if keep_gid is not None:
        for p in lo.plugins:
            for local, c in p.interior_cells().items():
                if lo.global_id(p, local) != keep_gid:
                    continue
                for r in c["refs"]:
                    if r.type != "ACHR" or not r.sub("NAME"):
                        continue
                    b = lo.record(lo.global_id(p, _u32(r.sub("NAME"))), types=("NPC_", "LVLN"))
                    eid = (b.editor_id() or "").lower() if b is not None else ""
                    if eid == OPENING_BODY and not r.flags & RECORD_INITIALLY_DISABLED:
                        consider(r, keep_index, 1, body=True)
                    elif eid.startswith("mq101torturer") and not r.flags & RECORD_INITIALLY_DISABLED:
                        consider(r, keep_index, 1)
                    elif r.flags & RECORD_INITIALLY_DISABLED and eid.startswith("mq101imperial"):
                        consider(r, keep_index, 1)
                    elif r.flags & RECORD_INITIALLY_DISABLED and eid.startswith("mq101stormcloak"):
                        consider(r, keep_index, 2)
    # The walled cities' and the worlds of their own (Labyrinthian's skeletons, the grottos'
    # spriggans): place -2 - n, n their order in worlds.txt
    for n, (_prefix, (wcells, keys)) in enumerate(getattr(inside, "cities", {}).items()):
        for key in keys:
            for r in wcells[key]["refs"]:
                consider(r, -2 - n)
    # Dragons: the lairs' (placed, waiting for Skyrim's quests to wake them: here they wait at
    # their lair) and those met at random (by the player's level: the engine picks)
    dragon_npcs = {r.editor_id(): g for g, r in records.npcs.items() if r.editor_id() in DRAGON_NPCS}
    for eid in DRAGON_NPCS:
        if eid in dragon_npcs:
            kinds.creature(dragon_npcs[eid])
    for g, r in records.npcs.items():  # (the opening's: Alduin over Helgen, the carts' horse - engine/src/intro.c)
        if r.editor_id() in OPENING_NPCS:
            kinds.creature(g)
    seen_lairs = set()
    for p in lo.plugins:
        try:
            raw = p.exterior_cells("Tamriel", keep_removed=True)
        except KeyError:
            continue
        for (x, y), c in raw.items():
            if not (min_x <= x <= max_x and min_y <= y <= max_y):
                continue
            for r in c["refs"]:
                if r.type != "ACHR" or not r.sub("NAME") or not r.sub("DATA"):
                    continue
                gid = lo.global_id(p, r.form_id)
                base = lo.global_id(p, _u32(r.sub("NAME")))
                b = records.npcs.get(base) or records.lvln.get(base)
                ref_eid = (r.editor_id() or "").lower()
                if b is None or gid in seen_lairs or (b.editor_id() or "").lower().startswith(DRAGON_QUEST_PREFIXES) or \
                        ref_eid.startswith("mq") or "corpse" in ref_eid:
                    continue
                cands = [g for g in records.candidates(base) if records.race(records.npcs[g]) is not None and
                         records.race(records.npcs[g]).editor_id() in DRAGON_RACES]
                if not cands:
                    continue
                seen_lairs.add(gid)
                name = kinds.creature(cands[0])
                if name is None:
                    continue
                px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
                npc = records.npcs[cands[0]]
                mult, lv, lo_, hi, h0, hper = records.leveling(npc, records.race(npc))
                spawns.append(f"S|{name}|{px / UNITS_PER_METER:.2f}|{pz / UNITS_PER_METER:.2f}|{-py / UNITS_PER_METER:.2f}|"
                              f"{-rz:.3f}|-1|2|{mult}|{lv}|{lo_}|{hi}|{h0:g}|{hper:g}")
    # The stables' horses: their kind and where Skyrim keeps each (disabled till bought)
    horse_lines = []
    for eid, seller, markers in PLAYER_HORSES:
        g = next((g for g, r in records.npcs.items() if r.editor_id() == eid), None)
        name = kinds.creature(g) if g is not None else None
        at = None
        for p in lo.plugins if name else ():
            for world in ("Tamriel", "WhiterunWorld"):
                try:
                    raw = p.exterior_cells(world, keep_removed=True)
                except KeyError:
                    continue
                for _k, c in raw.items():
                    for r in c["refs"]:
                        named = (r.editor_id() or "") in markers and at is None  # (no horse placed: Skyrim's script brings it to the stables' marker)
                        if r.sub("DATA") and (named or (r.type == "ACHR" and r.sub("NAME") and lo.global_id(p, _u32(r.sub("NAME"))) == g)):
                            px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
                            at = (-1 if world == "Tamriel" else -2, px / UNITS_PER_METER, pz / UNITS_PER_METER,
                                  -py / UNITS_PER_METER, -rz)
        if name and at:
            horse_lines.append(f"H|{name}|{seller.lower()}|{at[0]}|{at[1]:.2f}|{at[2]:.2f}|{at[3]:.2f}|{at[4]:.3f}")
        kinds.stats["player horses" if name and at else f"player horse missing: {eid}"] += 1
    with open(os.path.join(pack, "horses.txt"), "w") as f:
        f.write("\n".join(horse_lines) + "\n")
    kinds.write_sets()
    lines = []
    for k in kinds.kinds.values():
        title = k["title"].replace("|", "/")
        lines.append(f"K|{k['name']}|{title}|{k['health']:g}|{k['damage']:g}|{k['reach']:.2f}|{k['aggression']}|"
                     f"{k['confidence']}|{k['height']:.2f}|{k['radius']:.2f}|{k['set']}|{k['flags']}|{k['weapon']}|"
                     f"{k['corpse']}|{k['armor']}|{k.get('power', 2.0):g}|{k.get('bite', 1.0):g}|"
                     f"{k.get('bite_poison', (0, 0))[0]:g}|{k.get('bite_poison', (0, 0))[1]:g}|"
                     f"{k.get('spit', (0, 0))[0]:g}|{k.get('spit', (0, 0))[1]:g}|{k.get('scale', 1.0):.3f}|"
                     f"{k.get('voice', '-')}|" + "|".join(f"{x:g}" for x in k.get("leveling", (0, 1, 1, 0, k["health"], 0))))
    for k in kinds.kinds.values():  # (guards: their hold's crime faction)
        if k.get("hold"):
            lines.append(f"W|{k['name']}|{k['hold']}")
    for k in kinds.kinds.values():  # (mages: their bolt - element, damage, area -, what they conjure, their healing)
        (element, damage, area), summon, heal = k.get("mage") or ((0, 0.0, 0.0), 0, 0.0)
        if element or summon or heal:
            called = SUMMON_KINDS.get(summon)  # (the player's conjurations made them: the same atronachs)
            lines.append(f"M|{k['name']}|{element}|{damage:g}|{area:.2f}|{called or '-'}|{heal:g}")
    for k in kinds.kinds.values():  # (those that shout: draugr bosses)
        if k.get("shouts"):
            lines.append(f"U|{k['name']}|{k['shouts']}")
    for k in kinds.kinds.values():  # (dragons: breath element, damage a second, level, bones)
        if k.get("dragon"):
            element, breath, level, bones = k["dragon"]
            lines.append(f"G|{k['name']}|{element}|{breath:g}|{level}|{bones}")
    with open(os.path.join(out_dir, "creatures.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines + _keep_spawn_order(prev_spawns, spawns)) + "\n")
    def ref_place(g):
        if g not in ref_at or not ref_at[g][1].sub("DATA"):
            return None
        place, rr = ref_at[g]
        x, y, z, _a, _b, rz = struct.unpack_from("<6f", rr.sub("DATA"))
        return place, x / UNITS_PER_METER, z / UNITS_PER_METER, -y / UNITS_PER_METER, -rz

    guard_voices.discard(None)
    crime.export(lo, pack, strings, people.talk, records, ref_place, getattr(people, "exported", []), guard_voices, ref_at)
    by_kind = Counter(s.split("|")[1] for s in spawns)
    print(f"creatures: {len(kinds.kinds)} kinds, {len(kinds.sets)} clip sets, {len(spawns)} spawn points;",
          dict(kinds.stats))
    print("  spawns:", ", ".join(f"{kinds_title(kinds, n)} {c}" for n, c in by_kind.most_common()))


def kinds_title(kinds, name):
    return next((k["title"] for k in kinds.kinds.values() if k["name"] == name), name)


def _keep_spawn_order(prev, spawns):
    """The spawn points (S lines, each with its patrol's R lines) in the last export's order, new
    ones after: a save keeps the creatures killed and the bodies searched by their numbers here -
    10-21's wider map renumbered them all. A spawn is its place (x, y, z: the same spot twice is
    told apart by order)."""
    groups = []
    for line in spawns:
        if line.startswith("S|") or not groups:
            groups.append([line])
        else:
            groups[-1].append(line)
    def key(line):
        f = line.split("|")
        return tuple(round(float(v), 1) for v in f[2:5])
    if not prev:
        return spawns
    by_key = {}
    for g in groups:
        by_key.setdefault(key(g[0]) if g[0].startswith("S|") else None, []).append(g)
    out, used = [], set()
    for l in prev:
        same = by_key.get(key(l))
        if same:
            g = same.pop(0)
            out += g
            used.add(id(g))
    for g in groups:
        if id(g) not in used:
            out += g
    return out
