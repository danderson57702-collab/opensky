"""The Thieves Guild's questline (Skyrim's TG00 .. TG09, TG02B and TGLeadership): A Chance
Arrangement, Taking Care of Business, Loud and Clear, Meet the Family, Dampened Spirits, Scoundrel's
Folly, Speaking With Silence, Hard Answers, The Pursuit, Trinity Restored, Blindsighted, Darkness
Returns, Under New Management. Their conversations, scenes, items and marks, read by quests.py into
quests.txt (the engine plays them: engine/src/thieves.c)."""

_LAD = lambda s: (s.replace("{}", "lad"), s.replace("{}", "lass"))  # (Brynjolf's "lad" / "lass": your sex's)

TREES = (
    # A Chance Arrangement: Brynjolf in the market - the ring; after
    ("tg00_brynjolf", "Brynjolf", "TG00BrynjolfIntroBranchTopic",
     _LAD("Never done an honest day's work in your life for all that coin you're carrying, eh {}?") +
     ("#000352E0+0004FCDB",), [], None),
    ("tg00_brynjolf_done", "Brynjolf", "TG00BrynjolfOutroPreBranchTopic", "#000A6D76", [], None),
    # Taking Care of Business: the Ragged Flagon; the three who owe; back to Brynjolf
    ("tg01_brynjolf", "Brynjolf", "TG01BrynjolfIntroductionTopic", _LAD("Well, well... color me impressed, {}."), [], None),
    ("tg01_keerava", "Keerava", "TG01KeeravaQuestBranchTopic", "", [], None),
    ("tg01_bersi", "Bersi", "TG01BersiQuestBranchTopic", "", [], None),
    ("tg01_haelga", "Haelga", "TG01HaelgaQuestBranchTopic", "", [], None),
    ("tg01_brynjolf_done", "Brynjolf", "TG01BrynjolfDoneTopicTopic", "#000A29A9", [], None),
    # Loud and Clear: Brynjolf on Goldenglow; Aringoth; back
    ("tg02_brynjolf", "Brynjolf", "TG02BrynjolfStartupBranchTopic", _LAD("Welcome to the family, {}."), [], None),
    ("tg02_brynjolf_done", "Brynjolf", "TG02BrynjolfQuestDoneBranchTopic", _LAD("Word on the street is Goldenglow's been hit. Good job, {}."), [], None),
    # Meet the Family: Vex, Delvin, Tonilia
    ("tg02b_vex", "Vex", "TG02BVexIntroBranchTopic", "#000848EC", [], None),
    ("tg02b_delvin", "DelvinMallory", "TG02BDelvinIntroBranchTopic", "#00084845", [], None),
    ("tg02b_tonilia", "Tonilia", "TG02BToniliaBranchTopic", "#000D33C0", [], None),
    # Dampened Spirits: Maven, Mallus, Sabjorn; Sabjorn after; Maven, Brynjolf after
    ("tg03_maven", "Maven", "TG03MavenIntroBranchTopic", "#0002BA7D", [], None),
    ("tg03_mallus", "MallusMaccius", "TG03MallusIntroBranchTopic02", "#0002BA7B", [], None),
    ("tg03_sabjorn", "Sabjorn", "TG03SabjornIntroBranchTopic01", "#0005413D", [], None),
    ("tg03_sabjorn_done", "Sabjorn", "TG03SabjornTaskDoneBranchTopic", "#000549D1", [], None),
    ("tg03_maven_done", "Maven", "TG03MavenReturnBranchTopic", "", [], None),
    ("tg03_brynjolf_done", "Brynjolf", "TG03BrynjolfReturnBranchTopic", "#0004E3A5", [], None),
    # Scoundrel's Folly: Mercer; Gulum-Ei; Mercer after
    ("tg04_mercer", "MercerFrey", "TG04MercerIntroBranchTopic", "#0002C2D6+000C0085", [], None),
    ("tg04_gulumei", "GulumEi", "TG04GulumEiInitialBranchTopic", "#0002C2C8", [], None),
    ("tg04_gulumei_wine", "GulumEi", "TG04GulumEiFirebrandWineBranchTopic01", "#0004FEEB", [], None),
    ("tg04_mercer_done", "MercerFrey", "TG04MercerOutroBranchTopic", "#0002CC41", [], None),
    # Speaking With Silence: Mercer at Snow Veil; Karliah when you wake
    ("tg05_mercer", "MercerFrey", "TG05MercerIntroBranchTopic", "#0002C353", [], None),
    ("tg05_karliah", "Karliah", "TG05KarliahIntroBranchTopic", "#0001BB42", [], None),
    # Hard Answers: Enthir, Calcelmo; Enthir again; Karliah
    ("tg06_enthir", "Enthir", "TG06EnthirIntroBranchTopic", "#00032D55", [], None),
    ("tg06_calcelmo", "Calcelmo", "TG06CalcelmoIntroBranchTopic", "#00032D58", [], None),
    ("tg06_enthir_done", "Enthir", "TG06EnthirOutroBranchInitialTopic", "#00089BA4", [], None),
    ("tg06_karliah", "Karliah", "TG06KarliahOutroBranchTopic", "#000531A3", [], None),
    # The Pursuit: Karliah at the Flagon; Brynjolf; Brynjolf after Mercer's house
    ("tg07_karliah", "Karliah", "TG07KarliahInitialBranchTopic", "#00055AA2+00058EC2", [], None),
    ("tg07_brynjolf", "Brynjolf", "TG07BrynjolfIntroBranchTopic00", "#00055FC4", [], None),
    ("tg07_brynjolf_done", "Brynjolf", "TG07BrynjolfOutroBranchTopic", "#00037CD5", [], None),
    # Trinity Restored: Karliah at the standing stone; after the Oath; Brynjolf
    ("tg08a_karliah", "Karliah", "TG08AKarliahInitialBranchTopic", "#00055AA2+00058EC2", [], None),
    ("tg08a_karliah_oath", "Karliah", "TG08AKarliahPostOathBranchTopic", "#0005AE9E", [], None),
    ("tg08a_brynjolf", "Brynjolf", "TG08ABrynjolfPostOathBranchTopic", "#00058ECB", [], None),
    # Blindsighted: Karliah at Irkngthand; Mercer; Karliah after
    ("tg08b_karliah", "Karliah", "TG08BKarliahIntroBranchTopic", "#00039E36", [], None),
    ("tg08b_mercer", "MercerFrey", "TG08BMercerOutroBranchTopic", "#00038A3C", [], None),
    ("tg08b_karliah_done", "Karliah", "TG08BKarliahOutroBranchTopic", "#0003A065", [], None),
    # Darkness Returns: Gallus's spirit; Karliah after
    ("tg09_gallus", "Gallus", "TG09PlayerInitialReactionBranchTopic", "#000967F7", [], None),
    ("tg09_gallus_key", "Gallus", "TG09GallusIntroBranchTopic", "#0001A2C5", [], None),
    # Under New Management: Brynjolf; Tonilia
    ("tgl_brynjolf", "Brynjolf", "TGLeadershipBrynjolfBranchTopic", "#000E3F4B", [], None),
    ("tgl_tonilia", "Tonilia", "TGLeadershipToniliaBranchTopic", "#000AA77C", [], None),
)

SCENES = ()

SCENE_EXPORTS = ()

ITEMS = (("tg00_ring", "ARMO", "TG00MadesiRing"),
                  ("tg02_bill", "BOOK", "TG02BillofSale"), ("tg02_key", "KEYM", "TG02GoldenglowSafeKey"),
         ("tg03_poison", "MISC", "TG03Poison"), ("tg03_letter", "BOOK", "TG03SabjornLetter"),
         ("tg04_wine", "MISC", "TG04FirebrandWineCase"),
         ("tg06_rubbing", "MISC", "TG06Rubbing"),
         ("tg07_plans", "MISC", "TG07MercersPlans"),
         ("tg08_key", "MISC", "TG08SkeletonKey"),
         ("tg02b_armor", "ARMO", "ArmorThievesGuildCuirassPlayer"),
         ("tg08_ng_cuirass", "ARMO", "ArmorNightingaleCuirassPlayer01"), ("tg08_ng_boots", "ARMO", "ArmorNightingaleBootsPlayer01"),
         ("tg08_ng_gloves", "ARMO", "ArmorNightingaleGauntletsPlayer01"), ("tg08_ng_hood", "ARMO", "ArmorNightingaleHelmetPlayer01"),
         ("tgl_armor", "ARMO", "ArmorThievesGuildLeaderCuirass"),
         ("tg09_journal", "BOOK", "TG09NystromsJournal"))

import struct

# Marks by editor id: the Ragged Flagon and the Cistern, Goldenglow, Honningbrew, the East Empire
# warehouse, Snow Veil Sanctum, Calcelmo's tower, Mercer's house, Nightingale Hall, Irkngthand
# (legacy mapping preserved for documentation integrity)
PLACES = {
    "tg01brynjolfstart": "tg_flagon_brynjolf", "tg02brynjolfmeetingmarker": "tg_cistern_brynjolf",
    "tg02mercermeetingmarker": "tg_cistern_mercer", "tgleadershipmarkerplayer": "tg_cistern_centre",
    "tg02hivesmarker": "tg02_hives", "tg02safemarker": "tg02_safe",
    "tg03mallusatmeaderymarker": "tg03_mallus", "tg03sabjornqueststartmarker": "tg03_sabjorn",
    "tg03quicktonest": "tg03_nest", "tg03meaderylidref": "tg03_vat", "tg03sabjornchestref": "tg03_chest",
    "tg03mavenmeetingspot": "tg03_maven",
    "tg04eecstart": "tg04_warehouse", "tg04warehousedoorref": "tg04_warehouse_door",
    "tg05mercermeetsplayer": "tg05_mercer", "tg05maindungeondoorref": "tg05_door",
    "tg05playerawakenmarker": "tg05_wake", "tg05karliahfinalmarker": "tg05_karliah",
    "tg06doortobalconyref": "tg06_balcony", "tg07plansmarker": "tg07_plans",
    "tg07karliahmeetingmarker": "tg07_karliah", "tg08akarliahmeetingmarker": "tg08a_karliah",
    "tg08amarkerstoneref": "tg08a_stone", "tg08anightingalearmoractivatorref": "tg08a_armor_stone",
    "tg08aplayerfloorglyphref": "tg08a_glyph", "tg08karliahcirclemarker": "tg08a_karliah_glyph",
    "tg08brynjolfcirclemarker": "tg08a_brynjolf_glyph",
    "tg08bstart": "tg08b_start", "tg08bkarliahstartmarker": "tg08b_karliah", "tg08bbrynjolfstartmarker": "tg08b_brynjolf",
    "tg08bmercerstart": "tg08b_mercer", "tg08bswimoutmarker": "tg08b_escape", "tg08bkarliahfinalmarker": "tg08b_karliah_end",
    "tg08bbrynjolffinalmarker": "tg08b_brynjolf_end", "tg09postkarliahmarker": "tg09_karliah",
}
# Exact placed references for all Thieves Guild marks pulled without heuristic cell matching: {form id: key}
PLACES_ID = {
    # A Chance Arrangement (TG00): Madesi's strongbox in Riften market
    0x00076F14: "tg00_strongbox",
    # Taking Care of Business (TG01): Brynjolf at the Ragged Flagon
    0x000361BB: "tg_flagon_brynjolf",
    # Loud and Clear (TG02): Brynjolf & Mercer meeting in Cistern, Goldenglow hives, safe
    0x00025D86: "tg_cistern_brynjolf", 0x00025D85: "tg_cistern_mercer",
    0x00025D92: "tg02_hives", 0x000480BB: "tg02_safe",
    # Dampened Spirits (TG03): Mallus, Sabjorn start, nest, vat lid, Sabjorn chest, Maven
    0x00082540: "tg03_mallus", 0x00086788: "tg03_sabjorn", 0x000B9774: "tg03_nest",
    0x000C5ACA: "tg03_vat", 0x000813B9: "tg03_chest", 0x000BEBC8: "tg03_maven",
    # Scoundrel's Folly (TG04): EEC warehouse start, warehouse door
    0x000CAEFE: "tg04_warehouse", 0x0003BE89: "tg04_warehouse_door",
    # Speaking With Silence (TG05): Mercer outside Snow Veil, door, wake marker, Karliah
    0x0005576C: "tg05_mercer", 0x0002DDD0: "tg05_door", 0x00032E3E: "tg05_wake",
    0x00032E1F: "tg05_karliah",
    # Hard Answers (TG06): Calcelmo's balcony door, rubbing stone
    0x00088295: "tg06_balcony", 0x0003376E: "tg06_stone",
    # The Pursuit (TG07): Karliah at the Flagon, Mercer's plans
    0x00055AAF: "tg07_karliah", 0x00056159: "tg07_plans",
    # Trinity Restored (TG08A): Karliah outside Nightingale Hall, stone, armor stone, floor glyphs
    0x0005880B: "tg08a_karliah", 0x00058EDD: "tg08a_stone", 0x000B11D0: "tg08a_armor_stone",
    0x00047CD7: "tg08a_glyph", 0x00039E4D: "tg08a_karliah_glyph", 0x00039E4C: "tg08a_brynjolf_glyph",
    # Blindsighted (TG08B): Irkngthand start, Karliah & Brynjolf start, Mercer start, swimout escape, final markers
    0x0005AEA5: "tg08b_start", 0x0005274D: "tg08b_karliah", 0x0005274E: "tg08b_brynjolf",
    0x000527E9: "tg08b_mercer", 0x0006F6F7: "tg08b_escape", 0x0005353E: "tg08b_karliah_end",
    0x0005353F: "tg08b_brynjolf_end",
    # Darkness Returns (TG09): Ebonmere, Gallus, Nystrom's skeleton, Karliah post marker
    0x001018B7: "tg09_ebonmere", 0x0001A2D1: "tg09_gallus", 0x000FF209: "tg09_nystrom",
    0x000E3648: "tg09_karliah",
    # Under New Management (TGLeadership): Cistern center ceremony marker
    0x000E3F80: "tg_cistern_centre",
}

# Trigger volumes (XPRM primitives) for Thieves Guild quests: {form id: key}
TRIGGERS = {
    # Darkness Returns: Ebonmere trigger (TG09EbonmereTriggerRef)
    0x001018B7: "tg09_ebonmere_trig",
    # Trinity Restored: Nightingale Hall pre-oath chamber trigger (TG08APreOathTrigRef)
    0x0005A19C: "tg08a_preoath_trig",
    # Blindsighted: Bronze Water Cave swimout escape trigger (TG08BEscapeTriggerRef)
    0x0005353D: "tg08b_escape_trig",
    # The Pursuit: Cistern trigger (TG07TriggerRef)
    0x00036FF4: "tg07_trig",
    # Trinity Restored: Cistern guild trigger (TG08GuildTriggerRef)
    0x00058803: "tg08_guild_trig",
    # Speaking with Silence: Snow Veil Sanctum sanctuary trigger (TG05LastTriggerRef)
    0x00072876: "tg05_last_trig",
    # Darkness Returns: Nightingale Shadow glyph trigger (TG09NightingaleShadowTriggerRef)
    0x0001A3B7: "tg09_shadow_trig",
    # Darkness Returns: Nightingale Strife glyph trigger (TG09NightingaleStrifeTriggerRef)
    0x0001A3B9: "tg09_strife_trig",
    # Darkness Returns: Nightingale Subterfuge glyph trigger (TG09NightingaleSubterfugeTriggerRef)
    0x0001A3BA: "tg09_subterfuge_trig",
}

FOE_IDS = {0x0006F98A: "tg04_wine"}  # (the quest's own, disabled till then: the Firebrand Wine case in the Blue Palace)
QUEST_KINDS = ("MercerFrey",)
QUEST_REFS = set()
CITY_WORLDS = ("RiftenWorld",)  # (Riften's market: Madesi's strongbox)
ENABLED_ANYWAY = set()


def extract_triggers(lo):
    """Extracts exact trigger volume records for the Thieves Guild questline with
    half-extents, primitive shape types, and float XYZ coordinates."""
    cell_of = {}
    for p in lo.plugins:
        for _cid, c in p.interior_cells().items():
            ceid = (c["cell"].editor_id() or "").lower() if c["cell"] is not None else ""
            for r in c["refs"]:
                g = lo.global_id(p, r.form_id)
                if g is not None:
                    cell_of[g] = ceid
    refs = lo.refs_by_id(set(TRIGGERS))
    shape_names = {1: "box", 2: "sphere", 3: "cylinder"}
    out = []
    for g, key in TRIGGERS.items():
        r = refs.get(g)
        if not r or not r.sub("DATA"):
            continue
        px, py, pz, rx, ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
        xprm = r.sub("XPRM")
        hx, hy, hz, stype = 0.0, 0.0, 0.0, 1
        if xprm and len(xprm) >= 12:
            hx, hy, hz = struct.unpack_from("<3f", xprm)
            if len(xprm) >= 32:
                stype = struct.unpack_from("<I", xprm, 28)[0]
        scale = struct.unpack_from("<f", r.sub("XSCL"))[0] if r.sub("XSCL") else 1.0
        out.append({
            "form_id": g,
            "form_id_hex": f"0x{g:08X}",
            "key": key,
            "cell": cell_of.get(g, "-"),
            "pos": (px, py, pz),
            "rot": (rx, ry, rz),
            "heading": -rz,
            "scale": scale,
            "primitive_type": stype,
            "shape": shape_names.get(stype, "unknown"),
            "half_extents": (hx, hy, hz),
            "engine_pos": (px / 70.0, pz / 70.0, -py / 70.0),
            "engine_half_extents": (hx / 70.0, hz / 70.0, hy / 70.0),
        })
    return out


def extract_places(lo):
    """Extracts exact reference marks for the Thieves Guild questline."""
    cell_of = {}
    for p in lo.plugins:
        for _cid, c in p.interior_cells().items():
            ceid = (c["cell"].editor_id() or "").lower() if c["cell"] is not None else ""
            for r in c["refs"]:
                g = lo.global_id(p, r.form_id)
                if g is not None:
                    cell_of[g] = ceid
    refs = lo.refs_by_id(set(PLACES_ID))
    out = []
    for g, key in PLACES_ID.items():
        r = refs.get(g)
        if not r or not r.sub("DATA"):
            continue
        px, py, pz, rx, ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
        scale = struct.unpack_from("<f", r.sub("XSCL"))[0] if r.sub("XSCL") else 1.0
        out.append({
            "form_id": g,
            "form_id_hex": f"0x{g:08X}",
            "key": key,
            "cell": cell_of.get(g, "-"),
            "pos": (px, py, pz),
            "rot": (rx, ry, rz),
            "heading": -rz,
            "scale": scale,
            "engine_pos": (px / 70.0, pz / 70.0, -py / 70.0),
        })
    return out
