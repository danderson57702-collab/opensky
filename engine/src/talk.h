#ifndef TALK_H
#define TALK_H

// NPC speech (pack/actors/*.sktalk, see converter/dialogue.py): a passing "hello" when
// you walk up to someone, and their own lines when you talk to them (talk button while
// facing them). Lines are voiced (voice.h) with subtitles (ui.h); the music dips while
// someone speaks, and the voice fades and pans with where they are.

void talk_init(const char *pack_dir);
// The person under the crosshair (look ray from eye, dir unit) within talking range (-1:
// nobody) and their distance along the ray; anyone's name
int talk_aim(const float eye[3], const float dir[3], float *along);
const char *talk_name(int i);
int talk_named(int i); // (their own name, from their lines: not just their editor id)
// Each frame: lines going on, passing hellos; talk_to: someone the talk button was pressed
// on this frame (talk_aim's pick; -1: none). Returns 1 if they started talking.
int talk_update(float dt_s, float player_x, float player_z, float yaw_deg, int talk_to);

// A quest's scene: someone says these lines in turn (voice files under voice/, subtitles; the
// texts are kept by pointer), cutting off what they were saying
int talk_scene(int who, const char *const *paths, const char *const *texts, int n);
// No passing hellos for this long (a scene going on)
void talk_hush(float seconds);
// Who is speaking now (-1: no one)
int talk_speaking(void);

#endif
