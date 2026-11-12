#include "perks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "equip.h"
#include "fileio.h"
#include "items.h"
#include "skills.h"

#define MAX_EFFECTS 320
#define MAX_CONDS 6
#define MAX_ABILITIES 32
#define FIRST_SKILL_AV 6 // One-handed's actor value

// Skyrim's condition functions the entries ask (others never hold)
enum {
	F_GET_LOCK_LEVEL = 65, F_GET_BASE_AV = 277, F_HAS_PERK = 448, F_HAS_KEYWORD = 560, F_WORN_HAS_KEYWORD = 682,
	F_SPELL_HAS_KEYWORD = 693, F_SPELL_HAS_SKILL = 696, F_IS_ATTACK_TYPE = 697, F_SPELL_CASTING_PERK = 713,
	F_WORN_KEYWORD_COUNT = 722, F_GET_RANDOM_PERCENT = 77, F_IS_BLOCKING = 569, F_GET_IS_ID = 72, F_GET_SLEEPING = 49
};

typedef struct {
	uint8_t tab, op, or_next;
	uint16_t func;
	float value;
	uint32_t param;
} Cond;

typedef struct {
	int perk, entry, function;
	float value;
	int cond_count;
	Cond conds[MAX_CONDS];
} Effect;

typedef struct {
	uint32_t form;
	int level, need_count, needs[4];
	char name[48];
	char *desc;
} Perk;

typedef struct {
	float x, y;
	int link_count, links[8];
	int rank_count, ranks[PERK_RANKS_MAX];
	int used;
} Node;

static Perk perk_list[PERKS_MAX];
static int perk_count;
static Node nodes[SKILL_COUNT][PERK_NODES_MAX];
static int node_count[SKILL_COUNT];
static Effect effects[MAX_EFFECTS];
static int effect_count;
static struct {
	int perk, av;
	float value;
} abilities[MAX_ABILITIES];
static int ability_count;
static uint8_t taken[PERKS_MAX];
static int taken_count;

void perks_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sperks.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	char *line = buf;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		int skill, node, perk, n;
		if (line[0] == 'N' && sscanf(line + 2, "%d %d%n", &skill, &node, &n) == 2 && skill >= 0 && skill < SKILL_COUNT &&
		    node >= 0 && node < PERK_NODES_MAX) {
			Node *nd = &nodes[skill][node];
			char *p = line + 2 + n;
			float x, y;
			int k;
			if (sscanf(p, "%f %f%n", &x, &y, &k) == 2) {
				nd->x = x, nd->y = y, nd->used = 1;
				p += k;
				int link;
				while (nd->link_count < 8 && sscanf(p, "%d%n", &link, &k) == 1)
					nd->links[nd->link_count++] = link, p += k;
				if (node + 1 > node_count[skill])
					node_count[skill] = node + 1;
			}
		} else if (line[0] == 'R' && sscanf(line + 2, "%d %d %d", &skill, &node, &perk) == 3 && skill >= 0 &&
		           skill < SKILL_COUNT && node >= 0 && node < PERK_NODES_MAX) {
			Node *nd = &nodes[skill][node];
			if (nd->rank_count < PERK_RANKS_MAX)
				nd->ranks[nd->rank_count++] = perk;
		} else if (line[0] == 'P' && sscanf(line + 2, "%d", &perk) == 1 && perk >= 0 && perk < PERKS_MAX) {
			Perk *pk = &perk_list[perk];
			unsigned form;
			sscanf(line + 2, "%*d %x %d", &form, &pk->level);
			pk->form = form;
			char *a = strchr(line, '|'), *b = a ? strchr(a + 1, '|') : NULL;
			if (a && b) {
				snprintf(pk->name, sizeof(pk->name), "%.*s", (int)(b - a - 1), a + 1);
				pk->desc = strdup(b + 1);
			}
			if (perk + 1 > perk_count)
				perk_count = perk + 1;
		} else if (line[0] == 'Q' && sscanf(line + 2, "%d %d", &perk, &n) == 2 && perk >= 0 && perk < PERKS_MAX) {
			Perk *pk = &perk_list[perk];
			if (pk->need_count < 4)
				pk->needs[pk->need_count++] = n;
		} else if (line[0] == 'E' && effect_count < MAX_EFFECTS) {
			Effect *e = &effects[effect_count];
			memset(e, 0, sizeof(*e));
			int k;
			if (sscanf(line + 2, "%d %d %d %f%n", &e->perk, &e->entry, &e->function, &e->value, &k) == 4) {
				char *p = line + 2 + k;
				int tab, func, op, orf, used;
				float value;
				unsigned param;
				while (e->cond_count < MAX_CONDS &&
				       sscanf(p, " %d:%d:%d:%f:%x:%d%n", &tab, &func, &op, &value, &param, &orf, &used) == 6) {
					Cond *c = &e->conds[e->cond_count++];
					c->tab = (uint8_t)tab, c->func = (uint16_t)func, c->op = (uint8_t)op, c->value = value;
					c->param = param, c->or_next = (uint8_t)orf;
					p += used;
				}
				effect_count++;
			}
		} else if (line[0] == 'A' && ability_count < MAX_ABILITIES) {
			if (sscanf(line + 2, "%d %d %f", &abilities[ability_count].perk, &abilities[ability_count].av,
			           &abilities[ability_count].value) == 3)
				ability_count++;
		}
		line = next;
	}
	free(buf);
}

void perks_reset(void) {
	memset(taken, 0, sizeof(taken));
	taken_count = 0;
}

int perks_node_count(int skill) {
	return skill >= 0 && skill < SKILL_COUNT ? node_count[skill] : 0;
}

int perks_node(int skill, int node, float *x, float *y, const int **links, int *link_count, const int **ranks,
               int *rank_count) {
	if (skill < 0 || skill >= SKILL_COUNT || node < 0 || node >= node_count[skill] || !nodes[skill][node].used)
		return 0;
	const Node *nd = &nodes[skill][node];
	*x = nd->x, *y = nd->y;
	*links = nd->links, *link_count = nd->link_count;
	*ranks = nd->ranks, *rank_count = nd->rank_count;
	return 1;
}

const char *perks_name(int perk) {
	return perk >= 0 && perk < perk_count ? perk_list[perk].name : "";
}

const char *perks_description(int perk) {
	return perk >= 0 && perk < perk_count && perk_list[perk].desc ? perk_list[perk].desc : "";
}

int perks_level(int perk) {
	return perk >= 0 && perk < perk_count ? perk_list[perk].level : 0;
}

int perks_has(int perk) {
	return perk >= 0 && perk < PERKS_MAX && taken[perk];
}

int perks_taken_count(void) {
	return taken_count;
}

int perks_node_taken(int skill, int node) {
	if (skill < 0 || skill >= SKILL_COUNT || node < 0 || node >= PERK_NODES_MAX)
		return 0;
	const Node *nd = &nodes[skill][node];
	int n = 0;
	while (n < nd->rank_count && perks_has(nd->ranks[n]))
		n++;
	return n;
}

// Whether a node leading here has a perk taken (or is the root): Skyrim's menu needs one
static int reachable(int skill, int node) {
	for (int i = 0; i < node_count[skill]; i++) {
		const Node *nd = &nodes[skill][i];
		for (int k = 0; k < nd->link_count; k++)
			if (nd->links[k] == node && (nd->rank_count == 0 || perks_node_taken(skill, i) > 0))
				return 1;
	}
	return 0;
}

int perks_can_take(int skill, int node, char *why, int why_len) {
	if (skill < 0 || skill >= SKILL_COUNT || node < 0 || node >= node_count[skill]) {
		snprintf(why, why_len, "%s", "");
		return 0;
	}
	const Node *nd = &nodes[skill][node];
	const int have = perks_node_taken(skill, node);
	if (have >= nd->rank_count) {
		snprintf(why, why_len, "%s", nd->rank_count ? "Taken" : "");
		return 0;
	}
	const Perk *pk = &perk_list[nd->ranks[have]];
	if (skills_state()->level[skill] < pk->level) {
		snprintf(why, why_len, "Requires %s %d", skills_name(skill), pk->level);
		return 0;
	}
	if (!reachable(skill, node)) {
		snprintf(why, why_len, "%s", "Requires a perk that leads to it");
		return 0;
	}
	for (int k = 0; k < pk->need_count; k++)
		if (!perks_has(pk->needs[k]) && pk->needs[k] != nd->ranks[have]) {
			// (a rank needs the rank before it: that's this node's own)
			int own = 0;
			for (int r = 0; r < have; r++)
				own |= nd->ranks[r] == pk->needs[k];
			if (!own) {
				snprintf(why, why_len, "Requires %s", perks_name(pk->needs[k]));
				return 0;
			}
		}
	if (skills_state()->perk_points <= 0) {
		snprintf(why, why_len, "%s", "No perk points: gain a level");
		return 0;
	}
	snprintf(why, why_len, "%s", "");
	return 1;
}

int perks_grant_named(const char *name) { // (a test's: the perk taken, no points spent)
	for (int i = 0; i < perk_count; i++)
		if (!strcmp(perk_list[i].name, name) && !taken[i]) {
			taken[i] = 1, taken_count++;
			return i;
		}
	return -1;
}

int perks_take(int skill, int node) {
	char why[64];
	if (!perks_can_take(skill, node, why, sizeof(why)))
		return 0;
	const int perk = nodes[skill][node].ranks[perks_node_taken(skill, node)];
	taken[perk] = 1, taken_count++;
	skills_state()->perk_points--;
	return 1;
}

// --- Effects ---------------------------------------------------------------------------

static int perk_by_form(uint32_t form);

int perks_has_form(uint32_t form) {
	return perks_has(perk_by_form(form));
}

static int perk_by_form(uint32_t form) {
	for (int i = 0; i < perk_count; i++)
		if (perk_list[i].form == form)
			return i;
	return -1;
}

static int has_keyword(const uint32_t *kw, int n, uint32_t want) {
	for (int i = 0; i < n; i++)
		if (kw[i] == want)
			return 1;
	return 0;
}

// How many worn pieces have the keyword
static int worn_with(uint32_t want) {
	uint32_t forms[32];
	const int n = equip_save(forms, 32);
	int count = 0;
	for (int i = 0; i < n; i++) {
		const uint32_t *kw;
		const int item = items_by_form(forms[i]), nk = items_keywords(item, &kw);
		count += has_keyword(kw, nk, want);
	}
	return count;
}

static float cond_value(const Cond *c, const PerkContext *ctx) {
	switch (c->func) {
	case F_GET_BASE_AV:
		return c->param >= FIRST_SKILL_AV && c->param < FIRST_SKILL_AV + SKILL_COUNT
		       ? (float)skills_state()->level[c->param - FIRST_SKILL_AV] : 0.0f;
	case F_HAS_PERK:
		return (float)perks_has(perk_by_form(c->param));
	case F_HAS_KEYWORD:
	case F_SPELL_HAS_KEYWORD:
		return c->tab == 1 && ctx ? (float)has_keyword(ctx->keywords, ctx->keyword_count, c->param) : 0.0f;
	case F_WORN_HAS_KEYWORD:
		return worn_with(c->param) > 0;
	case F_WORN_KEYWORD_COUNT:
		return (float)worn_with(c->param);
	case F_SPELL_HAS_SKILL:
		return ctx && ctx->school == (int)c->param;
	case F_SPELL_CASTING_PERK:
		return ctx && ctx->casting_perk == c->param;
	case F_GET_LOCK_LEVEL:
		return ctx && ctx->lock_known ? (float)ctx->lock_level : NAN;
	case F_IS_ATTACK_TYPE:
		return ctx && ctx->power_attack == c->param;
	case F_GET_RANDOM_PERCENT: // (a chance: Bullseye's 15)
		return (float)(rand() % 100);
	case F_IS_BLOCKING:
		return ctx ? (float)ctx->blocking : 0.0f;
	case F_GET_IS_ID: // (the item it's about: Cutpurse's gold)
		return c->tab >= 1 && ctx ? (float)(ctx->item_form == c->param) : NAN;
	case F_GET_SLEEPING: // (the target asleep: Skyrim's 3)
		return c->tab >= 1 && ctx ? (ctx->target_asleep ? 3.0f : 0.0f) : NAN;
	}
	return NAN; // (not modelled: never holds)
}

static int compare(float v, int op, float want) {
	if (isnan(v))
		return 0;
	switch (op) {
	case 0: return v == want;
	case 1: return v != want;
	case 2: return v > want;
	case 3: return v >= want;
	case 4: return v < want;
	case 5: return v <= want;
	}
	return 0;
}

// Skyrim's condition lists: OR binds a condition to the next, the groups are ANDed
static int conditions_hold(const Effect *e, const PerkContext *ctx) {
	int result = 1, any = 0, in_or = 0;
	for (int i = 0; i < e->cond_count; i++) {
		const Cond *c = &e->conds[i];
		const int v = compare(cond_value(c, ctx), c->op, c->value);
		if (c->or_next) {
			any |= v, in_or = 1;
			continue;
		}
		result &= in_or ? (any | v) : v;
		any = in_or = 0;
	}
	if (in_or)
		result &= any;
	return result;
}

float perks_apply(int entry_point, float value, const PerkContext *ctx) {
	if (!taken_count)
		return value;
	float set = NAN, mult = 1.0f, add = 0.0f;
	for (int i = 0; i < effect_count; i++) {
		const Effect *e = &effects[i];
		if (e->entry != entry_point || !perks_has(e->perk) || !conditions_hold(e, ctx))
			continue;
		if (e->function == 1)
			set = e->value;
		else if (e->function == 2)
			add += e->value;
		else if (e->function == 3)
			mult *= e->value;
	}
	return (isnan(set) ? value : set) * mult + add;
}

float perks_av(int av) {
	float sum = 0.0f;
	for (int i = 0; i < ability_count; i++)
		if (abilities[i].av == av && perks_has(abilities[i].perk))
			sum += abilities[i].value;
	return sum;
}

void perks_save(uint8_t bits[PERKS_MAX / 8]) {
	memset(bits, 0, PERKS_MAX / 8);
	for (int i = 0; i < PERKS_MAX; i++)
		if (taken[i])
			bits[i / 8] |= (uint8_t)(1 << (i % 8));
}

void perks_load(const uint8_t bits[PERKS_MAX / 8]) {
	perks_reset();
	for (int i = 0; i < PERKS_MAX && i < perk_count; i++)
		if (bits[i / 8] & (1 << (i % 8)))
			taken[i] = 1, taken_count++;
}
