"""Skyrim's services and set conversations the engine plays by hand: the trainers (who teaches
what, to what level - their class, CLAS - and what each says when asked), and topics with their
player prompts and voiced answers - the Whiterun gate guard's stop (Persuade, Intimidate, Bribe),
the brawls, the favours.

  PACK_DIR/topics.txt, one a line:
    T|trainer (editor id, lower case)|skill (actor value order)|the most they teach|prompt|voice path or -|text
    N|trainer|voice path or -|text           (nothing more to teach you)
    P|key|the player's words
    L|key|part (0 a line's first)|speaker (editor id, lower case, or -)|voice path or -|text
"""
import os
import struct
from collections import Counter

from dialogue import line_is

F_GET_IS_ID = 72
# key: (topic editor id, whose lines (an NPC's editor id; None: the topic's own speaker, by its
# GetIsID - else a guard's voice), only lines starting with these words ("" any))
TOPICS = (
    ("gate_halt", "DialogueWhiterunGuardGateStopBranchTopic", None, ""),
    ("gate_note", "DialogueWhiterunGuardGateStopNote", None, "#000D1980"),
    ("gate_persuade", "DialogueWhiterunGuardGateStopPersuade", None, ""),
    ("gate_intimidate", "DialogueWhiterunGuardGateStopIntimidate", None, ""),
    ("gate_bribe", "DialogueWhiterunGuardGateStopBribe", None, ""),
    ("gate_later", "DialogueWhiterunGuardGateStopNevermind", None, ""),
    ("gate_hello", "DialogueWhiterunGuardGateStopHellos", None, ""),
    # Uthgerd's brawl (Favor017, then DGIntimidateQuest's yield and pay-up)
    ("brawl_give", "Favor017QuestGiveTopic", "Uthgerd", ""),
    ("brawl_accept", "Favor017QuestAccept", "Uthgerd", "#00055A0B"),
    ("brawl_poor", "Favor017QuestAccept", "Uthgerd", "#00055A0C"),
    ("brawl_reject", "Favor017QuestReject", "Uthgerd", ""),
    ("brawl_won", "DGIntimidateVictoryTopic", "Uthgerd", ""),
    ("brawl_paid", "DGIntimidateVictoryYes", "Uthgerd", ""),
    # A Lovely Letter (FreeformRiverwood01): Sven, Faendal and Camilla
    ("letter_sven_intro", "FreeformRiverwood01SvenIntroBranchTopic", "Sven", ""),
    ("letter_sven_give", "FreeformRiverwood01SvenTopic02", "Sven", ""),
    ("letter_sven_tell", "FreeformRiverwood01SvenTellBranchTopic", "Sven", ""),
    ("letter_sven_reward", "FreeformRiverwood01SvenRewardBranchTopic", "Sven", ""),
    ("letter_faendal_intro", "FreeformRiverwood01FaendalIntroBranchTopic", "Faendal", ""),
    ("letter_faendal_give", "FreeformRiverwood01FaendalTopic02", "Faendal", ""),
    ("letter_faendal_tell", "FreeformRiverwood01FaendalTellBranchTopic", "Faendal", ""),
    ("letter_faendal_reward", "FreeformRiverwood01FaendalRewardBranchTopic", "Faendal", ""),
    ("letter_camilla_lie_sven", "FreeformRiverwood01CamillaSvensLetterBranchTopic", "CamillaValerius", ""),
    ("letter_camilla_truth_sven", "FreeformRiverwood01CamillaSvensLetter02BranchTopic", "CamillaValerius", ""),
    ("letter_camilla_lie_faendal", "FreeformRiverwood01CamillaFaendalsLetterBranchTopic", "CamillaValerius", ""),
    ("letter_camilla_truth_faendal", "FreeformRiverwood01CamillaFaendalsLetter02BranchTopic", "CamillaValerius", ""),
    # The Golden Claw (MS13): Lucan's break-in, his claw back
    ("claw_ask", "MS13LucanQuestGiveBranchTopic", "LucanValerius", ""),
    ("claw_volunteer", "MS13LucanVolunteerTopic", "LucanValerius", "#0008AFA1+0008AFA2"),
    ("claw_return", "MS13LucanHaveGoldStatueBranchTopic", "LucanValerius", "#0005D473"),
    # Before the Storm in Riverwood, talked over afterwards (the introductions missed: an older save)
    ("bts_alvor", "MQ102RiverwoodHelpTopic", "Alvor", "#00041F16"),
    ("bts_alvor_friend", "MQ102AlvorHelpA1", "Alvor", ""),
    ("bts_gerdur", "MQ102BRiverwoodHelpTopic", "Gerdur", "#00052CB0"),
    ("bts_gerdur_friend", "MQ102GerdurHelpA1", "Gerdur", ""),
    # Diplomatic Immunity: the Solar's guard in the Embassy courtyard (MQ201ThalmorEmbassy's
    # SolarGuard) - his challenge, your persuasion (Skyrim's: Speech against SpeechAverage) or
    # walking away; the guards who can't be fooled
    ("solar_halt", "MQ201SolarGuardBlockingTopic", "MQ201ThalmorMageSolarGuard", "#0006B1A7"),
    ("solar_trick", "MQ201ThalmorEmbassyTrickeryBranchTopic", "MQ201ThalmorMageSolarGuard", "#0006F176"),
    ("solar_trick_fail", "MQ201ThalmorEmbassyTrickeryBranchTopic", "MQ201ThalmorMageSolarGuard", "#0002525C"),
    ("solar_walk", "MQ201SolarGuardWalkawayTopic", "MQ201ThalmorMageSolarGuard", ""),
    ("thalmor_intruder", "MQ201SolarGuardBlockingTopic", "MQ201ThalmorGuardCanBeFooled", "#0006F180|#00015F50|#00015F51"),
    # The stables: a horse for sale (Skulvar Sable-Hilt's)
    ("horse_buy", "StablesBuyHorseTopic", "SkulvarSableHilt", "#00068CC2|#000983BF"),
    ("horse_bought", "StablesAcceptTopic", "SkulvarSableHilt", "#00068CF1|#00068CF4"),
    ("horse_poor", "StablesAcceptTopic", "SkulvarSableHilt", "#00068CF2|#00068CF5"),
    ("horse_sold", "StablesBuyHorseTopic", "SkulvarSableHilt", "#00068CC0+000983ED|#00068CC3"),
)
GUARD_VOICES = ("maleguard", "malenord", "malecommander")


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _speakers(lo, info):
    return {lo.global_id(info.plugin, _u32(v, 12)) for k, v in info.subrecords()
            if k == "CTDA" and len(v) >= 16 and struct.unpack_from("<H", v, 8)[0] == F_GET_IS_ID}


def _voice(lo, talk, npc, depth=0):
    """An NPC's voice type: its own, or (its traits from a template) the template's."""
    if npc is None or depth > 6:
        return None
    acbs, tplt = npc.sub("ACBS"), npc.sub("TPLT")
    uses_traits = acbs and len(acbs) >= 20 and struct.unpack_from("<H", acbs, 18)[0] & 0x1
    if tplt and (uses_traits or not npc.sub("VTCK")):
        t = lo.record(lo.global_id(npc.plugin, _u32(tplt)), types=("NPC_",))
        v = _voice(lo, talk, t, depth + 1) if t is not None else None
        if v:
            return v
    vt = npc.sub("VTCK")
    return talk.voice_types.get(lo.global_id(npc.plugin, _u32(vt))) if vt else None


def _recorded(talk, lo, info):
    """The voice type an INFO's first response was recorded in (for those whose voice is a template's)."""
    if not hasattr(talk, "_any_voice"):
        talk._any_voice = {}
        for (_o, vt, f, nn) in talk.voice_index:
            if nn == 1:
                talk._any_voice.setdefault(f, vt)
    return talk._any_voice.get(lo.global_id(info.plugin, info.form_id) & 0xFFFFFF)


def export(lo, pack, strings, talk, actors, extra=()):
    """talk: the people's dialogue.Dialogue; actors: [(editor id, NPC)] exported; extra: more
    TOPICS entries (the quests' own)."""
    stats = Counter()
    npcs = {lo.global_id(p, n.form_id): n for p in lo.plugins for n in p.records_of_type("NPC_")}
    by_eid = {(n.editor_id() or "").lower(): n for n in npcs.values()}
    dials = {}
    for p in lo.plugins:  # (a later plugin's topic holds only the lines it changed: merged, its win)
        infos = p.topic_infos()
        for d in p.records_of_type("DIAL"):
            _p0, d0, merged = dials.get(d.editor_id(), (p, d, {}))
            merged = dict(merged)
            for info in infos.get(d.form_id, []):
                merged[lo.global_id(p, info.form_id)] = info
            dials[d.editor_id()] = (p, d if d.sub("FULL") or d0 is None else d0, merged)
    dials = {k: (p, d, list(m.values())) for k, (p, d, m) in dials.items()}
    lines = []
    # Trainers: their class says what they teach; their line from OffersTrainingTopic (theirs by GetIsID)
    classes = {lo.global_id(p, c.form_id): c for p in lo.plugins for c in p.records_of_type("CLAS")}
    offers = dials.get("OffersTrainingTopic", (None, None, []))[2]
    shared = dials.get("DialogueTrainersSharedInfos", (None, None, []))[2]
    for eid, npc in actors:
        c = npc.sub("CNAM")
        cl = classes.get(lo.global_id(npc.plugin, _u32(c))) if c else None
        d = cl.sub("DATA") if cl is not None else None
        if cl is None or not (cl.editor_id() or "").startswith("Trainer") or not d or len(d) < 6:
            continue
        gid, voice = lo.global_id(npc.plugin, npc.form_id), _voice(lo, talk, npc)
        prompt, said = "", None
        for info in offers:  # (theirs: the first with words for you and a voice for them)
            if gid not in _speakers(lo, info):
                continue
            r = info.sub("RNAM")
            prompt = prompt or (strings.get(info.plugin, _u32(r), "strings") if r and len(r) == 4 else "")
            said = said or (talk._responses(info, voice or _recorded(talk, lo, info)))
        path, text = (talk._convert(said[0][0]) or "-", said[0][1]) if said else ("-", "")
        lines.append(f"T|{eid.lower()}|{d[4]}|{d[5]}|{prompt or '-'}|{path}|{text or '-'}")
        stats["trainers"] += 1
        for info in shared:
            r = talk._responses(info, voice) if voice else None
            if r:
                lines.append(f"N|{eid.lower()}|{talk._convert(r[0][0]) or '-'}|{r[0][1]}")
    # Set conversations: the player's words (the topic's), the answers in the speaker's voice
    for key, topic, who, opening in tuple(TOPICS) + tuple(extra):
        got = dials.get(topic)
        if got is None:
            stats[f"missing {topic}"] += 1
            continue
        p, d, infos = got
        full = d.sub("FULL")
        prompt = strings.get(p, _u32(full), "strings") if full and len(full) == 4 else ""
        want = by_eid.get(who.lower()) if who else None
        own, shared = [], []  # (their own lines - naming them - before anyone's)
        for info in infos:
            ids = _speakers(lo, info)
            if want is not None and ids and lo.global_id(want.plugin, want.form_id) not in ids:
                continue  # (someone else's take of it)
            speaker = want if who else (npcs.get(next(iter(ids))) if ids else None)
            voices = [_voice(lo, talk, speaker)] if speaker is not None else list(GUARD_VOICES)
            voices = [v for v in voices if v] + [_recorded(talk, lo, info)]
            fallback = len(voices) > 1  # (the last: another voice's recording - only if theirs has none at all)
            for vi, voice in enumerate(voices):
                responses = talk._responses(info, voice) if voice else None
                if not responses or not line_is(lo, info, responses[0][1], opening):
                    continue
                name = (speaker.editor_id() or "-").lower() if speaker is not None else "-"
                r = info.sub("RNAM")  # (the player's words for this line, where the topic's own are blank)
                (own if ids else shared).append((name, responses, r, fallback and vi == len(voices) - 1))
                break
        picked = own or shared
        if any(not other for *_x, other in picked):  # (their own voice's lines: not another's)
            picked = [x for x in picked if not x[3]]
        for name, responses, r, _other in picked[:1]:  # (the chosen line's own words for the player, over the topic's)
            if r and len(r) == 4:
                prompt = strings.get(lo.plugins[0], _u32(r), "strings") or prompt
        for name, responses, r, _other in picked:
            for n, (vp, text) in enumerate(responses):
                lines.append(f"L|{key}|{n}|{name}|{talk._convert(vp) or '-'}|{text.replace('|', '/')}")
        n_lines = len(picked)
        if prompt:
            lines.append(f"P|{key}|{prompt}")
        stats["topic lines"] += n_lines
        if not n_lines:
            stats[f"no lines: {key}"] += 1
    # Skyrim's prices the prompts name (<Global=HorseCost> ...)
    for p in lo.plugins:
        for g in p.records_of_type("GLOB"):
            if (g.editor_id() or "") in ("HorseCost", "CarriageCost") and g.sub("FLTV"):
                lines.append(f"G|{g.editor_id().lower()}|{struct.unpack('<f', g.sub('FLTV'))[0]:g}")
    # Everyone's own everyday conversations, with your answers (quests.TreeMaker: D lines)
    from quests import TreeMaker
    lines += TreeMaker(lo, talk).npc_branches(actors, lambda npc: _voice(lo, talk, npc) or "*")
    with open(os.path.join(pack, "topics.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print("topics:", dict(stats))
