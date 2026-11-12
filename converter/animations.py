"""Skyrim's character animations, for the engine's skinned characters.

Each clip (hkx.py decodes Havok's spline-compressed files) is posed on Skyrim's full
skeleton every frame, then reduced to the bones the engine animates (npcs.BONES), each
relative to its nearest animated ancestor (its local transform). The engine blends clips
joint by joint in that local space (so limbs keep their length), rebuilds the bones'
world transforms down the hierarchy and skins with

    D = BoneWorld(frame) * BoneWorld(bind)^-1

where the bind pose is the skeleton the meshes were built on (its local transforms are
stored here too). Locomotion cycles carry the speed they cover (measured from the planted
foot: Skyrim keeps root motion elsewhere), so the engine can play them at the rate the
character actually moves.

  PACK_DIR/anims/male.skanim, female.skanim, first.skanim (the first-person arms)
  PACK_DIR/anims/killmoves.txt: Skyrim's paired killmoves - per line "<name> <weapon> <ahead> <side>
            <turn> <duration>": the killer's clip km_<name>_k and the victim's km_<name>_v (both
            sets), the weapon they're for (1hm, 2hm, 2hw, h2h), where the victim stands to the
            killer (metres ahead and to the right) and which way it faces (radians from the
            killer's heading: pi face to face, 0 its back to you)
  PACK_DIR/anims/magic_nodes.txt: per skeleton ("male", "female", "first") the bind-pose
            points spells leave the left and right hands from (x y z x y z, metres)
  PACK_DIR/anims/root_motion.txt: for clips that move the whole character (getting into
            and out of bed), Skyrim's root motion (its behaviour data, animationdata\
            boundanims): per line "<set> <clip> <keys>" then keys "t x z yaw" (seconds;
            metres in model space, where the character faces -Z at the clip's start; yaw
            radians, turning as engine headings do)

.skanim layout (little-endian), version 3:
  char[4] "SKAN", u16 version, u16 bone count, u32 clip count
  per bone: i32 parent (-1: root), bind-pose local transform (f32 rotation x, y, z, w,
            f32 translation x, y, z)
  per clip: char[16] name, f32 duration (s), u32 frame count, f32 velocity x, z (m/s, model
            space: forward is -Z), u64 moved (bit per bone: its translation is animated),
            then per frame: per bone i16 rotation x, y, z, w (/ 32767), then f32 translation
            x, y, z of each moved bone (the others keep their bind-pose translation)
  (local transforms, engine space, metres)
"""
import math
import os
import struct

import numpy as np

import hkx
from bsa import BSA
from extract_textures import SKYRIM_DATA
from npcs import BONES, SKELETONS
from skin import Skeleton

SKAN_VERSION = 3
MOVED_M = 0.002  # a bone's translation counts as animated past this (metres)
UNITS_PER_METER = 70.0
ANIM_DIR = "meshes\\actors\\character\\animations\\"
HKX_SKELETONS = {False: "meshes\\actors\\character\\character assets\\skeleton.hkx",
                 True: "meshes\\actors\\character\\character assets female\\skeleton_female.hkx"}
# name -> file (the sex's folder first, then the shared one)
CLIPS = {
    "idle": "mt_idle", "walk": "mt_walkforward", "walk_back": "mt_walkbackward",
    "walk_left": "mt_walkleft", "walk_right": "mt_walkright", "run": "mt_runforward",
    "run_back": "mt_runbackward", "run_left": "mt_runleft", "run_right": "mt_runright",
    "sprint": "mt_sprintforward", "jump_start": "mt_jump", "jump_fall": "mt_jumpfall", "jump_land": "mt_jumpland",
    "swim_idle": "swimidle", "swim": "swimforward",
    "torch_idle": "tor_idle", "torch_move": "tor_mtpose",  # (the engine takes the left arm)
    "sneak_idle": "sneakmtidle", "sneak_walk": "sneakwalk_forward", "sneak_run": "sneakrun_forward",
    # dying (people's enemies: Skyrim's own ends lying down - its others hand over to the ragdoll
    # half way, standing)
    "death": "spinningdeathanim",
    # brought down but not killed (Skyrim's essential and protected people, your follower): on
    # their knees till they get up
    "bleedout": "bleedout_idle",
    # asleep (NPCs; placed at the bed's furniture marker): in a bed got into from the left,
    # the right, or a bedroll; getting in and out (the clips walk the body from beside the
    # bed into it and back)
    "bed_left": "bed_leftsleep", "bed_right": "bed_rightsleep", "bedroll": "bedroll_frontsleep",
    "bed_left_in": "bed_leftenter", "bed_right_in": "bed_rightenter", "bedroll_in": "bedroll_frontenter",
    "bed_left_out": "bed_leftexit", "bed_right_out": "bed_rightexit", "bedroll_out": "bedroll_frontexit",
}
# Spells (both views; the engine takes the casting arms): a spell readied in the right hand,
# the left or both, aimed concentration (Flames) and concentration on yourself (Healing)
MAGIC_CLIPS = {
    "mrh_idle": "mrh_idle", "mrh_aim": "mrh_aimedconcentration", "mrh_self": "mrh_selfconcentration",
    "mlh_idle": "mlh_idle", "mlh_aim": "mlh_aimedconcentration", "mlh_self": "mlh_selfconcentration",
    "dual_idle": "dualmagic_idle", "dual_aim": "dmagaimconloop", "dual_self": "dmagselfconloop",
}
CLIPS.update(MAGIC_CLIPS)
# A drawn weapon (both views; the engine takes the arms): one-handed, greatsword, battleaxe /
# warhammer, bow
WEAPON_CLIPS = {"w1h_idle": "1hm_idle", "w2hm_idle": "2hm_idle", "w2hw_idle": "2hw_idle"}
CLIPS.update(WEAPON_CLIPS)
CLIPS["bow_idle"] = "bow_idleheld"
# Moving with a weapon or spell readied (Skyrim's combat locomotion: the body faces where you
# look and steps sideways and back): one-handed, two-handed (both kinds walk alike), bow,
# magic. Ours "<set>_walk", "<set>_run_left"... -> "<set>_walkforward", "<set>_runleft"...
COMBAT_SETS = ("1hm", "2hm", "bow", "mag")
COMBAT_MOVES = {"walk": "walkforward", "walk_back": "walkbackward", "walk_left": "walkleft", "walk_right": "walkright",
                "run": "runforward", "run_back": "runbackward", "run_left": "runleft", "run_right": "runright"}
COMBAT_CLIPS = {f"{s}_{ours}": f"{s}_{theirs}" for s in COMBAT_SETS for ours, theirs in COMBAT_MOVES.items()}
CLIPS.update(COMBAT_CLIPS)
# Sitting (the player in a chair or on a stool; the clips walk the body from beside it onto it
# and back): entering from the front, left or right, sitting, getting up
SEAT_CLIPS = {"chair_front_in": "chair_frontentervar1", "chair_left_in": "chair_leftenter",
              "chair_right_in": "chair_rightenter", "chair_front_out": "chair_frontexit",
              "chair_left_out": "chair_leftexit", "chair_right_out": "chair_rightexit",
              "chair_idle": "chair_idlebasevar1", "stool_in": "stoolbackenter", "stool_out": "stoolbackexit",
              "stool_idle": "aobarstoolidle"}
CLIPS.update(SEAT_CLIPS)
# Caught in a spider's web (Arvel the Swift, Bleak Falls Barrow): hanging, struggling, cut down
WEB_CLIPS = {"web_idle": "webidlebase", "web_thrash": "webidlethrashshort", "web_exit": "webidleexit"}
CLIPS.update(WEB_CLIPS)
# Skyrim's opening (engine/src/intro.c): the prisoners in the carts (four seats, swaying with
# the ride; getting down), its driver, hands bound standing and walking, and the block - the
# prisoner kneeling to it and the headsman's swing, and both turning to Alduin
INTRO_CLIPS = {"cart_a": "cartprisonerasway", "cart_b": "cartprisonerbsway", "cart_c": "cartprisonercsway",
               "cart_d": "cartprisonerdsway", "cart_a_out": "cartprisoneraexit", "cart_b_out": "cartprisonerbexit",
               "cart_c_out": "cartprisonercexit", "cart_d_out": "cartprisonerdexit", "cart_driver": "cartdriveridlesway",
               "bound_idle": "offsetboundstanding", "block_in": "aoexecutioneeenter",
               "ledger_idle": "idlewriteledgeridle", "ledger_write": "idlewriteledgerwrite",  # (Hadvar's list)
               "block_idle": "aoexecutioneeidleonblockplayer", "block_alduin": "aoexecutioneealduinreaction",
               "headsman_idle": "aoexecutioneridle", "headsman_chop": "aoexecutionerchopplayerraiseaxe",
               "headsman_alduin": "aoexecutioneralduinreaction",
               # (the captain behind the prisoner at the block: stepping in and kicking him down to it)
               "guard_enter": "aoexecutionguardenter", "guard_chop": "aoexecutionguardchop",
               # (Alduin landing on the tower, his fire on the yard: thrown back, cowering)
               "stagger": "1hm_staggerbacklarge", "cower_in": "idlecowerenter", "cower": "idlecoweridle",
               # (down on one knee: Ralof searching the captain's body for the keep's key - Skyrim's IdleKneel)
               "kneel_in": "idlekneelenter", "kneel": "idlekneelidle", "kneel_out": "idlekneelexit"}
CLIPS.update(INTRO_CLIPS)
# Riding (Skyrim's horse_rider clips: the rider in the saddle, the body's root at the horse's -
# standing, walking, trotting, at a run and a gallop)
RIDER_CLIPS = {"horse_idle": "horse_rider\\idle", "horse_walk": "horse_rider\\walkforward",
               "horse_trot": "horse_rider\\trotforward", "horse_run": "horse_rider\\runforward",
               "horse_sprint": "horse_rider\\sprintforward",
               # (mounted combat, Skyrim 1.5's: a swing to either side, the weapon held ready, a bow drawn)
               "horse_atk_l": "horse_rider\\mc_1hm_chargerelease_l", "horse_atk_r": "horse_rider\\mc_1hm_chargerelease_r",
               "horse_w1h_idle": "horse_rider\\mc_1hm_idle", "horse_bow_draw": "horse_rider\\mc_bow_drawlight",
               "horse_bow_idle": "horse_rider\\mc_bow_idledrawn", "horse_bow_rel": "horse_rider\\mc_bow_release"}
CLIPS.update(RIDER_CLIPS)
# Fighting (both views; the engine plays them over the body's upper half): one-handed,
# greatsword, battleaxe / warhammer and unarmed swings (right, left: they alternate) and power
# attacks; blocking with a weapon or a shield; a light stagger when hit; a bow drawn and loosed
ATTACK_CLIPS = {"w1h_atk_r": "1hm_attackright", "w1h_atk_l": "1hm_attackleft", "w1h_power": "1hm_attackpower",
                "w2hm_atk_r": "2hm_attackright", "w2hm_atk_l": "2hm_attackleft", "w2hm_power": "2hm_attackpower",
                "w2hw_atk_r": "2hw_attackright", "w2hw_atk_l": "2hw_attackleft", "w2hw_power": "2hw_attackpower",
                "h2h_atk_r": "h2h_attackright", "h2h_atk_l": "h2h_attackleft",
                "w1h_block": "1hm_blockidle", "w2hm_block": "2hm_blockidle", "w2hw_block": "2hw_blockidle",
                "shd_block": "shd_blockidle", "w1h_hit": "1hm_staggerbacksmallest",
                "bow_draw": "bow_drawlight", "bow_release": "bow_release",
                # (sneaking with a weapon ready: Skyrim's crouched stances, so the standing ones
                # aren't laid over a crouch)
                "sneak_w1h_idle": "sneak1hm_idle", "sneak_h2h_idle": "sneakh2h_idle",
                "sneak_bow_idle": "sneakbow_drawlight", "sneak_bow_draw": "sneakbow_drawlight",
                "sneak_bow_release": "sneakbow_release",
                # dual wielding: both hands' stance, both swinging, both in a power attack
                "dw_idle": "dw1hm1hmidle", "dw_atk": "dw1hm1hm_attackright", "dw_power": "dw1hm1hm_powerattack",
                # bashing (block held, attack: Skyrim's), and its power bash; a shield's charge
                "shd_bash": "shd_blockbash", "shd_bash_power": "shd_blockbashpower", "shd_charge": "shd_blockbashsprint",
                "w1h_bash": "1hm_blockbash", "w1h_bash_power": "1hm_blockbashpower",
                "w2hm_bash": "2hm_blockbash", "w2hm_bash_power": "2hm_blockbashpower",
                "w2hw_bash": "2hw_blockbash", "w2hw_bash_power": "2hw_blockbashpower",
                "bow_bash": "bow_blockbash", "bow_bash_power": "bow_blockbashpower",
                "tor_bash": "tor_blockbash", "tor_bash_power": "tor_blockbashpower"}
# Stances Skyrim has no clip of their own for, held still at a clip's first frame: the bow held
# crouched, not drawn (its "sneakbow_idledrawn" is the string held pulled; the draw starts here)
STILL_CLIPS = ("sneak_bow_idle",)
# The left hand's swings: Skyrim mirrors the right hand's (ours "<clip>" from these, mirrored)
MIRRORED_CLIPS = {"w1h_atk_lh": "1hm_attackright", "w1h_atk_lh2": "1hm_attackleft", "w1h_power_lh": "1hm_attackpower"}
CLIPS.update(ATTACK_CLIPS)
# Clips with root motion: ours -> the clip generator name in Skyrim's behaviour data
ROOT_MOTION_CLIPS = {"chair_front_in": "Chair_FrontEnterVar1", "chair_left_in": "Chair_LeftEnter",
                     "chair_right_in": "Chair_RightEnter", "chair_front_out": "Chair_FrontExit",
                     "chair_left_out": "Chair_LeftExit", "chair_right_out": "Chair_RightExit",
                     "stool_in": "StoolBackEnter", "stool_out": "StoolBackExit",
                     "bed_left_in": "Bed_LeftEnter", "bed_left_out": "Bed_LeftExit",
                     "bed_right_in": "Bed_RightEnter", "bed_right_out": "Bed_RightExit",
                     "bedroll_in": "BedRoll_FrontEnter", "bedroll_out": "BedRoll_FrontExit"}
# Where spells leave the hands: Skyrim's magic nodes (bind pose, engine space, metres)
# Killmoves (Skyrim's paired animations, KillMove on, KillMoveRandom 50): each file holds both
# skeletons - tracks from 2 the victim's, from 102 the killer's - and track 1 where the victim
# stands to the killer. A Vita-sized handful: the one-handed, two-handed and bare-handed ones that
# keep the head on (Skyrim's decapitations want a severed head)
KILLMOVES = {"1hm_a": ("1hm", "paired_1hmkillmove"), "1hm_b": ("1hm", "paired_1hmkillmoveb"),
             "1hm_c": ("1hm", "paired_1hmkillmovec"), "1hm_d": ("1hm", "paired_1hmkillmoved"),
             "1hm_sb": ("1hm", "paired_1hmkillmoveshortb"), "1hm_back": ("back", "paired_1hmkillmovebackstab"),
             "2hw_a": ("2hw", "paired_2hwkillmovea"), "2hw_b": ("2hw", "paired_2hwkillmoveb"),
             "2hm_stab": ("2hm", "paired_2hmkillmovestaba"), "h2h_slam": ("h2h", "paired_h2hkillmoveslama")}
KM_PLACE, KM_VICTIM, KM_KILLER = 1, 2, 102
KILLMOVE_CLIPS = {f"km_{n}_{w}": f"{stem}#{KM_KILLER if w == 'k' else KM_VICTIM}"
                  for n, (_kind, stem) in KILLMOVES.items() for w in ("k", "v")}

MAGIC_NODES = ("NPC L MagicNode [LMag]", "NPC R MagicNode [RMag]")
# First person: the arms seen in front of the camera, on Skyrim's first-person skeleton
FIRST_PERSON_HKX = "meshes\\actors\\character\\_1stperson\\characterassets\\skeletonfirst.hkx"
FIRST_PERSON_NIF = "meshes\\actors\\character\\_1stperson\\skeleton.nif"
FIRST_PERSON_DIR = "meshes\\actors\\character\\_1stperson\\animations\\"
FIRST_PERSON_CLIPS = {"bound_idle": "offsetboundstanding",  # (the opening: your wrists tied before you)
                      "torch_idle": "tor_idle", "torch_walk": "tor_1stp_walk", "torch_run": "tor_1stp_run",
                      **MAGIC_CLIPS, **WEAPON_CLIPS, "bow_idle": "bow_idleheld",
                      **{k: v for k, v in COMBAT_CLIPS.items() if not k.startswith("mag")},
                      **{k: v for k, v in ATTACK_CLIPS.items() if not k.startswith("h2h")},
                      # (moving forward with a bow: Skyrim's first-person clips hold it upright before
                      # the camera; its strafing set's turn it side-on)
                      "bow_walk": "bow_1stp_walk", "bow_run": "bow_1stp_run"}
# Clips without a planted foot to measure their speed from (the engine paces them itself)
UNPACED_CLIPS = ("idle", "sneak_idle", "swim_idle", "swim", "torch_idle", "torch_move", "torch_walk", "torch_run",
                 "bed_left", "bed_right", "bedroll", "bed_left_in", "bed_right_in", "bedroll_in",
                 "bed_left_out", "bed_right_out", "bedroll_out",
                 *MAGIC_CLIPS, *WEAPON_CLIPS, "bow_idle", *SEAT_CLIPS, *ATTACK_CLIPS, "w1h_atk_lh", "w1h_atk_lh2",
                 "w1h_power_lh", *RIDER_CLIPS)
# Clips whose hop is in the bones (feet leave the ground): the engine's physics does the
# hop, so the lift is taken out and the body stays on the character's position
AIRBORNE_CLIPS = ("jump_start",)
FEET = ("NPC L Foot [Lft ]", "NPC R Foot [Rft ]")
C = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], np.float64)  # Skyrim -> engine axes
UNPACED_CLIPS += tuple(KILLMOVE_CLIPS)  # (in place: where each stands is the killmove's own)


def _quat(m):
    """Rotation matrix -> quaternion (x, y, z, w)."""
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def _airborne_lift(hk, poses):
    """Per frame, how far (metres) the lower foot is above its lowest point in the clip."""
    feet = [hk.bones.index(b) for b in FEET]
    low = [min(T[f][2] for f in feet) / UNITS_PER_METER for _, T in poses]
    base = min(low)
    return [max(0.0, z - base) for z in low]


def export(pack, mesh_archives, find):
    """Writes the male and female clip sets, and the first-person one."""
    from nif import NIF
    anims = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Animations.bsa"))
    out_dir = os.path.join(pack, "anims")
    os.makedirs(out_dir, exist_ok=True)
    for female in (False, True):
        sub = "female\\" if female else "male\\"
        clips = {name: [ANIM_DIR + sub + stem + ".hkx", ANIM_DIR + stem + ".hkx"]
                 for name, stem in {**CLIPS, **MIRRORED_CLIPS}.items()}
        clips.update({name: [ANIM_DIR + stem.split("#")[0] + ".hkx#" + stem.split("#")[1]]
                      for name, stem in KILLMOVE_CLIPS.items()})
        _write_set(os.path.join(out_dir, ("female" if female else "male") + ".skanim"), anims,
                   anims.read(HKX_SKELETONS[female]), NIF(find(mesh_archives, SKELETONS[female])), clips,
                   mirrored=tuple(MIRRORED_CLIPS))
    clips = {name: [FIRST_PERSON_DIR + stem + ".hkx"] for name, stem in {**FIRST_PERSON_CLIPS, **MIRRORED_CLIPS}.items()}
    _write_set(os.path.join(out_dir, "first.skanim"), anims, anims.read(FIRST_PERSON_HKX),
               NIF(find(mesh_archives, FIRST_PERSON_NIF)), clips, mirrored=tuple(MIRRORED_CLIPS))
    with open(os.path.join(out_dir, "root_motion.txt"), "w") as f:
        for female in (False, True):
            name = "female" if female else "male"
            motion = _root_motion(anims, "default" + name)
            for clip, generator in ROOT_MOTION_CLIPS.items():
                keys = motion.get(generator.lower())
                if keys:
                    f.write(f"{name} {clip} {len(keys)} " + " ".join(f"{t:.4f} {x:.4f} {z:.4f} {yaw:.4f}" for t, x, z, yaw in keys) + "\n")
    with open(os.path.join(out_dir, "killmoves.txt"), "w") as f:
        for name, (kind, stem) in KILLMOVES.items():
            path = ANIM_DIR + stem + ".hkx"
            if path not in anims.entries:
                continue
            anim = hkx.Animation(anims.read(path))
            pos, rot, _s = anim.frames[0][KM_PLACE]
            turn = 2.0 * math.atan2(rot[2], rot[3])  # (about Skyrim's up: the victim's facing to the killer's)
            f.write(f"{name} {kind} {pos[1] / UNITS_PER_METER:.3f} {pos[0] / UNITS_PER_METER:.3f} {turn:.4f} {anim.duration:.3f}\n")
    with open(os.path.join(out_dir, "magic_nodes.txt"), "w") as f:
        for name, path in (("male", SKELETONS[False]), ("female", SKELETONS[True]), ("first", FIRST_PERSON_NIF)):
            rest = Skeleton(NIF(find(mesh_archives, path))).world()
            if all(n in rest for n in MAGIC_NODES):
                pts = [C @ rest[n][:3, 3] / UNITS_PER_METER for n in MAGIC_NODES]
                f.write(name + " " + " ".join(f"{v:.4f}" for p in pts for v in p) + "\n")


def _root_motion(anims, project):
    """{clip generator name (lowercase): [(t, x, z, yaw)]} from a behaviour project's
    animation data: its clip list (name -> id) and the root motion per id (translation
    keys "t x y z" in Skyrim units, x right and y forward; rotation keys "t qx qy qz qw")."""
    clips = anims.read(f"meshes\\animationdata\\{project}.txt").decode("latin1").splitlines()
    ids = {}
    for i in range(len(clips) - 1):
        if clips[i + 1].strip().isdigit() and clips[i].strip() and not clips[i].strip().isdigit():
            ids.setdefault(int(clips[i + 1]), clips[i].strip().lower())
    lines = anims.read(f"meshes\\animationdata\\boundanims\\anims_{project}.txt").decode("latin1").splitlines()
    out, i = {}, 0
    while i < len(lines):
        if not lines[i].strip():
            i += 1
            continue
        cid, nt = int(lines[i]), int(lines[i + 2])
        trans = [tuple(map(float, x.split())) for x in lines[i + 3:i + 3 + nt]]
        j = i + 3 + nt
        nr = int(lines[j])
        rots = [tuple(map(float, x.split())) for x in lines[j + 1:j + 1 + nr]]
        i = j + 1 + nr
        name = ids.get(cid)
        if name is None:
            continue
        times = sorted({t[0] for t in trans} | {r[0] for r in rots})

        def at(keys, t, n):
            prev = (0.0,) + ((0.0, 0.0, 0.0, 1.0) if n == 4 else (0.0,) * n)  # (at the start: no motion)
            for k in keys:
                if k[0] >= t:
                    u = (t - prev[0]) / (k[0] - prev[0]) if k[0] > prev[0] else 1.0
                    return [a + (b - a) * u for a, b in zip(prev[1:], k[1:])]
                prev = k
            return list(prev[1:])
        keys = []
        for t in times:
            x, y, _z = at(trans, t, 3)
            qx, qy, qz, qw = at(rots, t, 4)
            keys.append((t, x / UNITS_PER_METER, -y / UNITS_PER_METER, 2.0 * np.arctan2(qz, qw)))
        out[name] = keys
    return out


def _mirror_pose(hk, R, T):
    """A pose mirrored left for right (Skyrim's +X is the body's right): each bone takes its
    twin's transform reflected across the body's middle."""
    import gear
    S = np.diag([-1.0, 1.0, 1.0])
    twin = [hk.bones.index(gear._mirror_name(b)) if gear._mirror_name(b) in hk.bones else i for i, b in enumerate(hk.bones)]
    R2 = np.array([S @ R[twin[i]] @ S for i in range(len(hk.bones))])
    T2 = np.array([S @ T[twin[i]] for i in range(len(hk.bones))])
    return R2, T2


def _write_set(path_out, anims, hkx_skeleton, rest_nif, clip_paths, bones=None, speeds=None, unpaced=None, mirrored=()):
    """One .skanim: clip_paths {name: [candidate .hkx paths, first found wins]}. bones: the
    bones kept (a creature's; default the characters' BONES); speeds {name: (x, y) game units
    a second}: a clip's travel given (creatures: from Skyrim's root motion) instead of measured
    from the feet; unpaced: clips not to measure (default UNPACED_CLIPS); mirrored: clips to mirror
    left for right."""
    hk = hkx.Skeleton(hkx_skeleton)
    rest = Skeleton(rest_nif).world()
    kept = [b for b in (bones if bones is not None else BONES) if b in rest and b in hk.bones]
    unpaced = UNPACED_CLIPS if unpaced is None else unpaced
    index = [hk.bones.index(b) for b in kept]
    # Each kept bone's nearest kept ancestor
    parents = []
    for b in kept:
        p = hk.parents[hk.bones.index(b)]
        while p >= 0 and hk.bones[p] not in kept:
            p = hk.parents[p]
        parents.append(kept.index(hk.bones[p]) if p >= 0 else -1)

    def local(world_of, k):
        w = world_of(k)
        return np.linalg.inv(world_of(parents[k])) @ w if parents[k] >= 0 else w

    def pack(m):
        return np.concatenate([_quat(C @ m[:3, :3] @ C.T), C @ m[:3, 3] / UNITS_PER_METER])

    rest_local = [pack(local(lambda j: rest[kept[j]], k)) for k in range(len(kept))]
    rest_t = [r[4:] for r in rest_local]
    header = b"".join(struct.pack("<i", parents[k]) + rest_local[k].astype(np.float32).tobytes() for k in range(len(kept)))
    clips = []
    for name, candidates in clip_paths.items():
        path = next((c for c in candidates if c.split("#")[0] in anims.entries), None)
        if path is None:
            print(f"animations: no {candidates[0]}")
            continue
        anim = hkx.Animation(anims.read(path.split("#")[0]))
        if "#" in path:  # (one skeleton of a paired file: its tracks from there, in the skeleton's order)
            first = int(path.split("#")[1])
            anim.frames = [f[first:first + len(hk.bones)] for f in anim.frames]
            anim.track_to_bone = None
        v = speeds[name] if speeds and name in speeds else \
            hkx.ground_speed(hk, anim) if name not in unpaced else (0.0, 0.0)
        frames = []
        poses = [hkx.pose_world(hk, frame, anim.track_to_bone) for frame in anim.frames]
        duration = anim.duration
        if name in STILL_CLIPS:
            poses, duration = poses[:1] * 2, 1.0
        if name in mirrored:  # (the left hand's swing: the right's, mirrored)
            poses = [_mirror_pose(hk, np.asarray(R), np.asarray(T)) for R, T in poses]
        lifts = _airborne_lift(hk, poses) if name in AIRBORNE_CLIPS else [0.0] * len(poses)
        for (R, T), lift in zip(poses, lifts):

            def world_of(j):
                m = np.eye(4)
                m[:3, :3], m[:3, 3] = R[index[j]], T[index[j]]
                return m
            for k in range(len(kept)):
                m = pack(local(world_of, k))
                if parents[k] < 0:
                    m[5] -= lift  # engine y (up)
                frames.append(m)
        data = np.array(frames, np.float64).reshape(len(poses), len(kept), 7)
        # Rotations as int16 (each frame, every bone); translations only for the bones whose
        # translation moves in this clip (the root; the others keep their bind-pose length)
        moved = [k for k in range(len(kept)) if np.abs(data[:, k, 4:] - rest_t[k]).max() > MOVED_M]
        mask = sum(1 << k for k in moved)
        q = np.clip(np.round(data[:, :, :4] * 32767.0), -32767, 32767).astype(np.int16)
        body = b"".join(q[f].tobytes() + data[f, moved, 4:].astype(np.float32).tobytes() for f in range(len(poses)))
        clips.append(struct.pack("<16sfIffQ", name.encode(), duration, len(poses),
                                 float(v[0]) / UNITS_PER_METER, -float(v[1]) / UNITS_PER_METER, mask) + body)
    body = struct.pack("<4sHHI", b"SKAN", SKAN_VERSION, len(kept), len(clips)) + header + b"".join(clips)
    with open(path_out, "wb") as f:
        f.write(body)
    print(f"animations: {os.path.basename(path_out)} {len(clips)} clips, {len(kept)} bones, {len(body) / 1e6:.1f} MB")
