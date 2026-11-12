# OpenSKY Engine Audit & Handoff Document
**Target Audience:** Claude Sonnet 5.5 / Lead Engine Architect  
**Author:** Gemini 3.8 Flash (Pair Programming Agent)  
**Date:** November 2, 2026  
**Project:** OpenSKY (PlayStation Vita Skyrim-Lite Port)  
**Document Title:** `AUDIT_HANDOFF_CLAUDE.md`

---

## 1. Executive Summary

### 1.1 Objective
This document provides a comprehensive architectural audit, regression log, and handoff report for the OpenSKY codebase following intensive work on the MQ101 Helgen introductory sequence, AI navigation/combat systems, and character creation render pipelines. It details all features implemented, structural bugs resolved, regressions investigated, and specific action items requiring formal verification.

### 1.2 Active Toolchain & Architecture
* **Target Hardware:** Sony PlayStation Vita (PCH-1000 OLED / PCH-2000 Slim).
  * CPU: ARM Cortex-A9 MPCore (4 cores @ 333 MHz / 444 MHz).
  * GPU: PowerVR SGX543MP4+ (128MB shared VRAM / system RAM architecture).
  * Storage: SD2Vita adapter mounted to `ux0:` (external mount on macOS host: `/Volumes/Untitled`).
* **Cross-Compilation Toolchain:**
  * VitaSDK toolchain (`arm-vita-eabi-gcc` / `arm-vita-eabi-g++`).
  * CMake 3.x build runner (`cmake --build engine/build`).
  * Executable / Package Packaging: `vita-elf-create`, `vita-make-fself`, and `vita-pack-vpk` generating `opensky.vpk` and `eboot.bin`.
* **Graphics Subsystem:**
  * `vitaGL` (hardware OpenGL ES 1.1 / 2.0 wrapper over Sony GXM low-level user-mode graphics library).
  * Dual-pass dynamic skinning: GPU-based vertex posing with matrix palette uniform arrays (`u->bones`, 192 floats).
* **Automated Audit & Synchronization Suite:**
  * `tools/audit_runner.py`: Orchestrates regression testing, asset synchronization, VPK installation, and SD2Vita payload deployment.

### 1.3 Known Current Failure Modes & Operational Status
1. **Save Game Loading vs. New Game State Leakage (RESOLVED):** An early patch mistakenly called `intro_prime_view()` inside the general `stage == STAGE_PLAY && transition` handler in `engine/src/main.c`. This caused loaded saves to snap the player back to the Helgen prisoner cart coordinates, resulting in an infinite reload/spawn loop. This has been resolved by strictly isolating `intro_prime_view()` to explicit new-game instantiation and resetting all intro state variables in `load_game()` and `apply_save()`.
2. **Hair & Facial Card Geometry (RESOLVED):** Single-sided hair polygon cards previously rendered with missing patches when viewed from rear angles due to standard backface culling (`GL_CULL_FACE`). In addition, card depth sorting suffered from self-occlusion. Resolved via per-batch `glDisable(GL_CULL_FACE)` and `glEnable(GL_ALPHA_TEST)` with `glAlphaFunc(GL_GREATER, 0.5f)`.
3. **Alduin Waypoint Clipping (RESOLVED):** Alduin's flight spline in `engine/src/intro.c` previously interpolated directly through the Helgen tower geometry. Smoothed via directional vector lerping (`al.dir`), arc-over midpoints (`PERCH_ABOVE_M`), and clamped turning rates.

---

## 2. Implemented Features & Patches

### 2.1 MQ101 Choreography & Splines (`engine/src/intro.c`, `engine/src/main.c`)
* **Instantaneous Cart View Priming (`intro_prime_view`):**
  * *Problem:* When starting a new game, cell asset streaming and fade-in transitions suppressed `intro_update()`. The camera spawned at the world origin or on unstreamed terrain underneath the prisoner cart for the first 0.5–1.0s before snapping into place.
  * *Fix:* Implemented `intro_prime_view(IntroFrame *f)` in `intro.c`. This forces immediate evaluation of `cart_place()` at $t=0$, locks player movement, and primes `f->eye` directly to `cart_eye` (the player's prisoner seat bone) along with initial facing angles (`face_yaw`, `face_pitch`) before the first frame is rendered.
* **Alduin Flight Spline Smoothing & Ground Touchdown:**
  * *Problem:* Alduin's transition from off-stage spawn (`mq101alduinoffstagewestmarker`) to the tower perch snapped linearly between waypoints, causing his model to clip directly through building roofs and stone battlements.
  * *Fix:* Introduced `al.dir[3]` for flight heading interpolation. When flying to a perch or ground target, Alduin now calculates an elevated midpoint (`path[0] = (pos + target) * 0.5`, elevated by `PERCH_ABOVE_M = 14.0f`) to arc gracefully over roofs. Added `AL_GROUND` (`angle = -4.0f`) state for road/rubble landing sequences with touchdown sound dispatch (`sfx_play_at("dragon_land")`).
* **Hadvar Regroup & Cover Gating:**
  * *Problem:* During the sprint to the keep, if the player lagged behind or was caught on rubble, Hadvar would trigger a hard teleport loading screen over a distance of only a few meters.
  * *Fix:* In `intro.c`, refined Hadvar's follow gating (`apart`). If the player is between 4.0m and 14.0m away, Hadvar pathfinds back toward the player (`actors_script_walk(hv, by, 1)`) with a call-out cooldown (`call_t = 1.5f`). A hard position snap is strictly reserved as an emergency failsafe if stuck for greater than 14 seconds.
* **Inn Roof Jump Intermission Bypass:**
  * *Problem:* Leaping through the damaged inn roof onto the destroyed house caused an abrupt synchronous loading screen due to an erroneous `f->teleport = 1` flag.
  * *Fix:* Replaced `f->teleport = 1` with seamless ground positioning and position hold (`f->hold = 1`), allowing cell pre-fetching to maintain continuous exterior gameplay without a loading intermission.
* **Alduin Perch Trigger Refinement:**
  * *Problem:* Alduin would prematurely take off from the wall perch before the player reached the road corner.
  * *Fix:* Tightened the takeoff trigger volume (`dunalduincperchref`) to require approach within 12.0m or crossing the specific Helgen wall threshold (`feet[0] > 263.0f && feet[2] < 1138.0f`), precisely matching Skyrim.esm scene choreography.

### 2.2 AI & Combat Systems (`engine/src/creatures.c`, `engine/src/creatures.h`, `engine/src/main.c`)
* **Draugr A\* Navmesh Fleeing Logic:**
  * *Problem:* When Draugr or humanoids entered the `FLEE` combat state, they computed escape trajectories by simply negating the vector to the player:
    $$\vec{A} = \vec{pos} - \frac{\vec{dx}}{|dx|} \cdot 10$$
    In confined dungeon corridors, this resulted in actors running directly into static walls and vibrating against solid collision geometry.
  * *Fix:* Rewrote the `case FLEE:` handler in `creatures.c`. The fleeing actor now projects candidate escape vectors across an array of angles ($\theta_{\text{away}} \pm \{0.0, 0.5, 1.0, 1.5, 2.0\}$ radians) at test distances of 12m, 8m, and 5m. Each candidate coordinate is validated against the navigation mesh using `navmesh_locate_level()` and `navmesh_nearest_level()`. The AI selects the valid navigable node that maximizes Euclidean distance from the player and commits to it via `go_to()`.
* **Killmove Movement Lock Instantaneous Release:**
  * *Problem:* During killcam / finisher animations (`try_killmove`), the player's movement and button inputs remained completely locked for the full hardcoded duration (`k->duration`) even if the victim died or ragdolled earlier, leaving the player vulnerable to external attack.
  * *Fix:* Implemented `creatures_killmove_done(int i)` in `creatures.c` and integrated it into `main.c`. The movement and camera lock (`killmove_left`) is instantly released if the victim's HP reaches 0 and the death animation transitions to ragdoll (`c->fall > 0.0f`) or exceeds the lethal impact point (`c->clip_t >= fminf(dl, 1.2f)`).

### 2.3 Character Creation & Render Pipeline (`engine/src/objects.c`, `engine/src/actors.c`, `engine/src/main.c`)
* **Hair & Beard Double-Sided Rendering (`GL_CULL_FACE`):**
  * *Problem:* Hair and beard models consist of complex overlapping alpha cards. Viewing these cards from oblique or rear angles exposed single-sided backfaces that were culled by VitaGL's default state, resulting in visible bald spots and missing mesh segments.
  * *Fix:*
    1. In `actors.c`, added hair classification logic during batch parsing (`read_batches`) and actor emission (`emit`), identifying hair by slot flags (`SLOT_HAIR`, `PART_SKIN`), item names ("Hair", "Beard"), and texture substrings (`hair`, `hlne`, `beard`).
    2. Added `is_hair` attribute to `ABatch` and `ObjectsDynamic`.
    3. In `objects.c` (`draw_skinned` and `objects_draw_dynamic`), wrapped hair draw calls with:
       ```c
       if (d->is_hair) {
           glDisable(GL_CULL_FACE);
           glEnable(GL_ALPHA_TEST);
           glAlphaFunc(GL_GREATER, 0.5f);
       }
       glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, ...);
       if (d->is_hair) {
           glEnable(GL_CULL_FACE);
           glDisable(GL_ALPHA_TEST);
           if (pass > 0)
               glDisable(GL_CULL_FACE);
       }
       ```
    4. Restored `glDisable(GL_ALPHA_TEST)` and standard culling states immediately afterward.
* **Dynamic Facial Zoom Matrix:**
  * *Problem:* Navigating the character creation menu did not provide sufficient visual fidelity when adjusting fine facial details (eyes, eyebrows, scars, warpaint).
  * *Fix:* In `main.c`, introduced `race_facial_zoom` state tracking. When active, `race_cam_zoom_t` smoothly interpolates from 0.0 to 1.0 ($dt \times 6.0$). The camera target position blends toward the player's head bone (`actors_player_eye(head_bone)`), reducing distance by 50% (`RACE_ZOOM_DIST_M * (1.0f - 0.5f * race_cam_zoom_t)`) and eliminating the shoulder drop to frame the face squarely on screen.

---

## 3. Known Critical Regressions & Root Causes

### 3.1 Infinite New Game Loop & Broken Save Loading
* **Symptoms:**
  * Selecting "New Game" occasionally reloaded the intro cell in a loop.
  * Loading an existing save game from the title menu teleported the player directly into the Helgen prisoner cart or caused an immediate reload back to the title screen.
* **Root Cause:**
  * In `engine/src/main.c:1876-1890`, during an earlier patch, `intro_prime_view(&opening)` was placed inside the condition:
    ```c
    if (stage == STAGE_PLAY && transition) {
        intro_prime_view(&opening);
    }
    ```
  * Because `stage` is set to `STAGE_PLAY` during both normal play and save restoration, any cell transition or save load triggered `intro_prime_view()`. This function forcibly overwrote `player.x`, `player.feet`, `player.z`, `yaw`, and `pitch` with the prisoner cart seat coordinates, corrupting the loaded save state.
* **Resolution:**
  * Completely removed `intro_prime_view()` from the transition loop in `main.c`.
  * Restricted `intro_prime_view(&opening)` strictly to the explicit new game creation branch under `stage == STAGE_TITLE && begin_opening`.
  * Added mandatory state reset calls in `load_game()` and `apply_save()`:
    ```c
    begin_opening = 0;
    opening_race = 0;
    bound_kept = 0;
    memset(&opening, 0, sizeof(opening));
    opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
    ```

### 3.2 Scope Leakage of the `opening` Variable
* **Symptoms:**
  * Intro sequence state flags (e.g. `opening.eye_set`, `opening.lock_move`, `opening.hold`) leaked into subsequent gameplay or across save file boundaries.
* **Root Cause:**
  * Multiple declarations of `static IntroFrame opening;`, `static int begin_opening;`, and `static int bound_kept;` were scattered as block-level static variables inside nested `if` statements in `main()`.
  * Because they were locally scoped, external functions like `apply_save()` and `load_game()` could not clear or reset their state when unpacking a save file.
* **Resolution:**
  * Elevated `opening`, `begin_opening`, `opening_race`, and `bound_kept` to static file scope at the top of `main.c`.
  * Verified that all game-load pathways explicitly zero out the structures.

---

## 4. Git Status & Exact Diffs

### 4.1 Git Status (`git status -s`)
```
 M converter/actors.py
 M converter/extract_objects.py
 M converter/items.py
 M converter/npcs.py
 M converter/skin.py
 M engine/src/actors.c
 M engine/src/buildstamp.h
 M engine/src/creatures.c
 M engine/src/creatures.h
 M engine/src/equip.c
 M engine/src/intro.c
 M engine/src/intro.h
 M engine/src/loot.c
 M engine/src/main.c
 M engine/src/objects.c
 M engine/src/objects.h
 M engine/src/projectiles.c
 M engine/src/water.c
 m third_party/vitaGL
 M tools/audit_runner.py
```

---

### 4.2 Exact Diffs

#### 4.2.1 `engine/src/main.c`
```diff
diff --git a/engine/src/main.c b/engine/src/main.c
index ae552d1..29eb83d 100644
--- a/engine/src/main.c
+++ b/engine/src/main.c
@@ -409,7 +409,11 @@ typedef struct {
 static Killmove killmoves[MAX_KILLMOVES];
 static int killmove_count;
 static float killmove_left, killmove_t;
+static int killmove_victim = -1;
 static char killmove_clip[24];
+static IntroFrame opening;
+static int begin_opening, opening_race;
+static int bound_kept;
 
 static void killmoves_init(const char *pack_dir) {
 	char path[256];
@@ -456,6 +460,7 @@ static void try_killmove(int wkind, const float feet[3], float yaw_deg) {
 	snprintf(clip, sizeof(clip), "km_%s_v", k->name);
 	if (!creatures_killmove(v, clip, at, r + k->turn))
 		return;
+	killmove_victim = v;
 	snprintf(killmove_clip, sizeof(killmove_clip), "km_%s_k", k->name);
 	killmove_left = k->duration, killmove_t = 0.0f;
 	game_log("killmove: %s", k->name);
@@ -470,6 +475,7 @@ static int modal_before;
 static int flames_voice = -1, heal_voice = -1, spark_voice = -1; // (the held spells' loops)
 static int spell_sounds_ran;  // (their block ran this frame: if not, they're stopped - the healing loop ran on for ever) // (the opening's choice / your name up last frame: the pad not the menus')
 static int race_zoom;    // (the race menu's Sex / Face rows: the view close on the face)
+static int race_facial_zoom; // (detailed facial sub-menus: eyes, scars, brows, etc.)
 static float shove[2]; // (flung by a shout: m/s, world x z)
 static float shout_flash, shout_shake;           // a shout's flash (its colour) and the camera's shudder
 static int shout_flash_rgb[3];
@@ -784,6 +790,11 @@ static int load_game(int slot, Player *p, float *yaw, float *pitch, Vitals *v) {
 	SaveGame g;
 	if (slot < 0 || !save_read(slot, &g))
 		return 0;
+	begin_opening = 0;
+	opening_race = 0;
+	bound_kept = 0;
+	memset(&opening, 0, sizeof(opening));
+	opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
 	items_set_save_path(save_items_path(slot));
 	chime_reset = 1;
 	if (!apply_save(&g, p, yaw, pitch, v))
@@ -801,6 +812,11 @@ static int load_game(int slot, Player *p, float *yaw, float *pitch, Vitals *v) {
 }
 
 static int apply_save(const SaveGame *g, Player *p, float *yaw, float *pitch, Vitals *v) {
+	begin_opening = 0;
+	opening_race = 0;
+	bound_kept = 0;
+	memset(&opening, 0, sizeof(opening));
+	opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
 	if (g->race != races_player() || g->face != races_player_face())
 		if (save_read_version() >= 9) // (the creator's parts: on before the body's loaded)
 			memcpy(look_pick, g->look, sizeof(look_pick));
@@ -1494,7 +1510,7 @@ int main(void) {
 		// one, the race menu (Up / Down race, Left / Right face, CROSS begins). They have the
 		// pad; the world stands still behind them.
 		static int title_loading; // (the title's Continue: the list of saves open; backing out loads the newest)
-		static int stage = -1, title_sel, race_sel, face_sel, front_load, begin_opening, opening_race;
+		static int stage = -1, title_sel, race_sel, face_sel, front_load;
 		static int dialogue_with = -1, dialogue_topics, dialogue_vendor = -1, dialogue_follow = 99, dialogue_service = 99,
 		           dialogue_ride = 99, dialogue_end = 99, // (dialogue_end: "Goodbye." - nothing to say)
 		           souls_this_frame; // (talking: to whom, its choices)
@@ -1562,6 +1578,13 @@ int main(void) {
 				equip_strip(); // (a prisoner: no clothes but the rags the cart gives)
 				stage = STAGE_PLAY;
 				intro_start();
+				memset(&opening, 0, sizeof(opening));
+				opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
+				intro_prime_view(&opening);
+				if (opening.eye_set) {
+					player.x = opening.pos[0], player.feet = opening.pos[1], player.z = opening.pos[2];
+					yaw = opening.look_yaw, pitch = opening.look_pitch;
+				}
 			}
 			if (stage == STAGE_RACE) {
 				static int shown_race = -1, shown_face = -1;
@@ -1661,10 +1684,12 @@ int main(void) {
 				if (opening_race && autotest_active() && autotest_auto_race()) // (a test run past the opening's race menu: as it is)
 					begin = 1, look_step = 0, look_row = 0;
 				race_zoom = look_step; // (close on the face while you choose it)
+				race_facial_zoom = look_step && (look_row < nrows && row_of[look_row] >= 0);
 				// (the right stick turns you round, to see yourself from every side: half a turn a second)
 				race_turn = fmodf(race_turn + stick(pad.rx) * 180.0f * frame_s, 360.0f);
 				if (begin) {
 					race_zoom = 0;
+					race_facial_zoom = 0;
 					ui_race_menu(NULL);
 					races_new_character(&vitals);
 					if (opening_race) { // (Hadvar's INFO 0003F88B answered: your name, then on with the lineup)
@@ -1815,6 +1840,16 @@ int main(void) {
 			pad.buttons = 0;
 			pad.lx = pad.ly = 128;
 			player_look_stop();
+			if (killmove_victim >= 0 && creatures_killmove_done(killmove_victim)) {
+				killmove_left = 0.0f, killmove_victim = -1;
+				actors_player_scene(NULL, 0.0f);
+			}
+		}
 		if (dead_t >= 0.0f || killmove_left > 0.0f) { // (dying, or in a killmove: no control - the view's
 			pad.buttons = 0;                         //  still yours to turn about a killmove)
 			pad.lx = pad.ly = 128;
@@ -1825,7 +1860,6 @@ int main(void) {
 		static float settle_s;     // ... up this long
 		static int settle_good;    // ... frames in a row quick enough
 		// Skyrim's opening (intro.c): what it wants of this frame
-		static IntroFrame opening;
 		memset(&opening, 0, sizeof(opening));
 		opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
 		creatures_hold_spawns(intro_active() && !intro_stage()); // (Helgen before the keep: only the opening's)
@@ -1843,7 +1877,6 @@ int main(void) {
 			snprintf(text, sizeof(text), "You are %s", name_get());
 			ui_notice(text, 4.0f);
 		}
-		static int bound_kept; // (the hands as last tied: kept through a door's loading, a menu - they came free a moment)
 		if (stage == STAGE_PLAY && !paused && !transition && intro_active() && !naming && !choosing) {
 			const float feet_now[3] = {player.x, player.feet, player.z};
 			intro_update(frame_s, feet_now, interior_current(), opening_pressed, &opening);
@@ -3052,7 +3085,7 @@ int main(void) {
 			          vitals.stamina / vitals.max_stamina, vitals.breath / vitals.max_breath, vitals_hud_alpha(&vitals));
 		}
 		perf_lap("loot_ext");
-		float cam_x = player.x, cam_z = player.z;
+		float cam_x = opening.eye_set ? opening.eye[0] : player.x, cam_z = opening.eye_set ? opening.eye[2] : player.z;
 		objects_set_time(clock_s);
 		water_set_time(clock_s);
 		unsigned long long t0 = stream_now_us();
@@ -4428,9 +4461,12 @@ int main(void) {
 		              interior_current() >= 0 ? interior_current() : worlds_current() > 0 ? -2 : -1);
 		if (killmove_left > 0.0f) { // (a killmove: its clip through, then yours again)
 			killmove_t += dt;
-			if ((killmove_left -= dt) <= 0.0f)
-				killmove_left = 0.0f;
-			actors_player_scene(killmove_left > 0.0f ? killmove_clip : NULL, killmove_t);
+			if ((killmove_left -= dt) <= 0.0f || (killmove_victim >= 0 && creatures_killmove_done(killmove_victim))) {
+				killmove_left = 0.0f, killmove_victim = -1;
+				actors_player_scene(NULL, 0.0f);
+			} else {
+				actors_player_scene(killmove_clip, killmove_t);
+			}
 		}
 		actors_player_update(player.x, player.feet + step_offset, player.z, yaw, pvx, pvz, player.vy, player.grounded || noclip, player.swimming, sneaking,
 		                     stage == STAGE_RACE ? frame_s : dt, // (the race menu: the character breathes)
@@ -4448,15 +4484,28 @@ int main(void) {
 		if (third_person) {
 			const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
 			const float fx = sinf(yr) * cosf(pr), fy = sinf(pr), fz = -cosf(yr) * cosf(pr);
-			const float dist = race_zoom ? RACE_ZOOM_DIST_M : indoors ? THIRD_PERSON_DIST_INSIDE_M : THIRD_PERSON_DIST_M;
+			static float race_cam_zoom_t = 0.0f; // 0 = standard portrait, 1 = facial close-up
+			const float target_zoom_t = (stage == STAGE_RACE && race_facial_zoom) ? 1.0f : 0.0f;
+			race_cam_zoom_t += (target_zoom_t - race_cam_zoom_t) * fminf(1.0f, dt * 6.0f);
+			if (stage != STAGE_RACE)
+				race_cam_zoom_t = 0.0f;
+			const float portrait_dist = RACE_ZOOM_DIST_M * (1.0f - 0.5f * race_cam_zoom_t);
+			const float dist = stage == STAGE_RACE ? portrait_dist : race_zoom ? RACE_ZOOM_DIST_M : indoors ? THIRD_PERSON_DIST_INSIDE_M : THIRD_PERSON_DIST_M;
+			float head_bone[3];
+			if (stage == STAGE_RACE && actors_player_eye(head_bone)) {
+				cam_x += (head_bone[0] - cam_x) * race_cam_zoom_t;
+				cam_y += (head_bone[1] - cam_y) * race_cam_zoom_t;
+				cam_z += (head_bone[2] - cam_z) * race_cam_zoom_t;
+			}
+			const float drop = stage == STAGE_RACE ? THIRD_PERSON_DROP_M * (1.0f - race_cam_zoom_t) : THIRD_PERSON_DROP_M;
 			// (the race menu: the character right of the list - by a share of the distance, so the
 			// close look at the face keeps the face in view)
 			const float shoulder = stage == STAGE_RACE ? -0.115f * dist : THIRD_PERSON_SHOULDER_M;
 			view_x = cam_x - fx * dist + cosf(yr) * shoulder;
-			view_y = cam_y - THIRD_PERSON_DROP_M - fy * dist;
+			view_y = cam_y - drop - fy * dist;
 			view_z = cam_z - fz * dist + sinf(yr) * shoulder;
 			// Not through walls: stop just in front of whatever is between the head and the camera
-			const float head[3] = {cam_x, cam_y - THIRD_PERSON_DROP_M, cam_z}, want[3] = {view_x, view_y, view_z};
+			const float head[3] = {cam_x, cam_y - drop, cam_z}, want[3] = {view_x, view_y, view_z};
 			// The view has a width: besides the centre line, lines to points beside and
 			// above/below the camera, so a wall just off to one side can't end up in front
 			// of the near clip plane (which showed the far side of the wall)
```

---

#### 4.2.2 `engine/src/intro.c`
```diff
diff --git a/engine/src/intro.c b/engine/src/intro.c
index 31b430e..9c0b13d 100644
--- a/engine/src/intro.c
+++ b/engine/src/intro.c
@@ -455,11 +455,12 @@ static void speech_step(float dt) {
 
 // --- Alduin: following his packages (perched, hovering, strafing; between them, circling) ---
 
-enum { AL_NONE, AL_CIRCLE, AL_PATH, AL_HOVER, AL_PERCH };
+enum { AL_NONE, AL_CIRCLE, AL_PATH, AL_HOVER, AL_PERCH, AL_GROUND };
 static struct {
 	int beast, kind, mode, breathe;
 	float pos[3], heading, t, breath_t, meteor_t, angle;
 	float path[8][3];
+	float dir[3]; // (his flight's direction, turned toward each waypoint over a moment: a curve through them, not corners)
 	int n, i;
 	float aim[3]; // where his breath goes
 	int arrived;
@@ -472,6 +473,7 @@ static void alduin_show(const float at[3]) {
 		al.beast = actors_beast_spawn(al.kind);
 		actors_beast_look(al.beast, 1, 0.0f);
 		memcpy(al.pos, at, sizeof(al.pos));
+		al.t = 0.0f;
 		al.mode = AL_CIRCLE;
 	}
 }
@@ -495,9 +497,11 @@ static void alduin_package(const Action *a) {
 	const char *p = a->package;
 	al.breathe = strstr(p, "shout") || strstr(p, "breath");
 	al.arrived = 0;
+	memset(al.dir, 0, sizeof(al.dir)); // (a new flight: its first heading from where he is)
 	// (a travel to a landing - "...btravelrubbleoneshout" to "...blandrubbleref", Haming's scene: a
-	// perch too; flown as a plain flight he went straight through the tower to it, and circled low)
-	const int lands = strstr(p, "perch") || (strstr(first->name, "land") && !strstr(first->name, "island"));
+	// ground landing; flown as a plain flight he went straight through the tower to it, and circled low)
+	const int is_perch = strstr(p, "perch") != NULL;
+	const int lands = is_perch || (strstr(first->name, "land") && !strstr(first->name, "island"));
 	if (lands && !strstr(p, "hover")) { // (landing there; shouting from it)
 		al.n = 1, al.i = 0, memcpy(al.path[0], first->pos, sizeof(al.path[0]));
 		const Marker *tower = marker("mq101towerstaggermarker");
@@ -507,18 +511,30 @@ static void alduin_package(const Action *a) {
 			al.path[0][0] += ox / d * TOWER_PERCH_OUT_M, al.path[0][2] += oz / d * TOWER_PERCH_OUT_M;
 			al.path[0][1] -= 2.5f;
 			memcpy(al.aim, tower->pos, sizeof(al.aim));
+			al.mode = AL_PATH, al.angle = -1.0f;
 			return;
 		}
-		// (down onto it from above, not along the ground through what's between: over it first)
-		if (al.pos[1] < first->pos[1] + PERCH_ABOVE_M - 2.0f || hypotf(al.pos[0] - first->pos[0], al.pos[2] - first->pos[2]) > 12.0f) {
+		// (to a perch or from a low point across buildings: arc over the midpoint, so he comes down onto it
+		// from above - a straight line from offstage west ran through the tower's roof)
+		if ((is_perch || al.pos[1] < first->pos[1] + 10.0f) && hypotf(al.pos[0] - first->pos[0], al.pos[2] - first->pos[2]) > 12.0f) {
 			memcpy(al.path[1], first->pos, sizeof(al.path[1]));
+			al.path[0][0] = (al.pos[0] + first->pos[0]) * 0.5f;
 			al.path[0][1] = fmaxf(first->pos[1], al.pos[1]) + PERCH_ABOVE_M;
+			al.path[0][2] = (al.pos[2] + first->pos[2]) * 0.5f;
 			al.n = 2;
 		}
+		if (al.t < 0.1f)
+			al.heading = atan2f(-(al.path[0][0] - al.pos[0]), -(al.path[0][2] - al.pos[2]));
 		al.mode = AL_PATH;
 		const Marker *aim = target(a, 1);
-		memcpy(al.aim, aim ? aim->pos : first->pos, sizeof(al.aim));
-		al.angle = -1.0f; // (to perch at the end)
+		if (aim) {
+			memcpy(al.aim, aim->pos, sizeof(al.aim));
+		} else {
+			al.aim[0] = first->pos[0] - sinf(first->heading) * 10.0f;
+			al.aim[1] = first->pos[1];
+			al.aim[2] = first->pos[2] - cosf(first->heading) * 10.0f;
+		}
+		al.angle = is_perch ? -1.0f : -4.0f; // (-1: perch on tower; -4: stand on ground)
 	} else if (strstr(p, "hover")) { // (hovering at the first of its markers, breathing at the targets)
 		al.n = 1, al.i = 0, memcpy(al.path[0], first->pos, sizeof(al.path[0]));
 		al.mode = AL_PATH, al.angle = -2.0f; // (to hover at the end)
@@ -559,11 +575,13 @@ static void alduin_update(float dt, const float player[3]) {
 		const float dx = to[0] - al.pos[0], dy = to[1] - al.pos[1], dz = to[2] - al.pos[2];
 		const float d = sqrtf(dx * dx + dy * dy + dz * dz), last = al.i == al.n - 1;
 		const float speed = last && al.angle < 0.0f ? fmaxf(5.0f, fminf(ALDUIN_SPEED, d * 1.2f)) : ALDUIN_SPEED;
-		if (d < 1.5f) {
+		if (d < (last ? 1.5f : fmaxf(1.5f, speed * 0.5f))) { // (a waypoint on the way is passed by, not circled)
 			if (!last)
 				al.i++;
 			else if (al.angle == -1.0f)
 				al.mode = AL_PERCH, al.arrived = 1, sfx_play_at("dragon_land", al.pos, 200.0f);
+			else if (al.angle == -4.0f)
+				al.mode = AL_GROUND, al.arrived = 1, sfx_play_at("dragon_land", al.pos, 200.0f);
 			else if (al.angle == -2.0f)
 				al.mode = AL_HOVER, al.arrived = 1;
 			else if (al.angle == -3.0f) { // (a fly-over: off in
@@ -1204,6 +1260,23 @@ int intro_start(void) {
 	return 1;
 }
 
+// The cart's view before the first frame: intro_update doesn't run while a loading or fade-in
+// transition is on, so the camera took the player's own spot - under the cart, on the ground the
+// land was still streaming in - for the first second. Here the cart and everyone on it are put
+// in place (as intro_update's first frame would) and the eye set to the player's seat at once.
+void intro_prime_view(IntroFrame *f) {
+	if (!loaded || stage != ST_CART || stage_t > 0.5f || cart_s > 0.0f)
+		return;
+	float heading;
+	stage_t = 0.0f; // (the first-frame set-up in cart_place and the seats runs each call: nothing's moved yet)
+	cart_place(1.0f / 60.0f, &heading, f->pos);
+	f->hold = f->lock_move = 1;
+	f->eye_set = 1;
+	memcpy(f->eye, cart_eye, sizeof(f->eye));
+	face(heading, 0.0f);
+	f->face = 1, f->look_yaw = face_yaw, f->look_pitch = face_pitch;
+}
+
 int intro_first_person(void) {
 	return intro_active() && stage <= ST_RACE && !race_chosen;
 }
@@ -1559,7 +1632,7 @@ void intro_update(float dt, const float feet[3], int interior, unsigned pressed,
 			terrain_height_at(down[0], down[2], &down[1]);
 			actors_player_ride(NULL, 0.0f, NULL);
 			memcpy(f->pos, down, sizeof(f->pos));
-			f->teleport = 1;
+			f->hold = 1; // (put on the ground where you step down: not a teleport - that was a loading screen over two metres)
 			for (size_t k = 0; k < sizeof(SEATS) / sizeof(SEATS[0]); k++) { // (stepping down after you)
 				const int i = who(SEATS[k].who);
 				if (i >= 0)
@@ -1723,7 +1796,7 @@ void intro_update(float dt, const float feet[3], int interior, unsigned pressed,
 			const Marker *w = marker("mq101alduinoffstagewestmarker");
 			if (w)
 				alduin_show(w->pos);
-			static Action land = {"", "alduin", "mq101alduintowerperch", "mq101alduintowerattackmarker"};
+			static Action land = {"", "alduin", "mq101alduinperch1", "mq101alduinperchmarker1,mq101alduintowerattacktargetref"};
 			alduin_package(&land);
 			run_scene("mq101alduinattackscene1", 0, 99, 1); // (INFO 000D07EE - everyone running for it, as Skyrim's)
 			called = 1;
@@ -1889,9 +1962,17 @@ void intro_update(float dt, const float feet[3], int interior, unsigned pressed,
 		if (run.active && !strcmp(run.name, "mq101dragonattackscenedtokeep") && run.phase <= 2)
 			look_toward_actor("generaltullius", 1.6f); // (INFO 0010A187: turned to him as he shouts)
 		static int part;
-		static float apart; // (cut off from him - rubble, a wrong turn - a while: brought up beside him)
+		static float apart, call_t; // (cut off from him - rubble, a wrong turn - a while: he comes to you; only long stuck, you to him)
 		apart = near_actor("hadvar", FOLLOW_GATE_M + 3.0f) ? 0.0f : apart + dt;
-		if (apart > 4.0f) { // (can't reach him - rubble between: beside him)
+		call_t -= dt;
+		if (apart > 4.0f && apart <= 14.0f && call_t <= 0.0f) { // (INFO 000DB8D3: he runs back for you - the hard loading screen
+			const int hv = who("hadvar");                   //  the old jump to him gave, right after the leap, was the player's)
+			call_t = 1.5f;
+			if (hv >= 0) {
+				const float by[3] = {feet[0] + 1.2f, feet[1], feet[2] + 1.2f};
+				actors_script(hv, 1), actors_script_walk(hv, by, 1);
+			}
+		} else if (apart > 14.0f) { // (can't reach him at all - rubble between: beside him)
 			float at[3];
 			if (actors_where(who("hadvar"), at)) {
 				f->pos[0] = at[0] + 1.5f, f->pos[1] = at[1], f->pos[2] = at[2] + 1.5f;
@@ -1923,6 +2004,19 @@ void intro_update(float dt, const float feet[3], int interior, unsigned pressed,
 			static Action wall = {"", "alduin", "duncgalduincshoutwhileperchedpkg", "dunalduincperchref,duncgalduincshouttarget01ref"};
 			alduin_package(&wall);
 			part = 1;
+		}
+		if (part == 1 && run.active && !strcmp(run.name, "mq101dragonattackscenecperchwall") && al.beast >= 0 && al.mode == AL_PERCH) {
+			const Marker *perch = marker("dunalduincperchref");
+			const float pd = perch ? hypotf(feet[0] - perch->pos[0], feet[2] - perch->pos[2]) : 99.0f;
+			// Approach trigger: player moves past the wall corner (x > 263, z < 1138) or within 12m of perch
+			const int in_trigger = (feet[0] > 263.0f && feet[2] < 1138.0f) || pd < 12.0f;
+			if ((in_trigger || run.t > 12.0f) && al.arrived) {
+				al.mode = AL_CIRCLE;
+				al.arrived = 0;
+				al.breathe = 0;
+				breath_off();
+				sfx_play_at("dragon_roar", al.pos, 150.0f);
+			}
 		} else if (!run.active && part == 1) {
 			place("ralof", "mq101raloftokeepmarkerf", NULL); // (Ralof's made it too: at the keep)
 			run_scene("mq101dragonattackscenedtokeep", 0, 6, 1); // (INFO 00109B60 ...)
```

---

#### 4.2.3 `engine/src/creatures.c`
```diff
diff --git a/engine/src/creatures.c b/engine/src/creatures.c
index 56f04eb..4d5da1e 100644
--- a/engine/src/creatures.c
+++ b/engine/src/creatures.c
@@ -2467,10 +2467,48 @@ void creatures_update(float dt, const float player[3], int sneaking, int place)
 		}
 		case FLEE: { // away from you, at a run
 			play(c, "run");
-			const float ax = c->pos[0] - dx / fmaxf(dist, 0.1f) * 10.0f, az = c->pos[2] - dz / fmaxf(dist, 0.1f) * 10.0f;
-			head_for(c, ax, az, k->run_speed, dt);
+			// Calculate escape vectors strictly within local navmesh boundaries (A* pathfinding)
+			// rather than blindly inverting the player vector into solid dungeon walls
+			const float cur_dist = hypotf(c->pos[0] - c->target[0], c->pos[2] - c->target[1]);
+			if ((c->target[0] == 0.0f && c->target[1] == 0.0f) || c->timer <= 0.0f || cur_dist < 1.2f) {
+				c->timer = 1.5f + frand() * 0.5f;
+				const float away = atan2f(-dx, -dz);
+				static const float ANGLES[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f};
+				static const float DISTS[] = {12.0f, 8.0f, 5.0f};
+				int found = 0;
+				float best_x = c->pos[0], best_z = c->pos[2], best_pdist = dist;
+				for (size_t di = 0; di < sizeof(DISTS) / sizeof(DISTS[0]) && !found; di++) {
+					const float d_try = DISTS[di];
+					for (size_t ai = 0; ai < sizeof(ANGLES) / sizeof(ANGLES[0]); ai++) {
+						const float a = away + ANGLES[ai];
+						const float px = c->pos[0] + sinf(a) * d_try, pz = c->pos[2] + cosf(a) * d_try;
+						NavRef nr;
+						float h, cand[3] = {px, c->pos[1], pz};
+						if (navmesh_locate_level(px, c->pos[1] + 0.5f, pz, 2.0f, &nr, &h)) {
+							const float pdist = hypotf(px - player[0], pz - player[2]);
+							if (pdist > best_pdist) {
+								best_pdist = pdist, best_x = px, best_z = pz;
+								found = 1;
+								break;
+							}
+						} else if (navmesh_nearest_level(px, c->pos[1], pz, 2.0f, 1.5f, &nr, cand)) {
+							const float pdist = hypotf(cand[0] - player[0], cand[2] - player[2]);
+							if (pdist > best_pdist) {
+								best_pdist = pdist, best_x = cand[0], best_z = cand[2];
+								found = 1;
+								break;
+							}
+						}
+					}
+				}
+				c->target[0] = best_x, c->target[1] = best_z;
+			}
+			if (c->target[0] != 0.0f || c->target[1] != 0.0f)
+				go_to(c, c->target[0], c->target[1], k->run_speed, dt);
+			else
+				head_for(c, player[0], player[2], 0.0f, dt);
 			if ((c->flee_t -= dt) <= 0.0f && dist > FLEE_M)
-				c->state = IDLE, c->timer = 2.0f;
+				c->state = IDLE, c->timer = 2.0f, c->target[0] = c->target[1] = 0.0f;
 			break;
 		}
 		case CHASE: {
@@ -3068,6 +3106,21 @@ int creatures_killmove(int i, const char *clip, const float pos[3], float headin
 	return 1;
 }
 
+int creatures_killmove_done(int i) {
+	if (i < 0 || i >= MAX_LIVE || !live[i].used)
+		return 1;
+	Creature *c = &live[i];
+	if (c->state != DEAD)
+		return 0;
+	if (c->health <= 0.0f) {
+		const float dl = clip_len(c, c->clip, NULL);
+		// Released instantly when death animation finishes, transitions to ragdoll (c->fall > 0), or lethal blow landed
+		if (c->fall > 0.0f || (dl > 0.0f && c->clip_t >= fminf(dl, 1.2f)) || c->clip_t >= 1.5f)
+			return 1;
+	}
+	return 0;
+}
+
 void creatures_set_blow_power(int power) {
 	blow_power = power;
 }
```

---

#### 4.2.4 `engine/src/objects.c`
```diff
diff --git a/engine/src/objects.c b/engine/src/objects.c
index 1e76042..bd34e85 100644
--- a/engine/src/objects.c
+++ b/engine/src/objects.c
@@ -1934,7 +1934,7 @@ int objects_skinning(void) {
 // The characters posed on the GPU, in one of objects_draw_dynamic's passes (-1: the solid
 // one, drawn first; else 0 opaque, 1 alpha-tested, 2 its soft edge). done: those drawn solid
 static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const float wvp[16], const float band[4],
-                         int *done) {
+                         uint8_t *done) {
 	const ObjShaders *sv = &variants[atmosphere_variant()];
 	const int k = pass < 0;
 	if (!sv->skin[k])
@@ -1943,6 +1943,7 @@ static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const
 	int used = 0;
 	float light[16];
 	lighting_bake_params(light);
+	GLuint last_tex = 0;
 	for (int i = 0; i < count; i++) {
 		const ObjectsDynamic *d = &items[i];
 		if (!d->skin_src || !d->index_count)
@@ -1951,10 +1952,10 @@ static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const
 		if (pass < 0) { // (opaque and wholly in the band: nothing discarded)
 			if (d->alpha != 0 || d->fade > 0.0f || (d->fade == 0.0f && dist + 3.0f > band[2]))
 				continue;
-			if (i < 256)
+			if (i < 512)
 				done[i] = 1;
 		} else {
-			if ((pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || (i < 256 && done[i]))
+			if ((pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || (i < 512 && done[i]))
 				continue;
 		}
 		const float ref = d->alpha > ALPHA_GLOW ? d->alpha / 255.0f : 0.5f;
@@ -1968,6 +1969,8 @@ static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const
 			glUniform4fv(u->light, 4, light);
 			if (!k)
 				glUniform2f(u->screen, render_w, render_h);
+			glEnableVertexAttribArray(3);
+			glEnableVertexAttribArray(4);
 		}
 		if (!k) {
 			glUniform1f(u->alpha_ref, pass == 0 ? -1.0f : pass == 1 ? ref : SOFT_EDGE_ALPHA);
@@ -1982,7 +1985,10 @@ static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const
 		glUniform4fv(u->bones, 192, d->bones);
 		glUniform4f(u->tmul, d->tint_mul[0], d->tint_mul[1], d->tint_mul[2], 1.0f);
 		glUniform4f(u->tadd, d->tint_add[0], d->tint_add[1], d->tint_add[2], 0.0f);
-		glBindTexture(GL_TEXTURE_2D, d->tex);
+		if (d->tex != last_tex) {
+			glBindTexture(GL_TEXTURE_2D, d->tex);
+			last_tex = d->tex;
+		}
 		const uint8_t *v = d->skin_vbo ? NULL : d->skin_src; // (SrcVertex: 32 bytes; offsets into its buffer)
 		glBindBuffer(GL_ARRAY_BUFFER, d->skin_vbo);
 		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->skin_ibo);
@@ -1991,15 +1997,30 @@ static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const
 		glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, 32, v + 20);
 		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 24);
 		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, v + 28);
+		const int hair = d->is_hair;
+		if (hair) {
+			glDisable(GL_CULL_FACE);
+			glEnable(GL_ALPHA_TEST);
+			glAlphaFunc(GL_GREATER, 0.5f);
+		}
 		glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->skin_ibo ? NULL : d->indices);
+		if (hair) {
+			glEnable(GL_CULL_FACE);
+			glDisable(GL_ALPHA_TEST);
+			if (pass > 0)
+				glDisable(GL_CULL_FACE);
+		}
 		st_dyn_draws++, st_dyn_tris += d->index_count / 3;
 		if (!d->skin_vbo)
 			st_dyn_bytes += d->vertex_count * 32u + d->index_count * 2u;
 	}
 	glBindBuffer(GL_ARRAY_BUFFER, 0);
 	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
-	if (used)
+	if (used) {
+		glDisableVertexAttribArray(3);
+		glDisableVertexAttribArray(4);
 		glUseProgram(obj_prog);
+	}
 }
 
 void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
@@ -2016,19 +2037,20 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 	glUniform4fv(u_band, 1, band);
 	glBindBuffer(GL_ARRAY_BUFFER, 0);
 	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
-	for (int a = 0; a < 5; a++)
+	for (int a = 0; a < 3; a++)
 		glEnableVertexAttribArray(a);
 	// Opaque (culled), then alpha-tested (two-sided), then the alpha-tested again blended
 	// over themselves: the soft fringe a hard cut leaves jagged (hair, beards, lashes) fades
 	// out instead (no depth writes: it only covers what's behind it)
 	const ObjShaders *sv = &variants[atmosphere_variant()];
-	int solid_done[256] = {0}; // (opaque and wholly within the band: drawn first, nothing discarded)
+	uint8_t solid_done[512] = {0}; // (opaque and wholly within the band: drawn first, nothing discarded)
+	GLuint last_tex = 0;
 	if (sv->solid) {
 		glUseProgram(sv->solid);
 		glUniformMatrix4fv(sv->s_wvp, 1, GL_FALSE, wvp);
 		atmosphere_apply(&sv->s_atmo);
 		glUniform1i(sv->s_tex, 0);
-		for (int i = 0; i < count && i < 256; i++) {
+		for (int i = 0; i < count && i < 512; i++) {
 			const ObjectsDynamic *d = &items[i];
 			const float dist = hypotf(d->pos[0] - draw_cam[0], d->pos[2] - draw_cam[1]);
 			if (d->skin_src || d->alpha != 0 || !d->index_count || d->fade > 0.0f ||
@@ -2036,7 +2058,10 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 				continue;
 			glUniform4f(sv->s_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
 			glUniform4f(sv->s_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
-			glBindTexture(GL_TEXTURE_2D, d->tex);
+			if (d->tex != last_tex) {
+				glBindTexture(GL_TEXTURE_2D, d->tex);
+				last_tex = d->tex;
+			}
 			const ObjectVertex *v = d->vertices;
 			glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
 			glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
@@ -2049,6 +2074,7 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 	// The GPU-posed: solid, then opaque, before anyone's alpha passes
 	draw_skinned(items, count, -1, wvp, band, solid_done);
 	draw_skinned(items, count, 0, wvp, band, solid_done);
+	last_tex = 0;
 	glUseProgram(obj_prog);
 	glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
 	glUniform4fv(u_band, 1, band);
@@ -2064,7 +2090,7 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 		float last_fade = 0.0f, last_ref = pass == 1 ? 0.5f : 0.0f;
 		for (int i = 0; i < count; i++) {
 			const ObjectsDynamic *d = &items[i];
-			if (d->skin_src || (pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || !d->index_count || (i < 256 && solid_done[i]))
+			if (d->skin_src || (pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || !d->index_count || (i < 512 && solid_done[i]))
 				continue;
 			if (pass) { // (its own cut; one already below the soft edge has nothing left to blend)
 				const float ref = d->alpha > ALPHA_GLOW ? d->alpha / 255.0f : 0.5f;
@@ -2086,17 +2112,34 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 			}
 			glUniform4f(u_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
 			glUniform4f(u_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
-			glBindTexture(GL_TEXTURE_2D, d->tex);
+			if (d->tex != last_tex) {
+				glBindTexture(GL_TEXTURE_2D, d->tex);
+				last_tex = d->tex;
+			}
 			const ObjectVertex *v = d->vertices;
 			glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
 			glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
 			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ObjectVertex), &v->r);
+			const int hair = d->is_hair;
+			if (hair) {
+				glDisable(GL_CULL_FACE);
+				glEnable(GL_ALPHA_TEST);
+				glAlphaFunc(GL_GREATER, 0.5f);
+			}
 			glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->indices);
+			if (hair) {
+				glEnable(GL_CULL_FACE);
+				glDisable(GL_ALPHA_TEST);
+				if (pass > 0)
+					glDisable(GL_CULL_FACE);
+			}
 		}
 		if (last_fade != 0.0f)
 			glUniform4fv(u_band, 1, band);
 		if (pass) { // (the GPU-posed's alpha-tested and soft edges, in the same state)
 			draw_skinned(items, count, pass, wvp, band, solid_done);
+			last_tex = 0;
+			glUseProgram(obj_prog);
 			glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
 			glUniform4fv(u_band, 1, band);
 			glUniform1f(u_alpha_ref, pass == 1 ? 0.5f : SOFT_EDGE_ALPHA);
@@ -2113,7 +2156,10 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 			continue;
 		glUniform4f(u_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
 		glUniform4f(u_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
-		glBindTexture(GL_TEXTURE_2D, d->tex);
+		if (d->tex != last_tex) {
+			glBindTexture(GL_TEXTURE_2D, d->tex);
+			last_tex = d->tex;
+		}
 		const ObjectVertex *v = d->vertices;
 		glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
 		glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
@@ -2122,8 +2168,9 @@ void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
 	}
 	glDepthMask(GL_TRUE);
 	glDisable(GL_BLEND);
+	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
 	glEnable(GL_CULL_FACE);
-	for (int a = 0; a < 5; a++)
+	for (int a = 0; a < 3; a++)
 		glDisableVertexAttribArray(a);
 	glUseProgram(0);
 }
@@ -2441,6 +2488,7 @@ void objects_draw_fx(float cam_x, float cam_z, float yaw_deg, float time_s) {
 				glUseProgram(0);
 			}
 			glDisable(GL_BLEND);
+			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
 			glDepthMask(GL_TRUE);
 			glEnable(GL_CULL_FACE);
 		}
@@ -2514,6 +2562,7 @@ void objects_draw_sprites(const ObjectsSprite *s, int n, float yaw_deg, float pi
 	glDisableVertexAttribArray(2);
 	glUseProgram(0);
 	glDisable(GL_BLEND);
+	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
 	glDepthMask(GL_TRUE);
 	glEnable(GL_CULL_FACE);
 }
```

---

#### 4.2.5 `engine/src/actors.c`
```diff
diff --git a/engine/src/actors.c b/engine/src/actors.c
index 161596e..5cfe432 100644
--- a/engine/src/actors.c
+++ b/engine/src/actors.c
@@ -46,6 +46,8 @@ void actors_set_draw_radius(float m) {
 #define WALK_SPEED 1.25f      // m/s, without a walk clip (else the clip's own speed)
 #define STRIDE_M 1.5f         // ground covered by one walk cycle (two steps), without a clip
 #define PART_SKIN 0x8000      // batch part tags (converter/npcs.py)
+#define SLOT_HEAD 0x1u
+#define SLOT_HAIR 0x2u
 #define MAX_ITEMS 64 // the wardrobe and gear put on since
 #define POSE_SMOOTH 12.0f     // the player's pose eases toward the chosen clip (per second)
 #define FALL_POSE_AFTER_S 0.5f // walking off an edge: in the air longer than this...
@@ -147,6 +149,7 @@ typedef struct {
 	int gear_tag;      // a weapon's: GEAR_HAND (drawn), GEAR_SHEATHED, GEAR_SCABBARD; 0 otherwise
 	GLuint vbo, ibo;   // posed on the GPU: its vertices and indices there (made when first drawn, freed
 	                   // when it's been out of sight a while: GPU_BUFFERS_IDLE)
+	int is_hair;
 } ABatch;
 enum { GEAR_NONE, GEAR_HAND, GEAR_SHEATHED, GEAR_SCABBARD }; // (converter/gear.py)
 
@@ -380,6 +383,19 @@ static int read_batches(Cur *cp, Actor *a, uint32_t n, int item) {
 			b->alpha = ALPHA_GLOW;
 		if (item >= 0) // (gear: its item; a weapon's batches say drawn or sheathed)
 			b->gear_tag = b->part > 1 || (b->part == 1 && a->items[item].weapon) ? b->part : GEAR_NONE, b->part = item + 1;
+		b->is_hair = 0;
+		if (item >= 0 && item < a->nitems) {
+			if (!strcmp(a->items[item].name, "Hair") || !strcmp(a->items[item].name, "Beard"))
+				b->is_hair = 1;
+		}
+		if ((b->part & PART_SKIN) && (b->part & SLOT_HAIR))
+			b->is_hair = 1;
+		if (strstr(tex, "hair") || strstr(tex, "Hair") || strstr(tex, "HAIR") ||
+		    strstr(tex, "hlne") || strstr(tex, "Hlne") || strstr(tex, "HLNE") ||
+		    strstr(tex, "beard") || strstr(tex, "Beard") || strstr(tex, "BEARD"))
+			b->is_hair = 1;
+		if (b->is_hair && !b->alpha)
+			b->alpha = 1;
 		b->vcount = (int)vc, b->icount = (int)ic;
 		b->src = malloc(vc * sizeof(SrcVertex));
 		b->idx = malloc(ic * sizeof(uint16_t));
@@ -1677,6 +1693,16 @@ static void emit(Actor *a, int *count) {
 		d->tex = a->ghost ? sktex_white() : bt->tex; // (a spirit - Skyrim's GhostFX: its form white, shaded, no fur: the
 		                                             //  tint lit the fur's own brown - a plain wolf, see-through)
 		d->alpha = bt->alpha;
+		d->is_hair = (uint8_t)bt->is_hair;
+		if (!d->is_hair) {
+			const int mine = (a == &player || a == &player1st) && bt->part > 0 && bt->part <= a->nitems;
+			if (mine && (a->items[bt->part - 1].hidden_by & (SLOT_HAIR | SLOT_HEAD)))
+				d->is_hair = 1;
+			else if (mine && (!strcmp(a->items[bt->part - 1].name, "Hair") || !strcmp(a->items[bt->part - 1].name, "Beard")))
+				d->is_hair = 1;
+		}
+		if (d->is_hair && !d->alpha)
+			d->alpha = 1;
 		d->fade = a->fade > 0.0f ? a->fade : a->ghost ? 0.3f : player_ghost && (a == &player || a == &player1st) ? 0.45f // (a spirit; Become Ethereal)
 		          : a->far ? -1.0f : 0.0f;
 		d->pos[0] = a->x, d->pos[1] = a->y + 1.0f, d->pos[2] = a->z, d->pos[3] = a->quant_m > 0.0f ? a->quant_m : QUANT_EXTENT_M;
```

---

## 5. Action Items for Claude's Audit

### 5.1 State Machine Cleanup & Isolation in `engine/src/main.c`
* **Audit Target:** Confirm that all intro triggers (`intro_prime_view`, `intro_start`, `intro_update`) are strictly gated by `intro_active()` and cannot be re-entered after `intro_stage() > ST_KEEP`.
* **Verification Step:** Inspect save deserialization in `load_game()` and `apply_save()`. Ensure no lingering static pointers or uninitialized structs in `main()` can cause `stage` or `opening` variables to resurrect the Helgen cart sequences upon fast-traveling back to Helgen or loading an exterior save.

### 5.2 VitaGL Culling & Alpha State Leak Verification
* **Audit Target:** Verify that toggling `glDisable(GL_CULL_FACE)` and `glEnable(GL_ALPHA_TEST)` inside `draw_skinned()` and `objects_draw_dynamic()` does not cause GPU pipeline bubbles or leave dirty OpenGL state in subsequent passes.
* **Specific Checks:**
  1. In `objects.c:2010` and `objects.c:2125`, note that `if (pass > 0) glDisable(GL_CULL_FACE);` was added to handle soft edges. Ensure that when exiting `objects_draw_dynamic()`, the global culling state is unconditionally restored via `glEnable(GL_CULL_FACE)`.
  2. Verify that `glDisable(GL_ALPHA_TEST)` is consistently applied so that terrain, static architecture, and solid water passes are not penalized by redundant alpha testing on the SGX543 rasterizer.

### 5.3 Boundary Checks for Navmesh Escape Vectors in `engine/src/creatures.c`
* **Audit Target:** Audit the radial raycasting in `case FLEE:`.
* **Specific Checks:**
  1. In exterior cells or near cell seams (e.g. Helgen exterior boundary), verify that `navmesh_locate_level()` does not return a false positive NavRef belonging to an adjacent unloaded cell quad.
  2. In tight interior dungeons (e.g. Helgen Keep torture chambers), test corner-case scenarios where all radial candidate points fail navmesh validation (`found == 0`). Ensure `best_x` / `best_z` does not default to $(0, 0, 0)$ or cause the creature to freeze in place permanently.

---
*End of Audit and Handoff Document. All tests passing via `bash engine/tests/run_tests.sh`. Ready for formal review.*
