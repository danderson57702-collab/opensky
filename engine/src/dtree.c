#include "dtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "fileio.h"
#include "gamelog.h"
#include "items.h"
#include "quests.h"
#include "races.h"
#include "skills.h"
#include "talk.h"

#define MAX_TREES 1024    // (the quests' - the guilds' too - and everyone's everyday ones: 253 of those)
#define MAX_NODES 6144
#define MAX_VARIANTS 8192
#define MAX_RESPONSES 12288
#define MAX_LINKS 8192
#define MAX_TAIL 96
#define MAX_NAMES 8
#define PER_LINE 8 // (a reply's lines at most, said in one go)

typedef struct {
	char *cond, *rnam;
	int r0, rn, l0, ln;
} Variant;
typedef struct {
	char *topic, *prompt;
	int v0, vn;
} Node;
typedef struct {
	char key[48], speaker[48];
	int n0, nn, x0, xn;
} Tree;

static Tree trees[MAX_TREES];
static Node nodes[MAX_NODES];
static Variant variants[MAX_VARIANTS];
static char *r_path[MAX_RESPONSES], *r_text[MAX_RESPONSES];
static int links[MAX_LINKS];
static char *x_path[MAX_TAIL], *x_text[MAX_TAIL];
static int tree_count, node_count, variant_count, response_count, link_count, tail_count;
static struct { char alias[32], name[48]; } names[MAX_NAMES];
static int name_count;

// What's going on: the tree, who, the node being said, its variant; waiting on your answer; the tail
static int cur = -1, with = -1, at_node = -1, at_var = -1, waiting, tail_said, said_started;
static const char *reached;
static const char *reached_cond; // (... the reply given to it: its conditions, how many topics it links on to)
static int reached_links;
static char said_text[PER_LINE][320]; // (the lines with the names put in)
static const char *say_paths[PER_LINE], *say_texts[PER_LINE];

static int split(char *line, char **f, int max) { // (tab-separated, the last field keeping the rest)
	int n = 0;
	f[n++] = line;
	while (n < max) {
		char *tab = strchr(f[n - 1], '\t');
		if (!tab)
			break;
		*tab = 0, f[n++] = tab + 1;
	}
	return n;
}

static void load(const char *path);

void dtree_init(const char *pack_dir) { // (the quests' conversations, and everyone's everyday ones)
	char path[256];
	snprintf(path, sizeof(path), "%squests.txt", pack_dir);
	load(path);
	snprintf(path, sizeof(path), "%stopics.txt", pack_dir);
	load(path);
	game_log("dtree: %d conversations, %d answers, %d replies", tree_count, node_count, variant_count);
	if (tree_count >= MAX_TREES || node_count >= MAX_NODES || variant_count >= MAX_VARIANTS || link_count >= MAX_LINKS)
		game_log("dtree: FULL - conversations left out (raise MAX_*)"); // (they had been dropped without a word)
}

static void load(const char *path) {
	long size = 0;
	char *text = file_read_all(path, &size);
	if (!text)
		return;
	Tree *t = NULL;
	for (char *line = text, *end = text + size; line < end;) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		*nl = 0;
		char *f[7];
		if (line[0] == 'D' && line[1]) {
			const char kind = line[1];
			const int n = split(line, f, kind == 'R' ? 6 : kind == 'V' ? 6 : kind == 'N' ? 5 : kind == 'L' ? 5 : kind == 'X' ? 4 : 3);
			if (kind == 'T' && n >= 3 && tree_count < MAX_TREES) {
				t = &trees[tree_count++];
				snprintf(t->key, sizeof(t->key), "%s", f[1]);
				snprintf(t->speaker, sizeof(t->speaker), "%s", f[2]);
				t->n0 = node_count, t->x0 = tail_count;
			} else if (!t || strcmp(f[1], t->key)) {
			} else if (kind == 'N' && n >= 5 && node_count < MAX_NODES && atoi(f[2]) == t->nn) {
				Node *nd = &nodes[node_count++];
				nd->topic = strdup(f[3]), nd->prompt = strdup(f[4]), nd->v0 = variant_count, nd->vn = 0;
				t->nn++;
			} else if (kind == 'V' && n >= 5 && variant_count < MAX_VARIANTS && t->nn > 0) {
				Variant *v = &variants[variant_count++];
				v->cond = strdup(f[4]), v->rnam = strdup(n >= 6 ? f[5] : "");
				v->r0 = response_count, v->rn = 0, v->l0 = link_count, v->ln = 0;
				nodes[node_count - 1].vn++;
			} else if (kind == 'R' && n >= 6 && response_count < MAX_RESPONSES && variant_count > 0) {
				r_path[response_count] = strdup(f[4]), r_text[response_count] = strdup(f[5]), response_count++;
				variants[variant_count - 1].rn++;
			} else if (kind == 'L' && n >= 5 && link_count < MAX_LINKS && variant_count > 0) {
				links[link_count++] = atoi(f[4]);
				variants[variant_count - 1].ln++;
			} else if (kind == 'X' && n >= 4 && tail_count < MAX_TAIL) {
				x_path[tail_count] = strdup(f[2]), x_text[tail_count] = strdup(f[3]), tail_count++;
				t->xn++;
			}
		}
		line = nl + 1;
	}
	free(text);
}

int dtree_everyday(const char *speaker, int *out, int max) {
	int n = 0;
	for (int i = 0; i < tree_count && n < max; i++)
		if (!strncmp(trees[i].key, "npc_", 4) && !strcmp(trees[i].speaker, speaker) && trees[i].nn > 0 &&
		    nodes[trees[i].n0].prompt[0])
			out[n++] = i;
	return n;
}

const char *dtree_prompt(int tree) {
	return tree >= 0 && tree < tree_count && trees[tree].nn > 0 ? nodes[trees[tree].n0].prompt : "";
}

const char *dtree_speaker(int tree) {
	return tree >= 0 && tree < tree_count ? trees[tree].speaker : "";
}

int dtree_find(const char *key) {
	for (int i = 0; i < tree_count; i++)
		if (!strcmp(trees[i].key, key))
			return i;
	return -1;
}

void dtree_set_name(const char *alias, const char *name) {
	for (int i = 0; i < name_count; i++)
		if (!strcmp(names[i].alias, alias)) {
			snprintf(names[i].name, sizeof(names[i].name), "%s", name);
			return;
		}
	if (name_count < MAX_NAMES) {
		snprintf(names[name_count].alias, sizeof(names[0].alias), "%s", alias);
		snprintf(names[name_count++].name, sizeof(names[0].name), "%s", name);
	}
}

// The text with its aliases' names in ("{riverwoodfriend} is afraid...": Gerdur)
static void fill(char *out, size_t size, const char *in) {
	size_t o = 0;
	while (*in && o + 1 < size) {
		const char *close = *in == '{' ? strchr(in, '}') : NULL;
		if (close) {
			const char *name = "";
			for (int i = 0; i < name_count; i++)
				if ((size_t)(close - in - 1) == strlen(names[i].alias) && !strncmp(in + 1, names[i].alias, close - in - 1))
					name = names[i].name;
			o += snprintf(out + o, size - o, "%s", name);
			in = close + 1;
			continue;
		}
		out[o++] = *in++;
	}
	out[o < size ? o : size - 1] = 0;
}

// A reply's conditions as the engine can decide them (its speaker, the player's sex and race, an
// item held - "72:0:1:balgruufthegreater;47:3:1:df202"); others aren't the engine's to know: passed
static int passes(const char *cond, int actor) {
	if (!cond || !cond[0] || !strcmp(cond, "-"))
		return 1;
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", cond);
	int in_or = 0, or_ok = 0; // (Skyrim's ORs: a run of them, then the next, one group - any of it)
	char *save = NULL;
	for (char *c = strtok_r(buf, ";", &save); c; c = strtok_r(NULL, ";", &save)) {
		int fn = 0, op = 0;
		float comp = 0.0f;
		char param[48] = "";
		const int orr = c[0] == 'o';
		if (sscanf(c + orr, "%d:%d:%f:%47s", &fn, &op, &comp, param) < 3)
			continue;
		float value = comp;
		if (fn == 72) // (GetIsID: the one speaking is this one)
			value = actor >= 0 && !strcmp(actors_id(actor), param) ? 1.0f : 0.0f;
		else if (fn == 131) // (GetPCIsSex)
			value = (actors_player_female() ? 1 : 0) == atoi(param) ? 1.0f : 0.0f;
		else if (fn == 130) { // (GetPCIsRace)
			const Race *r = races_get(races_player());
			value = r && !strcmp(r->eid, param) ? 1.0f : 0.0f;
		} else if (fn == 47) { // (GetItemCount: the Dragonstone on you)
			const int it = items_by_form((uint32_t)strtoul(param, NULL, 16));
			value = it >= 0 ? (float)items_have(it) : 0.0f;
		} else if (fn == 14 && !strncmp(param, "av", 2)) { // (GetActorValue on you: a skill - Speech against SpeechEasy)
			const int sk = atoi(param + 2) - 6; // (Skyrim's skills: actor values 6 One-handed .. 23 Enchanting)
			value = sk >= 0 && sk < SKILL_COUNT ? (float)skills_state()->level[sk] : 0.0f;
		} else if (fn == 182) { // (GetEquipped: a thing of a list worn - an Amulet of Articulation)
			value = quests_list_worn(param) ? 1.0f : 0.0f;
		} else if (fn == 403 || fn == 638) { // (GetRelationshipRank; IsInFriendStateWithPlayer)
			const int rank = quests_relationship(actor);
			value = fn == 403 ? (float)rank : rank >= 1 ? 1.0f : 0.0f;
		} else {
			continue;
		}
		int ok = op == 0 ? value == comp : op == 1 ? value != comp : op == 2 ? value > comp : op == 3 ? value >= comp
		       : op == 4 ? value < comp : value <= comp;
		if (orr) {
			in_or = 1, or_ok |= ok;
			continue;
		}
		if (in_or)
			ok |= or_ok, in_or = 0, or_ok = 0;
		if (!ok)
			return 0;
	}
	return !in_or || or_ok;
}

static int pick(int node) { // (its first reply that passes, as Skyrim's: -1 if none)
	const Node *nd = &nodes[node];
	for (int v = nd->v0; v < nd->v0 + nd->vn; v++)
		if (passes(variants[v].cond, with))
			return v;
	return -1;
}

static void say(const char *const *paths, const char *const *texts, int n) {
	n = n < PER_LINE ? n : PER_LINE;
	for (int i = 0; i < n; i++) {
		fill(said_text[i], sizeof(said_text[i]), texts[i]);
		say_paths[i] = paths[i], say_texts[i] = said_text[i];
	}
	said_started = n > 0 && talk_scene(with, say_paths, say_texts, n);
}

static void finish(void) {
	game_log("dtree: %s done", cur >= 0 ? trees[cur].key : "?");
	cur = -1, with = -1, at_node = at_var = -1, waiting = 0;
}

static void go(int node) {
	const Tree *t = &trees[cur];
	at_node = t->n0 + node, at_var = pick(at_node), waiting = 0;
	reached = nodes[at_node].topic;
	reached_cond = at_var >= 0 ? variants[at_var].cond : "", reached_links = at_var >= 0 ? variants[at_var].ln : 0;
	game_log("dtree: %s - %s (reply %d)", t->key, nodes[at_node].topic, at_var);
	if (at_var < 0) { // (nothing they'd say to it: on to the end)
		waiting = -1;
		return;
	}
	const Variant *v = &variants[at_var];
	say((const char *const *)&r_path[v->r0], (const char *const *)&r_text[v->r0], v->rn);
}

int dtree_start(int tree, int actor) {
	if (tree < 0 || tree >= tree_count || actor < 0)
		return 0;
	cur = tree, with = actor, tail_said = 0;
	go(0);
	return 1;
}

static int answers(int out[], int max) { // (the answers offered now: the reply's linked topics that have something said back)
	if (at_var < 0)
		return 0;
	const Variant *v = &variants[at_var];
	int n = 0;
	for (int k = 0; k < v->ln && n < max; k++) {
		const int node = trees[cur].n0 + links[v->l0 + k];
		if (links[v->l0 + k] < trees[cur].nn && pick(node) >= 0 && nodes[node].prompt[0])
			out[n++] = node;
	}
	return n;
}

// A linked topic with no line of yours (Skyrim's said straight on, no choice: INFO 000D2327
// after Gerdur's believed you): the first such that has a reply, or -1
static int straight_on(void) {
	if (at_var < 0)
		return -1;
	const Variant *v = &variants[at_var];
	for (int k = 0; k < v->ln; k++) {
		const int node = trees[cur].n0 + links[v->l0 + k];
		if (links[v->l0 + k] < trees[cur].nn && !nodes[node].prompt[0] && pick(node) >= 0)
			return links[v->l0 + k];
	}
	return -1;
}

void dtree_update(void) {
	if (cur < 0 || talk_speaking() >= 0)
		return;
	int got[16];
	if (waiting == 0) {
		const int on = straight_on();
		if (on >= 0) {
			go(on);
			return;
		}
	}
	if (waiting == 0 && answers(got, 16) > 0) { // (said: your answer now)
		waiting = 1;
		return;
	}
	if (waiting == 1)
		return;
	if (!tail_said && trees[cur].xn > 0) { // (the end of it: what they've still to say)
		tail_said = 1;
		say((const char *const *)&x_path[trees[cur].x0], (const char *const *)&x_text[trees[cur].x0], trees[cur].xn);
		waiting = 2;
		return;
	}
	finish();
}

int dtree_busy(void) {
	return cur >= 0;
}

int dtree_actor(void) {
	return with;
}

int dtree_choices(const char **opts, int max) {
	if (cur < 0 || waiting != 1)
		return 0;
	int got[16];
	const int n = answers(got, max < 16 ? max : 16);
	static char prompts[16][200];
	for (int i = 0; i < n; i++) {
		const Variant *v = &variants[pick(got[i])];
		fill(prompts[i], sizeof(prompts[i]), v->rnam[0] ? v->rnam : nodes[got[i]].prompt); // (a reply's own wording first)
		opts[i] = prompts[i];
	}
	return n;
}

void dtree_choose(int choice) {
	if (cur < 0 || waiting != 1)
		return;
	int got[16];
	const int n = answers(got, 16);
	if (choice < 0 || choice >= n) { // (Goodbye: what they had left to say, then done)
		waiting = 3;
		return;
	}
	go(got[choice] - trees[cur].n0);
}

const char *dtree_reached_reply(int *links) {
	*links = reached_links;
	return reached_cond ? reached_cond : "";
}

const char *dtree_take_reached(void) {
	const char *r = reached;
	reached = NULL;
	return r;
}
