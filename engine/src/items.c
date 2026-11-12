#include "items.h"
#include "races.h"
#include "perks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effects.h"
#include "fileio.h"
#include "gamelog.h"
#include "skills.h"
#include "gametext.h"

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#define items_log sceClibPrintf
#define ITEMS_SAVE "ux0:data/opensky_items.dat"
#else
#define items_log printf
#define ITEMS_SAVE "opensky_items.dat"
#endif

#define GOLD_FORM 0x0000000Fu   // Gold001
#define MAX_ENTRIES 48          // per leveled list / container record
#define MAX_CONTAINERS 512      // opened (rolled) containers remembered
#define CONTAINER_SLOTS 128     // different items in one container (40 till a merchant's chest - Lucan's rolls more kinds than that before its gold: they had none to pay you with)
#define MAX_TAKEN 4096          // loose things taken
#define LIST_DEPTH 8
#define SAVE_MAGIC "SKIT"
#define SAVE_VERSION 12 // (12: plants harvested, and when - they grow back; (11: as 10 - but 10's were made while every container opened empty: those forgotten on loading; (10: enchanted weapons' charge spent; (9: the stolen goods carried; (8: a made thing's damage or armour and improvement; (7: enchanted things made - their item - and the enchantments learned; (1: before lockpicking: no unlocked list; loading one gives the starting lockpicks;
                       //  2: before equipment: no worn list; 3: before storing and dropping; 4: before
                       //  alchemy: no potions made, no ingredient effects known; 5: before resets:
                       //  no times the containers were rolled)
#define CREATED_FORM 0xFD000000u // (potions made: this | their number)
#define CORPSE_REFS 0xFD000000u  // (bodies' containers: creatures.c's CORPSE_REF | its spawn point)
#define MAX_DROPPED 256
#define DROPPED_REF 0xFE000000u // (dropped things' made-up references: this | their number)
#define MAX_WORN 64
#define LOCKPICK_FORM 0x0000000Au
#define STARTING_LOCKPICKS 50 // (for trying lockpicking)
#define AV_HEALTH 24
#define AV_MAGICKA 25
#define AV_STAMINA 26

typedef struct {
	uint32_t form;
	uint8_t type;
	float weight;
	int value;
	char name[48];
	uint8_t food, potion, poison;
	uint8_t enchanted; // (a weapon's or armour's effects: its enchantment)
	int made_from;     // (one enchanted or improved here: the item it was, + 1; 0 none)
	int8_t temper;     // improved at a grindstone or workbench: 1 Fine .. 6 Legendary (0 not)
	int8_t effect_count;
	int stat;  // a weapon's damage, armour's rating
	int model; // objects model index (the inventory's 3D view), -1
	int flight_model;           // arrows: the model in flight (-1), speed (m/s), gravity (x g)
	float flight_speed, flight_gravity;
	uint8_t keyword_count;
	uint32_t keywords[12]; // (perks ask about weapons and armour; merchants buy by them)
	struct {
		int16_t av, mgef; // (mgef: its magic effect, -1 an older pack's)
		uint8_t harmful;
		int8_t resist;    // (an enchantment's: the actor value resisting it - 41 fire, 42 shock, 43 frost; -1)
		float magnitude, seconds;
	} effects[ITEM_EFFECTS_MAX];
	ItemScroll scroll; // (scrolls)
	char *spell;       // (books: the spell a tome teaches, "<editor id> <name>"; NULL)
	int charge_max;    // an enchanted weapon's charge (Skyrim's EAMT; 0 none), what a strike costs
	float charge_cost, charge_used; // (its ENCH), and how much is spent (saved)
} Item;

static char items_pack[128];

typedef struct {
	uint8_t kind; // 'I' item, 'L' list
	uint16_t level;
	int index, count;
} Entry;

typedef struct {
	int flags, chance_none, n;
	Entry *e;
	char global[40]; // (its chance of nothing: this global's, as the game sets it - list_chance_none)
} List;

typedef struct {
	char name[48];
	int n;
	Entry *e;
} ContainerDef;

typedef struct {
	uint32_t ref;
	int n;
	int kept; // the player put something in: never forgotten (a chest at home)
	float opened_at; // game hours when it was rolled (it resets 10 days after)
	int item[CONTAINER_SLOTS], count[CONTAINER_SLOTS];
} Opened;

typedef struct {
	uint32_t ref;
	int item, count, place;
	float pos[3], yaw;
} Dropped;

static Item *items;
static int item_count;  // (with the potions made: base_count + created_count)
static int base_count, created_count;
static uint8_t *known;  // per item: ingredient effects known (bits)
static MagicEffect *mgefs;
static int mgef_count;
static List *lists;
static int list_count;
static ContainerDef *defs;
static int def_count;
static int *carried; // per item
static Opened opened[MAX_CONTAINERS];
static int opened_count;
static uint32_t taken[MAX_TAKEN];
#define MAX_HARVESTED 2048
#define REGROW_HOURS 240.0f // (Skyrim's plants: back in ten days)
static uint32_t harvested[MAX_HARVESTED];
static float harvested_at[MAX_HARVESTED]; // (game hours)
static int harvested_count;
static int taken_count;
static uint32_t unlocked[MAX_TAKEN];
static int unlocked_count;
static uint32_t worn[MAX_WORN];
static int worn_count;
static int gold_item = -1;
static int *stolen;                     // of what's carried, how many are stolen (Skyrim's mark: fences buy them)
static int theft_handle = -1, theft_kind; // the container being looked through, if it's someone's (items_set_theft)
static int theft_value;                 // what was stolen from it since last asked
static void kept_stolen(int item);
static Dropped dropped[MAX_DROPPED];
static int dropped_count;
static uint32_t next_drop = 1;
#define MAX_POCKETS 1024
static struct {
	char id[48];
	int container;
} pockets[MAX_POCKETS]; // NPCs' inventories (pickpocketing)
static int pocket_count;
#define MAX_VENDORS 256 // (the whole map: 133)
#define VENDOR_KEYWORDS 24
static struct {
	char id[48];
	int container, start, end, flags; // (flags: 1 buys all but the keywords' things, 2 stolen goods only)
	uint32_t kw[VENDOR_KEYWORDS];
	int nkw;
} vendors[MAX_VENDORS]; // merchants (Skyrim's vendor factions)
static int vendor_count;
#define INVEST_GOLD 500
static uint32_t invested[(MAX_VENDORS + 31) / 32]; // (Investor's stores: items_vendor_invest)
static float invest_rolled[MAX_VENDORS];           // (... the stock its 500 was last added to: when it was rolled)
#define MAX_RECIPES 1200
#define RECIPE_PARTS 6
typedef struct {
	uint8_t station, nparts, nperks;
	int made, count;
	int part[RECIPE_PARTS], need[RECIPE_PARTS];
	uint32_t perk[2];
} Recipe;
static Recipe *recipes; // Skyrim's (COBJ): what the forge, smelter, tanning rack and cooking pot make
static int recipe_count;
// Enchantments learned by disenchanting (the Arcane Enchanter's): the effect and its strength
#define MAX_LEARNED 64
static struct { int mgef; float magnitude, seconds; } learned[MAX_LEARNED];
static int learned_count;
#define MAX_RESOURCES 3072
typedef struct { uint32_t ref; int item, per_use, total, ntools, tool[6]; } Resource;
static Resource resources[MAX_RESOURCES];
static int resource_count; // ore veins and chopping blocks (Z lines)
#define VENDOR_REFS 0xFC000000u // (merchants' chests: this | the vendor)
#define VENDOR_RESTOCK_H 48.0f  // Skyrim's iDaysToRespawnVendor
#define BARTER_MIN 2.0f         // fBarterMin, fBarterMax: the price factor from Speech 100 to 0
#define BARTER_MAX 3.3f
static unsigned rng = 0x9e3779b9u;

static const char *type_names[IT_TYPES] = {"MISC", "WEAP", "ARMO", "ALCH", "INGR", "BOOK", "KEYM", "AMMO", "SLGM", "SCRL", "LIGH"};

static int parse_entries(char *s, Entry *out, int with_level) {
	int n = 0;
	for (char *tok = strtok(s, " "); tok && n < MAX_ENTRIES; tok = strtok(NULL, " ")) {
		Entry e = {0};
		char kind;
		int level = 0;
		if (with_level ? sscanf(tok, "%d:%c:%d:%d", &level, &kind, &e.index, &e.count) == 4
		               : sscanf(tok, "%c:%d:%d", &kind, &e.index, &e.count) == 3) {
			e.kind = (uint8_t)kind, e.level = (uint16_t)level;
			out[n++] = e;
		}
	}
	return n;
}

static char *field(char **p) { // the next tab-separated field
	char *s = *p;
	if (!s)
		return "";
	char *t = strchr(s, '\t');
	if (t)
		*t = 0, *p = t + 1;
	else
		*p = NULL;
	return s;
}

void items_init(const char *pack_dir) {
	snprintf(items_pack, sizeof(items_pack), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sitems/items.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	char *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		items_log("items: no %s\n", path);
		return;
	}
	buf[size] = 0;
	// Count records first
	int ni = 0, nl = 0, nc = 0, nm = 0;
	for (char *l = buf; *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : l + strlen(l))
		ni += l[0] == 'I', nl += l[0] == 'L', nc += l[0] == 'C', nm += l[0] == 'M';
	items = calloc(ni + ITEMS_CREATED_MAX, sizeof(Item));
	known = calloc(ni + ITEMS_CREATED_MAX, 1);
	mgefs = calloc(nm ? nm : 1, sizeof(MagicEffect));
	lists = calloc(nl ? nl : 1, sizeof(List));
	defs = calloc(nc ? nc : 1, sizeof(ContainerDef));
	carried = calloc(ni + ITEMS_CREATED_MAX, sizeof(int));
	stolen = calloc(ni + ITEMS_CREATED_MAX, sizeof(int));
	static Entry scratch[MAX_ENTRIES];
	char *line = buf;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		char *p = line;
		char *kind = field(&p);
		char *second = field(&p);
		int idx = atoi(second);
		if (kind[0] == 'I' && idx >= 0 && idx < ni) {
			Item *it = &items[idx];
			it->form = (uint32_t)strtoul(field(&p), NULL, 16);
			const char *type = field(&p);
			for (int t = 0; t < IT_TYPES; t++)
				if (!strcmp(type, type_names[t]))
					it->type = (uint8_t)t;
			it->weight = (float)atof(field(&p));
			it->value = atoi(field(&p));
			snprintf(it->name, sizeof(it->name), "%s", field(&p));
			char *extra = field(&p);
			it->model = p ? atoi(field(&p)) : -1; // (after extra: read before extra is split up)
			it->stat = atoi(extra);
			it->flight_model = -1;
			if (it->type == IT_BOOK) { // ("<skill av> <spell editor id> <spell name>": a tome's spell)
				const char *sp = strchr(extra, ' ');
				it->spell = sp && sp[1] && sp[1] != '-' ? strdup(sp + 1) : NULL;
			}
			if (it->type == IT_AMMO) {
				float dmg = 0.0f;
				if (sscanf(extra, "%f %f %f %d", &dmg, &it->flight_speed, &it->flight_gravity, &it->flight_model) >= 1)
					it->stat = (int)dmg;
			}
			if (it->type == IT_SCRL) {
				ItemScroll *sc = &it->scroll;
				snprintf(sc->kind, sizeof(sc->kind), "none");
				sscanf(extra, "%f %d %15s %f %f %f %31s", &sc->charge, &sc->delivery, sc->kind, &sc->magnitude, &sc->area,
				       &sc->seconds, sc->extra);
				if (!strcmp(sc->extra, "-"))
					sc->extra[0] = 0;
			}
			if (it->type == IT_ALCH || it->type == IT_INGR) {
				for (char *tok = strtok(extra, " "); tok; tok = strtok(NULL, " ")) {
					int av, harmful, mgef = -1;
					float mag, secs;
					if (!strcmp(tok, "food"))
						it->food = 1;
					else if (!strcmp(tok, "potion"))
						it->potion = 1;
					else if (!strcmp(tok, "poison"))
						it->poison = 1;
					else if (sscanf(tok, "%d:%f:%f:%d:%d", &av, &mag, &secs, &harmful, &mgef) >= 4 &&
					         it->effect_count < ITEM_EFFECTS_MAX) {
						it->effects[it->effect_count].av = (int16_t)av;
						it->effects[it->effect_count].mgef = (int16_t)mgef;
						it->effects[it->effect_count].magnitude = mag;
						it->effects[it->effect_count].seconds = secs;
						it->effects[it->effect_count].harmful = (uint8_t)harmful;
						it->effect_count++;
					}
				}
			}
			if (it->form == GOLD_FORM)
				gold_item = idx;
		} else if (kind[0] == 'M' && idx >= 0 && idx < nm) {
			MagicEffect *m = &mgefs[idx];
			snprintf(m->eid, sizeof(m->eid), "%s", field(&p));
			m->flags = (uint32_t)strtoul(field(&p), NULL, 16);
			m->cost = (float)atof(field(&p));
			m->archetype = atoi(field(&p));
			m->av = atoi(field(&p));
			m->keywords = atoi(field(&p));
			snprintf(m->name, sizeof(m->name), "%s", field(&p));
			m->desc = strdup(field(&p));
		} else if (kind[0] == 'G' && idx >= 0 && idx < nl) { // (a list's chance from a global)
			snprintf(lists[idx].global, sizeof(lists[idx].global), "%s", field(&p));
		} else if (kind[0] == 'L' && idx >= 0 && idx < nl) {
			List *li = &lists[idx];
			li->flags = atoi(field(&p));
			li->chance_none = atoi(field(&p));
			li->n = parse_entries(field(&p), scratch, 1);
			li->e = malloc(li->n * sizeof(Entry) + 1);
			if (li->e)
				memcpy(li->e, scratch, li->n * sizeof(Entry));
		} else if (kind[0] == 'K' && idx >= 0 && idx < ni) {
			Item *it = &items[idx];
			for (char *tok = strtok(field(&p), " "); tok && it->keyword_count < 12; tok = strtok(NULL, " "))
				it->keywords[it->keyword_count++] = (uint32_t)strtoul(tok, NULL, 16);
		} else if ((kind[0] == 'H' || (kind[0] == 'C' && p && !strchr(p, '\t'))) && idx >= 0 && idx < ni) {
			// (an enchanted weapon's charge: "capacity cost" - H; packs made 10-21 wrote these as C, three
			// fields, and every container's C line went here with them: all of them opened empty)
			sscanf(field(&p), "%d %f", &items[idx].charge_max, &items[idx].charge_cost);
		} else if (kind[0] == 'T' && idx >= 0 && idx < ni) { // (a staff's spell, as a scroll's)
			ItemScroll *sc = &items[idx].scroll;
			snprintf(sc->kind, sizeof(sc->kind), "none");
			sscanf(field(&p), "%f %d %15s %f %f %f %31s", &sc->charge, &sc->delivery, sc->kind, &sc->magnitude, &sc->area,
			       &sc->seconds, sc->extra);
			if (!strcmp(sc->extra, "-"))
				sc->extra[0] = 0;
		} else if (kind[0] == 'E' && idx >= 0 && idx < ni) { // (an enchantment: "av:mag:secs:harmful:m:resist" each)
			Item *it = &items[idx];
			for (char *tok = strtok(field(&p), " "); tok && it->effect_count < ITEM_EFFECTS_MAX; tok = strtok(NULL, " ")) {
				int av, harmful, mgef = -1, resist = -1;
				float mag, secs;
				if (sscanf(tok, "%d:%f:%f:%d:%d:%d", &av, &mag, &secs, &harmful, &mgef, &resist) < 5)
					continue;
				it->effects[it->effect_count].av = (int16_t)av, it->effects[it->effect_count].mgef = (int16_t)mgef;
				it->effects[it->effect_count].magnitude = mag, it->effects[it->effect_count].seconds = secs;
				it->effects[it->effect_count].harmful = (uint8_t)harmful;
				it->effects[it->effect_count].resist = (int8_t)resist;
				it->effect_count++;
			}
			it->enchanted = it->effect_count > 0;
		} else if (kind[0] == 'R' && recipe_count < MAX_RECIPES) { // (R: the second field is the station)
			if (!recipes)
				recipes = calloc(MAX_RECIPES, sizeof(*recipes));
			if (recipes) {
				Recipe *r = &recipes[recipe_count];
				memset(r, 0, sizeof(*r));
				r->station = (uint8_t)idx;
				r->made = atoi(field(&p));
				r->count = atoi(field(&p));
				char *parts = field(&p), *perks = field(&p);
				for (char *tok = strtok(parts, " "); tok && r->nparts < RECIPE_PARTS; tok = strtok(NULL, " "))
					if (sscanf(tok, "%d:%d", &r->part[r->nparts], &r->need[r->nparts]) == 2)
						r->nparts++;
				for (char *tok = strtok(perks, " "); tok && r->nperks < 2 && tok[0] != '-'; tok = strtok(NULL, " "))
					r->perk[r->nperks++] = (uint32_t)strtoul(tok, NULL, 16);
				if (r->made >= 0 && r->made < ni && r->nparts)
					recipe_count++;
			}
		} else if (kind[0] == 'Z' && resource_count < MAX_RESOURCES) { // (Z: the second field is the reference)
			Resource *r = &resources[resource_count];
			memset(r, 0, sizeof(*r));
			r->ref = (uint32_t)strtoul(second, NULL, 16);
			r->item = atoi(field(&p));
			r->per_use = atoi(field(&p));
			r->total = atoi(field(&p));
			for (char *tok = strtok(field(&p), " "); tok && r->ntools < 6 && tok[0] != '-'; tok = strtok(NULL, " "))
				r->tool[r->ntools++] = atoi(tok);
			if (r->item >= 0 && r->item < ni)
				resource_count++;
		} else if (kind[0] == 'V' && vendor_count < MAX_VENDORS) { // (V: the second field is the NPC's editor id)
			snprintf(vendors[vendor_count].id, sizeof(vendors[0].id), "%s", second);
			vendors[vendor_count].container = atoi(field(&p));
			vendors[vendor_count].start = atoi(field(&p));
			vendors[vendor_count].end = atoi(field(&p));
			vendors[vendor_count].flags = atoi(field(&p));
			for (char *tok = strtok(field(&p), " "); tok && vendors[vendor_count].nkw < VENDOR_KEYWORDS; tok = strtok(NULL, " "))
				vendors[vendor_count].kw[vendors[vendor_count].nkw++] = (uint32_t)strtoul(tok, NULL, 16);
			vendor_count++;
		} else if (kind[0] == 'N' && pocket_count < MAX_POCKETS) {
			// (N: the second field is the NPC's editor id)
			snprintf(pockets[pocket_count].id, sizeof(pockets[0].id), "%s", second);
			pockets[pocket_count++].container = atoi(field(&p));
		} else if (kind[0] == 'C' && idx >= 0 && idx < nc) {
			ContainerDef *c = &defs[idx];
			field(&p); // form id
			snprintf(c->name, sizeof(c->name), "%s", field(&p));
			c->n = parse_entries(field(&p), scratch, 0);
			c->e = malloc(c->n * sizeof(Entry) + 1);
			if (c->e)
				memcpy(c->e, scratch, c->n * sizeof(Entry));
		}
		line = next;
	}
	item_count = base_count = ni, list_count = nl, def_count = nc, mgef_count = nm;
	free(buf);
	items_log("items: %d items, %d leveled lists, %d containers, %d magic effects\n", item_count, list_count, def_count,
	          mgef_count);
}

const ItemScroll *items_staff(int item) {
	return item >= 0 && item < item_count && items[item].type == IT_WEAP && items[item].scroll.kind[0] &&
	       strcmp(items[item].scroll.kind, "none") != 0 ? &items[item].scroll : NULL;
}

const ItemScroll *items_scroll(int item) {
	return item >= 0 && item < item_count && items[item].type == IT_SCRL && strcmp(items[item].scroll.kind, "none") != 0
	           ? &items[item].scroll
	           : NULL;
}

int items_flight(int item, float *speed, float *gravity) {
	if (item < 0 || item >= item_count || items[item].type != IT_AMMO)
		return -1;
	*speed = items[item].flight_speed > 1.0f ? items[item].flight_speed : 50.0f;
	*gravity = items[item].flight_gravity;
	return items[item].flight_model;
}

int items_keywords(int item, const uint32_t **out) {
	if (item < 0 || item >= item_count) {
		*out = NULL;
		return 0;
	}
	*out = items[item].keywords;
	return items[item].keyword_count;
}

int items_count(void) {
	return item_count;
}

const char *items_name(int item) {
	return item >= 0 && item < item_count ? items[item].name : "";
}

int items_type(int item) {
	return item >= 0 && item < item_count ? items[item].type : IT_MISC;
}

float items_weight(int item) {
	return item >= 0 && item < item_count ? items[item].weight : 0.0f;
}

int items_value(int item) {
	return item >= 0 && item < item_count ? items[item].value : 0;
}

int items_category(int item) {
	if (item < 0 || item >= item_count)
		return CAT_MISC;
	switch (items[item].type) {
	case IT_WEAP: case IT_AMMO: return CAT_WEAPONS;
	case IT_ARMO: return CAT_APPAREL;
	case IT_ALCH: return items[item].food ? CAT_FOOD : CAT_POTIONS;
	case IT_SCRL: return CAT_SCROLLS;
	case IT_INGR: return CAT_INGREDIENTS;
	case IT_BOOK: return CAT_BOOKS;
	case IT_KEYM: return CAT_KEYS;
	default: return CAT_MISC;
	}
}

const char *items_category_name(int category) {
	static const char *names[CAT_COUNT] = {"All", "Weapons", "Apparel", T_MESG_0010D9AE_FULL0, "Scrolls", "Food", "Ingredients",
	                                       "Books", "Keys", "Misc"};
	return category >= 0 && category < CAT_COUNT ? names[category] : "";
}

float items_carry_max(const Vitals *v) {
	return 300.0f + (v->max_stamina - 100.0f) * 0.5f;
}

int items_model(int item) {
	return item >= 0 && item < item_count ? items[item].model : -1;
}

int items_stat(int item) {
	return item >= 0 && item < item_count ? items[item].stat : 0;
}

void items_describe(int item, char *out, int size) {
	out[0] = 0;
	if (item < 0 || item >= item_count)
		return;
	const Item *it = &items[item];
	int n = 0;
	if (it->type == IT_INGR) { // (Skyrim's card: its four effects, the unknown ones "Unknown")
		for (int e = 0; e < it->effect_count && n < size - 1; e++) {
			const MagicEffect *m = items_magic_effect(it->effects[e].mgef);
			n += snprintf(out + n, size - n, "%s%s", e ? ", " : "", m && (known[item] >> e & 1) ? m->name : "Unknown");
		}
		return;
	}
	for (int e = 0; e < it->effect_count && n < size - 1; e++) {
		if (it->effects[e].mgef >= 0) {
			char one[160];
			items_effect_text(it->effects[e].mgef, it->effects[e].magnitude, it->effects[e].seconds, one, sizeof(one));
			if (one[0])
				n += snprintf(out + n, size - n, "%s%s", n ? " " : "", one);
			continue;
		}
		const char *what = it->effects[e].av == AV_HEALTH ? "Health" : it->effects[e].av == AV_MAGICKA ? T_AVIF_000003E9_FULL0
		                 : it->effects[e].av == AV_STAMINA ? "Stamina" : NULL;
		if (!what)
			continue;
		const int mag = (int)(it->effects[e].magnitude + 0.5f), secs = (int)it->effects[e].seconds;
		if (secs > 1)
			n += snprintf(out + n, size - n, "%s%s %d points of %s per second for %d seconds.", n ? " " : "",
			              it->effects[e].harmful ? "Lose" : "Restore", mag, what, secs);
		else
			n += snprintf(out + n, size - n, "%s%s %d points of %s.", n ? " " : "",
			              it->effects[e].harmful ? "Lose" : "Restore", mag, what);
		if (n >= size)
			break;
	}
}

int items_magic_effect_count(void) {
	return mgef_count;
}

const MagicEffect *items_magic_effect(int mgef) {
	return mgef >= 0 && mgef < mgef_count ? &mgefs[mgef] : NULL;
}

int items_magic_effect_find(const char *eid) {
	for (int i = 0; i < mgef_count; i++)
		if (!strcmp(mgefs[i].eid, eid))
			return i;
	return -1;
}

int items_enchantment(int item, ItemEffect *out, int *resist, int max) {
	if (item < 0 || item >= item_count || !items[item].enchanted)
		return 0;
	const int n = items_effects(item, out, max);
	for (int e = 0; e < n && resist; e++)
		resist[e] = items[item].effects[e].resist;
	return n;
}

int items_effects(int item, ItemEffect *out, int max) {
	if (item < 0 || item >= item_count)
		return 0;
	int n = 0;
	for (int e = 0; e < items[item].effect_count && n < max; e++, n++) {
		out[n].mgef = items[item].effects[e].mgef;
		out[n].magnitude = items[item].effects[e].magnitude, out[n].seconds = items[item].effects[e].seconds;
	}
	return n;
}

void items_effect_text(int mgef, float magnitude, float seconds, char *out, int size) {
	out[0] = 0;
	const MagicEffect *m = items_magic_effect(mgef);
	if (!m || size < 2)
		return;
	if (!m->desc || !m->desc[0]) {
		snprintf(out, size, "%s.", m->name);
		return;
	}
	int n = 0; // (Skyrim's <mag> and <dur> filled in; other tags left out)
	for (const char *p = m->desc; *p && n < size - 1;) {
		if (*p == '<') {
			const char *close = strchr(p, '>');
			if (!close)
				break;
			if (!strncmp(p, "<mag>", 5))
				n += snprintf(out + n, size - n, "%d", (int)(magnitude + 0.5f));
			else if (!strncmp(p, "<dur>", 5))
				n += snprintf(out + n, size - n, "%d", (int)(seconds + 0.5f));
			p = close + 1;
			continue;
		}
		out[n++] = *p++;
	}
	if (n >= size)
		n = size - 1;
	out[n] = 0;
}

int items_known(int item) {
	return item >= 0 && item < item_count ? known[item] : 0;
}

void items_learn(int item, int bits) {
	if (item >= 0 && item < item_count)
		known[item] |= (uint8_t)bits;
}

int items_create_potion(const char *name, int poison, int value, const ItemEffect *e, int n) {
	if (n > ITEM_EFFECTS_MAX)
		n = ITEM_EFFECTS_MAX;
	for (int i = base_count; i < item_count; i++) { // (the same again: the same item)
		const Item *it = &items[i];
		int same = it->effect_count == n && it->poison == poison && !strcmp(it->name, name);
		for (int k = 0; same && k < n; k++)
			same = it->effects[k].mgef == e[k].mgef && fabsf(it->effects[k].magnitude - e[k].magnitude) < 0.01f &&
			       fabsf(it->effects[k].seconds - e[k].seconds) < 0.01f;
		if (same)
			return i;
	}
	if (created_count >= ITEMS_CREATED_MAX)
		return -1;
	const int idx = base_count + created_count++;
	item_count = base_count + created_count;
	Item *it = &items[idx];
	memset(it, 0, sizeof(*it));
	it->form = CREATED_FORM | (uint32_t)(created_count - 1);
	it->type = IT_ALCH, it->weight = 0.5f, it->value = value; // (Skyrim's made potions weigh half a pound)
	it->potion = !poison, it->poison = (uint8_t)poison;
	snprintf(it->name, sizeof(it->name), "%s", name);
	it->model = it->flight_model = -1;
	for (int k = 0; k < n; k++) {
		const MagicEffect *m = items_magic_effect(e[k].mgef);
		it->effects[k].mgef = (int16_t)e[k].mgef;
		it->effects[k].av = (int16_t)(m ? m->av : -1);
		it->effects[k].harmful = m && (m->flags & MGEF_DETRIMENTAL);
		it->effects[k].magnitude = e[k].magnitude, it->effects[k].seconds = e[k].seconds;
	}
	it->effect_count = (int8_t)n;
	// Its look: a bottle of Skyrim's with the same main effect (else any potion / poison's)
	for (int pass = 0; pass < 2 && it->model < 0; pass++)
		for (int i = 0; i < base_count; i++)
			if (items[i].type == IT_ALCH && !items[i].food && items[i].poison == poison && items[i].model >= 0 &&
			    (pass || (items[i].effect_count && items[i].effects[0].mgef == e[0].mgef))) {
				it->model = items[i].model;
				break;
			}
	return idx;
}

int items_usable(int item) {
	return item >= 0 && item < item_count &&
	       ((items[item].type == IT_ALCH && !items[item].poison) || items[item].type == IT_INGR);
}

// Skyrim.esm's pickpocket settings (over the game's defaults)
#define PP_ACTOR_SKILL_BASE 20.0f   // fPickPocketActorSkillBase
#define PP_ACTOR_SKILL_MULT 1.0f    // fPickPocketActorSkillMult
#define PP_TARGET_SKILL_BASE 20.0f  // fPickPocketTargetSkillBase
#define PP_TARGET_SKILL_MULT -0.25f // fPickPocketTargetSkillMult
#define PP_WEIGHT_MULT -4.0f        // fPickPocketWeightMult
#define PP_AMOUNT_MULT -0.1f        // fPickPocketAmountMult (per gold of value)
#define PP_MIN 0.0f                 // fPickPocketMinChance
#define PP_MAX 90.0f                // fPickPocketMaxChance

int items_pickpocket_chance(int item, int count, int skill, int target_skill) {
	float c = PP_ACTOR_SKILL_BASE + PP_ACTOR_SKILL_MULT * skill + PP_TARGET_SKILL_BASE + PP_TARGET_SKILL_MULT * target_skill +
	          PP_WEIGHT_MULT * items_weight(item) * count + PP_AMOUNT_MULT * items_value(item) * count;
	return (int)fmaxf(PP_MIN, fminf(PP_MAX, c));
}

int items_npc_pockets(const char *id) {
	for (int i = 0; i < pocket_count; i++)
		if (!strcmp(pockets[i].id, id))
			return pockets[i].container;
	return -1;
}

const char *items_container_name(int container) {
	return container >= 0 && container < def_count ? defs[container].name : "";
}

int items_use(int item, Vitals *v) {
	if (!items_usable(item) || carried[item] <= 0)
		return 0;
	const Item *it = &items[item];
	if (it->type == IT_INGR)
		known[item] |= 1; // (eaten: its first effect learned)
	for (int e = 0; e < it->effect_count; e++) {
		if (it->effects[e].mgef >= 0) { // (Skyrim's effect: effects.c)
			effects_apply(it->effects[e].mgef, it->effects[e].magnitude, it->effects[e].seconds, v);
			continue;
		}
		// (an older pack's: restores over time given at once, magnitude x seconds)
		float amount = it->effects[e].magnitude * (it->effects[e].seconds > 1.0f ? it->effects[e].seconds : 1.0f);
		if (it->effects[e].harmful)
			amount = -amount;
		float *value = it->effects[e].av == AV_HEALTH ? &v->health : it->effects[e].av == AV_MAGICKA ? &v->magicka
		             : it->effects[e].av == AV_STAMINA ? &v->stamina : NULL;
		float max = it->effects[e].av == AV_HEALTH ? v->max_health : it->effects[e].av == AV_MAGICKA ? v->max_magicka
		          : v->max_stamina;
		if (value)
			*value = fmaxf(0.0f, fminf(max, *value + amount));
	}
	v->shown_for = 0.0f;
	carried[item]--;
	kept_stolen(item);
	return 1;
}

int items_have(int item) {
	return item >= 0 && item < item_count ? carried[item] : 0;
}

// (fewer carried: the stolen among them no more than there are)
static void kept_stolen(int item) {
	if (stolen && stolen[item] > carried[item])
		stolen[item] = carried[item];
}

void items_add(int item, int count) {
	if (item >= 0 && item < item_count) {
		carried[item] += count;
		if (carried[item] < 0)
			carried[item] = 0;
		kept_stolen(item);
	}
}

int items_stolen(int item) {
	return stolen && item >= 0 && item < item_count && item != gold_item ? stolen[item] : 0;
}

void items_mark_stolen(int item, int count) {
	if (!stolen || item < 0 || item >= item_count || item == gold_item) // (gold isn't marked, as Skyrim's)
		return;
	stolen[item] += count;
	if (stolen[item] < 0)
		stolen[item] = 0;
	kept_stolen(item);
}

void items_set_theft(int handle, int kind) {
	theft_handle = kind ? handle : -1, theft_kind = kind;
}

int items_take_theft(void) {
	const int v = theft_value;
	theft_value = 0;
	return v;
}

int items_gold(void) {
	return items_have(gold_item);
}

float items_carried(void) {
	float w = 0.0f;
	for (int i = 0; i < item_count; i++)
		if (carried[i])
			w += carried[i] * items[i].weight;
	return w;
}

static const int kind_order[IT_TYPES] = {7, 0, 1, 2, 4, 5, 8, 3, 6, 5, 9}; // weapons, armour, potions, ...

static int by_kind_name(const void *a, const void *b) {
	const Item *x = &items[*(const int *)a], *y = &items[*(const int *)b];
	int k = kind_order[x->type] - kind_order[y->type];
	return k ? k : strcmp(x->name, y->name);
}

int items_inventory(int *out, int max) {
	int n = 0;
	for (int i = 0; i < item_count && n < max; i++)
		if (carried[i] > 0 && i != gold_item)
			out[n++] = i;
	qsort(out, n, sizeof(int), by_kind_name);
	return n;
}

// --- Leveled lists -------------------------------------------------------------------------

static unsigned rnd(void) {
	rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
	return rng;
}

static void put(Opened *o, int item, int count) {
	for (int i = 0; i < o->n; i++)
		if (o->item[i] == item) {
			o->count[i] += count;
			return;
		}
	if (o->n < CONTAINER_SLOTS)
		o->item[o->n] = item, o->count[o->n] = count, o->n++;
}

static void roll(Opened *o, const Entry *e, int level, int depth);

// A list's chance of nothing when the game's global sets it (Skyrim's perk and skill scripts set
// these): Golden Touch's gold, Treasure Hunter's special loot, Master Trader's vendor gold, an
// Imperial's luck, the spell tomes a school's Adept and Expert open; otherwise as the list has it
#define PERK_GOLDEN_TOUCH 0x0005820au
#define PERK_TREASURE_HUNTER 0x00105f26u
#define PERK_MASTER_TRADER 0x001090a5u
#define SPECIAL_LOOT_NONE 90     // (Skyrim's SpecialLootChance: one in ten...)
#define TREASURE_HUNTER_NONE 85  // (... half again as often with Treasure Hunter)
static int list_chance_none(const List *li) {
	static const char *const SCHOOLS[5] = {"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"};
	static const int SCHOOL_SKILL[5] = {SK_ALTERATION, SK_CONJURATION, SK_DESTRUCTION, SK_ILLUSION, SK_RESTORATION};
	const char *g = li->global;
	if (!g[0])
		return li->chance_none;
	if (!strcmp(g, "PerkGoldenTouch"))
		return perks_has_form(PERK_GOLDEN_TOUCH) ? 0 : 100;
	if (!strcmp(g, "PerkMasterTrader"))
		return perks_has_form(PERK_MASTER_TRADER) ? 0 : 100;
	if (!strcmp(g, "SpecialLootChance"))
		return perks_has_form(PERK_TREASURE_HUNTER) ? TREASURE_HUNTER_NONE : SPECIAL_LOOT_NONE;
	if (!strcmp(g, "ImperialLuck")) // (Imperial Luck: an Imperial finds more gold)
		return !strcmp(races_get(races_player()) ? races_get(races_player())->eid : "", "ImperialRace") ? 0 : 100;
	for (int k = 0; k < 5; k++) // (PCDestructionAdept, PCDestructionExpert: that school at 50, at 75)
		if (!strncmp(g, "PC", 2) && strstr(g, SCHOOLS[k]))
			return skills_state()->level[SCHOOL_SKILL[k]] >= (strstr(g, "Expert") ? 75 : 50) ? 0 : 100;
	return li->chance_none;
}

// One roll of a leveled list at the player's level (Skyrim's rules: chance of nothing; the
// highest level reached, or any level up to it; or everything)
static void roll_list(Opened *o, int index, int count, int level, int depth) {
	if (index < 0 || index >= list_count || depth > LIST_DEPTH)
		return;
	const List *li = &lists[index];
	int rolls = li->flags & 2 ? count : 1, each = li->flags & 2 ? 1 : count;
	for (int r = 0; r < rolls; r++) {
		if ((int)(rnd() % 100) < list_chance_none(li))
			continue;
		if (li->flags & 4) { // all of them
			for (int i = 0; i < li->n; i++)
				if (li->e[i].level <= level) {
					Entry e = li->e[i];
					e.count *= each;
					roll(o, &e, level, depth + 1);
				}
			continue;
		}
		int best_level = -1;
		for (int i = 0; i < li->n; i++)
			if (li->e[i].level <= level && li->e[i].level > best_level)
				best_level = li->e[i].level;
		if (best_level < 0)
			continue;
		int candidates[MAX_ENTRIES], n = 0;
		for (int i = 0; i < li->n; i++)
			if (li->e[i].level <= level && ((li->flags & 1) || li->e[i].level == best_level))
				candidates[n++] = i;
		if (!n)
			continue;
		Entry e = li->e[candidates[rnd() % n]];
		e.count *= each;
		roll(o, &e, level, depth + 1);
	}
}

static void roll(Opened *o, const Entry *e, int level, int depth) {
	if (e->kind == 'L')
		roll_list(o, e->index, e->count, level, depth);
	else if (e->index >= 0 && e->index < item_count)
		put(o, e->index, e->count);
}

static float clock_hours; // (the game's hours: items_set_clock)

void items_set_clock(float game_hours) {
	clock_hours = game_hours;
}

int items_reset_containers(float hours) {
	int n = 0, k = 0;
	for (int i = 0; i < opened_count; i++) {
		const Opened *o = &opened[i];
		// (not one you keep things in; bodies go with their creature: items_forget_container)
		if (!o->kept && (o->ref & 0xFF000000u) != CORPSE_REFS && clock_hours - o->opened_at >= hours) {
			n++;
			continue;
		}
		opened[k++] = opened[i];
	}
	opened_count = k;
	return n;
}

void items_forget_container(uint32_t ref) {
	for (int i = 0; i < opened_count; i++)
		if (opened[i].ref == ref && !opened[i].kept)
			memmove(opened + i, opened + i + 1, sizeof(Opened) * (--opened_count - i)), i--;
}

uint32_t items_container_ref(int handle) {
	return handle >= 0 && handle < opened_count ? opened[handle].ref : 0;
}

int items_open_container(uint32_t ref, int container) {
	for (int i = 0; i < opened_count; i++)
		if (opened[i].ref == ref)
			return i;
	if (opened_count >= MAX_CONTAINERS) { // (the oldest is forgotten: it refills; not one you stored things in)
		int oldest = 0;
		while (oldest < opened_count - 1 && opened[oldest].kept)
			oldest++;
		memmove(opened + oldest, opened + oldest + 1, sizeof(Opened) * (--opened_count - oldest));
	}
	Opened *o = &opened[opened_count];
	memset(o, 0, sizeof(*o));
	o->ref = ref, o->opened_at = clock_hours;
	if (container >= 0 && container < def_count) {
		rng ^= ref * 2654435761u;
		const int level = skills_state()->player_level;
		for (int i = 0; i < defs[container].n; i++)
			roll(o, &defs[container].e[i], level, 0);
	}
	game_log("items: container %08x rolled from %d (%s): %d kinds", (unsigned)ref, container,
	          container >= 0 && container < def_count ? defs[container].name : "-", o->n);
	return opened_count++;
}

int items_in_container(int handle, int *out_items, int *counts, int max) {
	if (handle < 0 || handle >= opened_count)
		return 0;
	const Opened *o = &opened[handle];
	int n = 0;
	for (int i = 0; i < o->n && n < max; i++)
		if (o->count[i] > 0)
			out_items[n] = o->item[i], counts[n] = o->count[i], n++;
	return n;
}

void items_take(int handle, int item, int count) {
	if (handle < 0 || handle >= opened_count)
		return;
	Opened *o = &opened[handle];
	for (int i = 0; i < o->n; i++)
		if (o->item[i] == item) {
			int n = count < o->count[i] ? count : o->count[i];
			o->count[i] -= n;
			items_add(item, n);
			if (handle == theft_handle && n > 0) { // (someone's: stolen - reported if seen, unless a pocket picked unnoticed)
				items_mark_stolen(item, n);
				if (theft_kind == 1)
					theft_value += n * (item == gold_item ? 1 : items_value(item));
			}
		}
}

void items_container_add(int handle, int item, int count) {
	if (handle >= 0 && handle < opened_count && item >= 0 && item < item_count && count > 0)
		put(&opened[handle], item, count);
}

void items_put(int handle, int item, int count) {
	if (handle < 0 || handle >= opened_count || item < 0 || item >= item_count)
		return;
	if (count > carried[item])
		count = carried[item];
	if (count <= 0)
		return;
	Opened *o = &opened[handle];
	int fits = 0;
	for (int i = 0; i < o->n; i++)
		fits |= o->item[i] == item;
	if (!fits && o->n >= CONTAINER_SLOTS)
		return; // (full)
	put(o, item, count);
	carried[item] -= count;
	kept_stolen(item);
	o->kept = 1;
}

// --- Dropped things ------------------------------------------------------------------------

int items_drop(int item, int count, const float pos[3], float yaw, int place) {
	if (item < 0 || item >= item_count || count <= 0 || carried[item] <= 0)
		return 0;
	if (count > carried[item])
		count = carried[item];
	if (dropped_count >= MAX_DROPPED) // (the oldest goes)
		memmove(dropped, dropped + 1, sizeof(Dropped) * --dropped_count);
	Dropped *d = &dropped[dropped_count++];
	d->ref = DROPPED_REF | next_drop++, d->item = item, d->count = count, d->place = place, d->yaw = yaw;
	memcpy(d->pos, pos, sizeof(d->pos));
	carried[item] -= count;
	kept_stolen(item);
	return count;
}

int items_dropped(int index, int place, int *item, int *count, float pos[3], float *yaw, uint32_t *ref) {
	if (index < 0 || index >= dropped_count || dropped[index].place != place)
		return 0;
	const Dropped *d = &dropped[index];
	*item = d->item, *count = d->count, *yaw = d->yaw, *ref = d->ref;
	memcpy(pos, d->pos, sizeof(d->pos));
	return 1;
}

int items_dropped_count(void) {
	return dropped_count;
}

int items_pick_up(uint32_t ref) {
	for (int i = 0; i < dropped_count; i++)
		if (dropped[i].ref == ref) {
			const int n = dropped[i].count;
			items_add(dropped[i].item, n);
			dropped[i] = dropped[--dropped_count];
			return n;
		}
	return 0;
}

int items_taken(uint32_t ref) {
	for (int i = 0; i < taken_count; i++)
		if (taken[i] == ref)
			return 1;
	return 0;
}

int items_harvested(uint32_t ref, float now_hours) {
	for (int i = 0; i < harvested_count; i++)
		if (harvested[i] == ref)
			return now_hours - harvested_at[i] < REGROW_HOURS;
	return 0;
}

void items_set_harvested(uint32_t ref, float now_hours) {
	int at = -1, oldest = 0;
	for (int i = 0; i < harvested_count; i++) {
		if (harvested[i] == ref || now_hours - harvested_at[i] >= REGROW_HOURS) // (its own, or one grown back: reused)
			at = at < 0 ? i : at;
		if (harvested_at[i] < harvested_at[oldest])
			oldest = i;
	}
	if (at < 0)
		at = harvested_count < MAX_HARVESTED ? harvested_count++ : oldest;
	harvested[at] = ref, harvested_at[at] = now_hours;
}

void items_set_taken(uint32_t ref) {
	if (!items_taken(ref) && taken_count < MAX_TAKEN)
		taken[taken_count++] = ref;
}

int items_by_form(uint32_t form);

int items_form(int item) {
	return item >= 0 && item < item_count ? (int)items[item].form : 0;
}

void items_set_worn(const uint32_t *forms, int n) {
	worn_count = n < MAX_WORN ? n : MAX_WORN;
	memcpy(worn, forms, worn_count * sizeof(uint32_t));
}

int items_worn(uint32_t *forms, int max) {
	int n = worn_count < max ? worn_count : max;
	memcpy(forms, worn, n * sizeof(uint32_t));
	return n;
}

int items_unlocked(uint32_t ref) {
	for (int i = 0; i < unlocked_count; i++)
		if (unlocked[i] == ref)
			return 1;
	return 0;
}

void items_set_unlocked(uint32_t ref) {
	if (!items_unlocked(ref) && unlocked_count < MAX_TAKEN)
		unlocked[unlocked_count++] = ref;
}

void items_set_locked(uint32_t ref) {
	for (int i = 0; i < unlocked_count; i++)
		if (unlocked[i] == ref)
			unlocked[i--] = unlocked[--unlocked_count];
}

// --- Saving --------------------------------------------------------------------------------
// "SKIT", u16 version, u16 0; u32 carried count, (form, count)...; u32 opened count, per
// container: ref, n, (form, count)...; u32 taken count, refs. Forms, not indices: the
// converter may number items differently next time.

// By form: a sorted index of the items (rebuilt when items are added), searched by halves - a
// gear map's thousands of lookups each scanned every item (the race menu's stall)
static uint32_t *by_form_key;
static int *by_form_at, by_form_n, by_form_cap;

static int by_form_cmp(const void *a, const void *b) {
	const uint32_t fa = items[*(const int *)a].form, fb = items[*(const int *)b].form;
	return fa < fb ? -1 : fa > fb ? 1 : *(const int *)a - *(const int *)b;
}

static void by_form_build(void) {
	if (item_count > by_form_cap) {
		by_form_cap = item_count + 256;
		by_form_key = realloc(by_form_key, by_form_cap * sizeof(uint32_t));
		by_form_at = realloc(by_form_at, by_form_cap * sizeof(int));
	}
	if (!by_form_key || !by_form_at) {
		by_form_n = by_form_cap = 0;
		return;
	}
	for (int i = 0; i < item_count; i++)
		by_form_at[i] = i;
	qsort(by_form_at, item_count, sizeof(int), by_form_cmp);
	for (int i = 0; i < item_count; i++)
		by_form_key[i] = items[by_form_at[i]].form;
	by_form_n = item_count;
}

int items_by_form(uint32_t form) {
	if (by_form_n != item_count)
		by_form_build();
	int lo = 0, hi = by_form_n - 1, found = -1;
	while (lo <= hi) { // (the first of that form: the lowest index, as the plain scan found)
		const int mid = (lo + hi) / 2;
		if (by_form_key[mid] < form)
			lo = mid + 1;
		else {
			if (by_form_key[mid] == form)
				found = mid;
			hi = mid - 1;
		}
	}
	return found >= 0 ? by_form_at[found] : -1; // (forms are set as items are added, never changed)
}

void items_reset(void) {
	if (carried)
		memset(carried, 0, (base_count + ITEMS_CREATED_MAX) * sizeof(int));
	if (stolen)
		memset(stolen, 0, (base_count + ITEMS_CREATED_MAX) * sizeof(int));
	if (known)
		memset(known, 0, base_count + ITEMS_CREATED_MAX);
	created_count = 0, item_count = base_count, learned_count = 0;
	opened_count = taken_count = unlocked_count = worn_count = dropped_count = harvested_count = 0;
}

void items_new_game(void) {
	items_reset();
	items_add(items_by_form(LOCKPICK_FORM), STARTING_LOCKPICKS);
}

static char save_path[128] = ITEMS_SAVE; // (the save slot's: items_set_save_path)

void items_set_save_path(const char *path) {
	snprintf(save_path, sizeof(save_path), "%s", path);
}

void items_save(void) {
	char tmp_path[sizeof(save_path) + 4];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save_path);
	(void)tmp_path;
	long cap = 4 + 64 * 8 + 68 + opened_count * 4L + (long)item_count * 16 + created_count * (72L + ITEM_EFFECTS_MAX * 12) + 4 + MAX_LEARNED * 12 + opened_count * (12 + CONTAINER_SLOTS * 8) +
	           (taken_count + unlocked_count + worn_count) * 4 + dropped_count * 36L + 4 + (long)item_count * 8 + 4 + harvested_count * 8L;
	uint8_t *buf = malloc(cap), *p = buf;
	if (!buf)
		return;
#define PUT32(v) do { uint32_t v_ = (uint32_t)(v); memcpy(p, &v_, 4); p += 4; } while (0)
	memcpy(p, SAVE_MAGIC, 4), p += 4;
	PUT32(SAVE_VERSION);
	// (version 5) the potions made (before the inventory: it holds them), the ingredient effects known
	PUT32(created_count);
	for (int i = base_count; i < item_count; i++) {
		const Item *it = &items[i];
		memcpy(p, it->name, 48), p += 48;
		PUT32(it->poison);
		PUT32(it->value);
		PUT32(it->effect_count);
		for (int k = 0; k < it->effect_count; k++) {
			PUT32(it->effects[k].mgef);
			memcpy(p, &it->effects[k].magnitude, 4), p += 4;
			memcpy(p, &it->effects[k].seconds, 4), p += 4;
		}
		PUT32(it->made_from ? items[it->made_from - 1].form : 0); // (version 7: an enchanted thing's item)
		PUT32(it->stat);   // (version 8: its damage or armour, its improvement)
		PUT32(it->temper);
	}
	PUT32(learned_count); // (version 7: the enchantments learned)
	for (int i = 0; i < learned_count; i++) {
		PUT32(learned[i].mgef);
		memcpy(p, &learned[i].magnitude, 4), p += 4;
		memcpy(p, &learned[i].seconds, 4), p += 4;
	}
	uint8_t *known_at = p;
	PUT32(0);
	uint32_t kn = 0;
	for (int i = 0; i < base_count; i++)
		if (known[i]) {
			PUT32(items[i].form);
			PUT32(known[i]);
			kn++;
		}
	memcpy(known_at, &kn, 4);
	uint8_t *count_at = p;
	PUT32(0);
	uint32_t n = 0;
	for (int i = 0; i < item_count; i++)
		if (carried[i] > 0) {
			PUT32(items[i].form);
			PUT32(carried[i]);
			n++;
		}
	memcpy(count_at, &n, 4);
	PUT32(opened_count);
	for (int c = 0; c < opened_count; c++) {
		PUT32(opened[c].ref);
		PUT32(opened[c].n);
		for (int i = 0; i < opened[c].n; i++) {
			PUT32(items[opened[c].item[i]].form);
			PUT32(opened[c].count[i]);
		}
	}
	PUT32(taken_count);
	for (int i = 0; i < taken_count; i++)
		PUT32(taken[i]);
	PUT32(unlocked_count);
	for (int i = 0; i < unlocked_count; i++)
		PUT32(unlocked[i]);
	PUT32(worn_count);
	for (int i = 0; i < worn_count; i++)
		PUT32(worn[i]);
	// (version 4) the containers you stored things in, the things you dropped
	uint32_t kept = 0;
	for (int c = 0; c < opened_count; c++)
		kept += opened[c].kept;
	PUT32(kept);
	for (int c = 0; c < opened_count; c++)
		if (opened[c].kept)
			PUT32(opened[c].ref);
	PUT32(dropped_count);
	for (int i = 0; i < dropped_count; i++) {
		PUT32(items[dropped[i].item].form);
		PUT32(dropped[i].count);
		PUT32(dropped[i].place);
		memcpy(p, dropped[i].pos, 12), p += 12;
		memcpy(p, &dropped[i].yaw, 4), p += 4;
	}
	// (version 6) when each opened container was rolled (game hours)
	PUT32(opened_count);
	for (int c = 0; c < opened_count; c++)
		memcpy(p, &opened[c].opened_at, 4), p += 4;
	// (version 9) the stolen goods carried
	uint8_t *stolen_at = p;
	uint32_t ns = 0;
	PUT32(0);
	for (int i = 0; i < item_count; i++)
		if (items_stolen(i) > 0) {
			PUT32(items[i].form);
			PUT32(items_stolen(i));
			ns++;
		}
	memcpy(stolen_at, &ns, 4);
	// (version 10) the charge spent of each enchanted weapon
	uint8_t *charge_at = p;
	uint32_t nc = 0;
	PUT32(0);
	for (int i = 0; i < item_count && nc < 64; i++)
		if (items[i].charge_max > 0 && items[i].charge_used > 0.0f) {
			PUT32(items[i].form);
			memcpy(p, &items[i].charge_used, 4), p += 4;
			nc++;
		}
	memcpy(charge_at, &nc, 4);
	// (version 12) the plants harvested, and when
	PUT32(harvested_count);
	for (int i = 0; i < harvested_count; i++) {
		PUT32(harvested[i]);
		memcpy(p, &harvested_at[i], 4), p += 4;
	}
#undef PUT32
	long len = p - buf;
#ifdef __vita__
	SceUID fd = sceIoOpen(tmp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	int ok = fd >= 0 && sceIoWrite(fd, buf, len) == len;
	if (fd >= 0)
		sceIoClose(fd);
	if (ok) {
		sceIoRemove(save_path);
		sceIoRename(tmp_path, save_path);
	}
#else
	FILE *f = fopen(save_path, "wb");
	if (f) {
		fwrite(buf, 1, len, f);
		fclose(f);
	}
#endif
	free(buf);
}

static int create_from(int base, const char *name, int value, int stat, int temper, const ItemEffect *e, int n);

static int (*corpse_number)(int old); // (a save from before the wider map: its bodies' spawn points then -> now)

void items_load_corpses_by(int (*fn)(int old)) {
	corpse_number = fn;
}

int items_load(void) {
	items_reset();
	int loaded = 0;
	long size = 0;
	uint8_t *d = file_read_all(save_path, &size);
	if (!d) { // (a game saved before items: a fresh inventory)
		items_new_game();
		return 0;
	}
	const uint8_t *p = d, *end = d + size;
#define GET32(v) do { if (p + 4 > end) goto done; memcpy(&(v), p, 4); p += 4; } while (0)
	uint32_t version, n, form, count, ref;
	if (size < 8 || memcmp(d, SAVE_MAGIC, 4))
		goto done;
	p += 4;
	GET32(version);
	if (version < 1 || version > SAVE_VERSION)
		goto done;
	loaded = (int)version;
	if (version < 2)
		items_add(items_by_form(LOCKPICK_FORM), STARTING_LOCKPICKS);
	if (version >= 5) {
		GET32(n);
		for (uint32_t i = 0; i < n; i++) {
			char name[48];
			uint32_t poison, value, count_e;
			ItemEffect e[ITEM_EFFECTS_MAX];
			if (p + 48 > end)
				goto done;
			memcpy(name, p, 48), p += 48;
			name[47] = 0;
			GET32(poison);
			GET32(value);
			GET32(count_e);
			for (uint32_t k = 0; k < count_e; k++) {
				ItemEffect x;
				GET32(x.mgef);
				GET32(x.magnitude);
				GET32(x.seconds);
				if (k < ITEM_EFFECTS_MAX)
					e[k] = x;
			}
			uint32_t base_form = 0, stat = 0, temper = 0;
			if (version >= 7)
				GET32(base_form);
			if (version >= 8) {
				GET32(stat);
				GET32(temper);
			}
			const int ne = count_e < ITEM_EFFECTS_MAX ? (int)count_e : ITEM_EFFECTS_MAX;
			const int from = base_form ? items_by_form(base_form) : -1;
			const int made = !base_form ? items_create_potion(name, (int)poison, (int)value, e, ne)
			               : create_from(from, name, (int)value, version >= 8 ? (int)stat : from >= 0 ? items[from].stat : 0,
			                             (int)temper, e, ne);
			if (made < 0)
				goto done;
		}
		if (version >= 7) {
			GET32(n);
			for (uint32_t i = 0; i < n; i++) {
				int mgef;
				float mag, secs;
				GET32(mgef);
				GET32(mag);
				GET32(secs);
				if (learned_count < MAX_LEARNED)
					learned[learned_count].mgef = mgef, learned[learned_count].magnitude = mag,
					learned[learned_count++].seconds = secs;
			}
		}
		GET32(n);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t bits;
			GET32(form);
			GET32(bits);
			items_learn(items_by_form(form), (int)bits);
		}
	}
	GET32(n);
	for (uint32_t i = 0; i < n; i++) {
		GET32(form);
		GET32(count);
		items_add(items_by_form(form), (int)count);
	}
	GET32(n);
	for (uint32_t c = 0; c < n && opened_count < MAX_CONTAINERS; c++) {
		uint32_t slots;
		GET32(ref);
		GET32(slots);
		if (corpse_number && (ref & 0xFF000000u) == CORPSE_REFS) { // (a body searched: its spawn point's number now)
			const int s = corpse_number((int)(ref & 0x00FFFFFFu));
			ref = s >= 0 ? CORPSE_REFS | (uint32_t)s : 0xFFFFFFFFu;
		}
		Opened *o = &opened[opened_count++];
		memset(o, 0, sizeof(*o));
		o->ref = ref;
		for (uint32_t i = 0; i < slots; i++) {
			GET32(form);
			GET32(count);
			int item = items_by_form(form);
			if (item >= 0)
				put(o, item, (int)count);
		}
	}
	GET32(n);
	for (uint32_t i = 0; i < n && taken_count < MAX_TAKEN; i++) {
		GET32(ref);
		taken[taken_count++] = ref;
	}
	if (version >= 2) {
		GET32(n);
		for (uint32_t i = 0; i < n && unlocked_count < MAX_TAKEN; i++) {
			GET32(ref);
			unlocked[unlocked_count++] = ref;
		}
	}
	if (version >= 3) {
		GET32(n);
		for (uint32_t i = 0; i < n && worn_count < MAX_WORN; i++) {
			GET32(ref);
			worn[worn_count++] = ref;
		}
	}
	if (version >= 4) {
		GET32(n);
		for (uint32_t i = 0; i < n; i++) {
			GET32(ref);
			for (int c = 0; c < opened_count; c++)
				if (opened[c].ref == ref)
					opened[c].kept = 1;
		}
		GET32(n);
		for (uint32_t i = 0; i < n && dropped_count < MAX_DROPPED; i++) {
			Dropped d = {0};
			uint32_t place;
			GET32(form);
			GET32(count);
			GET32(place);
			if (p + 16 > end)
				goto done;
			memcpy(d.pos, p, 12), memcpy(&d.yaw, p + 12, 4), p += 16;
			d.item = items_by_form(form), d.count = (int)count, d.place = (int)place;
			d.ref = DROPPED_REF | next_drop++;
			if (d.item >= 0)
				dropped[dropped_count++] = d;
		}
	}
	for (int c = 0; c < opened_count; c++) // (an older save: as if rolled now)
		opened[c].opened_at = clock_hours;
	if (version >= 6) {
		GET32(n);
		for (uint32_t c = 0; c < n && c < (uint32_t)opened_count; c++) {
			if (p + 4 > end)
				goto done;
			memcpy(&opened[c].opened_at, p, 4), p += 4;
		}
	}
	if (version >= 9) {
		GET32(n);
		for (uint32_t i = 0; i < n; i++) {
			GET32(form);
			GET32(count);
			items_mark_stolen(items_by_form(form), (int)count);
		}
	}
	if (version >= 10) {
		GET32(n);
		for (uint32_t i = 0; i < n && i < 64; i++) {
			GET32(form);
			float used = 0.0f;
			GET32(count);
			memcpy(&used, &count, 4);
			const int it = items_by_form(form);
			if (it >= 0)
				items[it].charge_used = used;
		}
	}
	if (version >= 12) {
		GET32(n);
		for (uint32_t i = 0; i < n && harvested_count < MAX_HARVESTED; i++) {
			GET32(ref);
			GET32(count);
			harvested[harvested_count] = ref;
			memcpy(&harvested_at[harvested_count++], &count, 4);
		}
	}
	if (version == 10) { // (the build that opened every container empty: those it remembered, rolled again when opened)
		int kept = 0, forgot = 0;
		for (int c = 0; c < opened_count; c++) {
			int any = opened[c].kept;
			for (int i = 0; i < opened[c].n && !any; i++)
				any = opened[c].count[i] > 0;
			if (any)
				opened[kept++] = opened[c];
			else
				forgot++;
		}
		opened_count = kept;
		game_log("items: %d empty containers from the 10-21 build forgotten", forgot);
	}
#undef GET32
done:
	free(d);
	return loaded;
}

int items_find_gold(void) {
	return gold_item;
}

int items_find_name(const char *name) {
	for (int i = 0; i < item_count; i++)
		if (!strcmp(items_name(i), name))
			return i;
	return -1;
}

int items_book_skill(int item) {
	if (item < 0 || item >= item_count || items[item].type != IT_BOOK || items[item].stat < 6)
		return -1;
	return items[item].stat - 6; // (actor value -> skill)
}

int items_book_spell(int item, char *key, int key_size, const char **name) {
	if (item < 0 || item >= item_count || !items[item].spell)
		return 0;
	const char *s = items[item].spell, *gap = strchr(s, ' ');
	const int n = gap ? (int)(gap - s) : (int)strlen(s);
	snprintf(key, key_size, "%.*s", n, s);
	*name = gap ? gap + 1 : s;
	return 1;
}

int items_book_text(int item, char *out, int size) {
	out[0] = 0;
	char path[256];
	snprintf(path, sizeof(path), "%sitems/books.txt", items_pack);
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	static char line[65536]; // (the longest of Skyrim's books: under 40 KB)
	int found = 0;
	while (!found && fgets(line, sizeof(line), f)) {
		char *tab = strchr(line, '\t');
		if (!tab || atoi(line) != item)
			continue;
		int n = 0; // (its escapes: \n a line, \f a page)
		for (const char *p = tab + 1; *p && *p != '\n' && n < size - 1; p++) {
			if (p[0] == '\\' && (p[1] == 'n' || p[1] == 'f'))
				out[n++] = p[1] == 'n' ? '\n' : '\f', p++;
			else
				out[n++] = *p;
		}
		out[n] = 0;
		found = 1;
	}
	fclose(f);
	return found;
}

// Skill books read (their forms): each raises its skill once, as in Skyrim
static uint32_t books_read[ITEMS_BOOKS_READ_MAX];
static int books_read_count;

int items_book_first_read(int item) {
	if (item < 0 || item >= item_count)
		return 0;
	for (int i = 0; i < books_read_count; i++)
		if (books_read[i] == items[item].form)
			return 0;
	if (books_read_count < ITEMS_BOOKS_READ_MAX)
		books_read[books_read_count++] = items[item].form;
	return 1;
}

int items_books_read(uint32_t *out, int max) {
	int n = 0;
	for (; n < books_read_count && n < max; n++)
		out[n] = books_read[n];
	return n;
}

void items_set_books_read(const uint32_t *forms, int n) {
	books_read_count = 0;
	for (int i = 0; i < n && i < ITEMS_BOOKS_READ_MAX; i++)
		if (forms[i])
			books_read[books_read_count++] = forms[i];
}

int items_is_poison(int item) {
	return item >= 0 && item < item_count && items[item].type == IT_ALCH && items[item].poison;
}

float items_poison_damage(int item) {
	if (!items_is_poison(item))
		return 0.0f;
	float d = 0.0f; // (Skyrim's Damage / Ravage / Lingering Damage Health: at once; over seconds, all of it)
	for (int e = 0; e < items[item].effect_count; e++)
		if (items[item].effects[e].av == AV_HEALTH && items[item].effects[e].harmful)
			d += items[item].effects[e].magnitude * fmaxf(1.0f, items[item].effects[e].seconds);
	return d;
}

// --- Merchants -----------------------------------------------------------------------------

int items_vendor(const char *npc_id) {
	for (int v = 0; v < vendor_count; v++)
		if (!strcmp(vendors[v].id, npc_id))
			return v;
	return -1;
}

int items_vendor_open_now(int v, float hour) {
	if (v < 0 || v >= vendor_count)
		return 0;
	const int s = vendors[v].start, e = vendors[v].end;
	if (s == e || (s == 0 && e >= 24))
		return 1; // (any hour)
	return s < e ? hour >= s && hour < e : hour >= s || hour < e;
}

int items_vendor_stock(int v) {
	if (v < 0 || v >= vendor_count)
		return -1;
	const uint32_t ref = VENDOR_REFS | (uint32_t)v;
	for (int i = 0; i < opened_count; i++) // (Skyrim restocks them every two days: rolled again)
		if (opened[i].ref == ref && clock_hours - opened[i].opened_at >= VENDOR_RESTOCK_H) {
			memmove(opened + i, opened + i + 1, sizeof(Opened) * (--opened_count - i));
			break;
		}
	const int handle = items_open_container(ref, vendors[v].container);
	for (int i = 0; i < opened_count && items_vendor_invested(v); i++) // (an investor's: its 500 more, each restock)
		if (opened[i].ref == ref && opened[i].opened_at != invest_rolled[v]) {
			invest_rolled[v] = opened[i].opened_at;
			items_put(handle, gold_item, INVEST_GOLD);
		}
	return handle;
}

// Investor: a store you've put 500 gold into - 500 more of its own at each restock (Skyrim's
// PerkInvestor lists); kept in the save (bits by the merchant's number)

int items_vendor_invested(int v) {
	return v >= 0 && v < vendor_count && (invested[v / 32] >> (v % 32) & 1);
}

int items_vendor_invest(int v) {
	if (v < 0 || v >= vendor_count || items_vendor_invested(v) || items_gold() < INVEST_GOLD)
		return 0;
	items_add(gold_item, -INVEST_GOLD);
	invested[v / 32] |= 1u << (v % 32);
	invest_rolled[v] = -1.0f;
	game_log("items: invested in %s's store", vendors[v].id);
	return 1;
}

void items_vendors_invested(uint32_t *bits, int words, int save) {
	for (int w = 0; w < words && w < (int)(sizeof(invested) / 4); w++) {
		if (save)
			bits[w] = invested[w];
		else
			invested[w] = bits[w];
	}
	for (int v = 0; v < MAX_VENDORS && !save; v++)
		invest_rolled[v] = -1.0f;
}

int items_vendor_fence(int v) {
	return v >= 0 && v < vendor_count && (vendors[v].flags & 2);
}

int items_vendor_buys(int v, int item) {
	if (v < 0 || v >= vendor_count || item < 0 || item >= item_count || item == gold_item)
		return 0;
	if (vendors[v].flags & 2) // (a fence: stolen goods)
		return items_stolen(item) > 0;
	int listed = 0;
	for (int k = 0; k < items[item].keyword_count && !listed; k++)
		for (int j = 0; j < vendors[v].nkw; j++)
			listed |= items[item].keywords[k] == vendors[v].kw[j];
	return (vendors[v].flags & 1) ? !listed : listed;
}

// Skyrim's price factor: fBarterMax at Speech 0 down to fBarterMin at 100
static float barter_factor(int speech) {
	const float s = speech < 0 ? 0.0f : speech > 100 ? 1.0f : speech / 100.0f;
	return BARTER_MAX - (BARTER_MAX - BARTER_MIN) * s;
}

int items_buy_price(int item, int speech, float perk_mult) {
	const int v = items_value(item);
	const int p = (int)(v * barter_factor(speech) * perk_mult + 0.5f);
	return p < 1 && v > 0 ? 1 : p;
}

int items_sell_price(int item, int speech, float perk_mult) {
	const int p = (int)(items_value(item) / barter_factor(speech) * perk_mult);
	const int buy = items_buy_price(item, speech, 1.0f);
	return p > buy ? buy : p; // (never more than it'd cost to buy back)
}

int items_give_rolled(int kind, int index, int count, int *first) {
	Opened o; // (rolled as a container would be, then into your pack)
	memset(&o, 0, sizeof(o));
	const Entry e = {(uint8_t)(kind == 'L' ? 'L' : 'I'), 0, index, count};
	roll(&o, &e, skills_state()->player_level, 0);
	int n = 0;
	for (int i = 0; i < o.n; i++)
		if (o.count[i] > 0) {
			items_add(o.item[i], o.count[i]);
			if (!n++ && first)
				*first = o.item[i];
		}
	return n;
}

// --- Crafting -------------------------------------------------------------------------------

int items_recipes(int station, int *out, int max) {
	int n = 0;
	for (int i = 0; i < recipe_count && n < max; i++)
		if (recipes[i].station == station)
			out[n++] = i;
	return n;
}

int items_recipe(int r, int *made, int *count, const int **parts, const int **need, int *nparts, const uint32_t **perks,
                 int *nperks) {
	if (r < 0 || r >= recipe_count)
		return 0;
	*made = recipes[r].made, *count = recipes[r].count;
	*parts = recipes[r].part, *need = recipes[r].need, *nparts = recipes[r].nparts;
	*perks = recipes[r].perk, *nperks = recipes[r].nperks;
	return 1;
}

int items_craft(int r) {
	if (r < 0 || r >= recipe_count)
		return 0;
	for (int k = 0; k < recipes[r].nparts; k++)
		if (items_have(recipes[r].part[k]) < recipes[r].need[k])
			return 0;
	for (int k = 0; k < recipes[r].nparts; k++)
		items_add(recipes[r].part[k], -recipes[r].need[k]);
	items_add(recipes[r].made, recipes[r].count);
	return 1;
}

// --- Enchanting -----------------------------------------------------------------------------

int items_learned(int i, int *mgef, float *magnitude, float *seconds) {
	if (i < 0 || i >= learned_count)
		return 0;
	*mgef = learned[i].mgef, *magnitude = learned[i].magnitude, *seconds = learned[i].seconds;
	return 1;
}

int items_learned_count(void) {
	return learned_count;
}

int items_disenchant(int item) {
	if (item < 0 || item >= item_count || !items[item].enchanted || carried[item] <= 0)
		return 0;
	int got = 0;
	for (int e = 0; e < items[item].effect_count; e++) {
		int k = 0;
		while (k < learned_count && learned[k].mgef != items[item].effects[e].mgef)
			k++;
		if (k == learned_count && learned_count < MAX_LEARNED)
			learned[learned_count++].mgef = items[item].effects[e].mgef, got++;
		if (k < learned_count && items[item].effects[e].magnitude > learned[k].magnitude) // (the strongest seen)
			learned[k].magnitude = items[item].effects[e].magnitude, learned[k].seconds = items[item].effects[e].seconds;
	}
	carried[item]--; // (destroyed in the learning, as Skyrim's)
	kept_stolen(item);
	return got + 1;
}

int items_soul(int item) {
	return item >= 0 && item < item_count && items[item].type == IT_SLGM ? items[item].stat : 0;
}

// A weapon or armour made here from another (enchanted, improved): its look, weight and keywords
// from that one, its own name, value, damage or armour, and effects (n 0: the other's, if any)
static int create_from(int base, const char *name, int value, int stat, int temper, const ItemEffect *e, int n) {
	if (base < 0 || base >= item_count || created_count >= ITEMS_CREATED_MAX)
		return -1;
	for (int i = base_count; i < item_count; i++) // (made before, the same: the same item)
		if (!strcmp(items[i].name, name) && items[i].type == items[base].type && items[i].stat == stat &&
		    items[i].temper == temper && (n <= 0 || (items[i].effect_count == n && items[i].effects[0].mgef == e[0].mgef &&
		                                            fabsf(items[i].effects[0].magnitude - e[0].magnitude) < 0.01f)))
			return i;
	const int idx = base_count + created_count++;
	item_count = base_count + created_count;
	Item *it = &items[idx];
	*it = items[base]; // (its look, weight, keywords, and an enchantment it had)
	it->form = CREATED_FORM | (uint32_t)(created_count - 1);
	it->value = value, it->stat = stat, it->temper = (int8_t)temper;
	it->made_from = base + 1;
	snprintf(it->name, sizeof(it->name), "%s", name);
	if (n <= 0)
		return idx;
	if (n > ITEM_EFFECTS_MAX)
		n = ITEM_EFFECTS_MAX;
	for (int k = 0; k < n; k++) {
		const MagicEffect *m = items_magic_effect(e[k].mgef);
		it->effects[k].mgef = (int16_t)e[k].mgef;
		it->effects[k].av = (int16_t)(m ? m->av : -1);
		it->effects[k].harmful = m && (m->flags & MGEF_DETRIMENTAL);
		it->effects[k].magnitude = e[k].magnitude, it->effects[k].seconds = e[k].seconds;
		it->effects[k].resist = -1;
	}
	it->effect_count = (int8_t)n;
	it->enchanted = 1;
	return idx;
}

int items_create_enchanted(int base, const char *name, int value, const ItemEffect *e, int n) {
	if (base < 0 || base >= item_count || n <= 0)
		return -1;
	return create_from(base, name, value, items[base].stat, items[base].temper, e, n);
}

// Skyrim's improvement names, Fine (1) .. Legendary (6)
static const char *const TEMPERS[7] = {"", "Fine", "Superior", "Exquisite", "Flawless", "Epic", "Legendary"};

int items_made_from(int item) {
	return item >= 0 && item < item_count && items[item].made_from ? items[item].made_from - 1 : -1;
}

int items_temper_level(int item) {
	return item >= 0 && item < item_count ? items[item].temper : 0;
}

int items_temper(int item, int level) {
	if (item < 0 || item >= item_count || level < 1 || level > 6 || level <= items[item].temper)
		return -1;
	const int t = items[item].type, was = items[item].temper;
	// (the plain one's name: an improved one's "Iron Sword (Fine)" loses its old quality)
	char plain[48];
	snprintf(plain, sizeof(plain), "%s", items[item].name);
	char *paren = was ? strrchr(plain, '(') : NULL;
	if (paren && paren > plain)
		paren[-1] = 0;
	char name[48];
	snprintf(name, sizeof(name), "%.36s (%s)", plain, TEMPERS[level]);
	const int bonus = t == IT_WEAP ? (level * 3 + 1) / 2 - (was * 3 + 1) / 2 : 2 * (level - was); // (weapons +1.5 a level, armour +2)
	const int base = items[item].made_from ? items[item].made_from - 1 : item;
	ItemEffect e[ITEM_EFFECTS_MAX];
	const int n = items[item].enchanted ? items_effects(item, e, ITEM_EFFECTS_MAX) : 0;
	const int made = create_from(base, name, items[item].value + bonus * 10, items[item].stat + bonus, level, e, n);
	return made;
}

// --- Ore veins and chopping blocks ----------------------------------------------------------

int items_resource(uint32_t ref, int *item, int *per_use, int *total, const int **tools, int *ntools) {
	for (int i = 0; i < resource_count; i++)
		if (resources[i].ref == ref) {
			*item = resources[i].item, *per_use = resources[i].per_use, *total = resources[i].total;
			*tools = resources[i].tool, *ntools = resources[i].ntools;
			return 1;
		}
	return 0;
}

int items_find_named(const char *name) {
	int first = -1; // (Skyrim has several of some names: the one recipes use, if any)
	for (int i = 0; i < base_count; i++) {
		if (strcmp(items[i].name, name))
			continue;
		if (first < 0)
			first = i;
		for (int r = 0; r < recipe_count; r++)
			for (int k = 0; k < recipes[r].nparts; k++)
				if (recipes[r].part[k] == i)
					return i;
	}
	return first;
}

int items_give_crafting_kit(void) {
	static const struct { const char *name; int count; } KIT[] = {
		{T_MISC_0005ACE4_FULL0, 20}, {T_MISC_0005ACE5_FULL0, 15}, {T_MISC_0005AD93_FULL0, 10}, {"Leather", 15}, {T_MISC_000800E4_FULL0, 20},
		{T_MISC_00071CF3_FULL0, 10}, {T_MISC_0005ACDB_FULL0, 6}, {T_MISC_0003AD90_FULL0, 4}, {T_MISC_0003AD74_FULL0, 4}, {T_MISC_0003AD8F_FULL0, 2},
		{T_WEAP_000E3C16_FULL0, 1}, {T_WEAP_0002F2F4_FULL0, 1}, {T_WEAP_0001397E_FULL0, 2}, {T_WEAP_00012EB7_FULL0, 1}, {T_ARMO_00012E4D_FULL0, 1},
		{T_ALCH_00065C99_FULL0, 3}, {T_INGR_00034CDF_FULL0, 5}, {"Leek", 3}, {"Potato", 3}, {"Cabbage", 3}, {"Garlic", 2},
		{T_INGR_00077E1C_FULL0, 5}, {"Wheat", 5}, {"Blisterwort", 5}, {"Lavender", 5}, {T_INGR_0001B3BD_FULL0, 5},
		{T_INGR_0005076E_FULL0, 5}, {T_FLOR_0004DA08_FULL0, 5}, {T_INGR_000727E0_FULL0, 5}, {T_INGR_00052695_FULL0, 3}, {"Gold", 1000},
	};
	int given = 0;
	for (unsigned k = 0; k < sizeof(KIT) / sizeof(KIT[0]); k++) {
		const int i = items_find_named(KIT[k].name);
		if (i >= 0)
			items_add(i, KIT[k].count), given++;
	}
	int gems = 0; // (filled soul gems: one of each size there is)
	for (int i = 0; i < base_count && gems < 5; i++)
		if (items[i].type == IT_SLGM && items[i].stat > 0)
			items_add(i, 2), gems++, given++;
	for (int i = 0; i < base_count; i++) // (an enchanted weapon, to learn its enchantment from)
		if (items[i].enchanted && items[i].type == IT_WEAP && items[i].value < 200) {
			items_add(i, 1), given++;
			break;
		}
	return given;
}

int items_charge(int item, float *left) {
	if (item < 0 || item >= item_count || items[item].charge_max <= 0)
		return 0;
	const Item *it = &items[item];
	if (left)
		*left = fmaxf(0.0f, 1.0f - it->charge_used / (float)it->charge_max);
	return 1;
}

int items_charge_use(int item) {
	if (item < 0 || item >= item_count)
		return 0;
	Item *it = &items[item];
	if (it->charge_max <= 0)
		return 1; // (none counted: an older pack's, or one made here)
	const float cost = it->charge_cost > 0.0f ? it->charge_cost : 1.0f;
	if (it->charge_used + cost > (float)it->charge_max)
		return 0;
	it->charge_used += cost;
	return 1;
}

void items_add_charge(int item, float amount) {
	if (item >= 0 && item < item_count && items[item].charge_max > 0)
		items[item].charge_used = fmaxf(0.0f, items[item].charge_used - amount);
}

int items_recharge(int item) {
	if (item < 0 || item >= item_count || items[item].charge_max <= 0 || items[item].charge_used <= 0.0f)
		return 0;
	static const float SOUL_CHARGE[6] = {0.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 3000.0f}; // (Skyrim's: petty .. grand)
	int best = -1;
	for (int i = 0; i < item_count; i++)
		if (carried[i] > 0 && items_soul(i) > 0 && (best < 0 || items_soul(i) > items_soul(best)))
			best = i;
	if (best < 0)
		return 0;
	const int soul = items_soul(best) > 5 ? 5 : items_soul(best);
	items_add(best, -1);
	items[item].charge_used = fmaxf(0.0f, items[item].charge_used -
	                                perks_apply(EP_MOD_SOUL_GEM_RECHARGE, SOUL_CHARGE[soul], NULL)); // (Soul Squeezer: 250 more)
	return 1;
}
