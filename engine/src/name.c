#include "name.h"

#include <stdio.h>
#include <string.h>

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#define NAME_MAX_CHARS 24

static char current[32] = "Prisoner";
static uint16_t title16[32], initial16[32], text16[NAME_MAX_CHARS + 1];
static int running;

static void to16(const char *s, uint16_t *out, int max) {
	int n = 0;
	for (; s[n] && n < max - 1; n++)
		out[n] = (unsigned char)s[n];
	out[n] = 0;
}

// (the keyboard's answer: kept to the characters our fonts draw - Latin-1)
static void from16(const uint16_t *s, char *out, int max) {
	int n = 0;
	for (int i = 0; s[i] && n < max - 1; i++)
		if (s[i] >= 32 && s[i] < 256)
			out[n++] = (char)s[i];
	out[n] = 0;
}

int name_ask(const char *title) {
	if (running)
		return 1;
	SceImeDialogParam p;
	sceImeDialogParamInit(&p);
	to16(title, title16, 32);
	to16(current, initial16, 32);
	memset(text16, 0, sizeof(text16));
	p.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
	p.languagesForced = SCE_FALSE;
	p.type = SCE_IME_TYPE_DEFAULT;
	p.option = 0;
	p.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
	p.maxTextLength = NAME_MAX_CHARS;
	p.title = title16;
	p.initialText = initial16;
	p.inputTextBuffer = text16;
	running = sceImeDialogInit(&p) >= 0;
	return running;
}

int name_asking(void) {
	if (!running)
		return 0;
	if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
		return 1;
	SceImeDialogResult r;
	memset(&r, 0, sizeof(r));
	sceImeDialogGetResult(&r);
	if (r.button == SCE_IME_DIALOG_BUTTON_ENTER) {
		char got[32];
		from16(text16, got, sizeof(got));
		if (got[0])
			snprintf(current, sizeof(current), "%s", got);
	}
	sceImeDialogTerm();
	running = 0;
	return 0;
}

const char *name_get(void) {
	return current;
}

void name_set(const char *name) {
	snprintf(current, sizeof(current), "%s", name && name[0] ? name : "Prisoner");
}
