"""Skyrim's followers: the exported people who can follow you (PotentialFollowerFaction), the
sellswords among them (PotentialHireling: hired for HirelingGold), and what each says when
asked to follow, wait, trade or part ways - Skyrim's lines in their own voices (their own take
of a topic where they have one, else the shared one).

  PACK_DIR/followers.txt, one a line:
    F|editor id (lower case)|1 a sellsword (else: a friend's favour)|their price
    P|topic|the player's words
    L|editor id|topic|part (0 a line's first)|voice path (under voice/) or -|text
  Topics: recruit (DIAL 000B0EE6: a friend), hire, short (hire with too little
  gold), follow, wait, trade, dismiss.
"""
import os
import struct
from collections import Counter

TOPICS = {"DialogueFavorGenericFollowBranchTopic": "recruit", "HirelingQuestTopic1": "hire",
          "DialogueFollowerFollowTopic": "follow", "DialogueFollowerWaitTopic": "wait",
          "DialogueFollowerTradeTopic": "trade", "DialogueFollowerDismissTopic": "dismiss"}
F_GET_IS_ID = 72


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _speakers(lo, info):
    """The NPCs an INFO's GetIsID conditions name (empty: anyone's)."""
    return {lo.global_id(info.plugin, _u32(v, 12)) for k, v in info.subrecords()
            if k == "CTDA" and len(v) >= 16 and struct.unpack_from("<H", v, 8)[0] == F_GET_IS_ID}


def export(lo, pack, strings, talk, actors):
    """talk: the people's dialogue.Dialogue; actors: [(editor id, NPC)] exported."""
    stats = Counter()
    facts = {}
    for p in lo.plugins:
        for f in p.records_of_type("FACT"):
            if f.editor_id() in ("PotentialFollowerFaction", "PotentialHireling"):
                facts[lo.global_id(p, f.form_id)] = f.editor_id()
    gold = next((struct.unpack("<f", g.sub("FLTV"))[0] for p in lo.plugins for g in p.records_of_type("GLOB")
                 if (g.editor_id() or "").lower() == "hirelinggold" and g.sub("FLTV")), 500.0)
    who = []
    for eid, npc in actors:
        fs = {facts.get(lo.global_id(npc.plugin, _u32(v))) for k, v in npc.subrecords() if k == "SNAM" and len(v) >= 4}
        if "PotentialFollowerFaction" in fs:
            vt = npc.sub("VTCK")
            voice = talk.voice_types.get(lo.global_id(npc.plugin, _u32(vt))) if vt else None
            who.append((eid, npc, lo.global_id(npc.plugin, npc.form_id), "PotentialHireling" in fs, voice))
    lines = [f"F|{eid.lower()}|{int(hired)}|{int(gold) if hired else 0}" for eid, _n, _g, hired, _v in who]
    topic_infos = {}
    for p in lo.plugins:
        topics = p.topic_infos()
        for dial in p.records_of_type("DIAL"):
            topic = TOPICS.get(dial.editor_id())
            if topic is None:
                continue
            full = dial.sub("FULL")
            prompt = strings.get(p, _u32(full), "strings") if full and len(full) == 4 else ""
            if prompt:
                prompt = prompt.replace("<global=hirelinggold>", f"{int(gold)} gold").replace("<Global=HirelingGold>", f"{int(gold)} gold")
                lines.append(f"P|{topic}|{prompt}")
            topic_infos.setdefault(topic, []).extend(topics.get(dial.form_id, []))
    for eid, npc, gid, _hired, voice in who:
        if not voice:
            stats["no voice"] += 1
            continue
        for topic, infos in topic_infos.items():
            own, shared = [], []
            for info in infos:
                ids = _speakers(lo, info)
                if ids and gid not in ids:
                    continue
                responses = talk._responses(info, voice)
                if responses:
                    (own if ids else shared).append(responses)
            for responses in own or shared:
                text = " ".join(t for _p, t in responses)
                t = "short" if topic == "hire" and "short on coin" in text else topic
                if "comrade go" in text or "someone already" in text or "help already" in text:
                    continue  # (one follower at a time: we don't swap them)
                for n, (vp, line) in enumerate(responses):
                    lines.append(f"L|{eid.lower()}|{t}|{n}|{talk._convert(vp) or '-'}|{line.replace('|', '/')}")
                stats[f"{t} lines"] += 1
    with open(os.path.join(pack, "followers.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print("followers:", len(who), [e for e, *_ in who], dict(stats))
