#include "activators.h"

#include <math.h>

#include <stdio.h>
#include <string.h>

#include "aim.h"
#include "creatures.h"
#include "doors.h"
#include "gamelog.h"
#include "items.h"
#include "sfx.h"
#include "shouts.h"
#include "traps.h"
#include "ui.h"
#include "gametext.h"

#define MAX_ACTIVATORS 64
#define MAX_LINKS 8
#define REACH_M 2.6f
#define WEB_HOLDS_M 2.5f // (a web and the one caught in it)
#define DART_DAMAGE 15.0f // a failed puzzle's darts (Skyrim's dart traps: about this at the start)

enum { ACT_PILLAR = 1, ACT_LEVER, ACT_PUZZLE_LEVER, ACT_GATE, ACT_BRIDGE, ACT_KEYHOLE, ACT_CLAWBASE, ACT_WEB, ACT_STONE, ACT_OPENER };
// Ustengrav's Whirlwind Sprint puzzle (converter/interiors.py ACT_STONE): a dragon stone holds its gate
// open while you stand on it; stepped off, the gate shuts after STONE_CLOSE_S (dunUstenPuzGateScript's
// CloseGate) and your shout is ready again (resetsShoutOnClose). A lever pulled at one: open, then shut
// after a second more. The trigger past the gates (ACT_OPENER) opens them for good
#define STONE_CLOSE_S 1.5f
#define STONE_LEVER_S 1.0f
#define STONE_HEIGHT_M 3.0f
#define GATE_CLEAR_M 6.0f // (you near a gate once sprinted: it waits. Skyrim's window is for its run, ~5 m/s;
                          //  ours is 3.2 (main.c RUN_SPEED_MS). Walked, not sprinted, you're 14-20 m off when they drop)
#define SPRINT_GATE_S 1.0f // (NorPortcullisGate01's Forward and Backward: 1.0 s, 0.97 s - its text keys)
// A Hall of Stories door (converter/interiors.py): its three rings are pillars with this for a
// controller, its keyhole wants the claw and their three signs right; then all of it goes
#define CLAW_CONTROLLER 0xC1A30000u
#define CLAW_PART(a) ((a)->kind == ACT_KEYHOLE || (a)->kind == ACT_CLAWBASE || ((a)->kind == ACT_PILLAR && (a)->controller == CLAW_CONTROLLER))
// (a gate, or a retractable bridge - its deck raised when "open": both a lever's to work)
#define TWO_STATE(k) ((k) == ACT_GATE || (k) == ACT_BRIDGE)

typedef struct {
	uint32_t ref, controller;
	int kind, state, solve;
	float radius, centre[3];
	uint32_t links[MAX_LINKS], failures[MAX_LINKS];
	int link_count, failure_count;
	char label[48];
	int door; // doors.c (its moving part)
	int on_it;     // (a dragon stone: stood on now)
	float close_t; // (a dragon stone: its gate shuts when this runs out; 0 none due)
} Activator;

static Activator acts[MAX_ACTIVATORS];
static void claw_sync(void);
static int act_count;
static float damage;

// Whether a gate is open: how Skyrim starts it (state 2: open), flipped each time a lever's
// pulled (the remembered set holds the flipped ones)
static int gate_open(const Activator *a) {
	return (a->state == 2) ^ items_unlocked(a->ref);
}

const uint8_t *activators_load(const uint8_t *p, const uint8_t *end) {
	act_count = 0;
	return activators_append(p, end);
}

void activators_clear(void) {
	act_count = 0;
}

const uint8_t *activators_append(const uint8_t *p, const uint8_t *end) {
	if (!p || p + 4 > end)
		return NULL;
	uint32_t n;
	memcpy(&n, p, 4);
	p += 4;
	for (uint32_t i = 0; i < n; i++) {
		if (!p || p + 20 > end)
			return NULL;
		Activator a;
		memset(&a, 0, sizeof(a));
		memcpy(&a.ref, p, 4);
		a.kind = p[4], a.state = p[5], a.solve = p[6];
		const int label_len = p[7];
		memcpy(&a.radius, p + 8, 4);
		memcpy(&a.controller, p + 12, 4);
		const int nl = p[16], nf = p[17];
		p += 20;
		if (p + (nl + nf) * 4 + label_len > end)
			return NULL;
		for (int k = 0; k < nl; k++, p += 4)
			if (a.link_count < MAX_LINKS)
				memcpy(&a.links[a.link_count++], p, 4);
		for (int k = 0; k < nf; k++, p += 4)
			if (a.failure_count < MAX_LINKS)
				memcpy(&a.failures[a.failure_count++], p, 4);
		snprintf(a.label, sizeof(a.label), "%.*s", label_len, (const char *)p);
		p += (label_len + 3) & ~3;
		if (p + 60 <= end)
			memcpy(a.centre, p + 48, 12);
		p = doors_add_activator(p, end, &a.door);
		if (p && act_count < MAX_ACTIVATORS) {
			// A gate as left (open from the start - state 2 - or opened since)
			if (TWO_STATE(a.kind) && gate_open(&a))
				doors_set_open_now(a.door, 1);
			if ((a.kind == ACT_BRIDGE || a.kind == ACT_WEB) && items_unlocked(a.ref ^ 0x80000000u)) // (brought down, cut: gone)
				doors_remove(a.door), a.kind = 0;
			if (a.kind == ACT_PILLAR) // (its first face as Skyrim sets it: face n is n - 1 turns of 120 degrees)
				doors_set_turns(a.door, (float)(a.state - 1));
			acts[act_count++] = a;
			game_log("activator: kind %d '%s' at %.0f %.0f %.0f state %d links %d ref %08x%s", a.kind, a.label, a.centre[0],
			         a.centre[1], a.centre[2], a.state, a.link_count, (unsigned)a.ref, TWO_STATE(a.kind) && gate_open(&a) ? " open" : "");
		}
	}
	claw_sync();
	for (int i = 0; i < act_count; i++) { // (a gate shut that nothing here opens: a way that can't be taken - logged)
		const Activator *g = &acts[i];
		if (g->kind != ACT_GATE || gate_open(g) || !strcmp(g->label, "Lid"))
			continue;
		int opened = 0;
		for (int j = 0; j < act_count && !opened; j++)
			for (int k = 0; k < acts[j].link_count; k++)
				opened |= acts[j].links[k] == g->ref;
		if (!opened)
			game_log("activator: gate %08x '%s' at %.0f %.0f %.0f - nothing here opens it", (unsigned)g->ref, g->label, g->centre[0],
			         g->centre[1], g->centre[2]);
	}
	return p;
}

// A claw door opened (its keyhole remembered unlocked): rings, keyhole and base away
static void claw_sync(void) {
	int open = 0;
	for (int i = 0; i < act_count; i++)
		open |= acts[i].kind == ACT_KEYHOLE && items_unlocked(acts[i].ref);
	for (int i = 0; i < act_count && open; i++)
		if (CLAW_PART(&acts[i]))
			doors_remove(acts[i].door), acts[i].kind = 0;
}

int activators_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < act_count; i++) {
		if (TWO_STATE(acts[i].kind) || acts[i].kind == ACT_CLAWBASE || acts[i].kind == ACT_WEB || acts[i].kind == ACT_STONE ||
		    acts[i].kind == ACT_OPENER || acts[i].kind == 0)
			continue; // (opened by levers, not by hand; stood on)
		float t;
		if (aim_hits(eye, dir, acts[i].centre, acts[i].radius, REACH_M + acts[i].radius * 0.5f, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0 && CLAW_PART(&acts[best])) {
		// (a claw door's rings and keyhole share a middle: the one aimed at is the smallest the
		// ray passes within - the keyhole at the centre, then the inner, middle and outer rings)
		const float *c = acts[best].centre;
		const float v[3] = {c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]};
		const float t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2];
		const float off[3] = {v[0] - dir[0] * t, v[1] - dir[1] * t, v[2] - dir[2] * t};
		const float miss = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
		float least = 1e9f;
		for (int i = 0; i < act_count; i++)
			if (CLAW_PART(&acts[i]) && acts[i].kind != ACT_CLAWBASE && acts[i].radius >= miss && acts[i].radius < least)
				best = i, least = acts[i].radius;
	}
	if (best >= 0) {
		*along = best_t;
		*label = acts[best].label;
	}
	return best;
}

// The gates near pos opened (Skyrim's scripts raise them: the opening's last gate): how many
// The gates near pos opened whatever works them (a scene's: the keep's gate, raised by its lever - the
// Imperials' INFO 0005CE96); how many
int activators_force_open_near(const float pos[3], float radius) {
	int n = 0;
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->kind != ACT_GATE || gate_open(a) || fabsf(a->centre[1] - pos[1]) > 4.0f ||
		    hypotf(a->centre[0] - pos[0], a->centre[2] - pos[2]) > radius)
			continue;
		doors_set_open(a->door, 1);
		sfx_play_at("gate_open", a->centre, 30.0f);
		if (items_unlocked(a->ref))
			items_set_locked(a->ref);
		else
			items_set_unlocked(a->ref);
		n++;
	}
	return n;
}

void activators_log_near(const float pos[3], float radius) {
	for (int i = 0; i < act_count; i++) {
		const Activator *a = &acts[i];
		if (hypotf(a->centre[0] - pos[0], a->centre[2] - pos[2]) < radius)
			game_log("activators: #%d kind %d at %.1f %.1f %.1f ref %08x", i, a->kind, a->centre[0], a->centre[1], a->centre[2], (unsigned)a->ref);
	}
}

int activators_open_near(const float pos[3], float radius) {
	int n = 0;
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->kind != ACT_GATE || gate_open(a) || fabsf(a->centre[1] - pos[1]) > 4.0f ||
		    hypotf(a->centre[0] - pos[0], a->centre[2] - pos[2]) > radius)
			continue;
		int levered = 0; // (one a lever or chain here opens: by that - Circle on it - not by coming near)
		for (int j = 0; j < act_count && !levered; j++)
			for (int k = 0; k < acts[j].link_count; k++)
				levered |= acts[j].links[k] == a->ref && (acts[j].kind == ACT_LEVER || acts[j].kind == ACT_PUZZLE_LEVER);
		if (levered)
			continue;
		doors_set_open(a->door, 1);
		sfx_play_at("gate_open", a->centre, 30.0f);
		if (items_unlocked(a->ref)) // (remembered open, as a lever's: once - not raised again each frame)
			items_set_locked(a->ref);
		else
			items_set_unlocked(a->ref);
		n++;
	}
	return n;
}

static Activator *by_ref(uint32_t ref) {
	for (int i = 0; i < act_count; i++)
		if (acts[i].ref == ref)
			return &acts[i];
	return NULL;
}

// The sprint gates opened for good (the stage trigger past them reached)
static int sprint_open_for_good(void) {
	for (int i = 0; i < act_count; i++)
		if (acts[i].kind == ACT_OPENER && items_unlocked(acts[i].ref))
			return 1;
	return 0;
}

// A dragon stone's gate up or down (not remembered: it's held open, not opened)
static void stone_gate(const Activator *s, int open) {
	Activator *g = s->link_count ? by_ref(s->links[0]) : NULL;
	if (!g || g->kind != ACT_GATE || gate_open(g))
		return;
	doors_set_swing(g->door, SPRINT_GATE_S);
	doors_set_open(g->door, open);
	sfx_play_at(open ? "gate_open" : "gate_close", g->centre, 30.0f);
	game_log("activator: sprint gate %08x %s", (unsigned)g->ref, open ? "up" : "down");
}

void activators_update(float dt, const float feet[3]) {
	const int done = sprint_open_for_good();
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->kind != ACT_STONE && a->kind != ACT_OPENER)
			continue;
		const int in = hypotf(feet[0] - a->centre[0], feet[2] - a->centre[2]) < a->radius &&
		               fabsf(feet[1] - a->centre[1]) < STONE_HEIGHT_M;
		if (a->kind == ACT_OPENER) {
			if (in && !done) { // (dunUstengravQST stage 50: every gate up for good, the levers done)
				items_set_unlocked(a->ref);
				for (int k = 0; k < a->link_count; k++) {
					Activator *g = by_ref(a->links[k]);
					if (g && g->kind == ACT_GATE && !gate_open(g)) {
						doors_set_open(g->door, 1);
						items_set_unlocked(g->ref);
					}
				}
				game_log("activator: the sprint gates open for good");
			}
			continue;
		}
		if (done) {
			a->on_it = 0, a->close_t = 0.0f;
			continue;
		}
		if (in && !a->on_it)
			stone_gate(a, 1), a->close_t = 0.0f;
		else if (!in && a->on_it)
			a->close_t = STONE_CLOSE_S;
		a->on_it = in;
		if (a->close_t > 0.0f && (a->close_t -= dt) <= 0.0f) {
			const Activator *g = a->link_count ? by_ref(a->links[0]) : NULL;
			if (g && hypotf(feet[0] - g->centre[0], feet[2] - g->centre[2]) < GATE_CLEAR_M) {
				a->close_t = 0.1f; // (not down on you: in Skyrim you'd be pushed out of it - here it waits)
				continue;
			}
			a->close_t = 0.0f;
			stone_gate(a, 0);
			shouts_reset_recovery(); // (resetsShoutOnClose: the next try at once)
		}
	}
}

// Activates the activators with this reference (a lever's or puzzle's targets): a gate
// opens or closes (and is remembered open)
static void activate_ref(uint32_t ref) {
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->ref == ref && a->kind == ACT_STONE && !sprint_open_for_good() && !a->on_it) { // (a lever at a stone)
			stone_gate(a, 1);
			a->close_t = STONE_LEVER_S + STONE_CLOSE_S;
			continue;
		}
		if (a->ref != ref || !TWO_STATE(a->kind))
			continue;
		const int open = !gate_open(a);
		game_log("activator: '%s' %08x %s", a->label, (unsigned)a->ref, a->kind == ACT_BRIDGE ? (open ? "raised" : "let down") : (open ? "opened" : "closed"));
		doors_set_open(a->door, open);
		sfx_play_at(open ? "gate_open" : "gate_close", a->centre, 30.0f);
		if (items_unlocked(a->ref)) // (remembered: flipped from how it started)
			items_set_locked(a->ref);
		else
			items_set_unlocked(a->ref);
	}
}

int activators_open_lids(const float pos[3], float radius) {
	int n = 0;
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->kind != ACT_GATE || strcmp(a->label, "Lid") || gate_open(a) ||
		    hypotf(a->centre[0] - pos[0], a->centre[2] - pos[2]) > radius || fabsf(a->centre[1] - pos[1]) > 2.5f)
			continue;
		activate_ref(a->ref);
		n++;
	}
	return n;
}

void activators_use(int index) {
	if (index < 0 || index >= act_count)
		return;
	Activator *a = &acts[index];
	if (doors_moving(a->door))
		return;
	switch (a->kind) {
	case ACT_PILLAR: // the next face: 1 -> 2 -> 3 -> 1
		a->state = a->state % 3 + 1;
		if (a->controller == CLAW_CONTROLLER)
			game_log("claw door: %s now at %d (wants %d)", a->label, a->state, a->solve);
		doors_step(a->door);
		sfx_play("pillar_turn", 0.8f, 0.0f);
		break;
	case ACT_LEVER:
		game_log("activator: '%s' pulled (%d linked)", a->label, a->link_count);
		doors_pull(a->door);
		sfx_play("lever_pull", 0.8f, 0.0f);
		for (int k = 0; k < a->link_count; k++)
			activate_ref(a->links[k]);
		traps_pull_ref(a->ref); // (a trap's lever or chain: what's linked to it springs; blades stop)
		break;
	case ACT_KEYHOLE: { // The claw in the keyhole: the rings' three signs as on its palm, and the door goes down
		int claw = -1;
		if (a->controller) {
			int it = items_by_form(a->controller);
			if (it >= 0 && items_have(it) > 0)
				claw = it;
		}
		// Also accept alternate Golden Claw FormID if MS13/E3 variant
		if (claw < 0 && (a->controller == 0x00039647u || a->controller == 0x000999E7u)) {
			int alt = items_by_form(a->controller == 0x00039647u ? 0x000999E7u : 0x00039647u);
			if (alt >= 0 && items_have(alt) > 0)
				claw = alt;
		}
		// Check all known dragon claw variant names
		if (claw < 0) {
			static const char *const CLAW_NAMES[] = {
				T_MISC_00039647_FULL0,
				T_MISC_000AB7BB_FULL0,
				"Ivory Claw",
				T_MISC_0004B56C_FULL0,
				T_MISC_000ED417_FULL0,
				T_MISC_0005AF48_FULL0,
				T_MISC_0007C260_FULL0,
				T_MISC_000B634C_FULL0,
				T_MISC_000AB375_FULL0,
				T_MISC_000663D7_FULL0,
				T_MISC_0008CDFA_FULL0,
				T_MISC_0401CAC1_FULL0,
				T_MISC_0401CAC0_FULL0,
				"Amethyst Claw"
			};
			for (size_t k = 0; k < sizeof(CLAW_NAMES) / sizeof(CLAW_NAMES[0]); k++) {
				int it = items_find_name(CLAW_NAMES[k]);
				if (it >= 0 && items_have(it) > 0) {
					claw = it;
					break;
				}
			}
		}
		// (not any item with "Claw" in its name: an Ingredient's Bear Claws or a Sabre Cat's would turn the rings)
		if (claw < 0) {
			for (int k = 0; k < items_count(); k++)
				if (items_have(k) > 0 && strstr(items_name(k), "Dragon Claw")) {
					claw = k;
					break;
				}
		}
		game_log("claw door: the keyhole tried (%s)", claw >= 0 && items_have(claw) > 0 ? "with the claw" : "no claw");
		if (claw < 0 || items_have(claw) <= 0) {
			ui_notice("The keyhole takes a claw: three talons. You have none", 4.0f);
			break;
		}
		int rings = 0, solved = 1;
		for (int i = 0; i < act_count; i++)
			if (acts[i].kind == ACT_PILLAR && acts[i].controller == CLAW_CONTROLLER) {
				rings++, solved &= acts[i].state == acts[i].solve;
				game_log("claw door: %s at %d, wants %d (radius %.2f)", acts[i].label, acts[i].state, acts[i].solve, acts[i].radius);
			}
		if (rings && solved) {
			items_set_unlocked(a->ref);
			sfx_play("gate_open", 1.0f, 0.0f);
			ui_notice("The rings turn, and the door sinks into the floor", 4.0f);
			claw_sync();
			for (int k = 0; k < a->link_count; k++)
				activate_ref(a->links[k]);
		} else {
			damage += DART_DAMAGE;
			sfx_play("dart_fire", 0.9f, -0.4f);
			sfx_play("dart_hit", 0.8f, 0.3f);
			ui_notice("Darts! The rings' signs don't match the claw's", 4.0f);
		}
		break;
	}
	case ACT_PUZZLE_LEVER: {
		doors_pull(a->door);
		sfx_play("lever_pull", 0.8f, 0.0f);
		// Every pillar on its solved face; a two-stage pillar (Skuldafn Temple's: its other answer in solve's high
		// four bits) picks which success: its first face the links before a 0, its second those after
		int pillars = 0, solved = 1, two_stage = 0, answer = 0;
		for (int i = 0; i < act_count; i++) {
			const Activator *pl = &acts[i];
			if (pl->kind != ACT_PILLAR || pl->controller != a->ref)
				continue;
			pillars++;
			if (pl->solve >> 4) {
				two_stage = 1;
				answer = pl->state == (pl->solve & 15) ? 1 : pl->state == (pl->solve >> 4) ? 2 : 0;
			} else {
				solved &= pl->state == pl->solve;
			}
		}
		if (two_stage)
			solved &= answer != 0;
		game_log("puzzle: lever %08x - %d pillars, %s%s", (unsigned)a->ref, pillars, solved ? "solved" : "not solved",
		         two_stage && solved ? (answer == 1 ? " (its first answer)" : " (its other answer)") : "");
		if (pillars && solved) {
			int part = 1; // (the links before a 0: the first answer's; after it: the other's)
			for (int k = 0; k < a->link_count; k++) {
				if (!a->links[k]) {
					part = 2;
					continue;
				}
				if ((!two_stage || part == answer) && !items_unlocked(a->links[k]))
					activate_ref(a->links[k]);
			}
		} else { // (the wrong faces: the darts it's linked to fly; without any, a token stab)
			int sprung = 0;
			for (int k = 0; k < a->failure_count; k++)
				sprung += traps_spring_ref(a->failures[k]) + (traps_fire_ref(a->failures[k]), 0);
			if (!sprung) {
				damage += DART_DAMAGE;
				sfx_play("dart_fire", 0.9f, -0.4f);
				sfx_play("dart_hit", 0.8f, 0.3f);
			}
		}
		break;
	}
	default:
		break;
	}
}

int activators_collapse_near(const float pos[3], float radius) { // (a bridge brought down: gone, for good)
	int n = 0;
	for (int i = 0; i < act_count; i++)
		if (acts[i].kind == ACT_BRIDGE && hypotf(acts[i].centre[0] - pos[0], acts[i].centre[2] - pos[2]) < radius &&
		    fabsf(acts[i].centre[1] - pos[1]) < 6.0f) {
			doors_remove(acts[i].door), acts[i].kind = 0;
			items_set_unlocked(acts[i].ref ^ 0x80000000u); // (remembered: down)
			n++;
		}
	return n;
}

// Whether every gate a lever works is open already (a scene opened it: not pulled shut again)
static int lever_gates_open(const Activator *lv) {
	int gates = 0;
	for (int k = 0; k < lv->link_count; k++)
		for (int j = 0; j < act_count; j++)
			if (acts[j].ref == lv->links[k] && acts[j].kind == ACT_GATE) {
				gates++;
				if (!gate_open(&acts[j]))
					return 0;
			}
	return gates > 0;
}

int activators_lever_near(const float pos[3], float radius, float out[3], int skip) {
	int best = -1;
	float best_d = radius;
	for (int i = 0; i < act_count; i++) {
		const float d = hypotf(acts[i].centre[0] - pos[0], acts[i].centre[2] - pos[2]);
		if (acts[i].kind == ACT_LEVER && acts[i].link_count > 0 && i != skip && d < best_d &&
		    fabsf(acts[i].centre[1] - pos[1]) < 4.0f && !lever_gates_open(&acts[i]))
			best = i, best_d = d;
	}
	if (best >= 0)
		memcpy(out, acts[best].centre, 3 * sizeof(float));
	return best;
}

int activators_pull_near(const float pos[3], float radius) { // (testing: the lever or chain nearest, pulled)
	int best = -1;
	float best_d = radius;
	for (int i = 0; i < act_count; i++) {
		const float d = hypotf(acts[i].centre[0] - pos[0], acts[i].centre[2] - pos[2]);
		if (acts[i].kind == ACT_LEVER && d < best_d && fabsf(acts[i].centre[1] - pos[1]) < 4.0f)
			best = i, best_d = d;
	}
	if (best >= 0)
		activators_use(best);
	return best >= 0;
}

float activators_take_damage(void) {
	float d = damage;
	damage = 0.0f;
	return d;
}

static void web_down(Activator *a) {
	doors_remove(a->door), a->kind = 0;
	items_set_unlocked(a->ref ^ 0x80000000u); // (remembered: gone)
	sfx_play_at("web_cut", a->centre, 25.0f);
	creatures_release_near(a->centre, WEB_HOLDS_M); // (whoever it held - Arvel - comes down with it)
	game_log("activator: a web struck down at %.0f %.0f %.0f", a->centre[0], a->centre[1], a->centre[2]);
}

int activators_web_down_near(const float pos[3], float radius) {
	int n = 0;
	for (int i = 0; i < act_count; i++)
		if (acts[i].kind == ACT_WEB && fabsf(acts[i].centre[1] - pos[1]) < 4.0f &&
		    hypotf(acts[i].centre[0] - pos[0], acts[i].centre[2] - pos[2]) < radius)
			web_down(&acts[i]), n++;
	return n;
}

// A spider's web (Skyrim's destructible ones: Arvel's across Bleak Falls' passage) struck along
// from..from + dir * len - a blade, an arrow, a spell's fire: cut down, burnt away, for good
int activators_strike(const float from[3], const float dir[3], float len) {
	int n = 0;
	for (int i = 0; i < act_count; i++) {
		Activator *a = &acts[i];
		if (a->kind != ACT_WEB)
			continue;
		const float d[3] = {a->centre[0] - from[0], a->centre[1] - from[1], a->centre[2] - from[2]};
		float t = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
		t = t < 0.0f ? 0.0f : t > len ? len : t;
		const float c[3] = {from[0] + dir[0] * t - a->centre[0], from[1] + dir[1] * t - a->centre[1],
		                    from[2] + dir[2] * t - a->centre[2]};
		if (c[0] * c[0] + c[1] * c[1] + c[2] * c[2] > a->radius * a->radius)
			continue;
		web_down(a);
		n++;
	}
	return n;
}
