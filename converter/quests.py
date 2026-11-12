"""The main quest's opening (Milestone: quests): Before the Storm, Bleak Falls Barrow and
Dragon Rising - the Jarl of Whiterun, Farengar's Dragonstone, the dragon at the Western
Watchtower. Skyrim runs its quests in Papyrus scripts, which aren't converted; the engine
plays these three by hand (engine/src/quests.c). From the game's data come what they say -
their voiced lines and subtitles, by topic (DIAL) and the start of the line's text - and the
items they hand over (the Dragonstone, the rewards' leveled lists).

  PACK_DIR/quests.txt   tab-separated:
    S  scene  speaker (NPC editor id, lowercase)
    R  scene  voice file (under voice/)  text           one per response, in order
    I  name  kind  index    an item (kind I) or leveled list (kind L): items/items.txt's
    C  scene  phase  action  variant  speaker  race  voice file  text
         the opening's scenes (Skyrim's SCEN records of OPENING_QUESTS, engine/src/intro.c):
         each line one response of a scene's dialogue action, in phase order; speaker the
         NPC's editor id (lowercase) or the quest alias's name; race: whom it's for, "-"
         anyone, else the player's race (editor id, lowercase) and / or sex ("female",
         "male"), joined by ",", "!" before one: not; variant: a topic's other lines (by
         condition) after its first
    A  scene  start  end  index  who  package  targets
         the scenes' other actions: someone's package (Skyrim's AI: walk there, use that,
         Alduin's perches and strafing runs) from phase start to end; who as C's speaker;
         targets: the references it names (markers, as P's names), comma-separated
    P  marker  x  y  z  heading     a named reference of the opening (engine space; radians);
         unnamed ones a package names: "#" and its form id (hex)
    W  route  x  y  z               the cart's road into Helgen, a point every few metres
    O  name  model index            a model it moves (objects/models.txt: the cart)
"""
import os
import re
import struct

from dialogue import StringTables, line_is
import companions
import college
import thieves
GUILDS = (companions, college, thieves)  # (the side questlines' data modules)

# The scenes: (key, speaker, [(topic editor id or "" for any of the quest's, quest, the
# line's opening words or "" for its first)])
SCENES = (
    ("mq102_jarl", "BalgruufTheGreater", [("MQ102BalgruufIntroTopic", "MQ102", "#000D07FD"),
                                          ("MQ102BalgruufIntroHelgenB2", "MQ102", ""),
                                          ("MQ102BalgruufReward", "MQ102", "#000D553E")]),
    ("mq103_farengar", "FarengarSecretFire", [("MQ103FarengarIntroTopic", "MQ103", "#000D50E7"),
                                              ("MQ103FarengarIntroA0", "MQ103", ""),
                                              ("", "MQ103", "#000D50E1")]),
    # (the Jarl's, at Farengar's table: walked over with you from the throne, and back again)
    ("mq103_jarl_intro", "BalgruufTheGreater", [("", "MQ103", "#000D50F1")]),
    ("mq103_jarl_leave", "BalgruufTheGreater", [("", "MQ103", "#00000000")]),
    # Arvel the Swift in the web (MS13): before the spider's dead, after, his pleas, cut down
    ("arvel_spider", "MS13Arvel", [("MS13ArvelWebStuckTopic", "MS13", "#000AA84C")]),
    ("arvel_killed", "MS13Arvel", [("MS13ArvelWebStuckTopic", "MS13", "#00039626")]),
    ("arvel_plea1", "MS13Arvel", [("", "MS13", "#0003BC5C")]),
    ("arvel_plea2", "MS13Arvel", [("", "MS13", "#0003BC5D")]),
    ("arvel_plea3", "MS13Arvel", [("", "MS13", "#000993E8")]),
    ("arvel_free", "MS13Arvel", [("", "MS13", "#0003BC5E")]),
    ("arvel_flee", "MS13Arvel", [("", "MS13", "#0003BC5F")]),
    # (the bandits by the fire in Bleak Falls Temple, overheard: Skyrim's MS13BleakFallsBarrowCampfireScene - how you
    # learn of the claw without Lucan, its stage 12; and Arvel calling from the web, MS13BleakFallsBarrowArvelShouting)
    ("bfb_campfire", "*", [("", "MS13BleakFallsBarrowCampfireScene", "#0003A177"),
                           ("", "MS13BleakFallsBarrowCampfireScene", "#0003A16E"),
                           ("", "MS13BleakFallsBarrowCampfireScene", "#0004951C"),
                           ("", "MS13BleakFallsBarrowCampfireScene", "#0004951B")]),
    ("arvel_calling", "MS13Arvel", [("", "MS13BleakFallsBarrowArvelShouting", "#0003BC57")]),
    # The Horn of Jurgen Windcaller's end at High Hrothgar: Arngeir, Wulfgar's "Dah", the
    # Greybeards' greeting
    ("mq105_horn_return", "Arngeir", [("MQ105ArngeirReturnHornTopic", "MQ105", "#000341B9"),
                                      ("MQ105ArngeirReturnHornTopic", "MQ105", "#000C522A")]),
    ("mq105_dah", "Wulfgar", [("", "MQ105", "#000C436B")]),
    ("mq105_stand", "Arngeir", [("MQ105ArngeirReturnHornTopic", "MQ105", "#000C5228")]),
    ("mq105_chant", "*", [("", "MQ105", "#000C43AF"), ("", "MQ105", "#000C43AA")]),
    ("mq105_greeted", "Arngeir", [("", "MQ105", "#0007E96D")]),
    # A Blade in the Dark (MQ106): Delphine at the Sleeping Giant, Kynesgrove, Sahloknir
    ("mq106_rent", "Delphine", [("MQ106RentRoomTopic", "MQ106", "#000A56D4")]),
    # Diplomatic Immunity (MQ201): Delphine's plan at the Sleeping Giant, Malborn at the Winking
    # Skeever, Delphine at the Solitude stables
    ("mq201_intro", "Delphine", [("MQ201DelphineIntroTopic", "MQ201", "#00035D52"),
                                 ("MQ201DelphineIntroA1", "MQ201", "#0003B492"),
                                 ("MQ201DelphineIntroA4", "MQ201", "#0003B493"),
                                 ("MQ201DelphineIntro3EntryTopic", "MQ201", "#0003B49A"),
                                 ("MQ201DelphineIntroRiverwoodB1", "MQ201", ""),
                                 ("MQ201DelphineIntroRiverwoodC1", "MQ201", "#000410DC"),
                                 ("MQ201DelphineIntroRiverwoodC3", "MQ201", "#00040F9F")]),
    ("mq201_malborn", "Malborn", [("MQ201MalbornIntroTopic", "MQ201", "#000361DD"),
                                  ("MQ201MalbornEquipmentTopic", "MQ201", "#000361E5")]),
    ("mq201_malborn_ready", "Malborn", [("MQ201MalbornEquipmentReady", "MQ201", "#000361DF")]),
    ("mq201_stables", "Delphine", [("MQ201DelphineCarriageTopic", "MQ201", "#000361E3"),
                                   ("MQ201DelphineMetMalbornYes", "MQ201", "#000361DE"),
                                   ("MQ201DelphineCarriageTopic", "MQ201", "#000361E2"),
                                   ("MQ201DelphineExplainsCarriageRide", "MQ201", "")]),
    # (the party at the Thalmor Embassy: Malborn's bottle, Razelan's scene, the kitchen, Etienne
    # in the dungeon, Delphine back in Riverwood)
    ("mq201_party_malborn", "Malborn", [("MQ201PartyMalbornReadyTopic", "MQ201", "#000362CB")]),
    ("mq201_distraction", "*", [("", "MQ201Party", "#00063B53"), ("", "MQ201Party", "#00063B54"),
                                ("", "MQ201Party", "#000665DA")]),
    ("mq201_kitchen", "Malborn", [("", "MQ201", "#000362C3"), ("", "MQ201", "#000362C6")]),
    ("mq201_etienne", "EtienneRarnis", [("MQ201PrisonerIntroC4", "MQ201", "#00098F7F"),
                                        ("MQ201PrisonerBlockingTopic", "MQ201", "#0006965D")]),
    ("mq201_outro", "Delphine", [("", "MQ201", "#000A791E"),
                                 ("MQ201DelphineOutro2Topic", "MQ201", "#0003BC98"),
                                 ("MQ201DelphineOutro2B1", "MQ201", "#0003BCA0")]),
    ("mq106_intro", "Delphine", [("MQ106DelphineIntroTopic", "MQ106", "#00032902"),
                                 ("MQ106DelphineIntroA0", "MQ106", "#000C65A2"),
                                 ("", "MQ106", "#000328F8"),
                                 ("MQ106DelphineIntroExclusive2EntryTopic", "MQ106", "#00083052"),
                                 ("", "MQ106", "#00032912"),
                                 ("MQ106DelphineIntroEndA2", "MQ106", "#000CA637")]),
    ("mq106_arrive", "Delphine", [("", "MQ106", "#00046812")]),
    ("mq106_alduin_raise", "*", [("", "MQ106", "#000C8952")]),
    ("mq106_sahloknir", "*", [("", "MQ106", "#00035B4A")]),
    ("mq106_alduin_reply", "*", [("", "MQ106", "#00035B4B"), ("", "MQ106", "#000C8EA1")]),
    ("mq106_fight", "Delphine", [("", "MQ106", "#000CA631")]),
    ("mq106_after", "Delphine", [("", "MQ106", "#00061D73"), ("", "MQ106", "#0003290A")]),
    ("mq103_farengar_stone", "FarengarSecretFire", [("MQ103FarengarRetrieveBookTopic", "MQ103", "#000E0EB9")]),
    ("mq104_jarl", "BalgruufTheGreater", [("MQ104BalgruufBlockingTopic", "MQ104", "#000DF1F5"),
                                          ("MQ104BalgruufIntroA1", "MQ104", "#000DF1F8")]),
    ("mq104_jarl_outro", "BalgruufTheGreater", [("MQ104BalgruufOutroA3", "MQ104", ""),
                                                ("MQ104BalgruufOutroB1", "MQ104", ""),
                                                ("MQ104BalgruufReward1", "MQ104", "#000E0EB8")]),
    # Dragon Rising at the Western Watchtower: Irileth and her men, the watchtower's last guard,
    # Mirmulnir, and the men when you take its soul ("*": whoever's voice the line was recorded in)
    ("mq104_irileth_tower", "Irileth", [("", "MQ104", "#000A3F50")]),
    ("mq104_tower_guard", "*", [("", "MQ104", "#000A27B1")]),
    ("mq104_here_he_comes", "*", [("", "MQ104", "#000A27B8")]),
    ("mq104_fight", "Irileth", [("MQ104IrilethBlockingTopic", "MQ104", "#0005DD52")]),
    ("mq104_dragon_dies", "MQ104Dragon", [("", "MQ104", "#000BCF14")]),
    ("mq104_killed", "*", [("", "MQ104", "#000A3F84")]),
    ("mq104_dragonborn", "*", [("", "MQ104", "#000C28C6"),
                               ("", "MQ104", "#000A3F85"),
                               ("", "MQ104", "#000A3F8A"),
                               ("", "MQ104", "#000A3F80")]),
    ("mq104_greybeards_heard", "*", [("", "MQ104", "#0005EE3A")]),
    ("mq104_irileth_after", "Irileth", [("MQ104IrilethBlockingTopic", "MQ104", "#0005DD4F")]),
    # The Way of the Voice (MQ105): High Hrothgar - Arngeir, Einarth's "Ro", Borri's "Wuld"
    ("mq105_arrive", "Arngeir", [("", "MQ105", "#0002F2C4")]),
    ("mq105_intro", "Arngeir", [("MQ105ArngeirIntroA1", "MQ105", "#0002F2C5")]),
    ("mq105_welcome", "Arngeir", [("", "MQ105", "#000649B5"),
                                  ("", "MQ105", "#0002F7A2")]),
    ("mq105_ro", "Einarth", [("", "MQ105", "#000C2D4D")]),
    ("mq105_ro_learned", "Arngeir", [("", "MQ105", "#00035811"),
                                     ("", "MQ105", "#000661C0")]),
    ("mq105_borri", "Arngeir", [("", "MQ105", "#00078B3C"),
                                ("", "MQ105", "#00078B3E")]),
    ("mq105_wuld", "Borri", [("", "MQ105", "#000C39FD")]),
    ("mq105_horn", "Arngeir", [("", "MQ105", "#00030315")]),
    # The Throat of the World (MQ204): Paarthurnax at the summit - a dragon, said as heard: his
    # greeting, his Yol, his answer to your Thu'um, and on Dragonrend (the time Alduin was cast out)
    ("mq204_paar_intro", "Paarthurnax", [("MQ204PaarIntroTopic", "MQ204", "#0003F88B"),
                                         ("", "MQ204", "#0005586E"),
                                         ("", "MQ204", "#00055989")]),
    ("mq204_paar_yol", "Paarthurnax", [("", "MQ204", "#000B89E0"), ("", "MQ204", "#000559DC")]),
    ("mq204_paar_after", "Paarthurnax", [("", "MQ204", "#0005586A")]),
    ("mq204_dragonrend", "Paarthurnax", [("MQ204PaarDragonrend3Topic", "MQ204", "#0003FA40"),
                                         ("MQ204PaarDragonrend3Topic", "MQ204", "#0003FA3F"),
                                         ("MQ204PaarB1", "MQ204", "#0003FA48"),
                                         ("MQ204PaarC1", "MQ204", "#0003FA46"),
                                         ("MQ204PaarC1A1", "MQ204", "#000BBEAA")]),
    # Alduin's Bane (MQ206): Paarthurnax on the Elder Scroll at the Time-Wound; Alduin come, and fled
    ("mq206_paar_scroll", "Paarthurnax", [("MQ206PaarthurnaxBlockingTopic", "MQ206", "#000C64EC")]),
    ("mq206_alduin_flees", "*", [("", "MQ206", "#00044CED")]),
    ("mq206_dragonrend", "Paarthurnax", [("", "MQ206", "#00044969")]),
    # The Fallen (MQ301): Paarthurnax on finding Alduin, then on calling Odahviing; Odahviing trapped
    ("mq301_paar_intro", "Paarthurnax", [("MQ301PaarthurnaxIntroTopic", "MQ301", "#00045919"),
                                         ("MQ301PaarthurnaxIntroA1", "MQ301", "#0004591B")]),
    ("mq301_paar_call", "Paarthurnax", [("MQ301PaarthurnaxCallDragonTopic", "MQ301", "#000D2CF5"),
                                        ("MQ301PaarthurnaxCallDragonA1", "MQ301", ""),
                                        ("MQ301PaarthurnaxCallDragonC1", "MQ301", "#000D2CEF")]),
    ("mq301_odahviing_comes", "Odahviing", [("", "MQ301", "#000D2D70")]),
    ("mq301_trapped", "*", [("", "MQ301", "#00048F06"), ("", "MQ301", "#00048F04")]),
    ("mq301_odahviing", "Odahviing", [("", "MQ301", "#00048F09"), ("MQ301OdahviingIntroTopic", "MQ301", "#00048F02"),
                                      ("MQ301OdahviingB1", "MQ301", ""), ("MQ301OdahviingB2", "MQ301", ""),
                                      ("MQ301OdahviingD1", "MQ301", "")]),
    # The World-Eater's Eyrie (MQ303): the trap opened, Odahviing's flight to Skuldafn
    ("mq303_gatekeeper", "*", [("MQ303GatekeeperTopic", "MQ303", "#0004DE41")]),
    ("mq303_free", "Odahviing", [("", "MQ303", "#0004DE40")]),
    ("mq303_fly", "Odahviing", [("MQ303OdahviingFreeTopic", "MQ303", "#0004DE3A"), ("MQ303OdahviingFreeA1", "MQ303", "")]),
    ("mq303_landed", "Odahviing", [("", "MQ303", "#000E16F6")]),
    # Sovngarde (MQ304) and Dragonslayer (MQ305): Tsun at the Whalebone Bridge, the heroes, the mist
    ("mq304_tsun_bridge", "Tsun", [("", "MQ304", "#0004FA20")]),
    ("mq304_tsun_worthy", "Tsun", [("", "MQ304", "#0005039E")]),
    ("mq305_mist", "*", [("", "MQ305", "#00050398")]),
    ("mq305_again", "*", [("", "MQ305", "#0005094F")]),
    ("mq305_face", "*", [("", "MQ305", "#000EDF4B")]),
    ("mq305_tsun_after", "Tsun", [("", "MQ305", "#0005195A")]),
)
ITEMS = (("dragonstone", "MISC", "MQ103Dragonstone"), ("horn", "MISC", "MQ105Horn"), ("horn_note", "BOOK", "MQ105Note"), ("reward_heavy", "LVLI", "LItemArmorCuirassHeavyReward"),
         ("reward_light", "LVLI", "LItemArmorCuirassLightReward"),
         ("gift_heavy", "LVLI", "LItemEnchArmorHeavyAllNoDragon"),
         ("gift_light", "LVLI", "LItemEnchArmorLightAllNoDragon"),
         # (Thane of Whiterun: the Jarl's weapon - MQ104's JarlWeapon alias; Lucan's for the claw - MS13's)
         ("thane_axe", "LVLI", "FavorRewardEnchAxeBest"), ("claw_reward", "LVLI", "LvlQuestReward02Medium"),
         # (the opening's: the prisoner's rags; Gunjar's gear (Ralof's way out) or the Imperial
         # kit Hadvar's finds)
         ("rags", "ARMO", "ClothesPrisonerTunic"), ("rags_shoes", "ARMO", "ClothesPrisonerShoes"),
         ("cuffs", "ARMO", "PrisonerCuffsPlayer"),  # (bound hands, till they're cut in the keep)
         ("sc_cuirass", "ARMO", "ArmorStormcloakCuirass"), ("sc_boots", "ARMO", "ArmorStormcloakBoots"),
         ("sc_gauntlets", "ARMO", "ArmorStormcloakGauntlets"), ("sc_weapon", "WEAP", "IronWarAxe"),
         ("imp_cuirass", "ARMO", "ArmorImperialLightCuirass"), ("imp_boots", "ARMO", "ArmorImperialLightBoots"),
         ("imp_gauntlets", "ARMO", "ArmorImperialLightGauntlets"), ("imp_weapon", "WEAP", "IronSword"),
         # (A Lovely Letter's two forgeries)
         # (Diplomatic Immunity: the party clothes and invitation; what's in Elenwen's chest)
         ("party_outfit", "ARMO", "MQ201PartyOutfit"), ("party_boots", "ARMO", "MQ201PartyBoots"),
         ("invitation", "BOOK", "MQ201Invitation"), ("esbern_dossier", "BOOK", "MQ201EsbernDossier"),
         ("elenwen_note", "BOOK", "MQ201ElenwenOfficeNote"),
         ("exit_key", "KEYM", "MQ201InterrogationExitKey"),  # (the trapdoor's: its interrogator isn't about)
         # (Elder Knowledge: Septimus's attunement sphere and lexicon, the Elder Scroll itself)
         ("sphere", "MISC", "DA04AttunementSphere"), ("lexicon", "MISC", "DA04DweLexiconCubeBlank01"), ("elder_scroll", "BOOK", "DA04ElderScroll"),
         ("arvel_journal", "BOOK", "MS13ArvelsJournal"),  # (on Arvel: how the claw opens the barrow's door - MS13's alias)
         ("letter_sven", "BOOK", "FreeformRiverwood01LetterSven"), ("letter_faendal", "BOOK", "FreeformRiverwood01LetterFaendal"))
for _g in GUILDS:  # (the Companions', the College's: converter/companions.py, college.py)
    ITEMS += _g.ITEMS
    SCENES += _g.SCENES


# Skyrim's lists of things worn that its conditions and scripts ask after: the Amulets of
# Articulation (a persuasion won), the Thalmor disguise's hoods and robes (MQ201's cut disguise
# script) - I lines named for the list and a number: "tgamuletofarticulationlist0"
WORN_LISTS = ("TGAmuletofArticulationList", "MQ201ThalmorDisguiseHeadItems", "MQ201ThalmorDisguiseBodyItems")


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def export(lo, pack, talk, items, models=None):
    """talk: npcs.Exporter's dialogue.Dialogue (voices, strings); items: the items.Database;
    models: extract_objects.ModelConverter (the opening's cart)."""
    by_eid = {}
    for p in lo.plugins:
        for t in ("QUST", "NPC_", "MISC", "LVLI", "ARMO", "WEAP", "BOOK", "KEYM"):
            for r in p.records_of_type(t):
                by_eid[(t, (r.editor_id() or "").lower())] = r  # (editor ids by any case: "BalgruuftheGreater")
    lines, missing = [], []
    for key, speaker, parts in SCENES:
        npc = by_eid.get(("NPC_", speaker.lower()))
        voice = talk.voice_types.get(lo.global_id(npc.plugin, _u32(npc.sub("VTCK")))) if npc is not None and \
            npc.sub("VTCK") else "*"  # (none of its own - a template's, or no one: whichever voice the line's in)
        responses = []
        for topic, quest, opening in parts:
            got = _topic_responses(lo, talk, by_eid, topic, quest, opening, voice)
            if not got:
                missing.append(f"{key}: {topic or quest} '{opening}'")
            responses += got
        if not responses:
            continue
        lines.append(f"S\t{key}\t{'-' if speaker == '*' else speaker.lower()}")
        for path, text in responses:
            lines.append(f"R\t{key}\t{path}\t" + text.replace("\t", " ").replace("\n", " "))
    for name, rtype, eid in ITEMS:
        rec = by_eid.get((rtype, eid.lower()))
        if rec is None:
            missing.append(f"item {eid}")
            continue
        gid = lo.global_id(rec.plugin, rec.form_id)
        kind, index = items.entry(gid) if items.entry(gid) else (None, None)
        if kind:
            lines.append(f"I\t{name}\t{kind}\t{index}")
    for flst in (r for pl in lo.plugins for r in pl.records_of_type("FLST") if r.editor_id() in WORN_LISTS):
        n = 0
        for t, v in flst.subrecords():
            entry = items.entry(lo.global_id(flst.plugin, _u32(v))) if t == "LNAM" else None
            if entry and entry[0] == "I":
                lines.append(f"I\t{flst.editor_id().lower()}{n}\tI\t{entry[1]}")
                n += 1
    lines += _trees(lo, talk, by_eid, missing)
    lines += _scenes(lo, talk, by_eid, missing)
    lines += _opening(lo, pack, talk, models, missing)
    lines += _quest_places(lo, missing)
    companions.export_beast(lo, pack, missing)  # (Beast Form: The Silver Hand's gift)
    with open(os.path.join(pack, "quests.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print(f"quests: {sum(1 for l in lines if l[0] == 'S')} scenes, {sum(1 for l in lines if l[0] == 'R')} lines"
          + (f"; missing {missing}" if missing else ""))


# Places the later main quest happens at (Skyrim's own references, by editor id): Ustengrav's
# horn (its stage trigger), Kynesgrove's dragon mound and its markers
QUEST_PLACES = {"mq105setstage180": "ustengrav_horn", "mq106dragonmoundref": "kynesgrove_mound",
                "mq106dragonresurrectmarker": "kynesgrove_rise", "mq106alduinlandingmarker": "kynesgrove_alduin",
                "mq106delphinehidingmarker": "kynesgrove_delphine",
                # Diplomatic Immunity: Delphine in her room at the Sleeping Giant, at the Solitude stables
                "mq201delphinewaitmarker": "riverwood_delphine_room", "mq201delphinewaitatcarriagemarker": "solitude_stables",
                # Alduin's Wall: Karthspire, where Delphine and Esbern wait outside it; The Throat of
                # the World: the storm on the path (MQ204's StormTarget), Paarthurnax's perch and ground
                "karthspiremapmarker": "karthspire", "mq203outsidekarthspirewaitmarker": "karthspire_wait",
                "mq204collision1": "throat_storm", "mq204arngeirintromarker2": "hh_courtyard", "mq204paarthurnaxgroundmarker": "throat_paarthurnax",
                "mq204paarthurnaxinitialperch": "throat_perch", "mq204dragonbreathtarget": "throat_breath",
                # Alduin's Bane: the Time-Wound, where Alduin comes from, Paarthurnax's landing; Dragonslayer's
                # way home from Sovngarde, and Paarthurnax over the summit after
                "mq206timewoundref": "time_wound", "mq206alduintamrielapproachmarker1": "alduin_approach",
                "mq206paarthurnaxlandingmarker": "paar_landing", "mq305returntotamrielmarker": "sovngarde_return",
                "mq306odahviingwaitmarker": "throat_odahviing", "mq306paarthurnaxhovermarker": "throat_paar_hover",
                "septimussignusoutpostmarkerref": "septimus_outside"}
# ... those in a land of its own (Skyrim's worldspace: the engine's own world, by its prefix)
QUEST_PLACES_WORLD = {("WhiterunDragonsreachWorld", "mq301odahviingstartmarker"): "porch_call",
                      ("WhiterunDragonsreachWorld", "mq301odahviingtrapmarker1"): "porch_trap",
                      ("WhiterunDragonsreachWorld", "mq301jarlstartmarker"): "porch_jarl",
                      ("WhiterunDragonsreachWorld", "mq303odahviingexittrapmarker"): "porch_exit",
                      ("SkuldafnWorld", "mq303skuldafnstartmarker"): "skuldafn_start",
                      ("SkuldafnWorld", "mq303odahviinglandingmarker"): "skuldafn_odahviing",
                      ("SkuldafnWorld", "mq303sovngardeportalref"): "skuldafn_portal",
                      ("SkuldafnWorld", "mq303bossactivatormarker"): "skuldafn_boss",
                      ("Sovngarde", "mq304lostsoulintromarker1"): "sovngarde_start",
                      ("Sovngarde", "tsunref"): "sovngarde_tsun",
                      ("Sovngarde", "mq304tsunallowmarker"): "sovngarde_bridge_end",
                      ("Sovngarde", "mq305alduinstartbattlemarker1"): "sovngarde_battle",
                      ("Sovngarde", "mq305alduinlandingmarker"): "sovngarde_alduin_land",
                      ("Sovngarde", "mq305hero1shoutmarker"): "sovngarde_hero1",
                      ("Sovngarde", "mq305hero2shoutmarker"): "sovngarde_hero2",
                      ("Sovngarde", "mq305hero3shoutmarker"): "sovngarde_hero3",
                      ("Sovngarde", "mq305tsunwatchbattlemarker"): "sovngarde_tsun_watch"}
# ... and those only in one interior (a name used in many): Malborn at the Winking Skeever
QUEST_PLACES_IN = {("solitudewinkingskeever", "drinkingsongmarker01"): "skeever_malborn",
                   # (the Thalmor Embassy's party: where you're shown in, its people, Malborn's
                   # kitchen and your gear, Elenwen's chest, Etienne's cell)
                   ("thalmorembassy01", "mq201partycentermarker"): "embassy_party",
                   ("thalmorembassy01", "mq201malbornpartywaitmarker"): "embassy_malborn",
                   ("thalmorembassy01", "mq201elenwenholdpositionmarker"): "embassy_elenwen",
                   ("thalmorembassy01", "razelanref"): "embassy_razelan",
                   ("thalmorembassy01", "mq201malborngearmarker"): "embassy_kitchen",
                   ("thalmorembassy01", "mq201gearcontainerref"): "embassy_gear",
                   ("thalmorembassy02", "mq201elenwenofficecontainerref"): "embassy_office_chest",
                   ("thalmorembassy03", "mq201cellmarker"): "embassy_etienne",
                   # (the guests about the hall: Skyrim's sandbox markers, Erikur's, the bard's, Brelas's)
                   ("thalmorembassy01", "mq201partysandboxmarker2"): "embassy_sandbox2",
                   ("thalmorembassy01", "mq201partysandboxmarker3"): "embassy_sandbox3",
                   ("thalmorembassy01", "mq201partysandboxmarker4"): "embassy_sandbox4",
                   ("thalmorembassy01", "mq201erikursandboxmarker"): "embassy_erikur",
                   ("thalmorembassy01", "mq201bardmarker"): "embassy_bard",
                   ("thalmorembassy01", "mq201partybrelasmarker"): "embassy_brelas",
                   ("thalmorembassy03", "mq201torturechambermarker"): "embassy_torture",
                   # (High Hrothgar's main hall, Skyrim's MQ105: the centre you're told to stand
                   # in, each Greybeard's mark for each trial (1 Ro, 2 Wuld, 3 the horn's Dah), the
                   # teacher's spot and the floor where his word lands, before you)
                   ("highhrothgar", "highhrothgarcentermeditationpatrolstart"): "hh_centre",
                   ("highhrothgar", "mq105einarthteachwordmarker"): "hh_teach_einarth",
                   ("highhrothgar", "mq105wulfgarteachwordmarker1"): "hh_teach_wulfgar",
                   ("highhrothgar", "mq105einarthwordtarget"): "hh_word_einarth",
                   ("highhrothgar", "mq105wulfgarwordtarget"): "hh_word_wulfgar",
                   ("highhrothgar", "mq105arngeirmarker1a"): "hh_arngeir1",
                   ("highhrothgar", "mq105arngeirmarker2"): "hh_arngeir2",
                   ("highhrothgar", "mq105arngeirmarker3"): "hh_arngeir3",
                   ("highhrothgar", "mq105einarthmarker1"): "hh_einarth1",
                   ("highhrothgar", "mq105einarthmarker2"): "hh_einarth2",
                   ("highhrothgar", "mq105einarthmarker3"): "hh_einarth3",
                   ("highhrothgar", "mq105borrimarker1"): "hh_borri1",
                   ("highhrothgar", "mq105borrimarker2"): "hh_borri2",
                   ("highhrothgar", "mq105borrimarker3"): "hh_borri3",
                   ("highhrothgar", "mq105wulfgarmarker1"): "hh_wulfgar1",
                   ("highhrothgar", "mq105wulfgarmarker2"): "hh_wulfgar2",
                   ("highhrothgar", "mq105wulfgarmarker3"): "hh_wulfgar3",
                   # (A Cornered Rat: Esbern's door in the Ratway Warrens, and him behind it; Alduin's
                   # Wall: the Sleeping Giant's cellar - Esbern's table, Delphine's spot - Sky Haven
                   # Temple's blood seal and Alduin's Wall, Esbern's and Delphine's marks before it)
                   ("riftenesbernsvault", "mq202esbernsdoorref"): "warrens_door",
                   ("riftenesbernsvault", "esbernref"): "warrens_esbern",
                   ("riverwoodsleepinggiantinn", "mq203esberntablemarker"): "inn_esbern",
                   ("riverwoodsleepinggiantinn", "mq203delphinesecretroommarker1"): "inn_delphine",
                   ("skyhaventemple", "mq203esbernskyhaventempleentrymarker"): "skyhaven_entry",
                   ("skyhaventemple", "mq203esbernskyhavenentrymarker1"): "skyhaven_seal",
                   ("skyhaventemple", "mq203esbernremanswallmarker1"): "skyhaven_wall_esbern",
                   ("skyhaventemple", "mq203delphineremanswallmarker1"): "skyhaven_wall_delphine",
                   ("skyhaventemple", "remanswalltarget3"): "skyhaven_wall",
                   ("highhrothgar", "mq204arngeirintromarker1"): "hh_mq204_arngeir",
                   # (Elder Knowledge: the Arcanaeum, Septimus, the Elder Scroll's place in the Tower of Mzark;
                   # Season Unending's council at High Hrothgar - each one's chair - and the Hall of Valor)
                   ("winterholdcollegearcanaeum", "da04uragplacebooksmarker"): "arcanaeum_books",
                   ("septimussignusoutpost", "da04septimusref"): "septimus",
                   ("towerofmzark", "mq205elderscrollmarker"): "mzark_scroll",
                   ("highhrothgar", "mq302playercouncilchair"): "council_player",
                   ("highhrothgar", "mq302ulfriccouncilchair"): "council_ulfric",
                   ("highhrothgar", "mq302tulliuscouncilchair"): "council_tullius",
                   ("highhrothgar", "mq302elenwencouncilchair"): "council_elenwen",
                   ("highhrothgar", "mq302rikkecouncilchair"): "council_rikke",
                   ("highhrothgar", "mq302galmarcouncilchair"): "council_galmar",
                   ("highhrothgar", "mq302elisifcouncilchair"): "council_elisif",
                   ("highhrothgar", "mq302balgruufcouncilchair"): "council_balgruuf",
                   ("highhrothgar", "mq302arngeircouncilchair"): "council_arngeir",
                   ("highhrothgar", "mq302esberncouncilchair"): "council_esbern",
                   ("highhrothgar", "mq302delphinecouncilchair"): "council_delphine",
                   ("sovngarde01", "mq304hallofheroesmarker"): "hall_heroes",
                   ("sovngarde01", "mq304ysgramorref"): "hall_ysgramor"}

# Exact FormIDs for Main Quest, Daedric, and Side Quest markers
QUEST_PLACES_ID = {
    # Main Quest Exterior & Tamriel Markers
    0x0001626C: "karthspire",  # karthspiremapmarker
    0x000212F1: "solitude_stables",  # mq201delphinewaitatcarriagemarker
    0x0002D511: "septimus_outside",  # septimussignusoutpostmarkerref
    0x00032D99: "kynesgrove_rise",  # mq106dragonresurrectmarker
    0x00032D9A: "kynesgrove_alduin",  # mq106alduinlandingmarker
    0x00034E03: "throat_storm",  # mq204collision1
    0x0003C1C1: "hh_courtyard",  # mq204arngeirintromarker2
    0x0003F8B7: "throat_breath",  # mq204dragonbreathtarget
    0x000430A3: "time_wound",  # mq206timewoundref
    0x0004424C: "alduin_approach",  # mq206alduintamrielapproachmarker1
    0x0004E840: "kynesgrove_delphine",  # mq106delphinehidingmarker
    0x0007A507: "ustengrav_horn",  # mq105setstage180
    0x0009C21D: "throat_paarthurnax",  # mq204paarthurnaxgroundmarker
    0x0009E083: "throat_perch",  # mq204paarthurnaxinitialperch
    0x0009E096: "sovngarde_return",  # mq305returntotamrielmarker
    0x0009E0A2: "throat_odahviing",  # mq306odahviingwaitmarker
    0x000A8D61: "karthspire_wait",  # mq203outsidekarthspirewaitmarker
    0x000CAF88: "kynesgrove_mound",  # mq106dragonmoundref
    0x0010D9EF: "riverwood_delphine_room",  # mq201delphinewaitmarker
    0x0010D9F3: "throat_paar_hover",  # mq306paarthurnaxhovermarker
    0x0010F1EF: "paar_landing",  # mq206paarthurnaxlandingmarker

    # Main Quest & Daedric Interior Markers
    0x00016FAC: "skyhaven_wall_esbern",  # mq203esbernremanswallmarker1
    0x00016FAE: "skyhaven_wall_delphine",  # mq203delphineremanswallmarker1
    0x00016FC0: "skyhaven_entry",  # mq203esbernskyhaventempleentrymarker
    0x000176A3: "skyhaven_wall",  # remanswalltarget3
    0x00019DFD: "warrens_esbern",  # esbernref
    0x0002726D: "arcanaeum_books",  # da04uragplacebooksmarker
    0x00027E83: "council_arngeir",  # mq302arngeircouncilchair
    0x00027E84: "council_balgruuf",  # mq302balgruufcouncilchair
    0x00027E86: "council_esbern",  # mq302esberncouncilchair
    0x00027E87: "council_rikke",  # mq302rikkecouncilchair
    0x00027E94: "council_tullius",  # mq302tulliuscouncilchair
    0x00027E95: "council_delphine",  # mq302delphinecouncilchair
    0x00027E96: "council_galmar",  # mq302galmarcouncilchair
    0x00027E97: "council_ulfric",  # mq302ulfriccouncilchair
    0x00028E35: "council_elenwen",  # mq302elenwencouncilchair
    0x00028E36: "council_player",  # mq302playercouncilchair
    0x0002D4F2: "septimus",  # da04septimusref
    0x0002EC6A: "hh_wulfgar1",  # mq105wulfgarmarker1
    0x0002EC6B: "hh_borri1",  # mq105borrimarker1
    0x0002EC6D: "hh_einarth1",  # mq105einarthmarker1
    0x0002ED4B: "hh_borri2",  # mq105borrimarker2
    0x0002ED4C: "hh_arngeir2",  # mq105arngeirmarker2
    0x0002ED4D: "hh_wulfgar2",  # mq105wulfgarmarker2
    0x0002ED55: "hh_einarth2",  # mq105einarthmarker2
    0x0002F2D6: "hh_einarth3",  # mq105einarthmarker3
    0x0002F2D7: "hh_borri3",  # mq105borrimarker3
    0x0002F2D8: "hh_wulfgar3",  # mq105wulfgarmarker3
    0x0002F5BF: "hh_teach_einarth",  # mq105einarthteachwordmarker
    0x000353A0: "embassy_kitchen",  # mq201malborngearmarker
    0x000361EF: "embassy_malborn",  # mq201malbornpartywaitmarker
    0x000362D6: "embassy_torture",  # mq201torturechambermarker
    0x00036366: "inn_esbern",  # mq203esberntablemarker
    0x000368C6: "embassy_party",  # mq201partycentermarker
    0x00037BE2: "inn_delphine",  # mq203delphinesecretroommarker1
    0x00039CF1: "embassy_gear",  # mq201gearcontainerref
    0x00039F1C: "embassy_office_chest",  # mq201elenwenofficecontainerref
    0x0003A1D4: "embassy_etienne",  # mq201cellmarker
    0x0003B8B6: "warrens_door",  # mq202esbernsdoorref
    0x0003C1B7: "hh_mq204_arngeir",  # mq204arngeirintromarker1
    0x000423B2: "embassy_elenwen",  # mq201elenwenholdpositionmarker
    0x000427D4: "skeever_malborn",  # drinkingsongmarker01
    0x000503A4: "hall_heroes",  # mq304hallofheroesmarker
    0x000649BE: "hh_arngeir1",  # mq105arngeirmarker1a
    0x000683A7: "embassy_razelan",  # razelanref
    0x00071C36: "skyhaven_seal",  # mq203esbernskyhavenentrymarker1
    0x000734CA: "embassy_brelas",  # mq201partybrelasmarker
    0x00098BD2: "hall_ysgramor",  # mq304ysgramorref
    0x000C0839: "embassy_bard",  # mq201bardmarker
    0x000C4379: "hh_teach_wulfgar",  # mq105wulfgarteachwordmarker1
    0x000C438F: "hh_arngeir3",  # mq105arngeirmarker3
    0x000D3149: "mzark_scroll",  # mq205elderscrollmarker
    0x000D8E83: "council_elisif",  # mq302elisifcouncilchair
    0x000E4EBD: "embassy_sandbox2",  # mq201partysandboxmarker2
    0x000E4EBE: "embassy_sandbox3",  # mq201partysandboxmarker3
    0x000FCBF4: "hh_word_einarth",  # mq105einarthwordtarget
    0x000FCBF5: "hh_word_wulfgar",  # mq105wulfgarwordtarget
    0x000FEAA5: "embassy_sandbox4",  # mq201partysandboxmarker4
    0x000FEAA8: "embassy_erikur",  # mq201erikursandboxmarker
    0x001094EF: "hh_centre",  # highhrothgarcentermeditationpatrolstart

    # Main Quest Worldspace Markers
    0x00027ACC: "skuldafn_portal",  # mq303sovngardeportalref
    0x00046EF4: "porch_call",  # mq301odahviingstartmarker
    0x00046F01: "porch_jarl",  # mq301jarlstartmarker
    0x00046F03: "porch_trap",  # mq301odahviingtrapmarker1
    0x0004DE49: "skuldafn_start",  # mq303skuldafnstartmarker
    0x0004DE4A: "skuldafn_odahviing",  # mq303odahviinglandingmarker
    0x0004F829: "sovngarde_tsun",  # tsunref
    0x000503A6: "sovngarde_hero1",  # mq305hero1shoutmarker
    0x000503A7: "sovngarde_hero2",  # mq305hero2shoutmarker
    0x000503A8: "sovngarde_hero3",  # mq305hero3shoutmarker
    0x000503AA: "sovngarde_bridge_end",  # mq304tsunallowmarker
    0x00098BCD: "sovngarde_tsun_watch",  # mq305tsunwatchbattlemarker
    0x00098BD5: "sovngarde_battle",  # mq305alduinstartbattlemarker1
    0x000E8AB8: "porch_exit",  # mq303odahviingexittrapmarker
    0x000E9343: "skuldafn_boss",  # mq303bossactivatormarker
    0x000EA71B: "sovngarde_start",  # mq304lostsoulintromarker1
    0x000F1A40: "sovngarde_alduin_land",  # mq305alduinlandingmarker
}


# Skyrim's scenes said whole (SCEN): each line by its actor - the scene's quest's alias, or one
# named for it here (the friend who makes your distraction) - in its own voice, in the scene's
# phase order: S key - ; R key path text who (who: the speaker's name, for its subtitle)
PARTY_FRIENDS = ("BalgruufTheGreater", "Siddgeir", "IdgrodRavencrone", "Igmund", "ElisifTheFair", "VittoriaVici",
                 "OrthusEndario", "Maven", "Ondolemar")
SCENE_EXPORTS = (("mq202_open_door", "MQ202EsbernOpenDoorScene", None), ("mq202_explain", "MQ202EsbernExplainScene", None),
                 ("mq203_riverwood", "MQ203RiverwoodScene", None), ("mq203_karthspire", "MQ203KarthspireScene", None),
                 ("mq203_puzzle", "MQ203PuzzleScene", None), ("mq203_entry", "MQ203SkyHavenEntryScene", None),
                 ("mq203_wall", "MQ203RemansWallScene", None), ("mq204_greybeards", "MQ204GreybeardsIntroScene", None),
                 ("mq204_clear_skies", "MQ204ClearSkiesScene", None), ("mq204_paarthurnax", "MQ204PaarthurnaxScene", None),
                 ("mq206_vision", "MQ206BattleScene", None), ("mq206_present", "MQ206PresentDayScene", None),
                 ("mq301_attack", "MQ301DragonAttackScene", None),
                 ("mq302_intro", "MQ302CouncilSceneIntro", None), ("mq302_begin", "MQ302CouncilSceneBegin", None),
                 ("mq302_s1", "MQ302CouncilScene1_I", None), ("mq302_s2a", "MQ302CouncilScene2a", None),
                 ("mq302_s2b", "MQ302CouncilScene2b", None), ("mq302_s4", "MQ302CouncilScene4", None),
                 ("mq302_s5", "MQ302CouncilScene5_I", None), ("mq302_end", "MQ302CouncilSceneEnd", None),
                 ("mq302_outro", "MQ302CouncilSceneOutroA", None),
                 ("mq303_portal", "MQ303PortalScene", None), ("mq304_hall", "MQ304HallofValorScene", None),
                 ("mq304_tsun_defeat", "MQ304TsunDefeatScene", None),
                 ("mq305_intro", "MQ305IntroScene", None), ("mq305_outro", "MQ305OutroScene", None),
                 ("mq306_dragons", "MQ306DragonScene", None), ("mq306_paar", "MQ306OdahviingScene", None),
                 ("mq201_erikur_intro", "MQ201PartyErikurIntroScene", None),
                 ("mq201_erikur_scene", "MQ201PartyDistractionErikurBrelasScene", None)) + tuple(
    (f"mq201_friend_scene_{f.lower()}", "MQ201PartyDistractionFriendScene", ("FriendDistraction", f)) for f in PARTY_FRIENDS) \
    + sum((g.SCENE_EXPORTS for g in GUILDS), ())


def _npc_voice(lo, talk, npc, depth=0):
    """An NPC's voice type (its own, else its template's)."""
    if npc is None or depth > 6:
        return None
    acbs, tplt = npc.sub("ACBS"), npc.sub("TPLT")
    if tplt and ((acbs and len(acbs) >= 20 and struct.unpack_from("<H", acbs, 18)[0] & 0x1) or not npc.sub("VTCK")):
        t = lo.record(lo.global_id(npc.plugin, _u32(tplt)), types=("NPC_",))
        v = _npc_voice(lo, talk, t, depth + 1)
        if v:
            return v
    vt = npc.sub("VTCK")
    return talk.voice_types.get(lo.global_id(npc.plugin, _u32(vt))) if vt else None


def _npc_name(lo, talk, npc, depth=0):
    full = npc.sub("FULL")
    if full and len(full) == 4:
        return talk.strings.get(npc.plugin, _u32(full), "strings")
    tplt = npc.sub("TPLT")
    t = lo.record(lo.global_id(npc.plugin, _u32(tplt)), types=("NPC_",)) if tplt and depth < 6 else None
    return _npc_name(lo, talk, t, depth + 1) if t is not None else (npc.editor_id() or "")


SCENE_ALIAS_NPCS = {"PartyGuard1": "MQ201PartyGuard", "PartyGuard2": "MQ201PartyGuard"}  # (filled as the game runs: the guard's own)


def _alias_npcs(lo, quest):
    """{alias id: [name, NPC or None, (other quest, its alias id) or None]}"""
    aliases, aid, ext_q = {}, None, None
    for t, v in quest.subrecords():
        if t in ("ALST", "ALLS"):
            aid, ext_q = _u32(v), None
            aliases[aid] = ["", None, None]
        elif aid is None:
            continue
        elif t == "ALID":
            aliases[aid][0] = v.rstrip(b"\0").decode("latin-1")
        elif t == "ALUA":
            aliases[aid][1] = lo.record(lo.global_id(quest.plugin, _u32(v)), types=("NPC_",))
        elif t == "ALFR":
            g = lo.global_id(quest.plugin, _u32(v))
            ref = lo.record(g, types=("ACHR",)) or lo.refs_by_id({g}).get(g)  # (a placed one: Jorrvaskr's Kodlak)
            if ref is not None and ref.sub("NAME"):
                aliases[aid][1] = lo.record(lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("NPC_",))
        elif t == "ALEQ":
            ext_q = lo.record(lo.global_id(quest.plugin, _u32(v)), types=("QUST",))
        elif t == "ALEA" and ext_q is not None:
            aliases[aid][2] = (ext_q, _u32(v))
    return aliases


def _scenes(lo, talk, by_eid, missing):
    out, maker = [], TreeMaker(lo, talk)
    scenes = {(r.editor_id() or ""): r for p in lo.plugins for r in p.records_of_type("SCEN")}
    for key, scene_eid, override in SCENE_EXPORTS:
        sc = scenes.get(scene_eid)
        quest = next((q for t, v in (sc.subrecords() if sc is not None else ()) if t == "PNAM" and len(v) == 4
                      for q in [lo.record(lo.global_id(sc.plugin, _u32(v)), types=("QUST",))] if q is not None), None)
        # (its PNAMs: the actors' packages, then the quest - the last)
        if quest is None:
            missing.append(f"scene {scene_eid}")
            continue
        aliases = _alias_npcs(lo, quest)
        for a in aliases.values():  # (another quest's alias - MQ201's Elenwen, Brelas, its party guard - resolved there)
            if a[1] is None and a[2] is not None:
                a[1] = _alias_npcs(lo, a[2][0]).get(a[2][1], ["", None, None])[1]
            if a[1] is None and a[0] in SCENE_ALIAS_NPCS:
                a[1] = by_eid.get(("NPC_", SCENE_ALIAS_NPCS[a[0]].lower()))
        if override:

            for a in aliases.values():
                if a[0] == override[0]:
                    a[1] = by_eid.get(("NPC_", override[1].lower()))
        friend = by_eid.get(("NPC_", override[1].lower())) if override else None
        actions, cur = [], {}
        for t, v in sc.subrecords():  # (its actions: ANAM opens and closes each)
            if t == "ANAM":
                if cur.get("topic"):
                    actions.append(cur)
                cur = {}
            elif t == "ALID":
                cur["alias"] = struct.unpack("<i", v)[0]
            elif t == "SNAM":
                cur["start"] = _u32(v)
            elif t == "DATA" and len(v) == 4 and _u32(v):
                cur["topic"] = lo.global_id(sc.plugin, _u32(v))
        seen, ordered = set(), []
        for a in sorted(actions, key=lambda a: a.get("start", 0)):
            k = (a.get("alias"), a["topic"])
            if k not in seen:
                seen.add(k)
                ordered.append(a)
        versions, by_sex = {}, False
        for sex, suffix in ((0, ""), (1, "_f")):  # (a woman's lines where Skyrim has them: INFO 000734B4)
            lines = []
            for a in ordered:
                name, npc, _ext = aliases.get(a.get("alias"), ("", None, None))
                if npc is None or a["topic"] not in maker.dial:
                    continue
                gid = lo.global_id(npc.plugin, npc.form_id)
                voice = _npc_voice(lo, talk, npc) or "*"
                who = {gid, lo.global_id(friend.plugin, friend.form_id)} if friend is not None else {gid}
                for inf in maker.infos.get(a["topic"], []):  # (the first meant for them: by GetIsID - its speaker, the friend)
                    ids = {lo.global_id(inf.plugin, p1) for _op, _c, fn, p1 in maker.ctdas(inf) if fn == F_GET_IS_ID}
                    if ids and not ids & who:
                        continue
                    sexes = {p1 for _op, _c, fn, p1 in maker.ctdas(inf) if fn == F_GET_PC_IS_SEX}
                    by_sex |= bool(sexes)
                    if sexes and sex not in sexes:
                        continue
                    said = maker.said(inf, voice)
                    if said:
                        title = _npc_name(lo, talk, npc)
                        lines += [f"R\t{key}{suffix}\t{path}\t{maker.clean(t)}\t{title}" for path, t in said]
                        break
            versions[suffix] = lines
        for suffix, lines in versions.items():
            if suffix and (not by_sex or not lines):
                continue
            if lines:
                out.append(f"S\t{key}{suffix}\t-")
                out += lines
            elif not suffix:
                missing.append(f"scene {key}")
    print(f"scenes: {sum(1 for l in out if l.startswith('S'))} from Skyrim's SCEN, {sum(1 for l in out if l.startswith('R'))} lines")
    return out


def _quest_places(lo, missing):
    """Q lines: key, the interior's editor id (- outdoors), x y z heading in engine space."""
    out, found = [], set()
    by_id = {**QUEST_PLACES_ID, **{k: v for g in GUILDS for k, v in g.PLACES_ID.items()}}
    by_eid = {**QUEST_PLACES, **{e: k for (_c, e), k in QUEST_PLACES_IN.items()},
              **{e: k for (_w, e), k in QUEST_PLACES_WORLD.items()},
              **{k: v for g in GUILDS for k, v in getattr(g, "PLACES", {}).items()}}

    def take_id(r, cell_eid):
        gid = lo.global_id(r.plugin, r.form_id)
        key = by_id.get(gid) or by_eid.get((r.editor_id() or "").lower())
        if key and r.sub("DATA") and key not in found:
            px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
            out.append(f"Q\t{key}\t{cell_eid}\t{px / 70.0:.3f}\t{pz / 70.0:.3f}\t{-py / 70.0:.3f}\t{-rz:.4f}")
            found.add(key)

    enabled_ids = set()

    # Collect across Tamriel exterior cells
    for (_x, _y), c in lo.exterior_cells("Tamriel").items():
        for r in c["refs"]:
            take_id(r, "-")
            enabled_ids.add(lo.global_id(r.plugin, r.form_id))

    # Collect across city exterior cells
    for world in {w for g in GUILDS for w in getattr(g, "CITY_WORLDS", ())}:
        for (_x, _y), c in lo.exterior_cells(world).items():
            for r in c["refs"]:
                take_id(r, "-")
                enabled_ids.add(lo.global_id(r.plugin, r.form_id))

    # Collect across interior cells
    for _k, v in lo.interior_cells().items():
        cell_eid = (v["cell"].editor_id() or "").lower() if v["cell"] is not None else ""
        for r in v["refs"]:
            take_id(r, cell_eid)
            enabled_ids.add(lo.global_id(r.plugin, r.form_id))

    # Quest foes (Companions, College, Thieves)
    out += companions.quest_foes(lo, StringTables(lo), enabled_ids)
    out += _foe_ids(lo, found, missing)

    # Extra worldspaces with land of their own
    import worlds
    prefix = {w: p for w, p, _t in worlds.OWN_LAND}
    id_worlds = {w for g in GUILDS for w in getattr(g, "ID_WORLDS", ())} | {
        "WhiterunDragonsreachWorld", "SkuldafnWorld", "Sovngarde"
    }
    for world in id_worlds:
        try:
            w_cells = lo.exterior_cells(world)
        except KeyError:
            continue
        for (_x, _y), c in w_cells.items():
            for r in c["refs"]:
                gid = lo.global_id(r.plugin, r.form_id)
                key = by_id.get(gid) or by_eid.get((r.editor_id() or "").lower())
                if key and r.sub("DATA") and key not in found:
                    px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
                    out.append(f"Q\t{key}\t@{prefix.get(world, world)}\t{px / 70.0:.3f}\t{pz / 70.0:.3f}\t{-py / 70.0:.3f}\t{-rz:.4f}")
                    found.add(key)
                enabled_ids.add(gid)

    missing += [f"place {k}" for k in list(by_id.values()) + list(by_eid.values()) if k not in found]
    return out


def _foe_ids(lo, found, missing):
    """Q lines of the guilds' FOE_IDS: references their quests enable (disabled, so left out of the
    cells) - where each stands: its interior's editor id, or outdoors."""
    want = {k: v for g in GUILDS for k, v in getattr(g, "FOE_IDS", {}).items()}
    out, cell_of = [], {}
    for p in lo.plugins:
        for _local, c in p.interior_cells().items():
            for r in c["refs"]:
                g = lo.global_id(p, r.form_id)
                if g in want and c["cell"] is not None:
                    cell_of[g] = (c["cell"].editor_id() or "").lower()
    for g, r in lo.refs_by_id(set(want)).items():
        if not r.sub("DATA") or want[g] in found:
            continue
        px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
        out.append(f"Q\t{want[g]}\t{cell_of.get(g, '-')}\t{px / 70.0:.3f}\t{pz / 70.0:.3f}\t{-py / 70.0:.3f}\t{-rz:.4f}")
        found.add(want[g])
    missing += [f"foe {k}" for k in want.values() if k not in found]
    return out


# Conversations as Skyrim has them: the NPC's line, then the player's answers to choose from (the
# line's linked topics, TCLT), each with its own reply and its own answers after. (key - the scene
# the engine plays for it, speaker, the topic it starts at, the opening words of its first line,
# [(topic, opening)] said after it's run its course). They had been said straight through, none of
# the player's choices offered.
TREES = (
    ("mq102_jarl", "BalgruufTheGreater", "MQ102BalgruufIntroTopic", "#000D07FD",
     [("MQ102BalgruufReward", "#000D553E")]),
    ("mq103_farengar", "FarengarSecretFire", "MQ103FarengarIntroTopic", "#000D50E7", []),
    ("mq103_farengar_stone", "FarengarSecretFire", "MQ103FarengarRetrieveBookTopic", "#000E0EB9", []),
    ("mq104_jarl", "BalgruufTheGreater", "MQ104BalgruufBlockingTopic", "#000DF1F5", []),
    ("mq104_jarl_outro", "BalgruufTheGreater", "MQ104BalgruufBlockingTopic", "#0005DD45",
     [("MQ104BalgruufReward1", "#000E0EB8")]),
    ("mq106_intro", "Delphine", "MQ106DelphineIntroTopic", "#000C65A1", []),
    ("mq201_intro", "Delphine", "MQ201DelphineIntroTopic", "#00035D52", []),
    ("mq201_malborn", "Malborn", "MQ201MalbornIntroTopic", "#000361DD", []),
    # (the party: Elenwen's welcome, Razelan's thirst - till he'll cause a scene)
    ("mq201_elenwen", "Elenwen", "MQ201PartyElenwenBlockingTopic", "#000410FB", []),
    ("mq201_razelan", "Razelan", "MQ201PartyDrunkTopic", "", []),
    # (the other ways to a distraction: a friend among the guests - a line of theirs
    # - their yes, no, or your persuasion; Erikur's eye for Brelas, and her answer)
    ("mq201_friend_balgruufthegreater", "BalgruufTheGreater", "MQ201PartyFriendDistractionTopic", "#000C082C", []),
    ("mq201_friend_siddgeir", "Siddgeir", "MQ201PartyFriendDistractionTopic", "#000C082D", []),
    ("mq201_friend_idgrodravencrone", "IdgrodRavencrone", "MQ201PartyFriendDistractionTopic", "#000C082E", []),
    ("mq201_friend_igmund", "Igmund", "MQ201PartyFriendDistractionTopic", "#000C082F", []),
    ("mq201_friend_elisifthefair", "ElisifTheFair", "MQ201PartyFriendDistractionTopic", "#000C0830", []),
    ("mq201_friend_vittoriavici", "VittoriaVici", "MQ201PartyFriendDistractionTopic", "#000C0425", []),
    ("mq201_friend_orthusendario", "OrthusEndario", "MQ201PartyFriendDistractionTopic", "#000665D3", []),
    ("mq201_friend_maven", "Maven", "MQ201PartyFriendDistractionTopic", "#00067ED6", []),
    ("mq201_friend_ondolemar", "Ondolemar", "MQ201PartyFriendDistractionTopic", "#00067ED7", []),
    ("mq201_erikur", "Erikur", "MQ201ErikurBrelasTopic", "#00069F26", []),
    ("mq201_erikur_back", "Erikur", "MQ201PartyErikurBlockingTopic", "#00069F30", []),
    ("mq201_brelas", "Brelas", "MQ201PartyBrelasErikurTopic", "#00069FA1", []),
    ("mq201_brelas_prisoner", "Brelas", "MQ201BrelasBlockingTopic2", "", []),
    # (A Cornered Rat: Brynjolf on the old man; Vekel at the Ragged Flagon - his price, your
    # persuasion or your fists; Esbern through his door, then within)
    ("mq202_brynjolf", "Brynjolf", "MQ202ByrnjolfEsbernTopic", "#0003B69A", []),
    ("mq202_vekel", "Vekel", "MQ202RatwayEsbernTopic", "", []),
    ("mq202_esbern_door", "Esbern", "MQ202EsbernThroughDoorTopic", "#0003BD99", []),
    ("mq202_esbern", "Esbern", "MQ202EsbernOutroTopic", "#000318F9", []),
    # (Alduin's Wall: Esbern after the Sleeping Giant's scene; Delphine before the Wall)
    ("mq203_esbern", "Esbern", "MQ203EsbernAlduinsWallWhere", "#000ADD12", []),
    ("mq203_delphine_wall", "Delphine", "MQ203DelphineBlockingTopic", "#00016F9E", []),
    # (The Throat of the World: Arngeir on the Shout that defeated Alduin)
    ("mq204_arngeir", "Arngeir", "MQ204ArngeirIntroTopic", "#0003C1A7", []),
    # (Elder Knowledge: Urag on the Elder Scrolls - Septimus Signus; Septimus in his outpost)
    ("da04_urag", "UraggroShub", "DA04PointerToSeptimusBranchTopic", "#0003277F", []),
    ("da04_septimus", "DA04Septimus", "DA04SeptimusIntroBranchTopic", "", []),
    # (The Fallen: Arngeir and the Jarl on trapping a dragon; Season Unending: Arngeir, Tullius, Ulfric)
    ("mq301_arngeir", "Arngeir", "MQ301ArngeirIntroTopic", "", []),
    ("mq301_jarl", "BalgruufTheGreater", "MQ301JarlDragonsreachTopic", "#00045D05", []),
    ("mq301_jarl_ready", "BalgruufTheGreater", "MQ301JarlReadyToTrapTopic", "", []),
    ("mq302_arngeir", "Arngeir", "MQ302ArngeirIntroTopic", "", []),
    ("mq302_tullius", "GeneralTullius", "MQ302TulliusIntroTopic", "", []),
    ("mq302_ulfric", "Ulfric", "MQ302UlfricIntroTopic", "", []),
    # (Paarthurnax: the Blades' price; Sovngarde: Tsun at the bridge, the heroes in the Hall of Valor)
    ("mqp_delphine", "Delphine", "MQPaarthurnaxIntroTopic", "#00042847", []),
    ("mqp_delphine_done", "Delphine", "MQPaarthurnaxTopic", "", []),
    ("mq304_tsun", "Tsun", "MQ304TsunIntroWhoAreYou", "", []),
    ("mq304_ysgramor", "MQ305Ysgramor", "MQ304HallofValorBlockingTopic", "#000EC39E", []),
    ("mq304_hero", "FelldirTheOld", "MQ304HeroFightAlduinTopic", "", []),

    # (Riverwood: asking Gerdur / Alvor for help - how you know Ralof / Hadvar, what happened at Helgen)
    ("bts_gerdur", "Gerdur", "MQ102BRiverwoodHelpTopic", "#00052CB0", []),
    ("bts_alvor", "Alvor", "MQ102RiverwoodHelpTopic", "#00041F16", []),
) + sum((g.TREES for g in GUILDS), ())  # (theirs carry a sixth: the quest aliases assumed - Farkas your Shield-Brother)
TREE_MAX_NODES = 48
# Conditions the engine can decide on a line (its speaker, the player's sex and race, an item held)
F_GET_IS_ID, F_GET_PC_IS_RACE, F_GET_PC_IS_SEX, F_GET_ITEM_COUNT = 72, 130, 131, 47
# ... and the player's skill against a check (GetActorValue on the player: Speech against
# SpeechAverage - a persuasion), a thing of a list worn (GetEquipped: an Amulet of Articulation),
# the speaker's relationship with the player (GetRelationshipRank; IsInFriendStateWithPlayer)
F_GET_ACTOR_VALUE, F_GET_EQUIPPED, F_GET_RELATIONSHIP_RANK, F_IS_FRIEND = 14, 182, 403, 638
CTDA_OR, CTDA_USE_GLOBAL = 0x01, 0x04


# Condition functions an NPC's own everyday conversations may have and still be the engine's to
# offer: who's speaking, the player's race and sex, a faction (the rest - quest stages, script
# variables, locations - aren't known here: such conversations are left out)
BRANCH_FNS = {F_GET_IS_ID, F_GET_PC_IS_RACE, F_GET_PC_IS_SEX, 69, 70, 71}
DLBR_TOP_LEVEL, QUST_START_ENABLED = 0x1, 0x1


class TreeMaker:
    """Skyrim's dialogue as trees (D lines: DT key speaker; DN key node topic prompt; DV key node
    variant conditions rnam; DR key node variant voice text; DL key node variant child; DX key
    voice text said at the end)."""

    def __init__(self, lo, talk):
        self.lo, self.talk = lo, talk
        self.assume = {}  # ({editor id: True / False}: the quest aliases a tree's lines ask GetIsID of, as filled)
        self.stop = ()    # (topics not followed: the engine's own - Faralda's test)
        self.dial, self.infos = {}, {}
        for p in lo.plugins:
            ti = p.topic_infos()
            for d in p.records_of_type("DIAL"):
                g = lo.global_id(p, d.form_id)
                self.dial[g] = d
                self.infos.setdefault(g, []).extend(ti.get(d.form_id, []))
        self.by_topic = {(d.editor_id() or ""): g for g, d in self.dial.items()}

    def text(self, rec, sub):
        v = rec.sub(sub)
        return self.talk.strings.get(rec.plugin, _u32(v), "strings") if v and len(v) == 4 else ""

    def said(self, info, voice):
        talk, lo = self.talk, self.lo
        if voice == "*":
            if not hasattr(talk, "_any_voice"):
                talk._any_voice = {}
                for (_o, vt, f, n) in talk.voice_index:
                    if n == 1:
                        talk._any_voice.setdefault(f, vt)
            voice = talk._any_voice.get(lo.global_id(info.plugin, info.form_id) & 0xFFFFFF)
        got = talk._responses(info, voice) if voice else None
        if not got:
            return []
        converted = [(talk._convert(path), t) for path, t in got]
        return converted if all(c for c, _t in converted) else []

    def ctdas(self, info, full=False):
        for t, v in info.subrecords():
            if t == "CTDA" and len(v) >= 24:
                c = (v[0] >> 5, struct.unpack_from("<f", v, 4)[0], struct.unpack_from("<H", v, 8)[0], _u32(v, 12))
                yield c + (v[0] & 0x1F, _u32(v, 4), _u32(v, 20)) if full else c

    def conditions(self, info):
        lo, conds = self.lo, []
        for op, comp, fn, p1, flags, comp_bits, run in self.ctdas(info, True):
            orr = "o" if flags & CTDA_OR else ""  # (OR'd with the next: Skyrim's ORs bind first)
            if flags & CTDA_USE_GLOBAL:  # (compared with a global: its value - SpeechAverage's 50)
                comp = self.talk.globals.get(lo.global_id(info.plugin, comp_bits), comp)
            if fn == F_GET_ACTOR_VALUE and run == 1:  # (the player's, in dialogue the target)
                conds.append(f"{orr}{fn}:{op}:{comp:g}:av{p1}")
                continue
            if fn == F_GET_EQUIPPED:
                rec = lo.record(lo.global_id(info.plugin, p1), types=("FLST", "ARMO", "WEAP"))
                if rec is not None:
                    conds.append(f"{orr}{fn}:{op}:{comp:g}:{(rec.editor_id() or '').lower()}")
                continue
            if fn in (F_GET_RELATIONSHIP_RANK, F_IS_FRIEND):
                conds.append(f"{orr}{fn}:{op}:{comp:g}:-")
                continue
            if fn in (F_GET_IS_ID, F_GET_PC_IS_RACE, F_GET_ITEM_COUNT):
                rec = lo.record(lo.global_id(info.plugin, p1), types=("NPC_", "RACE", "MISC", "BOOK", "WEAP", "ARMO", "KEYM"))
                name = (rec.editor_id() or "").lower() if rec is not None else f"{lo.global_id(info.plugin, p1):x}"
                if fn == F_GET_IS_ID and name in self.assume:  # (a quest alias's: who the engine has fill it)
                    if not self.assume[name]:
                        conds.append(f"{orr}{fn}:0:1:-")  # (no one: never so)
                    continue
                if fn == F_GET_ITEM_COUNT and rec is not None:
                    name = f"{lo.global_id(rec.plugin, rec.form_id):x}"
                conds.append(f"{orr}{fn}:{op}:{comp:g}:{name}")
            elif fn == F_GET_PC_IS_SEX:
                conds.append(f"{orr}{fn}:{op}:{comp:g}:{p1}")
        return ";".join(conds) or "-"

    @staticmethod
    def clean(t):
        return re.sub(r"<Alias=([A-Za-z]+)>", lambda m: "{" + m.group(1).lower() + "}", t.replace("\t", " ").replace("\n", " "))

    def tree(self, key, speaker, voice, start_g, firsts, start_prompt="", tail=(), missing=None, keep=None):
        """The tree from start_g (its first node's replies: firsts), as D lines; keep(info): a
        reply the speaker may give (None: any)."""
        lo, dial, infos = self.lo, self.dial, self.infos
        nodes, order = {None: 0}, [None]

        def node(g):
            if g not in nodes and len(order) < TREE_MAX_NODES:
                nodes[g] = len(order)
                order.append(g)
            return nodes.get(g)

        body = [f"DT\t{key}\t{speaker.lower()}"]
        queue, done = [(None, firsts)], set()
        while queue:
            g, its = queue.pop(0)
            idx = nodes[g]
            if idx in done:
                continue
            done.add(idx)
            prompt = self.clean(self.text(dial[g], "FULL")) if g is not None else start_prompt
            body.append(f"DN\t{key}\t{idx}\t{dial[g].editor_id() if g is not None else dial[start_g].editor_id()}\t{prompt}")
            v = 0
            for inf in its:
                if keep is not None and g is not None and not keep(inf):
                    continue
                r = self.said(inf, voice)
                links = [lo.global_id(inf.plugin, _u32(x)) for t, x in inf.subrecords() if t == "TCLT" and len(x) == 4]
                links = [l for l in links if l in dial and (dial[l].editor_id() or "") not in self.stop]
                silent = not any(t == "NAM1" and len(x) == 4 and self.talk.strings.get(inf.plugin, _u32(x), "ilstrings")
                                 for t, x in inf.subrecords())
                if not r and not links and not silent:
                    continue  # (its voice not found - not a reply meant to be said without words)
                rn = self.text(inf, "RNAM")  # (the player's line, for this reply - a prompt of its own)
                body.append(f"DV\t{key}\t{idx}\t{v}\t{self.conditions(inf)}\t{self.clean(rn)}")
                for path, t in r:
                    body.append(f"DR\t{key}\t{idx}\t{v}\t{path}\t{self.clean(t)}")
                for l in links:
                    c = node(l)
                    if c is not None:
                        body.append(f"DL\t{key}\t{idx}\t{v}\t{c}")
                        queue.append((l, infos.get(l, [])))
                v += 1
        for topic, op in tail:
            got = [r for inf in infos.get(self.by_topic.get(topic), []) for r in [self.said(inf, voice)] if r and line_is(lo, inf, r[0][1], op)]
            if not got and missing is not None:
                missing.append(f"tree {key} tail: {topic} '{op}'")
            for path, t in (got[0] if got else []):
                body.append(f"DX\t{key}\t{path}\t{self.clean(t)}")
        return body

    def npc_branches(self, actors, voice_of):
        """Everyone's own everyday conversations: Skyrim's top-level branches of quests running
        from the start (DIAL 000BABDF), theirs by GetIsID, as trees
        "npc_<editor id>_<n>". actors: [(editor id, NPC)]; voice_of(npc): its voice type."""
        lo, out = self.lo, []
        ours = {(n.editor_id() or "").lower(): (eid, n) for eid, n in actors}
        quests = {}
        for p in lo.plugins:
            for q in p.records_of_type("QUST"):
                d = q.sub("DNAM")
                quests[lo.global_id(p, q.form_id)] = bool(d and len(d) >= 2 and struct.unpack_from("<H", d)[0] & QUST_START_ENABLED)
        allowed = lambda inf: all(fn in BRANCH_FNS for _o, _c, fn, _p in self.ctdas(inf))
        per = {}
        for p in lo.plugins:
            for b in p.records_of_type("DLBR"):
                dn, sn, qn = b.sub("DNAM"), b.sub("SNAM"), b.sub("QNAM")
                if not dn or not sn or not (_u32(dn) & DLBR_TOP_LEVEL):
                    continue
                if qn and not quests.get(lo.global_id(b.plugin, _u32(qn)), False):
                    continue
                g = lo.global_id(b.plugin, _u32(sn))
                if g not in self.dial:
                    continue
                prompt = self.clean(self.text(self.dial[g], "FULL"))
                if not prompt:
                    continue
                for inf in self.infos.get(g, []):
                    if not allowed(inf):
                        continue
                    for op, comp, fn, p1 in self.ctdas(inf):
                        if fn != F_GET_IS_ID or op != 0 or comp != 1.0:
                            continue
                        rec = lo.record(lo.global_id(inf.plugin, p1), types=("NPC_",))
                        who = ours.get((rec.editor_id() or "").lower()) if rec is not None else None
                        if who is None:
                            continue
                        firsts = per.setdefault((who[0], g), [])
                        if inf not in firsts:
                            firsts.append(inf)
        count = {}
        for (eid, g), firsts in per.items():
            npc = ours[eid.lower()][1] if eid.lower() in ours else None
            voice = voice_of(npc) if npc is not None else "*"
            if not any(self.said(inf, voice) for inf in firsts):
                continue
            n = count.get(eid, 0)
            count[eid] = n + 1
            out += self.tree(f"npc_{eid.lower()}_{n}", eid, voice, g, firsts,
                             start_prompt=self.clean(self.text(self.dial[g], "FULL")), keep=allowed)
        print(f"everyday conversations: {sum(count.values())} for {len(count)} people")
        return out


def _trees(lo, talk, by_eid, missing):
    """The quests' conversations (TREES) as D lines."""
    out, maker = [], TreeMaker(lo, talk)
    for key, speaker, start, opening, tail, *assume in TREES:
        maker.assume = {k.lower(): v for k, v in (assume[0] or {}).items() if k != "_stop"} if assume else {}
        maker.stop = tuple((assume[0] or {}).get("_stop", ())) if assume else ()
        npc = by_eid.get(("NPC_", speaker.lower()))
        voice = talk.voice_types.get(lo.global_id(npc.plugin, _u32(npc.sub("VTCK")))) if npc is not None and \
            npc.sub("VTCK") else "*"
        g0 = maker.by_topic.get(start)
        openings = opening if isinstance(opening, tuple) else (opening,)  # (a line each: "boy" / "girl" - its conditions choose)
        firsts = [inf for inf in maker.infos.get(g0, []) for r in [maker.said(inf, voice)]
                  if r and any(line_is(lo, inf, r[0][1], o) for o in openings)]
        if not firsts:
            missing.append(f"tree {key}: {start} '{opening}'")
            continue
        out += maker.tree(key, speaker, voice, g0, firsts if isinstance(opening, tuple) else firsts[:1], tail=tail, missing=missing)
    maker.assume, maker.stop = {}, ()
    print(f"dialogue trees: {sum(1 for l in out if l.startswith('DT'))}, {sum(1 for l in out if l.startswith('DN'))} nodes")
    return out


def _topic_responses(lo, talk, by_eid, topic, quest, opening, voice):
    """[(voice path under voice/, text)] of the first line of the topic (or any of the quest's
    topics) that starts with opening and has its voice for this voice type."""
    quest_rec = by_eid.get(("QUST", quest.lower()))
    qid = lo.global_id(quest_rec.plugin, quest_rec.form_id) if quest_rec is not None else None
    for p in lo.plugins:
        infos = p.topic_infos()
        for d in p.records_of_type("DIAL"):
            if topic and d.editor_id() != topic:
                continue
            qn = d.sub("QNAM")
            if not topic and (not qn or lo.global_id(p, _u32(qn)) != qid):
                continue
            for info in infos.get(d.form_id, []):
                gid = lo.global_id(info.plugin, info.form_id)
                if voice == "*" and not hasattr(talk, "_any_voice"):  # (a line's recorded voice, by its form id)
                    talk._any_voice = {}
                    for (_o, vt, f, n) in talk.voice_index:
                        if n == 1:
                            talk._any_voice.setdefault(f, vt)
                v = voice if voice != "*" else talk._any_voice.get(gid & 0xFFFFFF)
                responses = talk._responses(info, v) if v else None
                if not responses or not line_is(lo, info, responses[0][1], opening):
                    continue
                converted = [(talk._convert(path), text) for path, text in responses]
                if all(c for c, _t in converted):
                    return converted
    return []


# --- Skyrim's opening (Unbound: the cart ride to Helgen, the execution, Alduin, the keep) ---

OPENING_QUESTS = ("MQ101", "MQ101DragonAttack", "MQ102", "MQ102A", "MQ102B")
OPENING_MARKER_PREFIXES = ("mq101", "centermarkerhelgen")
OPENING_MODELS = {"cart": ("ACTI", "CartPassenger")}
GET_PC_IS_RACE, GET_PC_IS_SEX = 130, 131  # (Skyrim's condition functions, on the player)
CTDA = struct.Struct("<B3xfH2xIIIIi")


def _aliases(lo, quest, npcs):
    """{alias id: speaker} of a quest: its unique actor's, forced reference's or created
    NPC (editor id, lowercase), else the alias's own name."""
    out, cur = {}, None
    forced = {lo.global_id(quest.plugin, _u32(v)) for t, v in quest.subrecords() if t == "ALFR" and len(v) == 4}
    refs = lo.refs_by_id(forced) if forced else {}
    for t, v in quest.subrecords():
        if t in ("ALST", "ALLS") and len(v) == 4:
            cur = _u32(v)
            out[cur] = None
        elif cur is None:
            continue
        elif t == "ALID":
            out[cur] = out[cur] or v.rstrip(b"\0").decode("cp1252").lower()
        elif t in ("ALUA", "ALCO") and len(v) == 4:
            npc = npcs.get(lo.global_id(quest.plugin, _u32(v)))
            if npc is not None:
                out[cur] = npc.editor_id().lower()
        elif t == "ALFR" and len(v) == 4:
            ref = refs.get(lo.global_id(quest.plugin, _u32(v)))
            npc = npcs.get(lo.global_id(ref.plugin, _u32(ref.sub("NAME")))) if ref is not None and ref.sub("NAME") else None
            if npc is not None:
                out[cur] = npc.editor_id().lower()
    return out


def _race_of(lo, info, races):
    """Whom an INFO is for, by the player's race and sex: "-" anyone, else conditions joined by
    "," - a race (editor id, lowercase), "female" or "male"; "!" before one: not."""
    out = []
    for t, v in info.subrecords():
        if t == "CTDA" and len(v) >= CTDA.size:
            kind, value, func, param, *_rest = CTDA.unpack_from(v)
            if func not in (GET_PC_IS_RACE, GET_PC_IS_SEX):
                continue
            what = races.get(lo.global_id(info.plugin, param), "?") if func == GET_PC_IS_RACE else \
                ("female" if param == 1 else "male")
            out.append(what if ((kind >> 5) == 0) == (value >= 0.5) else "!" + what)
    return ",".join(out) or "-"


def _opening(lo, pack, talk, models, missing):
    lines = []
    npcs, races = {}, {}
    for p in lo.plugins:
        for r in p.records_of_type("NPC_"):
            npcs[lo.global_id(p, r.form_id)] = r
        for r in p.records_of_type("RACE"):
            races[lo.global_id(p, r.form_id)] = (r.editor_id() or "").lower()
    by_eid = {(n.editor_id() or "").lower(): n for n in npcs.values()}
    any_voice = {}  # (an alias with no NPC: whichever voice the line was recorded in)
    for owner, vt, fid, number in talk.voice_index:
        if number == 1:
            any_voice.setdefault((owner, fid), vt)
    p = lo.plugins[0]
    quests = {r.form_id: r for r in p.records_of_type("QUST") if r.editor_id() in OPENING_QUESTS}
    packages = {r.form_id: r for r in p.records_of_type("PACK")}
    wanted = set()  # (the references the packages name: their markers too)
    ref_eid = {}
    placed = {}
    for (x, y), c in p.exterior_cells("Tamriel", keep_removed=True).items():
        for r in c["refs"]:
            g = lo.global_id(p, r.form_id)
            placed[g] = r
            if r.editor_id():
                ref_eid[g] = r.editor_id().lower()
    for cid, c in p.interior_cells().items():
        if c["cell"] is not None and (c["cell"].editor_id() or "").lower().startswith("helgen"):
            for r in c["refs"]:
                g = lo.global_id(p, r.form_id)
                placed[g] = r
                if r.editor_id():
                    ref_eid[g] = r.editor_id().lower()
    aliases = {qid: _aliases(lo, q, npcs) for qid, q in quests.items()}
    infos = p.topic_infos()
    owner = p.name.lower()
    count = 0
    for sc in p.records_of_type("SCEN"):
        pn = [_u32(v) for t, v in sc.subrecords() if t == "PNAM" and len(v) == 4]
        qid = next((x for x in pn if x in quests), None)
        if qid is None:
            continue
        scene = sc.editor_id().lower()
        act, actions = {}, []
        for t, v in sc.subrecords():
            if t == "ANAM" and len(v) == 2:
                act = {"type": struct.unpack("<H", v)[0], "packs": []}
                actions.append(act)
            elif act and t in ("ALID", "SNAM", "ENAM", "INAM") and len(v) == 4:
                act[t] = _u32(v)
            elif act and t == "DATA" and len(v) == 4 and act["type"] == 0:
                act["topic"] = _u32(v)
            elif act and t == "PNAM" and len(v) == 4 and act["type"] == 1:
                act["packs"].append(_u32(v))
        for a in actions:  # (the moves: who goes where, when)
            if a["type"] != 1:
                continue
            who = aliases[qid].get(a.get("ALID")) or "?"
            for pk in a["packs"]:
                rec = packages.get(pk)
                if rec is None:
                    continue
                targets = _package_targets(lo, rec)
                wanted.update(targets)
                lines.append(f"A\t{scene}\t{a.get('SNAM', 0)}\t{a.get('ENAM', 0)}\t{a.get('INAM', 0)}\t{who}\t"
                             f"{(rec.editor_id() or '').lower()}\t" + ",".join(_ref_name(t, ref_eid) for t in targets))
        for a in sorted((a for a in actions if "topic" in a), key=lambda a: (a.get("SNAM", 0), a.get("INAM", 0))):
            speaker = aliases[qid].get(a.get("ALID")) or "?"
            npc = by_eid.get(speaker)
            voice = talk.voice_types.get(lo.global_id(npc.plugin, _u32(npc.sub("VTCK")))) if npc is not None and \
                npc.sub("VTCK") else None
            for variant, info in enumerate(infos.get(a["topic"], [])):
                responses = talk._responses(info, voice) if voice else None
                if not responses:
                    vt = any_voice.get((owner, info.form_id & 0xFFFFFF))
                    responses = talk._responses(info, vt) if vt else None
                if not responses:
                    continue
                race = _race_of(lo, info, races)
                for path, text in responses:
                    out = talk._convert(path)
                    if out:
                        lines.append(f"C\t{scene}\t{a.get('SNAM', 0)}\t{a.get('INAM', 0)}\t{variant}\t{speaker}\t{race}\t"
                                     f"{out}\t" + text.replace("\t", " ").replace("\n", " "))
                        count += 1
    # Where everyone stands (Skyrim's markers, by editor id), outdoors and in Helgen Keep, and
    # whatever else the packages name
    for g, r in placed.items():
        _marker(lines, r, _ref_name(g, ref_eid) if g in wanted else None)
    if models is not None:
        for name, (rtype, eid) in OPENING_MODELS.items():
            rec = next((r for r in p.records_of_type(rtype) if r.editor_id() == eid), None)
            modl = rec.sub("MODL") if rec is not None else None
            index = models.index_for(modl.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\")) if modl else None
            if index is None:
                missing.append(f"opening model {eid}")
            else:
                lines.append(f"O\t{name}\t{index}")
    markers = {l.split("\t")[1]: tuple(map(float, l.split("\t")[2:5])) for l in lines if l[0] == "P"}
    vias = [markers[m] for m in CART_VIA if m in markers]
    route = _route(os.path.join(pack, "navmesh"), markers.get(CART_FROM), markers.get(CART_TO), vias)
    if not route:
        missing.append("the cart's route")
    lines += [f"W\tcart\t{x:.2f}\t{y:.2f}\t{z:.2f}" for x, y, z in route]
    print(f"opening: {count} lines, {len(markers)} markers, the cart's road {len(route)} points")
    return lines


CART_FROM, CART_TO = "mq101playerstartmarker", "mq101cart1marker5"
# (through Helgen's east gate as Skyrim's horses go: outside it, its middle, inside it)
CART_VIA = ("mq101carthorse1marker3", "mq101helgeneastgate", "mq101carthorse1marker3a")
ROAD_FLAG = 0x40  # (a navmesh triangle Skyrim's travel prefers: the roads)
OFF_ROAD_COST = 4.0
ROUTE_STEP_M = 3.0


def _route(nav_dir, start, end, vias=()):
    """The cart's way from start to end: A* over the exported navmesh (pack/navmesh), keeping
    to the roads, smoothed; points every ROUTE_STEP_M, or []."""
    import heapq
    if start is None or end is None:
        return []
    cell_m = 4096 / 70.0
    meshes = {}

    def mesh(cx, cy):
        if (cx, cy) not in meshes:
            meshes[(cx, cy)] = None
            path = os.path.join(nav_dir, f"tamriel_{cx}_{cy}.sknav")
            if os.path.exists(path):
                d = open(path, "rb").read()
                _m, _v, _z, _x, _y, nv, nt = struct.unpack_from("<4sHHiiII", d)
                verts = [struct.unpack_from("<3f", d, 24 + i * 12) for i in range(nv)]
                tris = [struct.unpack_from("<3HHIII", d, 24 + nv * 12 + t * 20) for t in range(nt)]
                meshes[(cx, cy)] = (verts, tris)
        return meshes[(cx, cy)]

    def centre(key):
        verts, tris = mesh(key[0], key[1])
        t = tris[key[2]]
        return tuple(sum(verts[t[k]][a] for k in range(3)) / 3.0 for a in range(3))

    def nearest(p):
        cx, cy = int(p[0] // cell_m), int(-p[2] // cell_m)
        best, best_d = None, 1e9
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                m = mesh(cx + dx, cy + dy)
                for t in range(len(m[1]) if m else 0):
                    c = centre((cx + dx, cy + dy, t))
                    d = (c[0] - p[0]) ** 2 + (c[2] - p[2]) ** 2 + 4 * (c[1] - p[1]) ** 2
                    if d < best_d:
                        best, best_d = (cx + dx, cy + dy, t), d
        return best

    dist = lambda p, q: ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5

    def leg(start, end):  # (A* from one to the other: the triangles' middles between, [] if none)
        a, b = nearest(start), nearest(end)
        if a is None or b is None:
            return None
        return astar(a, b)

    def astar(a, b):
        goal = centre(b)
        came, cost, todo = {a: None}, {a: 0.0}, [(0.0, a)]
        return _route_search(mesh, centre, dist, a, b, goal, came, cost, todo)

    # (through Skyrim's own way points on the way - the horses' markers through Helgen's gate: the
    # road found round the gate's corner had the cart through its wall)
    stops = [tuple(start)] + [tuple(v) for v in vias] + [tuple(end)]
    pts = [stops[0]]
    for p, q in zip(stops, stops[1:]):
        mid = leg(p, q)
        if mid is None:
            return []
        pts += mid + [q]
    return _route_smooth(pts)


def _route_search(mesh, centre, dist, a, b, goal, came, cost, todo):
    import heapq
    while todo:
        _f, k = heapq.heappop(todo)
        if k == b:
            break
        verts, tris = mesh(k[0], k[1])
        here = centre(k)
        for n in tris[k[2]][4:7]:
            if n == 0xFFFFFFFF:
                continue
            nk = (k[0] + ((n >> 24) & 3) - 1, k[1] + ((n >> 26) & 3) - 1, n & 0xFFFFFF)
            m = mesh(nk[0], nk[1])
            if m is None or nk[2] >= len(m[1]):
                continue
            there = centre(nk)
            step = dist(here, there) * (1.0 if m[1][nk[2]][3] & ROAD_FLAG else OFF_ROAD_COST)
            c = cost[k] + step
            if c < cost.get(nk, 1e18):
                cost[nk], came[nk] = c, k
                heapq.heappush(todo, (c + dist(there, goal), nk))
    if b not in came:
        return None
    pts, k = [], b
    while k is not None:
        pts.append(centre(k))
        k = came[k]
    pts.reverse()
    return pts


def _route_smooth(pts):
    dist = lambda p, q: ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5
    for _round in range(4):  # (Chaikin: the corners cut, a cart's curve)
        out = [pts[0]]
        for p, q in zip(pts, pts[1:]):
            out += [tuple(p[i] * 0.75 + q[i] * 0.25 for i in range(3)), tuple(p[i] * 0.25 + q[i] * 0.75 for i in range(3))]
        pts = out + [pts[-1]]
    route, acc = [pts[0]], 0.0  # (evenly spaced)
    for p, q in zip(pts, pts[1:]):
        acc += dist(p, q)
        if acc >= ROUTE_STEP_M:
            route.append(q)
            acc = 0.0
    if route[-1] != pts[-1]:
        route.append(pts[-1])
    return route


def _package_targets(lo, pack):
    """The references a package names (its target and locations: PTDA, PLDT of a reference)."""
    out = []
    for t, v in pack.subrecords():
        if t in ("PTDA", "PLDT") and len(v) >= 8 and _u32(v) == 0:
            g = lo.global_id(pack.plugin, _u32(v, 4))
            if g is not None and g not in out:
                out.append(g)
    return out


def _ref_name(g, ref_eid):
    return ref_eid.get(g) or f"#{g:x}"


def _marker(lines, r, name=None):
    eid = name or (r.editor_id() or "").lower()
    if not (name or eid.startswith(OPENING_MARKER_PREFIXES)) or not r.sub("DATA"):
        return
    px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
    lines.append(f"P\t{eid}\t{px / 70.0:.3f}\t{pz / 70.0:.3f}\t{-py / 70.0:.3f}\t{-rz:.4f}")
