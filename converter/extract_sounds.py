"""Skyrim's sound effects (WAV in "Skyrim - Sounds.bsa") for the engine's events: footsteps by
surface, swimming, drawing and sheathing weapons, spells, lockpicking, items, doors, levers
and traps, the menus. Each event gets its file's variants (Skyrim picks one at random each
time, so does the engine).

Usage: python3 extract_sounds.py <pack dir>   (also run by extract_objects.py)

PACK_DIR/sound/sfx.bank (little-endian): "SKSF", u16 version, u16 event count, u32 sample
rate, u32 block frames; per event: char[24] name, u8 variant count, u8 flags (1: loops), u16
0; per variant: u32 frames, then its IMA ADPCM blocks (mono, as music: a 4-byte header, then
block frames / 2 bytes; engine/src/adpcm.h). Like everything in the pack it's converted from the
user's own game files and must not be shared.
"""
import io
import os
import re
import struct
import sys

import ctypes

import av
import numpy as np

from extract_music import load_adpcm

from bsa import BSA
from extract_textures import SKYRIM_DATA

SKSF_VERSION = 2
BLOCK_FRAMES = 256
RATE = 22050
MAX_VARIANTS = 3        # (footsteps: 4) - enough not to repeat, little enough for the Vita's memory
MAX_SECONDS = 2.5       # one-shot sounds cut to this (with a fade); loops to LOOP_SECONDS
LOOP_SECONDS = 4.0
LONG_SECONDS = 7.0      # ... these kept whole: Battle Cry's roar, the shouts' thunder
LONG_EVENTS = ("power_", "shout_", "dragon_roar", "dragon_soul", "dragon_death", "dragon_crash", "greybeard_", "draugr_")
SILENCE = 300           # leading / trailing samples quieter than this are dropped
LOOP = 1

# event -> (regular expressions over the archive's paths, tried in turn: the first that
# matches gives the variants; flags)
FST = {"dirt": "dirt", "grass": "grass", "stone": "stonesolid", "wood": "wood", "snow": "snow"}
EVENTS = {}
for ours, theirs in FST.items():
    EVENTS[f"fst_{ours}_walk"] = ([rf"fx\\fst\\player\\{theirs}\\walk\\"], 0)
    EVENTS[f"fst_{ours}_run"] = ([rf"fx\\fst\\player\\{theirs}\\run\\armorlight\\", rf"fx\\fst\\player\\{theirs}\\run\\"], 0)
for ours, theirs in FST.items():  # (jumping off and landing: Skyrim's per surface)
    EVENTS[f"fst_{ours}_jump"] = ([rf"fx\\fst\\{theirs}\\jumpup\\", r"fx\\fst\\dirt\\jumpup\\"], 0)
    EVENTS[f"fst_{ours}_land"] = ([rf"fx\\fst\\{theirs}\\land\\", r"fx\\fst\\dirt\\land\\"], 0)
EVENTS.update({
    "fst_swim": ([r"fx\\fst\\swim\\swim\\"], 0),
    "fst_tread": ([r"fx\\fst\\swim\\tread\\"], 0),
    "fst_water": ([r"fx\\fst\\player\\water\\", r"fx\\fst\\player\\waterpuddle\\"], 0),
})
for kind in ("blade1hand", "blade2hand", "axe1hand", "axe2hand", "mace1hand", "blunt2hand", "bow", "lefthand", "unarmed"):
    EVENTS[f"wpn_{kind}_draw"] = ([rf"fx\\wpn\\{kind}\\wpn_{kind}_draw"], 0)
    EVENTS[f"wpn_{kind}_sheathe"] = ([rf"fx\\wpn\\{kind}\\wpn_{kind}_sheathe"], 0)
EVENTS.update({
    "mag_ready_alteration": ([r"fx\\mag\\alteration\\mag_alteration_drawsheathe"], 0),
    "mag_ready_restoration": ([r"fx\\mag\\restoration\\mag_restoration_drawsheathe"], 0),
    "mag_ready_destruction": ([r"fx\\mag\\frostbite\\mag_frostbite_drawsheathe"], 0),
    "mag_candlelight": ([r"fx\\mag\\light\\mag_light_fire"], 0),
    "mag_heal_lp": ([r"fx\\mag\\restoration\\mag_restoration_concentration_fire_lpm"], LOOP),
    "mag_flames_lp": ([r"fx\\mag\\flames\\mag_flames_fire_lpm"], LOOP),
    "mag_shock_lp": ([r"fx\\mag\\shock\\mag_shock_concentration_fire_lpm"], LOOP),
    "mag_shock_hit": ([r"fx\\mag\\shock\\impact\\"], 0),
    "mag_rune_cast": ([r"fx\\mag\\rune\\mag_rune_cast"], 0),
    "mag_rune_impact": ([r"fx\\mag\\rune\\mag_rune_impact"], 0),
    "ui_perk": ([r"fx\\ui\\skills\\perkselect\\ui_perkselect_2d"], 0),
    # Dragons: their flight, roars, landings and crashes, attacks, breath and death
    "dragon_roar": ([r"fx\\npc\\dragon\\flightroar\\npc_dragon_flightroar_0"], 0),
    "dragon_roar_far": ([r"fx\\npc\\dragon\\flightroar\\distant\\"], 0),
    "dragon_flap": ([r"fx\\npc\\dragon\\wing\\flap\\"], 0),
    "dragon_flyby": ([r"fx\\npc\\dragon\\flyby\\"], 0),
    "dragon_takeoff": ([r"fx\\npc\\dragon\\takeofffx\\"], 0),
    "dragon_land": ([r"fx\\npc\\dragon\\land\\"], 0),
    "dragon_crash": ([r"fx\\npc\\dragon\\landcrash\\long\\npc_dragon_landcrash_long\.wav"], 0),
    "dragon_bite": ([r"fx\\npc\\dragon\\attackbite\\"], 0),
    "dragon_tail": ([r"fx\\npc\\dragon\\attacktail\\npc_dragon_attack_tail_whip"], 0),
    "dragon_wing": ([r"fx\\npc\\dragon\\attackwing\\"], 0),
    "dragon_attack": ([r"fx\\npc\\dragon\\attack\\"], 0),
    "dragon_injured": ([r"fx\\npc\\dragon\\injured\\npc_dragon_injured_0"], 0),
    "dragon_step": ([r"fx\\npc\\dragon\\foot\\walk\\"], 0),
    "dragon_death": ([r"fx\\npc\\dragon\\death\\npc_dragon_death_0"], 0),
    "dragon_soul_fire": ([r"fx\\npc\\dragon\\deathsequence\\npc_dragon_deathsequence_fire\.wav"], 0),
    "dragon_soul_wind": ([r"fx\\npc\\dragon\\deathsequence\\npc_dragon_deathsequence_wind"], 0),
    "dragon_soul_burst": ([r"fx\\npc\\dragon\\deathsequence\\npc_dragon_deathsequence_explosion"], 0),
    # (a dragon's fire: Skyrim's dragon flames loop - npc_dragon_breathe_lp, used before, is the dragon breathing, not
    # its fire: the user heard nothing of Alduin's fire under the town's noise, Nov 8)
    "dragon_breath_lp": ([r"fx\\mag\\flamesdragon\\mag_flamesdragon_fire_0"], LOOP),
    "dragon_yol": ([r"fx\\voc\\shout\\dragontest\\firebreath\\voc_dragon_yol"], 0),  # (the shout before it: "Yol")
    "alchemy_potion": ([r"fx\\ui\\alchemy\\ui_alchemy_createpotion"], 0),
    "alchemy_poison": ([r"fx\\ui\\alchemy\\ui_alchemy_createpoison"], 0),
    "alchemy_fail": ([r"fx\\ui\\alchemy\\ui_alchemy_fail"], 0),
    "alchemy_learn": ([r"fx\\ui\\alchemy\\ui_alchemy_learneffect"], 0),
    "bow_pull": ([r"fx\\wpn\\bow\\pull\\"], 0),
    "bow_fire": ([r"fx\\wpn\\bow\\fire\\"], 0),
    "bow_nock": ([r"fx\\wpn\\bow\\nock\\"], 0),
    "arrow_flesh": ([r"fx\\wpn\\impact\\arrow\\flesh\\wpn_impact_arrow_flesh_0"], 0),
    "arrow_stick": ([r"fx\\wpn\\impact\\arrow\\stick\\"], 0),
    "arrow_player": ([r"fx\\wpn\\impact\\arrow\\flesh\\wpn_impact_arrow_fleshplayer"], 0),
    "itm_gold_up": ([r"fx\\itm\\gold\\up\\"], 0),
    "itm_coinpurse_up": ([r"fx\\itm\\coinpouch\\itm_coinpouch_up"], 0),
    "greybeard_call": ([r"fx\\qst\\qst_greybeardcall"], 0),  # (Dragon Rising: "Dovahkiin!" from the mountain)
    "greybeard_rumble": ([r"fx\\qst\\greybeardrumble\\"], 0),
    "itm_drop": ([r"fx\\phy\\coin\\l\\", r"fx\\itm\\gold\\down\\"], 0),
    "trp_tripwire": ([r"fx\\trp\\tripwire\\"], 0),
    "trp_plate": ([r"fx\\trp\\pressureplate\\stone\\trp_pressureplate_stone"], 0),
    "trp_beartrap": ([r"fx\\trp\\beartrap\\trp_beartrap_fire"], 0),
    "trp_swing": ([r"fx\\trp\\bladeswing\\swing\\"], 0),
    "trp_windup": ([r"fx\\trp\\bladeswing\\trp_bladeswing_windup"], 0),
    "trp_dart": ([r"fx\\trp\\dart\\fire\\"], 0),
    "trp_dart_hit": ([r"fx\\wpn\\impact\\trap\\dart\\flesh\\"], 0),
    "trp_rockfall": ([r"fx\\trp\\rockfallrelease\\", r"fx\\trp\\rockfallchute\\open\\a\\"], 0),
    "trp_spear": ([r"fx\\trp\\spear\\trp_spear_out"], 0),
    "trp_spikes": ([r"fx\\trp\\platformspikes\\up\\"], 0),
    "trp_mace": ([r"fx\\trp\\mace\\"], 0),
    "trp_ram": ([r"fx\\trp\\batteringram\\trp_batteringram_release"], 0),
    "trp_impact": ([r"fx\\trp\\trp_impact_impale"], 0),
    "trp_oil": ([r"fx\\trp\\oil\\ignite\\high\\"], 0),
    "trp_fire_lp": ([r"fx\\trp\\flamesfire\\"], LOOP),
    "trp_bones": ([r"fx\\trp\\bonealarm\\"], 0),
    "ui_ok": ([r"fx\\ui\\ui_menu_ok"], 0),
    "ui_cancel": ([r"fx\\ui\\ui_menu_cancel"], 0),
    "ui_focus": ([r"fx\\ui\\ui_menu_focus\.wav", r"fx\\ui\\focus\\ui_menu_focus_0"], 0),
    "ui_levelup": ([r"fx\\ui\\ui_levelup"], 0),
    # (a quest begun; its objective moved on - Skyrim's journal sounds)
    "ui_quest_new": ([r"fx\\ui\\ui_quest_new"], 0),
    "ui_objective": ([r"fx\\ui\\ui_objective_new_0"], 0),
    "ui_item_get": ([r"fx\\itm\\generic\\up\\", r"fx\\itm\\coinpouch\\itm_coinpouch_up"], 0),
    # (a word wall: its whisper while the word goes into you, its glow, the word learned)
    "wordwall_whisper": ([r"fx\\qst\\qst_wordwall_whisper_2d_lp"], LOOP),
    "wordwall_glow": ([r"fx\\qst\\qst_wordwall_glow_lp"], 0),
    "shout_learn": ([r"fx\\ui\\ui_shoutlearn_single"], 0),
    "web_cut": ([r"fx\\amb\\cobweb\\break\\amb_cobweb_break"], 0),
    # a draugr's shouts in its own voice (VoicePowers: "Fus..." "Ro...Da!", "Fo..." INFO 00050346,
    # "Zun..." "Haal...Viik!")
    "draugr_force_a": ([r"voice:crdraugrvoice\\voicepowers_shout01b_00013e04"], 0),
    "draugr_force_b": ([r"voice:crdraugrvoice\\voicepowers_shout03_00013e02"], 0),
    "draugr_frost_a": ([r"voice:crdraugrvoice\\voicepowers_shout01b_0005d167"], 0),
    "draugr_frost_b": ([r"voice:crdraugrvoice\\voicepowers_shout03_00050346"], 0),
    "draugr_disarm_a": ([r"voice:crdraugrvoice\\voicepowers_shout01b_0009cd3c"], 0),
    "draugr_disarm_b": ([r"voice:crdraugrvoice\\voicepowers_shout03_0009cd3d"], 0),
    "ui_skill": ([r"fx\\ui\\ui_skill_increase"], 0),
    "ui_fail": ([r"fx\\ui\\ui_activatefail"], 0),
    "lock_enter": ([r"fx\\ui\\lockpicking\\enter\\"], 0),
    "lock_move": ([r"fx\\ui\\lockpicking\\pickmovement\\"], 0),
    "lock_turn": ([r"fx\\ui\\lockpicking\\cylinderturn\\"], 0),
    "lock_stop": ([r"fx\\ui\\lockpicking\\cylinderstop\\"], 0),
    "lock_squeak": ([r"fx\\ui\\lockpicking\\cylindersqueak\\"], 0),
    "lock_break": ([r"fx\\ui\\lockpicking\\pickbreak\\"], 0),
    "lock_unlock": ([r"fx\\ui\\lockpicking\\unlock\\"], 0),
    "itm_up": ([r"fx\\itm\\generic\\up\\"], 0),
    "itm_gold": ([r"fx\\itm\\gold\\up\\"], 0),
    "itm_eat": ([r"fx\\itm\\foodeat\\"], 0),
    "itm_drink": ([r"fx\\itm\\potionuse\\"], 0),
    "itm_armor": ([r"fx\\itm\\generic\\armor\\up\\"], 0),
    "itm_cloth": ([r"fx\\itm\\clothing\\up\\"], 0),
    "door_open": ([r"^sound\\fx\\drs\\drs_woodsingle(?!ship|heavy)[^\\]*open", r"^sound\\fx\\drs\\drs_wood[^\\]*open"], 0),
    "door_close": ([r"^sound\\fx\\drs\\drs_woodsingle(?!ship|heavy)[^\\]*close", r"^sound\\fx\\drs\\drs_wood[^\\]*close"], 0),
    "chest_open": ([r"^sound\\fx\\(obj|drsc)\\(?!.*dlc).*chest.*open"], 0),
    "chest_close": ([r"^sound\\fx\\(obj|drsc)\\(?!.*dlc).*chest.*close"], 0),
    "lever_pull": ([r"fx\\obj\\levernordic01\\obj_levernordic_01_pull"], 0),
    "lever_return": ([r"fx\\obj\\levernordic01\\obj_levernordic_01_return"], 0),
    "pillar_turn": ([r"fx\\drs\\drs_ruinspuzzle_ringturn"], 0),
    "gate_open": ([r"fx\\drs\\drs_ironnordicportcullisgate_open"], 0),
    "gate_close": ([r"fx\\drs\\drs_ironnordicportcullisgate_close"], 0),
    "wpn_swing_blade": ([r"fx\\wpn\\swing\\blade\\medium\\"], 0),
    "wpn_swing_2h": ([r"fx\\wpn\\swing\\blade2hand\\"], 0),
    "wpn_swing_axe": ([r"fx\\wpn\\swing\\axe\\"], 0),
    "wpn_swing_unarmed": ([r"fx\\wpn\\swing\\unarmed\\"], 0),
    "wpn_hit_blade": ([r"fx\\wpn\\impact\\blade\\flesh\\"], 0),
    "wpn_hit_blunt": ([r"fx\\wpn\\impact\\blunt\\flesh\\"], 0),
    "wpn_hit_unarmed": ([r"fx\\wpn\\impact\\unarmed\\flesh\\"], 0),
    "wpn_block": ([r"fx\\wpn\\block\\blade\\"], 0),
    "wpn_block_shield": ([r"fx\\npc\\human\\combat\\shield\\block\\npc_"], 0),
    "bear_growl": ([r"fx\\npc\\bear\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\bear\\attack\\"], 0),
    "bear_attack": ([r"fx\\npc\\bear\\attack\\", r"fx\\npc\\bear\\attack(swing|swipe)\\"], 0),
    "bear_power": ([r"fx\\npc\\bear\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\bear\\attack\\"], 0),
    "bear_hurt": ([r"fx\\npc\\bear\\(injured|pain|hit)"], 0),
    "bear_death": ([r"fx\\npc\\bear\\death"], 0),
    "skeever_growl": ([r"fx\\npc\\skeever\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\skeever\\attack\\"], 0),
    "skeever_attack": ([r"fx\\npc\\skeever\\attack\\", r"fx\\npc\\skeever\\attack(swing|swipe)\\"], 0),
    "skeever_power": ([r"fx\\npc\\skeever\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\skeever\\attack\\"], 0),
    "skeever_hurt": ([r"fx\\npc\\skeever\\(injured|pain|hit)"], 0),
    "skeever_death": ([r"fx\\npc\\skeever\\death"], 0),
    "mudcrab_growl": ([r"fx\\npc\\mudcrab\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\mudcrab\\attack\\"], 0),
    "mudcrab_attack": ([r"fx\\npc\\mudcrab\\attack\\", r"fx\\npc\\mudcrab\\attack(swing|swipe)\\"], 0),
    "mudcrab_power": ([r"fx\\npc\\mudcrab\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\mudcrab\\attack\\"], 0),
    "mudcrab_hurt": ([r"fx\\npc\\mudcrab\\(injured|pain|hit)"], 0),
    "mudcrab_death": ([r"fx\\npc\\mudcrab\\death"], 0),
    "mammoth_growl": ([r"fx\\npc\\mammoth\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\mammoth\\attack\\"], 0),
    "mammoth_attack": ([r"fx\\npc\\mammoth\\attack\\", r"fx\\npc\\mammoth\\attack(swing|swipe)\\"], 0),
    "mammoth_power": ([r"fx\\npc\\mammoth\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\mammoth\\attack\\"], 0),
    "mammoth_hurt": ([r"fx\\npc\\mammoth\\(injured|pain|hit)"], 0),
    "mammoth_death": ([r"fx\\npc\\mammoth\\death"], 0),
    "giant_growl": ([r"fx\\npc\\giant\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\giant\\attack\\"], 0),
    "giant_attack": ([r"fx\\npc\\giant\\attack\\", r"fx\\npc\\giant\\attack(swing|swipe)\\"], 0),
    "giant_power": ([r"fx\\npc\\giant\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\giant\\attack\\"], 0),
    "giant_hurt": ([r"fx\\npc\\giant\\(injured|pain|hit)"], 0),
    "giant_death": ([r"fx\\npc\\giant\\death"], 0),
    "spriggan_growl": ([r"fx\\npc\\spriggan\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\spriggan\\attack\\"], 0),
    "spriggan_attack": ([r"fx\\npc\\spriggan\\attack\\", r"fx\\npc\\spriggan\\attack(swing|swipe)\\"], 0),
    "spriggan_power": ([r"fx\\npc\\spriggan\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\spriggan\\attack\\"], 0),
    "spriggan_hurt": ([r"fx\\npc\\spriggan\\(injured|pain|hit)"], 0),
    "spriggan_death": ([r"fx\\npc\\spriggan\\death"], 0),
    "horker_growl": ([r"fx\\npc\\horker\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\horker\\attack\\"], 0),
    "horker_attack": ([r"fx\\npc\\horker\\attack\\", r"fx\\npc\\horker\\attack(swing|swipe)\\"], 0),
    "horker_power": ([r"fx\\npc\\horker\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\horker\\attack\\"], 0),
    "horker_hurt": ([r"fx\\npc\\horker\\(injured|pain|hit)"], 0),
    "horker_death": ([r"fx\\npc\\horker\\death"], 0),
    "dog_growl": ([r"fx\\npc\\dog\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\dog\\attack\\"], 0),
    "dog_attack": ([r"fx\\npc\\dog\\attack\\", r"fx\\npc\\dog\\attack(swing|swipe)\\"], 0),
    "dog_power": ([r"fx\\npc\\dog\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\dog\\attack\\"], 0),
    "dog_hurt": ([r"fx\\npc\\dog\\(injured|pain|hit)"], 0),
    "dog_death": ([r"fx\\npc\\dog\\death"], 0),
    "troll_growl": ([r"fx\\npc\\troll\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\troll\\attack\\"], 0),
    "troll_attack": ([r"fx\\npc\\troll\\attack\\", r"fx\\npc\\troll\\attack(swing|swipe)\\"], 0),
    "troll_power": ([r"fx\\npc\\troll\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\troll\\attack\\"], 0),
    "troll_hurt": ([r"fx\\npc\\troll\\(injured|pain|hit)"], 0),
    "troll_death": ([r"fx\\npc\\troll\\death"], 0),
    "draugr_growl": ([r"fx\\npc\\draugr\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\draugr\\attack\\"], 0),
    "draugr_attack": ([r"fx\\npc\\draugr\\attack\\", r"fx\\npc\\draugr\\attack(swing|swipe)\\"], 0),
    "draugr_power": ([r"fx\\npc\\draugr\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\draugr\\attack\\"], 0),
    "draugr_hurt": ([r"fx\\npc\\draugr\\(injured|pain|hit)"], 0),
    "draugr_death": ([r"fx\\npc\\draugr\\death"], 0),
    "draugrskeleton_growl": ([r"fx\\npc\\skeleton\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\skeleton\\attack\\"], 0),
    "draugrskeleton_attack": ([r"fx\\npc\\skeleton\\attack\\", r"fx\\npc\\skeleton\\attack(swing|swipe)\\"], 0),
    "draugrskeleton_power": ([r"fx\\npc\\skeleton\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\skeleton\\attack\\"], 0),
    "draugrskeleton_hurt": ([r"fx\\npc\\skeleton\\(injured|pain|hit)"], 0),
    "draugrskeleton_death": ([r"fx\\npc\\skeleton\\death"], 0),
    "sabrecat_growl": ([r"fx\\npc\\sabrecat\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\sabrecat\\attack\\"], 0),
    "sabrecat_attack": ([r"fx\\npc\\sabrecat\\attack\\", r"fx\\npc\\sabrecat\\attack(swing|swipe)\\"], 0),
    "sabrecat_power": ([r"fx\\npc\\sabrecat\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\sabrecat\\attack\\"], 0),
    "sabrecat_hurt": ([r"fx\\npc\\sabrecat\\(injured|pain|hit)"], 0),
    "sabrecat_death": ([r"fx\\npc\\sabrecat\\death"], 0),
    "goat_growl": ([r"fx\\npc\\goat\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\goat\\attack\\"], 0),
    "goat_attack": ([r"fx\\npc\\goat\\attack\\", r"fx\\npc\\goat\\attack(swing|swipe)\\"], 0),
    "goat_power": ([r"fx\\npc\\goat\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\goat\\attack\\"], 0),
    "goat_hurt": ([r"fx\\npc\\goat\\(injured|pain|hit)"], 0),
    "goat_death": ([r"fx\\npc\\goat\\death"], 0),
    "highlandcow_growl": ([r"fx\\npc\\cow\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\cow\\attack\\"], 0),
    "highlandcow_attack": ([r"fx\\npc\\cow\\attack\\", r"fx\\npc\\cow\\attack(swing|swipe)\\"], 0),
    "highlandcow_power": ([r"fx\\npc\\cow\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\cow\\attack\\"], 0),
    "highlandcow_hurt": ([r"fx\\npc\\cow\\(injured|pain|hit)"], 0),
    "highlandcow_death": ([r"fx\\npc\\cow\\death"], 0),
    "frostbitespider_growl": ([r"fx\\npc\\spiderfrostbite\\(aggrowarning|aware|attackvocal|combatidle)\\", r"fx\\npc\\spiderfrostbite\\attack\\"], 0),
    "frostbitespider_attack": ([r"fx\\npc\\spiderfrostbite\\attack\\", r"fx\\npc\\spiderfrostbite\\attack(swing|swipe)\\"], 0),
    "frostbitespider_power": ([r"fx\\npc\\spiderfrostbite\\attack(power|forward|powerfoward|powerforward|stomp|combo)", r"fx\\npc\\spiderfrostbite\\attack\\"], 0),
    "frostbitespider_hurt": ([r"fx\\npc\\spiderfrostbite\\(injured|pain|hit)"], 0),
    "frostbitespider_death": ([r"fx\\npc\\spiderfrostbite\\death"], 0),
    # combat's impacts: a blow on a block (steel on steel), on you; spells striking
    "wpn_block_hit": ([r"fx\\wpn\\impact\\blade\\metal\\"], 0),
    "wpn_hit_player": ([r"fx\\wpn\\impact\\blade\\armor\\"], 0),
    "mag_fire_hit": ([r"fx\\mag\\flames\\impact\\"], 0),
    "mag_frost_hit": ([r"fx\\mag\\frostbite\\impact\\"], 0),
    "mag_frost_cast": ([r"fx\\mag\\frostbite\\mag_frostbite_fire_2d"], 0),
    "mag_firebolt_cast": ([r"fx\\mag\\firebolt\\"], 0),
    "itm_poisonuse": ([r"fx\\itm\\itm_poisonuse"], 0),
    "spider_spit": ([r"fx\\npc\\spiderfrostbite\\attack\\spit\\"], 0),
    "spider_spit_hit": ([r"fx\\npc\\spiderfrostbite\\spit\\impact\\"], 0),
    # shouts (each word's own: a one, b two, c three) and the racial powers
    "shout_sprint": ([r"fx\\voc\\shout\\fx\\sprint\\"], 0),
    "shout_push_hit": ([r"fx\\voc\\shout\\impact\\push\\voc_"], 0),
    "power_battlecry": ([r"fx\\mag\\powerracial\\mag_powerracial_battlecry"], 0),
    "wolf_growl": ([r"fx\\npc\\wolf\\aggrowarning\\"], 0),
    "wolf_attack": ([r"fx\\npc\\wolf\\attack\\"], 0),
    "wolf_power": ([r"fx\\npc\\wolf\\attackpower\\"], 0),
    "wolf_hurt": ([r"fx\\npc\\wolf\\(injured|pain|hit)"], 0),
    "wolf_death": ([r"fx\\npc\\wolf\\death"], 0),
    "wolf_breathe": ([r"fx\\npc\\wolf\\breathe\\"], 0),  # (a familiar at your side: its breathing now and then)
    "mag_conjure": ([r"fx\\mag\\conjure\\impact\\"], 0),  # (a summons appearing)
    # (the werewolf's - Beast Form's and the Companions' werewolves: its change, claws, feeding, the blood
    # ritual's cut)
    "werewolf_growl": ([r"fx\\npc\\werewolf\\growl\\"], 0),
    "werewolf_attack": ([r"fx\\npc\\werewolf\\attack\\"], 0),
    "werewolf_power": ([r"fx\\npc\\werewolf\\npc_werewolf_maul"], 0),
    "werewolf_hurt": ([r"fx\\npc\\werewolf\\npc_werewolf_injured"], 0),
    "werewolf_death": ([r"fx\\npc\\werewolf\\npc_werewolf_death"], 0),
    "werewolf_change": ([r"fx\\npc\\werewolf\\npc_werewolf_transformation_02_2d"], 0),
    "werewolf_feed": ([r"fx\\npc\\werewolf\\npc_werewolf_feeding_01"], 0),
    "werewolf_howl": ([r"fx\\npc\\werewolf\\npc_werewolf_shout_01"], 0),
    "c03_cut": ([r"fx\\qst\\c03\\qst_c03_werewolf_cut"], 0),
    "dart_fire": ([r"fx\\trp\\dart\\fire\\"], 0),
    "dart_hit": ([r"fx\\wpn\\impact\\trap\\dart\\flesh\\"], 0),
})


for _k, _dir in (("force", r"unrelentingforce\\thunder"), ("fire", "firebreath"), ("frost", "frostbreath")):
    for _n, _letter in ((1, "a"), (2, "b"), (3, "c")):
        EVENTS[f"shout_{_k}_{_n}"] = ([rf"fx\\voc\\shout\\fx\\{_dir}\\voc_shout_fx_\w+_{_letter}_"], 0)

def decode(data, max_seconds=MAX_SECONDS, loop=False):
    """A WAV's samples as mono i16 at RATE (silence trimmed; a one-shot cut long fades out)."""
    with av.open(io.BytesIO(data)) as c:
        resampler = av.AudioResampler(format="s16", layout="mono", rate=RATE)
        out = []
        for frame in c.decode(audio=0):
            for f in resampler.resample(frame):
                out.append(f.to_ndarray().reshape(-1))
        for f in resampler.resample(None):
            out.append(f.to_ndarray().reshape(-1))
    pcm = np.concatenate(out) if out else np.zeros(0, np.int16)
    if not loop:
        loud = np.nonzero(np.abs(pcm.astype(np.int32)) > SILENCE)[0]
        pcm = pcm[loud[0]:loud[-1] + 1] if len(loud) else pcm[:0]
    n = int(RATE * max_seconds)
    if len(pcm) > n:
        pcm = pcm[:n].astype(np.float32)
        if not loop:
            fade = min(n, RATE // 5)
            pcm[-fade:] *= np.linspace(1.0, 0.0, fade)
    return pcm.astype(np.int16)


# People's grunts (Skyrim's combat "effort" lines, per voice: DialogueCombat's DCETHit when
# struck, DCETDeath dying, DCETAttack swinging) for each voice the player's characters and the
# hostile people speak with: events "v_<voice, 14 letters>_hit" / _death / _attack
VOICE_GRUNTS = {"hit": "dcethit", "death": "dcetdeath", "attack": "dcetattack"}


def _voices_used(pack):
    """The voice types that can fight: the player's (shouts.PLAYER_VOICES) and the hostile
    people's (creatures.txt)."""
    from shouts import PLAYER_VOICES
    used = set(PLAYER_VOICES)
    try:
        with open(os.path.join(pack, "creatures", "creatures.txt"), encoding="cp1252") as f:
            for line in f:
                parts = line.rstrip("\n").split("|")
                if parts[0] == "K" and len(parts) > 22 and parts[22] != "-":
                    used.add(parts[22])
    except OSError:
        pass
    return sorted(used)


def export(pack):
    archive = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Sounds.bsa"))
    voices = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Voices_en0.bsa"))
    voice_names = sorted(n for n in voices.entries if n.endswith(".fuz") and "\\skyrim.esm\\" in n)
    events = dict(EVENTS)
    for v in _voices_used(pack):
        for ours, topic in VOICE_GRUNTS.items():
            events[f"v_{v[:14]}_{ours}"] = ([rf"voice:\\{re.escape(v)}\\dialogueco_{topic}_"], 0)
    names = sorted(n for n in archive.entries if n.endswith(".wav"))
    os.makedirs(os.path.join(pack, "sound"), exist_ok=True)
    dll = load_adpcm()
    block_bytes = 4 + BLOCK_FRAMES // 2

    def adpcm(pcm):
        pcm = np.ascontiguousarray(pcm, np.int16)
        blocks = (len(pcm) + BLOCK_FRAMES - 1) // BLOCK_FRAMES
        out = ctypes.create_string_buffer(blocks * block_bytes)
        dll.encode_channel(pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)), 1, len(pcm), BLOCK_FRAMES, out,
                           block_bytes)
        return out.raw

    body, count, total = b"", 0, 0
    for event, (patterns, flags) in events.items():
        files, source = [], archive
        for pattern in patterns:
            if pattern.startswith("voice:"):  # (a voice line: the .fuz's audio after its lip data)
                rx, source = re.compile(pattern[6:]), voices
                files = [n for n in voice_names if rx.search(n)]
            else:
                rx = re.compile(pattern)
                files = [n for n in names if rx.search(n) and "\\dlc0" not in n]
            if files:
                break
        variants = []
        loop = bool(flags & LOOP)
        for path in files[:4 if event.startswith("fst_") else MAX_VARIANTS]:
            try:
                data = source.read(path)
                if data[:4] == b"FUZE":
                    data = data[12 + struct.unpack_from("<I", data, 8)[0]:]
                longest = LONG_SECONDS if event.startswith(LONG_EVENTS) else MAX_SECONDS
                pcm = decode(data, LOOP_SECONDS if loop else longest, loop)
            except (av.AVError, ValueError) as e:
                print(f"sounds: can't decode {path}: {e}")
                continue
            if len(pcm):
                variants.append(pcm)
        if not variants:
            print(f"sounds: nothing for {event}")
            continue
        body += struct.pack("<24sBBH", event.encode(), len(variants), flags, 0)
        for pcm in variants:
            body += struct.pack("<I", len(pcm)) + adpcm(pcm)
            total += len(pcm)
        count += 1
    with open(os.path.join(pack, "sound", "sfx.bank"), "wb") as f:
        f.write(struct.pack("<4sHHII", b"SKSF", SKSF_VERSION, count, RATE, BLOCK_FRAMES) + body)
    print(f"sounds: {count} events, {total / RATE:.0f} s, {len(body) / 1e6:.1f} MB")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    export(sys.argv[1])
