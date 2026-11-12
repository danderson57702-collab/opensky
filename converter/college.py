"""The College of Winterhold's questline (Skyrim's MG01 .. MG08, played in its order: First Lessons,
Under Saarthal, Hitting the Books, Good Intentions, Revealing the Unseen (mg06), Containment (MG05),
The Staff of Magnus, The Eye of Magnus). Its conversations, scenes, items and marks, read by quests.py
into quests.txt (the engine plays them: engine/src/college.c).

Trees as quests.TREES ((key, speaker, topic, opening(s), tail, assume): assume may name "_stop": topics
not followed - those the engine plays itself, Faralda's test by your best school)."""

TREES = (
    # First Lessons: Faralda at the bridge (DIAL 000AF0D8) - her test, persuasion or the
    # Dragonborn's Voice; Mirabelle's tour; Tolfdir's first lesson and the ward
    ("mg01_faralda", "Faralda", "MG01Faralda1WhyAreYouHereBranchTopic", "#000AF0DF", [],
     {"_stop": ("MG01NiryaGreetingBranchTopic",)}),
    ("mg01_mirabelle", "MirabelleErvine", "MG01MirabelleStage30BranchTopic", "#00020029", [], None),
    ("mg01_tolfdir_opinion", "Tolfdir", "MG01TolfdirStage50SceneBranchTopic", "#00020A2A", [], None),
    ("mg01_tolfdir_ward", "Tolfdir", "MG01TolfdirClassroomScene2BranchTopic", "#00021A6E", [], None),
    # Under Saarthal: Tolfdir outside and within, Arniel, the vision told, the orb; the Arch-Mage
    ("mg02_tolfdir_go", "Tolfdir", "MG02TolfdirStage10BranchTopic", "#000233CA", [], None),
    ("mg02_tolfdir_now", "Tolfdir", "MG02TolfdirStage20Init", "#0002513F", [], None),
    ("mg02_tolfdir_nomore", "Tolfdir", "MG02TolfdirStage20NoMoreQuestions", "#00025142", [], None),
    ("mg02_arniel", "ArnielGane", "MG02ArnielInitialBranchTopic", "#0004B2B5", [], None),
    ("mg02_tolfdir_vision", "Tolfdir", "MG02TolfdirStage50BranchTopic", "#00026180", [], None),
    ("mg02_tolfdir_orb", "Tolfdir", "MG02TolfdirStage60BranchTopic", "#00026197", [], None),
    ("mg02_savos", "SavosAren", "MG02SavosStage70BranchTopic", "#000263D8", [], None),
    # Hitting the Books: Mirabelle, Urag, Orthorn in his cage, the books returned
    ("mg03_mirabelle", "MirabelleErvine", "MG03Stage10InitialBranchTopic", "#0002ABB7", [], None),
    ("mg03_urag", "UraggroShub", "MG03UragStage20InitialTopic", "#0002B36D", [], None),
    ("mg03_orthorn", "Orthorn", "MG03OrthornInitialBranchTopic", "#00044C06", [], None),
    ("mg03_urag_back", "UraggroShub", "MG03Stage60UragBranchTopic", "#0002B967", [], None),
    # Good Intentions: Tolfdir and the orb, Ancano's summons, Quaranir, the Augur of Dunlain, Savos
    ("mg04_tolfdir", "Tolfdir", "MG04TolfdirStage10BranchTopic", "#00082A10", [], None),
    ("mg04_ancano", "Ancano", "MG04Stage10AncanoBranchTopic", "#0002C8D3", [], None),
    ("mg04_quaranir", "Quaranir", "MG04Stage30QuaranirBranchTopic", "#0002C8D7", [], None),
    ("mg04_augur", "MG04Augur", "MG04Stage40AugurBranchTopic", "#0002C8DA", [], None),
    ("mg04_savos", "SavosAren", "MG04Stage50SavosArenBranchTopic", "#0002756F", [], None),
    # Revealing the Unseen: Mirabelle on the Staff, Paratus in Mzulft, the crystal; the Arch-Mage after
    ("mg06_mirabelle", "MirabelleErvine", "MG06MirabelleStage10BranchTopic", "#00027577", [], None),
    ("mg06_paratus", "ParatusDecimius", "MG06ParatusInitialBranchTopic", "#0007EAA8", [], None),
    ("mg06_paratus_crystal", "ParatusDecimius", "MG06ParatusStage35BranchTopic", "#0009F79A", [], None),
    ("mg06_mirabelle_after", "MirabelleErvine", "MG06Stage99BranchTopic", "#0004D863+00055BF7", [], None),
    ("mg06_savos", "SavosAren", "MG06Stage60ArenStaffBranchTopic", "#0004D877", [], None),
    # Containment: Tolfdir in the courtyard, Mirabelle once Winterhold's safe
    ("mg05_tolfdir", "Tolfdir", "MG05TolfdirForceGreetBranchTopic", "#0004D863+00055BF7", [], None),
    ("mg05_mirabelle", "MirabelleErvine", "MG05Stage50MirabelleBranchTopic", "#0005310D", [], None),
    # The Staff of Magnus and The Eye of Magnus: Tolfdir back at the College; after Ancano; Quaranir
    ("mg07_tolfdir", "Tolfdir", "MG07TolfdirForcegreetBranchTopic", "#000130DA", [], None),
    ("mg08_tolfdir_after", "Tolfdir", "MG08Stage40TolfdirBranchTopic", "#00041560", [], None),
    ("mg08_quaranir", "Quaranir", "MG08Stage50FollowUp1", "#00039830", [], None),
)

# Their single lines: Faralda's test by school, her INFO 0001F811 and welcome, the Voice heard
SCENES = (
    ("mg01_test_alteration", "Faralda", [("MG01NiryaGreetingBranchTopic", "MG01", "#0001F81E")]),
    ("mg01_test_conjuration", "Faralda", [("MG01NiryaGreetingBranchTopic", "MG01", "#0001F81D")]),
    ("mg01_test_destruction", "Faralda", [("MG01NiryaGreetingBranchTopic", "MG01", "#0001F81A")]),
    ("mg01_test_illusion", "Faralda", [("MG01NiryaGreetingBranchTopic", "MG01", "#0001F81C")]),
    ("mg01_test_restoration", "Faralda", [("MG01NiryaGreetingBranchTopic", "MG01", "#0001F81B")]),
    ("mg01_cast_done", "Faralda", [("MG01NiryaStage20BranchTopic", "MG01", "#0001F811")]),
    ("mg01_in", "Faralda", [("MG01FaraldaYoureInBranchTopic", "MG01", "#000B8119")]),
    ("mg01_voice", "Faralda", [("MG01FaraldaShoutForcegreetBranchTopic", "MG01", "#000B811D")]),
    ("mg02_tolfdir_trapped", "Tolfdir", [("MG02TolfdirStage40BranchInit", "MG02", "#00026185")]),
    ("mg03_orthorn_books", "Orthorn", [("MG03OrthornStage50BranchTopic", "MG03", "#00044C04")]),
    ("mg08_tolfdir_staff", "Tolfdir", [("MG08Stage10TolfdirBranchTopic", "MG08", "#000596F5")]),
    # (the Augur of Dunlain - no one there to speak it but his voice in the flame: his answers in turn)
    ("mg04_augur_lines", "*", [("MG04Stage40AugurBranchTopic", "MG04", "#0002C8DA"),
                               ("MG04Stage40AugurResponse2", "MG04", "#0004FD3E"),
                               ("MG04Stage40AugurNeed1", "MG04", "#000240B1"),
                               ("MG04Stage40AugurThalmor1", "MG04", "#000240AE")]),
)

SCENE_EXPORTS = (
    ("mg01_tour", "MG01TourScene", None), ("mg01_classroom", "MG01ClassroomScene", None),
    ("mg01_ward", "MG01WardScene", None), ("mg01_ward_post", "MG01WardPostScene", None),
    ("mg02_lecture", "MG02TolfdirLectureScene", None), ("mg02_instruction", "MG02TolfdirInstructionScene", None),
    ("mg02_amulet", "MG02TolfdirAmuletScene", None), ("mg02_vision", "MG02VisionScene", None),
    ("mg02_eye_approach", "MG02TolfdirEyeApproachScene", None), ("mg02_eye", "MG02TolfdirEyeScene", None),
    ("mg03_caller", "MG03CallerScene", None), ("mg03_end", "MG03EndScene", None),
    ("mg04_classroom", "MG04ClassroomScene", None), ("mg04_quaranir_open", "MG04QuaranirOpeningLineScene", None),
    ("mg04_quaranir_scene", "MG04QuaranirScene", None), ("mg04_augur", "MG04AugurSpeakScene", None),
    ("mg06_gavros", "MG06GavrosScene", None), ("mg06_explain", "MG06ParatusExplanationScene", None),
    ("mg06_oculory", "MG06ParatusOculoryScene", None), ("mg06_oculory_end", "MG06OculoryFinalScene", None),
    ("mg06_vision", "MG06VisionScene", None), ("mg06_mirabelle_scene", "MG06Stage99MirabelleScene", None),
    ("mg05_courtyard", "MG05TolfdirCourtyardScene", None), ("mg05_bridge", "MG05FaraldaBridgeScene", None),
    ("mg07_priest", "MG07DragonPriestScene", None), ("mg07_end", "MG07EndScene01", None),
    ("mg07_faralda", "MG07FaraldaEndScene01", None),
    ("mg08_approach", "MG08TolfdirCollegeApproachScene", None), ("mg08_taunt", "MG08AncanoTauntScene", None),
    ("mg08_eye_hit", "MG08AncanoEyeHitScene", None), ("mg08_after", "MG08TolfdirPostFightScene", None),
    ("mg08_monk", "MG08MonkScene", None),
)

ITEMS = (("mg02_ring", "ARMO", "MG02Ring01"), ("mg02_amulet", "ARMO", "MG02Amulet"),
         ("mg03_book1", "BOOK", "MG03Book01"), ("mg03_book2", "BOOK", "MG03Book02"), ("mg03_book3", "BOOK", "MG03Book03"),
         ("mg06_crystal", "MISC", "MG06Crystal"), ("mg06_journal", "BOOK", "MG06SynodJournal1"),
         ("mg07_staff", "WEAP", "MG07StaffofMagnus"), ("mg07_torc", "ARMO", "MG07Keystone"),
         ("mg08_robes", "ARMO", "ClothesCollegeRobesArchmage"))

import struct

# Marks by editor id (quests.QUEST_PLACES): the College's courtyard and halls, Saarthal, Fellglow Keep,
# Mzulft's Oculory, Labyrinthian (legacy mapping preserved for documentation integrity)
PLACES = {
    "mg01niryawaitmarker": "mg01_faralda", "mg01gatestoneref": "mg01_seal", "mg01mirabellescenemarkerref": "mg01_mirabelle",
    "mg01playercastmarker": "mg01_classroom", "mg03tolfdirholdmarker": "mg_tolfdir_hall",
    "mg02tolfdirstartmarker": "mg02_tolfdir_out", "mg02saarthaldoorextref": "mg02_door",
    "mg02tolfdirstage20marker": "mg02_tolfdir_in", "mg02saarthalarnielmarker": "mg02_arniel",
    "mg02enchanteditemmarker01": "mg02_item1", "mg02enchanteditemmarker02": "mg02_item2",
    "mg02enchanteditemmarker03": "mg02_item3", "mg02amuletresonanceref": "mg02_amulet",
    "mg02tolfdirstage40marker": "mg02_tolfdir_trap", "mg02visionplayertriggermarker": "mg02_vision",
    "mg02nerienmarker": "mg02_nerien", "mg02orbtriggerref": "mg02_orb", "mg02tolfdircastbossmarker": "mg02_tolfdir_boss",
    "mg03orthorncaptivemarker": "mg03_orthorn", "mg03callerstartmarker": "mg03_caller_spot", "mg03book01ref": "mg03_books",
    "mg03savoswaitmarker": "mg_savos", "mg04ancanogreetmarker": "mg04_ancano_quarters",
    "mg04ancanoclassmarker": "mg04_ancano_hall", "mg06mirabellemarker": "mg06_mirabelle",
    "mg06gavrosmarker": "mg06_gavros", "mg06paratusexplanationmarker": "mg06_paratus_explain",
    "mg06armillaryref": "mg06_armillary", "mg06paratusidlemarker": "mg06_paratus",
    "mg05savosarenmarker": "mg05_savos", "mg05tolfdirscenemarker": "mg05_tolfdir", "mg05mirabellestartmarker": "mg05_mirabelle",
    "mg05mirabelleendmarker": "mg05_mirabelle_end", "mg07tolfdirbridgemarker": "mg07_tolfdir_bridge",
    "mg08ancanomarker": "mg08_ancano", "mg08eyemarker": "mg08_eye", "mg08quaranirmarker": "mg08_quaranir",
    "mg08tolfdirentrymarker": "mg08_tolfdir", "mg08faraldamarker": "mg08_faralda",
}
# Exact placed references for all College marks pulled without heuristic cell matching: {form id: key}
PLACES_ID = {
    # First Lessons (MG01): Faralda at the bridge, the seal on the gate, Mirabelle, classroom
    0x000379EF: "mg01_faralda", 0x00051190: "mg01_seal", 0x0002A407: "mg01_mirabelle", 0x000F9363: "mg01_classroom",
    # Under Saarthal (MG02): Tolfdir exterior & interior, door, Arniel, enchanted items,
    # amulet, trap, vision, Nerien, orb, boss marker
    0x000233CB: "mg02_tolfdir_out", 0x00015E82: "mg02_door", 0x000233CF: "mg02_tolfdir_in",
    0x0004B2BC: "mg02_arniel", 0x0002584E: "mg02_item1", 0x0002584D: "mg02_item2",
    0x00025859: "mg02_item3", 0x00106BBA: "mg02_amulet", 0x0002618E: "mg02_tolfdir_trap",
    0x00040124: "mg02_vision", 0x000A393A: "mg02_nerien", 0x0003FF08: "mg02_orb",
    0x000EAB28: "mg02_tolfdir_boss",
    # Hitting the Books (MG03): Orthorn, Caller's spot, books, Savos, Tolfdir in the hall
    0x00044C09: "mg03_orthorn", 0x000D1EED: "mg03_caller_spot", 0x0004D24C: "mg03_books",
    0x000E5022: "mg_savos", 0x00104AA9: "mg_tolfdir_hall",
    # Good Intentions (MG04): Ancano in quarters and hall, Augur's flame
    0x0002C8F2: "mg04_ancano_quarters", 0x0002C8E9: "mg04_ancano_hall", 0x00108D76: "mg04_augur_flame",
    # Containment (MG05): Savos, Tolfdir, Mirabelle start/end
    0x0004DB9E: "mg05_savos", 0x00097A54: "mg05_tolfdir", 0x0004DB99: "mg05_mirabelle",
    0x000C9A16: "mg05_mirabelle_end",
    # Revealing the Unseen (MG06): Mirabelle, Gavros, Paratus explain/idle/ref, Armillary, crystal chest
    0x00027D20: "mg06_mirabelle", 0x0008F184: "mg06_gavros", 0x000B5FB7: "mg06_paratus_explain",
    0x00094ABD: "mg06_armillary", 0x000FF52D: "mg06_paratus", 0x0007D41B: "mg06_paratus_ref",
    0x000B0B56: "mg06_crystal_chest",
    # The Staff of Magnus (MG07): Tolfdir at the bridge, Morokei
    0x00053F18: "mg07_tolfdir_bridge", 0x00086CF4: "mg07_morokei",
    # The Eye of Magnus (MG08): Ancano, Eye, Quaranir, Tolfdir entry, Faralda
    0x00039835: "mg08_ancano", 0x000C3EC1: "mg08_eye", 0x00059700: "mg08_quaranir",
    0x0010DEA2: "mg08_tolfdir", 0x00082A4D: "mg08_faralda",
}

# Trigger volumes (XPRM primitives) for College of Winterhold quests: {form id: key}
TRIGGERS = {
    # Under Saarthal: Saarthal's orb trigger (MG02OrbTriggerRef)
    0x0003FF08: "mg02_orb_trig",
    # Under Saarthal: Vision collision plane trigger (MG02VisionCollisionPlaneREF)
    0x0004B98A: "mg02_vision_trig",
    # Under Saarthal: Draugr ambush trigger (MG02DraugrAmbushTrigger)
    0x00094E23: "mg02_ambush_trig",
    # Revealing the Unseen: Mzulft Oculory crystal trigger (MG06CrystalTriggerREF)
    0x00102ED2: "mg06_crystal_trig",
    # First Lessons: Hall of the Elements classroom trigger (MG01ClassroomTriggerRef)
    0x00032D81: "mg01_classroom_trig",
    # First Lessons: Bridge Flame Atronach casting test trigger (MG01AtronachTriggerRef)
    0x00037A60: "mg01_atronach_trig",
    # Hitting the Books: Hall of the Elements scene trigger (MG03EndSceneTrigger)
    0x000D67EB: "mg03_end_trig",
    # Good Intentions: The Midden Augur speak trigger (MG04AugurSpeakMarkerRef1)
    0x0004FA19: "mg04_augur_trig",
    # Containment: Winterhold town trigger (MG05WinterholdTriggerRef)
    0x00050785: "mg05_winterhold_trig",
    # Revealing the Unseen: Mzulft Synod vision trigger (MG06VisionTriggerRef)
    0x00034CCA: "mg06_vision_trig",
    # The Eye of Magnus: Eye of Magnus collision sphere (MG08EyeCollisionSphere)
    0x00105CBD: "mg08_eye_trig",
}

# The quest's own people the export would leave out (disabled till their quest): their marks, the
# engine places them - Containment's anomalies in Winterhold, the Caller, the Augur
FOE_IDS = {0x0001BF71: "mg05_anomaly1", 0x0001BF73: "mg05_anomaly2", 0x0001BF72: "mg05_anomaly3",
           0x000799C2: "mg05_anomaly4", 0x000799C1: "mg05_anomaly5", 0x000799BE: "mg05_anomaly6",
           0x000799C0: "mg05_anomaly7", 0x000799BF: "mg05_anomaly8", 0x000799C3: "mg05_anomaly9",
           0x000799C4: "mg05_anomaly10", 0x0004D247: "mg03_caller", 0x00108D7C: "mg04_augur"}
# Kinds the export makes though nothing places them (the engine places them): by their NPC
QUEST_KINDS = ("EncMagicAnomaly", "Ancano")
QUEST_REFS = {0x00086CF4}  # (Morokei: found by his reference)
ID_WORLDS = ("LabyrinthianWorld04",)  # (Morokei's: Labyrinthian's lands of its own)
# Fellglow Keep's doors to the Caller's floor (enabled by Hitting the Books): kept enabled
ENABLED_ANYWAY = {0x000C9E21, 0x000C9E20, 0x00073B87, 0x000F5360}


def extract_triggers(lo):
    """Extracts exact trigger volume records for the College of Winterhold questline with
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
    """Extracts exact reference marks for the College of Winterhold questline."""
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
