#ifndef CHARGEN_H
#define CHARGEN_H
#include <stdint.h>
// Skyrim's character creation on a race's "Custom" face (its bald base head): the head parts
// Skyrim lets that race and sex have (pack/chargen.txt, converter/chargen.py) - hair, beards,
// eyes, brows, scars - put on the player as worn gear (hair hides under a helmet), and the rest of
// Skyrim's race menu from the RACE record (pack/chargen/<race>_<m/f>.txt): its presets, skin tone
// and the face's tint layers (war paint, dirt, the colours of the lips, cheeks, eyes...), the
// complexion, the hair colour, the weight, the nose, eye and mouth types and the face's sliders
enum { CG_HAIR, CG_BEARD, CG_EYES, CG_BROWS, CG_SCAR, CG_KINDS };
#define CG_MORPHS 18     // Skyrim's NAM9: nose length, nose height, jaw height... (chargen.c MORPHS)
#define CG_TINT_TYPES 15 // Skyrim's tint layer types (TINP 1 lips .. 14 dirt; 0 unused)
enum { CG_TINT_LIPS = 1, CG_TINT_CHEEKS, CG_TINT_EYELINER, CG_TINT_SOCKET_UPPER, CG_TINT_SOCKET_LOWER, CG_TINT_SKIN,
       CG_TINT_PAINT, CG_TINT_LAUGH, CG_TINT_CHEEKS_LOWER, CG_TINT_NOSE, CG_TINT_CHIN, CG_TINT_NECK, CG_TINT_FOREHEAD,
       CG_TINT_DIRT };

// A character's look (saved with the game: save.h)
typedef struct {
	uint8_t version;               // 1 (0: none - an older save's parts only)
	uint8_t preset;                // the preset it began from
	int8_t pick[CG_KINDS];         // its head parts (-1 none)
	uint8_t hair_color, complexion, weight; // (weight 0 thin .. 100)
	uint8_t nose, eyes, mouth;     // types (Skyrim's NoseType<n>...; 0 none)
	int8_t morph[CG_MORPHS];       // the face's sliders, -100 .. 100
	struct {
		uint8_t layer;             // the race's tint layer (its index, TINI; 0 none)
		uint8_t color;             // ... its colour preset
		uint8_t value;             // ... how strong (0 .. 100)
	} tint[CG_TINT_TYPES];
	uint8_t reserved[20];
} ChargenLook; // 96 bytes

void chargen_init(const char *pack_dir);
const char *chargen_kind_name(int kind);             // ("Hair")
int chargen_count(const char *race_eid, int female, int kind); // choices of that part
// A look's defaults for a race and sex (its first hair, eyes, brows; no beard, no scar)
void chargen_defaults(const char *race_eid, int female, signed char pick[CG_KINDS]);
// Puts the picked parts (-1: none of that) on the player, the last ones off: after the
// player's body is loaded (equip_change_body); 0 if this race has no parts
int chargen_apply(const char *race_eid, int female, signed char pick[CG_KINDS]);
void chargen_clear(void); // (another face: the parts off)

// The race menu's (Skyrim's RACE data): loaded for a race and sex when first asked
int chargen_presets(const char *race_eid, int female);
int chargen_has_menu(const char *race_eid, int female); // (without loading it)            // presets (0: no data - an old pack)
int chargen_preset(const char *race_eid, int female, int n, ChargenLook *out); // a preset's look
int chargen_hair_colors(const char *race_eid, int female);
int chargen_complexions(const char *race_eid, int female);
// The race's tint layers of a type (war paint: one a pattern; the rest: one), and a layer's colours
int chargen_layers(const char *race_eid, int female, int type, int *tini_out, int max);
int chargen_layer_colors(const char *race_eid, int female, int tini);
int chargen_layer_color_value(const char *race_eid, int female, int tini, int color); // (its preset's strength, 0 .. 100)
// A type slider's choices (0 nose, 2 eyes, 3 mouth): the type numbers the race allows
int chargen_types(const char *race_eid, int female, int group, uint8_t *out, int max);
// The whole look on the player (after equip_change_body): its parts, morphs, skin and hair colours
// and the face's texture made from its tint layers. 0 if the race has no menu data
int chargen_apply_look(const char *race_eid, int female, ChargenLook *look);
// Only its colours and face texture again (a colour slider: no parts or morphs changed)
void chargen_apply_colors(const char *race_eid, int female, const ChargenLook *look);
// Only its morphs again (a face slider, the weight)
void chargen_apply_morphs(const ChargenLook *look);
#endif
