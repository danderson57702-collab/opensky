#include "save.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/rtc.h>

#include "fileio.h"

#define SAVE_DIR "ux0:data/opensky_saves/"
#define OLD_SAVE_PATH "ux0:data/opensky_save.dat"   // (before slots)
#define OLD_ITEMS_PATH "ux0:data/opensky_items.dat"
#define SAVE_VERSION 22
#define SLOT_VERSION 1

typedef struct {
	char magic[4]; // "SKSV"
	uint16_t version, size;
	SaveGame game;
} SaveFile;

// A slot's file: this, then its SaveFile
typedef struct {
	char magic[4]; // "SKSL"
	uint16_t version, autosave;
	uint32_t seq;
	char place[48], race[24], date[32];
	int32_t level;
	float hours;
} SlotHead;

static unsigned next_seq = 1;
static int last_version; // the version of the game save_read read
static char last_date[32]; // ... and when it was made

static void slot_path(int slot, const char *ext, char *out, size_t n) {
	snprintf(out, n, SAVE_DIR "%02d.%s", slot, ext);
}

// Writes beside the old file, then replaces it: a crash mid-write keeps the old one
static int write_file(const char *path, const void *a, int an, const void *b, int bn) {
	char tmp[160];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	SceUID fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) {
		sceClibPrintf("save: can't write %s (0x%08X)\n", path, fd);
		return 0;
	}
	int ok = sceIoWrite(fd, a, an) == an && (!b || sceIoWrite(fd, b, bn) == bn);
	sceIoClose(fd);
	if (!ok)
		return 0;
	sceIoRemove(path);
	return sceIoRename(tmp, path) >= 0;
}

// A SaveFile's game (any version still read) into g; 1 if valid
static int parse_game(const uint8_t *d, long size, SaveGame *g) {
	const SaveFile *f = (const SaveFile *)d;
	const long head = (long)offsetof(SaveFile, game);
	int ok = d && size >= head && !memcmp(f->magic, "SKSV", 4);
	if (ok && f->version == SAVE_VERSION)
		ok = f->size == sizeof(SaveGame) && size >= (long)sizeof(SaveFile);
	else if (ok && f->version == 21) // (before the journal's inactive quests and the favourites)
		ok = f->size == SAVE_GAME_V21_SIZE && size >= head + (long)SAVE_GAME_V21_SIZE;
	else if (ok && f->version == 20) // (before the character's whole look: its parts only)
		ok = f->size == SAVE_GAME_V20_SIZE && size >= head + (long)SAVE_GAME_V20_SIZE;
	else if (ok && f->version == 19) // (before the College and the Thieves Guild)
		ok = f->size == SAVE_GAME_V19_SIZE && size >= head + (long)SAVE_GAME_V19_SIZE;
	else if (ok && f->version == 18) // (before the Companions)
		ok = f->size == SAVE_GAME_V18_SIZE && size >= head + (long)SAVE_GAME_V18_SIZE;
	else if (ok && f->version == 17) // (before the main quest's last eight)
		ok = f->size == SAVE_GAME_V17_SIZE && size >= head + (long)SAVE_GAME_V17_SIZE;
	else if (ok && f->version == 16) // (before the last five shouts)
		ok = f->size == SAVE_GAME_V16_SIZE && size >= head + (long)SAVE_GAME_V16_SIZE;
	else if (ok && f->version == 15) // (before the townspeople could die)
		ok = f->size == SAVE_GAME_V15_SIZE && size >= head + (long)SAVE_GAME_V15_SIZE;
	else if (ok && f->version == 14) // (before the other shouts' words)
		ok = f->size == SAVE_GAME_V14_SIZE && size >= head + (long)SAVE_GAME_V14_SIZE;
	else if (ok && f->version == 13) // (before the interior kept by its name)
		ok = f->size == SAVE_GAME_V13_SIZE && size >= head + (long)SAVE_GAME_V13_SIZE;
	else if (ok && f->version == 12) // (before the word walls remembered)
		ok = f->size == SAVE_GAME_V12_SIZE && size >= head + (long)SAVE_GAME_V12_SIZE;
	else if (ok && f->version == 11) // (before horses)
		ok = f->size == SAVE_GAME_V11_SIZE && size >= head + (long)SAVE_GAME_V11_SIZE;
	else if (ok && f->version == 10) // (before jail cells and followers)
		ok = f->size == SAVE_GAME_V10_SIZE && size >= head + (long)SAVE_GAME_V10_SIZE;
	else if (ok && f->version == 9) // (before crime: no bounties)
		ok = f->size == SAVE_GAME_V9_SIZE && size >= head + (long)SAVE_GAME_V9_SIZE;
	else if (ok && f->version == 8) // (before the character creator: the face's own look)
		ok = f->size == SAVE_GAME_V8_SIZE && size >= head + (long)SAVE_GAME_V8_SIZE;
	else if (ok && f->version == 7) // (before names: "Prisoner")
		ok = f->size == SAVE_GAME_V7_SIZE && size >= head + (long)SAVE_GAME_V7_SIZE;
	else if (ok && f->version == 6) // (before quests: none begun)
		ok = f->size == SAVE_GAME_V6_SIZE && size >= head + (long)SAVE_GAME_V6_SIZE;
	else if (ok && f->version == 5) // (before the tome spells cast here: 24 learned, hands the seven's)
		ok = f->size == SAVE_GAME_V5_SIZE && size >= head + (long)SAVE_GAME_V5_SIZE;
	else if (ok && f->version == 4) // (before spell tomes: every spell known, no books read)
		ok = f->size == SAVE_GAME_V4_SIZE && size >= head + (long)SAVE_GAME_V4_SIZE;
	else if (ok && f->version == 3) // (before perks: none taken)
		ok = f->size == SAVE_GAME_V3_SIZE && size >= head + (long)SAVE_GAME_V3_SIZE;
	else if (ok && f->version == 2) // (before races: the first race and face)
		ok = f->size == SAVE_GAME_V2_SIZE && size >= head + (long)SAVE_GAME_V2_SIZE;
	else if (ok && f->version == 1) // (before skills: the rest stays zero, meaning fresh)
		ok = f->size == SAVE_GAME_V1_SIZE && size >= head + (long)SAVE_GAME_V1_SIZE;
	else
		ok = 0;
	if (ok) {
		last_version = f->version;
		memset(g, 0, sizeof(*g));
		memcpy(g, &f->game, f->size);
		ok = g->item_count >= 0 && g->item_count <= SAVE_ITEMS;
	}
	return ok;
}

static int read_head(int slot, SlotHead *h) {
	char path[128];
	slot_path(slot, "sav", path, sizeof(path));
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return 0;
	int ok = sceIoRead(fd, h, sizeof(*h)) == (int)sizeof(*h) && !memcmp(h->magic, "SKSL", 4) &&
	         h->version == SLOT_VERSION;
	sceIoClose(fd);
	h->place[sizeof(h->place) - 1] = h->race[sizeof(h->race) - 1] = h->date[sizeof(h->date) - 1] = 0;
	return ok;
}

static void copy_file(const char *from, const char *to) {
	long size = 0;
	void *d = file_read_all(from, &size);
	if (d)
		write_file(to, d, (int)size, NULL, 0);
	free(d);
}

void save_init(void) {
	sceIoMkdir("ux0:data/opensky_saves", 0777);
	int any = 0;
	for (int s = 0; s < SAVE_SLOTS; s++) {
		SlotHead h;
		if (read_head(s, &h)) {
			any = 1;
			if (h.seq >= next_seq)
				next_seq = h.seq + 1;
		}
	}
	if (any)
		return;
	// The save from before slots: into the first manual slot (the old files stay)
	long size = 0;
	uint8_t *d = file_read_all(OLD_SAVE_PATH, &size);
	SaveGame g;
	if (d && parse_game(d, size, &g)) {
		SlotHead h;
		memset(&h, 0, sizeof(h));
		memcpy(h.magic, "SKSL", 4);
		h.version = SLOT_VERSION, h.seq = next_seq++;
		snprintf(h.place, sizeof(h.place), "Earlier save");
		h.level = g.player_level > 0 ? g.player_level : 1, h.hours = g.hours;
		char path[128];
		slot_path(SAVE_AUTOS, "sav", path, sizeof(path));
		if (write_file(path, &h, sizeof(h), d, (int)size))
			copy_file(OLD_ITEMS_PATH, save_items_path(SAVE_AUTOS));
		sceClibPrintf("save: the earlier save is now slot %d\n", SAVE_AUTOS);
	}
	free(d);
}

int save_list(SaveInfo *out, int max) {
	int n = 0;
	for (int s = 0; s < SAVE_SLOTS && n < max; s++) {
		SlotHead h;
		if (!read_head(s, &h))
			continue;
		SaveInfo *i = &out[n++];
		memset(i, 0, sizeof(*i));
		i->slot = s, i->seq = h.seq, i->autosave = h.autosave, i->level = h.level, i->hours = h.hours;
		snprintf(i->place, sizeof(i->place), "%s", h.place);
		snprintf(i->race, sizeof(i->race), "%s", h.race);
		snprintf(i->date, sizeof(i->date), "%s", h.date);
	}
	for (int a = 1; a < n; a++) // newest first
		for (int b = a; b > 0 && out[b].seq > out[b - 1].seq; b--) {
			SaveInfo t = out[b];
			out[b] = out[b - 1], out[b - 1] = t;
		}
	return n;
}

int save_latest(void) {
	SaveInfo list[SAVE_SLOTS];
	return save_list(list, SAVE_SLOTS) > 0 ? list[0].slot : -1;
}

// The slot a new save goes in: the next autosave's (a free one, or the oldest of them), or a
// free manual slot (none free: the oldest manual save is replaced)
static int pick_slot(int autosave) {
	const int lo = autosave ? 0 : SAVE_AUTOS, hi = autosave ? SAVE_AUTOS : SAVE_SLOTS;
	int oldest = lo;
	unsigned oldest_seq = 0xffffffffu;
	for (int s = lo; s < hi; s++) {
		SlotHead h;
		if (!read_head(s, &h))
			return s;
		if (h.seq < oldest_seq)
			oldest_seq = h.seq, oldest = s;
	}
	return oldest;
}

int save_write(int slot, const SaveGame *g, const SaveInfo *info, const uint16_t *thumb) {
	const int autosave = slot == SAVE_AUTO;
	if (slot < 0)
		slot = pick_slot(autosave);
	if (slot >= SAVE_SLOTS)
		return -1;
	SaveFile f;
	memset(&f, 0, sizeof(f));
	memcpy(f.magic, "SKSV", 4);
	f.version = SAVE_VERSION, f.size = sizeof(SaveGame);
	f.game = *g;
	SlotHead h;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, "SKSL", 4);
	h.version = SLOT_VERSION, h.autosave = (uint16_t)autosave, h.seq = next_seq++;
	if (info) {
		snprintf(h.place, sizeof(h.place), "%s", info->place);
		snprintf(h.race, sizeof(h.race), "%s", info->race);
		h.level = info->level, h.hours = info->hours;
	}
	SceDateTime now;
	if (sceRtcGetCurrentClockLocalTime(&now) >= 0)
		snprintf(h.date, sizeof(h.date), "%04d-%02d-%02d %02d:%02d", now.year, now.month, now.day, now.hour,
		         now.minute);
	char path[128];
	slot_path(slot, "sav", path, sizeof(path));
	if (!write_file(path, &h, sizeof(h), &f, sizeof(f)))
		return -1;
	slot_path(slot, "thumb", path, sizeof(path));
	if (thumb)
		write_file(path, thumb, SAVE_THUMB_W * SAVE_THUMB_H * 2, NULL, 0);
	else
		sceIoRemove(path);
	return slot;
}

int save_read(int slot, SaveGame *g) {
	if (slot < 0 || slot >= SAVE_SLOTS)
		return 0;
	char path[128];
	slot_path(slot, "sav", path, sizeof(path));
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	int ok = d && size > (long)sizeof(SlotHead) && !memcmp(d, "SKSL", 4) &&
	         parse_game(d + sizeof(SlotHead), size - (long)sizeof(SlotHead), g);
	if (ok)
		snprintf(last_date, sizeof(last_date), "%.31s", ((const SlotHead *)d)->date);
	free(d);
	return ok;
}

const char *save_read_date(void) {
	return last_date;
}

int save_read_thumb(int slot, uint16_t *out) {
	char path[128];
	slot_path(slot, "thumb", path, sizeof(path));
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	int ok = d && size == SAVE_THUMB_W * SAVE_THUMB_H * 2;
	if (ok)
		memcpy(out, d, size);
	free(d);
	return ok;
}

const char *save_items_path(int slot) {
	static char path[128];
	slot_path(slot, "items", path, sizeof(path));
	return path;
}

int save_read_version(void) {
	return last_version;
}
