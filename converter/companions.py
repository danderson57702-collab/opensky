"""The Companions' questline (Skyrim's C00 .. C06): Take Up Arms, Proving Honor, the Brotherhood's
induction, The Silver Hand, Blood's Honor, Purity of Revenge and Glory of the Dead. Their
conversations, scenes, items and marks, read by quests.py into quests.txt (the engine plays them:
engine/src/companions.c).

Trees as quests.TREES ((key, speaker, topic, opening(s), tail, assume) - assume: {editor id: True /
False} for Skyrim's quest aliases its lines are conditioned on, which the engine fills as here:
Farkas your Shield-Brother in Proving Honor, the Brotherhood's).
"""

# (the Observer in Proving Honor and the one who brings you to the induction: Farkas)
_FARKAS_OBSERVER = {"farkas": True, "aelathehuntress": False}

import os
import struct

TREES = (
    # Take Up Arms: Kodlak's INFO 000A3E7A, then his question, Vilkas in the yard, Eorlund at the
    # Skyforge and his favour, Aela and her shield
    ("c00_kodlak", "KodlakWhitemane", "C00KodlakJoinUpStartTopic", "#000A3E7A", [], None),
    ("c00_kodlak_back", "KodlakWhitemane", "C00KodlakBackToThePlayerTopic",
     ("#000CC83F", "#000A3E73"), [], None),
    ("c00_vilkas_train", "Vilkas", "C00VilkasTrainPlayerTopic", "#000A3E99", [], None),
    ("c00_vilkas_over", "Vilkas", "C00VilkasTrainingQuestOverTopic", "#000A3E7F", [], None),
    ("c00_vilkas_magic", "Vilkas", "C00VilkasTrainingQuestOverTopic", "#0007476A", [], None),
    ("c00_eorlund", "EorlundGrayMane", "C00EorlundBlockEntryTopic", "#000A4A5D", [], None),
    ("c00_aela_shield", "AelaTheHuntress", "C00AelaIhaveYourShieldTopic", "#000F11BF", [], None),
    ("c00_aela_vilkas", "AelaTheHuntress", "C00AelaCanYaTakeHimTopic", "#000CC83E", [], None),
    # (and Eorlund's weapon for a Companion: DIAL 0004B6FF)
    ("c00_eorlund_weapon", "EorlundGrayMane", "C00EorlundBranchTopic", "#0004B706", [], None),
    # Proving Honor: Skjor's task, Farkas your Shield-Brother, and after the ambush; back to Skjor
    ("c01_skjor", "Skjor", "C01SkjorEntry", "#0002C2D6+000C0085", [], _FARKAS_OBSERVER),
    ("c01_farkas", "Farkas", "C01ObserverInitialHello", "#000C0084", [], _FARKAS_OBSERVER),
    ("c01_farkas_after", "Farkas", "C01ObserverTransformBranchTopic", "#00081DE1", [], _FARKAS_OBSERVER),
    ("c01_skjor_back", "Skjor", "C01SkjorUponReturnBranchTopic", "#0006F6BB", [], _FARKAS_OBSERVER),
    # The Brotherhood: Farkas waiting (INFO 00071DE5), Kodlak after
    ("c02_farkas", "Farkas", "C02QuestgiverKickoffBranchTopic", "#00071DE5", [], _FARKAS_OBSERVER),
    ("c02_kodlak", "KodlakWhitemane", "C02KodlakForceGreetPlayerTopic", ("#000734B5", "#000734B6", "Well, "), [], None),
    ("c02_kodlak_wolves", "KodlakWhitemane", "C02PostQuestKodlakBeginTopic", "#000A3E67", [], None),
    # The Silver Hand: Aela sends you to Skjor; Skjor at the Underforge, then within; Aela after the
    # change, and at Gallows Rock's end; telling the others Skjor has fallen
    ("c03_aela", "AelaTheHuntress", "C03SkorQuestStartBranchTopic", "#000C5C16", [], None),
    ("c03_skjor_door", "Skjor", "C03SkjorUnderforgeEntryBranchTopic", "#000AEBFA", [], None),
    ("c03_skjor_ritual", "Skjor", "C03SkjorReadyForThisBranchTopic", "#000AE72B", [], None),
    ("c03_aela_awake", "AelaTheHuntress", "C03AelaForceGreetPostTransformIntro", "#0003D28D", [], None),
    ("c03_aela_end", "AelaTheHuntress", "C03AelaForceGreetAtEndIntro", "#0003D28E", [], None),
    ("c03_kodlak_skjor", "KodlakWhitemane", "C03PostQuestTellThemBranchTopic", "#000B3DFE", [], None),
    # Blood's Honor: Kodlak on the witches; Vilkas after the attack on Jorrvaskr
    ("c04_kodlak", "KodlakWhitemane", "C04KodlakUhOhHeKnows", "#000ABDF7", [], None),
    ("c04_vilkas", "Vilkas", "C04VilkasPostAttackBranchTopic", "#0005829B", [], None),
    # Purity of Revenge: Vilkas back at Jorrvaskr
    ("c05_vilkas_back", "Vilkas", "C05VilkasUponReturnBranchTopic", ("#0005829C", "#000582EB"), [], None),
    # Glory of the Dead: Eorlund and the fragments, Kodlak's fragment; Vilkas at the tomb; Kodlak's ghost
    ("c06_eorlund", "EorlundGrayMane", "C06EorlundGotTheAxeBranchTopic", "#000582D6", [], None),
    ("c06_eorlund_last", "EorlundGrayMane", "C06EorlundHeresTheLastPieceTopic", "#000E4A31", [], None),
    ("c06_vilkas_tomb", "Vilkas", "C06VilkasIfGreeting", "#000956B1", [], None),
    ("c06_kodlak_ghost", "KodlakWhitemane", "C06KodlakHeyImAGhostBranchTopic", "#000582E9", [], None),
    ("c06_kodlak_end", "KodlakWhitemane", "C06KodlakSeeYaLaterBranchTopic", "#000582D7", [], None),
)

# Their single lines (quests.SCENES: key, speaker, [(topic, quest, opening)]): INFO 000231C8
# to any of the Circle, pointing you on to Kodlak; to Skjor after Proving Honor; the others on Skjor's death
SCENES = (
    ("c00_ask_aelathehuntress", "AelaTheHuntress", [("CRNoWorkBranchTopic", "C00", "#000231C8")]),
    ("c00_ask_vilkas", "Vilkas", [("CRNoWorkBranchTopic", "C00", "#0005477B")]),
    ("c00_ask_farkas", "Farkas", [("CRNoWorkBranchTopic", "C00", "#0005477C")]),
    ("c00_ask_skjor", "Skjor", [("CRNoWorkBranchTopic", "C00", "#0005477D")]),
    ("c00_kodlak_wait", "KodlakWhitemane", [("C00Hellos", "C00", "#000BD68D")]),
    ("c01_point_farkas", "Farkas", [("C01OtherCompanionsPointToSkjorBranchTopic", "C01", "#0006F6BD")]),
    ("c01_point_aelathehuntress", "AelaTheHuntress", [("C01OtherCompanionsPointToSkjorBranchTopic", "C01", "")]),
    ("c01_point_vilkas", "Vilkas", [("C01OtherCompanionsPointToSkjorBranchTopic", "C01", "")]),
    ("c03_tell_vilkas", "Vilkas", [("C03PostQuestTellThemBranchTopic", "C03PostQuest", "#000B3DFF")]),
    ("c03_tell_farkas", "Farkas", [("C03PostQuestTellThemBranchTopic", "C03PostQuest", "#000B3E00")]),
    ("c05_vilkas_hello", "Vilkas", [("C05Hellos", "C05", "#00092A43")]),
    ("c06_lead_vilkas", "Vilkas", [("C06PostQuestSoImInChargeNowHuhBranchTopic", "C06PostQuest", "#00000000")]),
    ("c06_lead_farkas", "Farkas", [("C06PostQuestSoImInChargeNowHuhBranchTopic", "C06PostQuest", "#000B6278")]),
    ("c06_lead_aelathehuntress", "AelaTheHuntress", [("C06PostQuestSoImInChargeNowHuhBranchTopic", "C06PostQuest", "#00000000")]),
)

# Skyrim's scenes (SCEN), said whole: (key, scene, alias override)
SCENE_EXPORTS = (
    ("c00_argue", "C00KodlakVilkasArgueScene", None), ("c00_argue_player", "C00KodlakVilkasArgueAboutPlayer", None),
    ("c00_dismiss", "C00KodlakDismissVilkas", None), ("c00_aela_skjor", "C00AelaSkjorScene", None),
    ("c00_farkas_call", "C00AelaSkjorFarkasScene", None), ("c00_farkas1", "C00FarkasNarration1", None),
    ("c00_farkas2", "C00FarkasNarration2", None), ("c00_farkas3", "C00FarkasNarration3", None),
    ("c00_farkas4", "C00FarkasNarration4", None), ("c00_fight", "C00JorrvaskrFightScene", None),
    ("c00_eorlund_followup", "C00EorlundFollowupScene", None),
    ("c01_ambush", "C01AmbushScene", ("Observer", "Farkas")), ("c01_post_ambush", "C01PostAmbush", ("Observer", "Farkas")),
    ("c02_induction", "C02InductionScene", ("LastQuestObserver", "Farkas")),  # (its witness: your Shield-Brother)
    ("c03_skjor_aela", "C03SkjorAelaGiveSpielScene", None), ("c03_ritual", "C03BloodRitual", None),
    ("c03_post_rampage", "C03PostRampage", None),
    ("c04_gawkers", "C04GawkersScene", None),
    ("c06_funeral", "C06FuneralForAFriendScene", None), ("c06_underforge", "C06UnderforgeArgument", None),
    ("c06_extraction", "C06Extraction", None),
)

# Their things: (name in quests.txt, record type, editor id)
ITEMS = (("c00_vilkas_sword", "WEAP", "SkyforgeSteelSword"), ("c00_aela_shield", "ARMO", "ArmorSteelShield"),
         ("c01_fragment", "MISC", "YsgramorsBladePiece07"), ("c04_witch_head", "MISC", "C04HagravenHead"),
         ("c05_fragments", "MISC", "C05FragmentSack"), ("c06_kodlak_fragment", "MISC", "YsgramorsBladePiece08"),
         ("c06_journal", "BOOK", "C06KodlaksJournal"), ("c06_wuuthrad", "WEAP", "C06BladeOfYsgramor"),
         ("c06_shield", "ARMO", "ArmorShieldofYsgramor"),
         # (Eorlund's weapons for a new Companion: Skyforge steel)
         ("c00_skyforge_sword", "WEAP", "SkyforgeSteelSword"), ("c00_skyforge_dagger", "WEAP", "SkyforgeSteelDagger"),
         ("c00_skyforge_waraxe", "WEAP", "SkyforgeSteelWarAxe"), ("c00_skyforge_battleaxe", "WEAP", "SkyforgeSteelBattleaxe"),
         ("c00_skyforge_greatsword", "WEAP", "SkyforgeSteelGreatsword"))

# Their marks, by Skyrim's reference (many have no editor id): {form id: key}. Whiterun's own (the
# Skyforge, the induction's places) are in Tamriel's coordinates, as the city's world shares them
PLACES_ID = {
    # (Jorrvaskr: Kodlak and Vilkas in the living quarters below, the training yard, the Skyforge)
    0x000A3EA0: "c00_kodlak_spot", 0x000A3EA1: "c00_vilkas_spot", 0x00023E55: "c00_training",
    0x0001AD6E: "skyforge", 0x000A3ECF: "c00_aela_wait", 0x000A3ECE: "c00_skjor_wait",
    0x000A3ED4: "c00_farkas_start", 0x000A3ED6: "c00_farkas_end", 0x0001F896: "jorrvaskr_centre",
    # (Proving Honor: Dustman's Cairn's fragment, its ambush, Farkas's place to stand)
    0x0006E807: "c01_fragment", 0x000A6F51: "c01_ambush", 0x000A6F5A: "c01_farkas_stay",
    # (the induction: the circle at the Skyforge's yard)
    0x0007165E: "c02_kodlak", 0x00017214: "c02_vilkas", 0x00017213: "c02_farkas", 0x00017212: "c02_aela",
    0x00017211: "c02_skjor", 0x0003B9A8: "c02_wait",
    # (The Silver Hand: the Underforge's door and within, the ritual's font, waking with Aela,
    # Gallows Rock)
    0x00067681: "c03_skjor_door", 0x0001A701: "underforge_door", 0x00067680: "c03_aela_inside",
    0x00108E1D: "c03_skjor_inside", 0x000AEC00: "c03_wake", 0x000AEBFF: "c03_wake_aela",
    0x000AEFBD: "c03_gallows_end",
    # (Blood's Honor: Glenmoril Coven; Jorrvaskr attacked)
    0x00016257: "c04_coven", 0x000B8E2F: "c04_attacked1", 0x000B8DB7: "c04_attacked2", 0x000AA033: "c04_chair",
    # (Purity of Revenge: Driftshade Refuge's box the fragments are in)
    0x000DB359: "c05_fragments",
    # (Glory of the Dead: the funeral, Kodlak's end table, the Underforge's circle, Ysgramor's Tomb -
    # the statue, the burial chamber's flame and Kodlak, the boss chest)
    0x000582FF: "c06_funeral", 0x000582FD: "c06_eorlund_funeral", 0x000582FC: "c06_farkas_funeral",
    0x000582FB: "c06_vilkas_funeral", 0x000582FA: "c06_aela_funeral", 0x000CEF13: "c06_end_table",
    0x000E3123: "c06_vilkas_wait", 0x000E3121: "c06_aela_wait", 0x000E30FF: "c06_farkas_wait",
    0x000AB105: "c06_statue", 0x00058302: "c06_flame", 0x0004E3D3: "c06_kodlak", 0x0001720E: "c06_chamber",
    0x000F60A4: "c06_vilkas_tomb", 0x000F60A3: "c06_aela_tomb", 0x000F60A2: "c06_farkas_tomb",
    0x000B0E15: "c06_boss_chest",
}
# Trigger volumes (XPRM primitives) for Companions quests: {form id: key}
TRIGGERS = {
    # Proving Honor: Dustman's Cairn ambush trigger box (C01CompanionTriggerBox)
    0x0010FF10: "c01_ambush_box",
    # Glory of the Dead: Ysgramor's Tomb burial chamber trigger (C06InBurialChamberTriggerREF)
    0x0001720E: "c06_chamber_trig",
    # Glory of the Dead: Ysgramor's Tomb statue activator trigger (C06StatueActivator)
    0x000ED2EE: "c06_statue_trig",
    # Totem slot activator triggers in the Underforge
    0x000EC1A0: "c06_totem_trig1",
    0x000EC1A5: "c06_totem_trig2",
    0x000EC1A6: "c06_totem_trig3",
}

# The quests' own foes: all 56 placed references (ACHR) extracted from Skyrim.esm without
# heuristic cell or title matching: {form id: key}
QUEST_FOES = {
    # Gallows Rock leader: Krev the Skinner
    0x0002516D: "c03_krev",
    # Glenmoril Coven: Blood's Honor witches (c04_witch1 .. c04_witch5)
    0x0004072B: "c04_witch1", 0x00040570: "c04_witch2", 0x0004056C: "c04_witch3",
    0x00040521: "c04_witch4", 0x00040470: "c04_witch5",
    # Dustman's Cairn: Proving Honor Silver Hand (c01_sh1 .. c01_sh15)
    0x000A6F4B: "c01_sh1", 0x000A6F1E: "c01_sh2", 0x000A6F43: "c01_sh3",
    0x000A6F0E: "c01_sh4", 0x000A6F0F: "c01_sh5", 0x0002AB6A: "c01_sh6",
    0x0002AB6B: "c01_sh7", 0x0002AB6C: "c01_sh8", 0x0002AB8A: "c01_sh9",
    0x0004A413: "c01_sh10", 0x0009AE07: "c01_sh11", 0x000957E2: "c01_sh12",
    0x000957DF: "c01_sh13", 0x00094481: "c01_sh14", 0x0009447F: "c01_sh15",
    # Gallows Rock: The Silver Hand (c03_sh1 .. c03_sh16)
    0x000A9D00: "c03_sh1", 0x000A9D02: "c03_sh2", 0x000A9C66: "c03_sh3",
    0x000A9CFC: "c03_sh4", 0x000A9CF9: "c03_sh5", 0x000A9CF8: "c03_sh6",
    0x000A9CF7: "c03_sh7", 0x000AA09E: "c03_sh8", 0x000AA08B: "c03_sh9",
    0x000AA085: "c03_sh10", 0x000ECAED: "c03_sh11", 0x000ED2C8: "c03_sh12",
    0x000ED2CB: "c03_sh13", 0x000ED2D5: "c03_sh14", 0x000ED2D1: "c03_sh15",
    0x000ED2D4: "c03_sh16",
    # Driftshade Sanctuary: Purity of Revenge (c05_sh1 .. c05_sh11)
    0x0006A99C: "c05_sh1", 0x0006A99B: "c05_sh2", 0x0003D6C3: "c05_sh3",
    0x0003D6C0: "c05_sh4", 0x0001E07F: "c05_sh5", 0x0001D700: "c05_sh6",
    0x00076A13: "c05_sh7", 0x00076A52: "c05_sh8", 0x00076A53: "c05_sh9",
    0x00076AC4: "c05_sh10", 0x00071BA3: "c05_sh11",
    # Driftshade Cellar: Purity of Revenge (c05_shb1 .. c05_shb8)
    0x0003DDA7: "c05_shb1", 0x0003E457: "c05_shb2", 0x0003E452: "c05_shb3",
    0x0005A802: "c05_shb4", 0x00076E10: "c05_shb5", 0x000907D8: "c05_shb6",
    0x000907D4: "c05_shb7", 0x000907D5: "c05_shb8",
}

# Legacy heuristic mapping preserved for documentation integrity
QUEST_FOE_CELLS = {"dustmanscairn01": ("c01_sh", ("Silver Hand",)), "gallowsrock01": ("c03_sh", ("Silver Hand",)),
                   "glenmorilcoven01": (None, ("Glenmoril Witch",)),
                   "driftshadesanctuary01": ("c05_sh", ("Silver Hand",)), "driftshadesanctuary02": ("c05_shb", ("Silver Hand",))}
QUEST_FOE_NAMES = {0x0002516D: "c03_krev", 0x0004072B: "c04_witch1", 0x00040570: "c04_witch2",
                   0x0004056C: "c04_witch3", 0x00040521: "c04_witch4", 0x00040470: "c04_witch5"}


def quest_foes(lo, strings=None, enabled_ids=None):
    """Q lines of Companions quest foes: exact placed references (56 foes across Dustman's Cairn,
    Gallows Rock, Glenmoril Coven, and Driftshade Refuge) pulled directly from the verified REFR
    extraction pipeline without heuristic cell or title matching."""
    cell_of = {}
    for p in lo.plugins:
        for _cid, c in p.interior_cells().items():
            ceid = (c["cell"].editor_id() or "").lower() if c["cell"] is not None else ""
            for r in c["refs"]:
                g = lo.global_id(p, r.form_id)
                if g is not None:
                    cell_of[g] = ceid
    refs = lo.refs_by_id(set(QUEST_FOES))
    out, count = [], {}
    for g, key in QUEST_FOES.items():
        r = refs.get(g)
        if not r or not r.sub("DATA"):
            continue
        cell = cell_of.get(g, "-")
        px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", r.sub("DATA"))
        out.append(f"Q\t{key}\t{cell}\t{px / 70.0:.3f}\t{pz / 70.0:.3f}\t{-py / 70.0:.3f}\t{-rz:.4f}")
        pfx = key.rstrip("0123456789")
        count[pfx] = count.get(pfx, 0) + 1
    print("companions' foes:", ", ".join(f"{k} {n}" for k, n in count.items()))
    return out


def extract_triggers(lo):
    """Extracts exact trigger volume records for the Companions questline with half-extents,
    primitive shape types, and float XYZ coordinates."""
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
    """Extracts exact reference marks for the Companions questline."""
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


# Placed foes a quest finds by Skyrim's reference (creatures.txt's S lines carry it): Krev the Skinner,
# Gallows Rock's Silver Hand leader
QUEST_REFS = set()  # (none needed now: the quests place their own - QUEST_FOE_CELLS)
# (in Whiterun's own world: the Skyforge and its yard - looked up there too)
CITY_WORLDS = ("WhiterunWorld",)


# --- Beast Form (Skyrim's werewolf: The Silver Hand's gift) ---------------------------------------

BEAST_LEVELS = ((10, "PlayerWerewolfLvl10AndBelowAbility"), (15, "PlayerWerewolfLvl15AndBelowAbility"),
                (20, "PlayerWerewolfLvl20AndBelowAbility"), (25, "PlayerWerewolfLvl25AndBelowAbility"),
                (30, "PlayerWerewolfLvl30AndBelowAbility"), (35, "PlayerWerewolfLvl35AndBelowAbility"),
                (40, "PlayerWerewolfLvl40AndBelowAbility"), (45, "PlayerWerewolfLvl45AndBelowAbility"),
                (999, "PlayerWerewolfLvl50AndOverAbility"))
AV_HEALTH, AV_STAMINA, AV_UNARMED, AV_HEAL_RATE = 24, 26, 35, 155  # (Skyrim's actor values)
RACE_UNARMED_AT, RACE_HEIGHT_AT = 0x60, 0x10  # (RACE DATA: unarmed damage, then reach; the male height)


def export_beast(lo, pack, missing):
    """pack/beast.txt: the werewolf race's claws (unarmed damage, reach in metres), the bonus by your
    level (the PlayerWerewolfLvl..Ability spells: L max_level bonus), AbWerewolf's health and stamina
    and whether health comes back, the feeding's health (WerewolfFeed), and PlayerWerewolfQuest's
    script's times (seconds: standard, a feeding's more, the warning)."""
    import interiors
    u32 = lambda b, o=0: struct.unpack_from("<I", b, o)[0]
    recs = {}
    for p in lo.plugins:
        for t in ("SPEL", "RACE", "QUST"):
            for r in p.records_of_type(t):
                recs[(t, r.editor_id() or "")] = r

    def effects(name):
        r, out, cur = recs.get(("SPEL", name)), [], None
        for t, v in (r.subrecords() if r is not None else ()):
            if t == "EFID":
                cur = lo.record(lo.global_id(r.plugin, u32(v)), types=("MGEF",))
            elif t == "EFIT" and cur is not None and cur.sub("DATA"):
                out.append((struct.unpack_from("<i", cur.sub("DATA"), 0x44)[0], struct.unpack_from("<f", v)[0]))
        return out

    race = recs.get(("RACE", "WerewolfBeastRace"))
    script = recs.get(("QUST", "PlayerWerewolfQuest"))
    if race is None or script is None:
        missing.append("beast form")
        return
    damage, reach = struct.unpack_from("<2f", race.sub("DATA"), RACE_UNARMED_AT)
    height = struct.unpack_from("<f", race.sub("DATA"), RACE_HEIGHT_AT)[0]
    lines = [f"C\t{damage:g}\t{reach / 70.0:.3f}\t{height:.3f}"]
    for top, name in BEAST_LEVELS:
        bonus = next((m for av, m in effects(name) if av == AV_UNARMED), 0.0)
        lines.append(f"L\t{top}\t{bonus:g}")
    ab = dict(effects("AbWerewolf"))
    feed = dict(effects("WerewolfFeed"))
    lines.append(f"A\t{ab.get(AV_HEALTH, 0):g}\t{ab.get(AV_STAMINA, 0):g}\t{1 if ab.get(AV_HEAL_RATE, 0) >= 100 else 0}\t{feed.get(AV_HEALTH, 0):g}")

    class _Lo:  # (interiors' Papyrus property reader wants only .lo)
        pass
    holder = _Lo()
    holder.lo = lo
    props = interiors.Exporter._scripts(holder, None, script).get("playerwerewolfchangescript", {})
    lines.append(f"T\t{props.get('StandardDurationSeconds', 150.0):g}\t{props.get('FeedExtensionTimeSeconds', 30.0):g}"
                 f"\t{props.get('DurationWarningTimeSeconds', 15.0):g}")
    with open(os.path.join(pack, "beast.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("beast form:", "; ".join(lines))
