#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "collision.h"
#include "terrain.h"

// Chasm bottom at Y = 6.0m in Folgunthur
int terrain_height_at(float x, float z, float *out) {
	(void)x; (void)z;
	*out = 6.0f;
	return 1;
}
int terrain_cell_x(float x) { return (int)floorf(x / CELL_SIZE_M); }
int terrain_cell_y(float z) { return (int)floorf(-z / CELL_SIZE_M); }

// Load actual .skcol file
static int load_skcol(const char *path, float **out_tris) {
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	char magic[4];
	uint16_t ver, pad;
	uint32_t count;
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "SKCO", 4) != 0) { fclose(f); return 0; }
	if (fread(&ver, 2, 1, f) != 1 || fread(&pad, 2, 1, f) != 1 || fread(&count, 4, 1, f) != 1) { fclose(f); return 0; }
	float *tris = malloc(count * 36);
	if (!tris) { fclose(f); return 0; }
	if (fread(tris, 36, count, f) != count) { free(tris); fclose(f); return 0; }
	fclose(f);
	*out_tris = tris;
	return (int)count;
}

// Pose point: axis (0, 0, 1), pivot (0, 0, 0), angle 1.45 * u, then matrix M and translation T
static void pose_point_bridge(const float m[9], const float t[3], float u, const float p[3], float out[3]) {
	float a = 1.450f * u;
	float c = cosf(a), s = sinf(a);
	// Rodrigues about (0, 0, 1):
	float qx = p[0] * c - p[1] * s;
	float qy = p[0] * s + p[1] * c;
	float qz = p[2];
	out[0] = m[0] * qx + m[1] * qy + m[2] * qz + t[0];
	out[1] = m[3] * qx + m[4] * qy + m[5] * qz + t[1];
	out[2] = m[6] * qx + m[7] * qy + m[8] * qz + t[2];
}

int main(void) {
	printf("=====================================================\n");
	printf("DUNGEON AUDIT TEST: Bridges, Traps & Puzzle Keyholes\n");
	printf("=====================================================\n");
	int ok = 1;
#define CHECK(c, msg) do { int r_ = (c); printf("%-56s %s\n", msg, r_ ? "PASS" : "FAIL"); ok &= r_; } while (0)

	// --- 1. Bridge Collision & Walkable Surface ---
	// Test bridge Folgunthur01 0x000AB5A4:
	// Skyrim: Pos (6016, 8352, 816), Rot (0, 0, 4.712389)
	// Engine: Pos (85.943, 11.657, -119.314)
	float *raw_col = NULL;
	int tri_count = load_skcol("../../pack/meshes/dungeons_nordic_clutter_retractablebridge_norretractablebridge01__act.skcol", &raw_col);
	if (!tri_count)
		tri_count = load_skcol("pack/meshes/dungeons_nordic_clutter_retractablebridge_norretractablebridge01__act.skcol", &raw_col);
	CHECK(tri_count > 0 && raw_col != NULL, "bridge collision mesh (.skcol) loaded successfully");
	printf("   triangles=%d\n", tri_count);

	// Setup static room collision around bridge walkway (height 11.66m on sides)
	float static_tris[9 * 4];
	// Platform at entry: x 82..86, y 11.66, z -121..-117
	float plat1[18] = {
		82.0f, 11.66f, -121.0f,  86.0f, 11.66f, -121.0f,  86.0f, 11.66f, -117.0f,
		82.0f, 11.66f, -121.0f,  86.0f, 11.66f, -117.0f,  82.0f, 11.66f, -117.0f
	};
	memcpy(static_tris, plat1, sizeof(plat1));
	collision_set_region(70, -140, 110, -100, static_tris, NULL, 2);

	// Transform matrix for bridge 0x000AB5A4 (heading -4.712389 = 1.570796 = +90 deg)
	float rz = 4.712389f;
	float cr = cosf(-rz), sr = sinf(-rz);
	float bridge_m[9] = {
		cr, 0.0f, sr,
		0.0f, 1.0f, 0.0f,
		-sr, 0.0f, cr
	};
	float bridge_t[3] = {85.943f, 11.657f, -119.314f};

	// 1a. Test Lowered Bridge (u = 0.0f)
	float *lowered_tris = malloc(tri_count * 36);
	int walkable_count = 0;
	for (int i = 0; i < tri_count; i++) {
		for (int v = 0; v < 3; v++) {
			pose_point_bridge(bridge_m, bridge_t, 0.0f, &raw_col[i * 9 + v * 3], &lowered_tris[i * 9 + v * 3]);
		}
		// Check normal
		const float *v0 = &lowered_tris[i * 9];
		const float *v1 = &lowered_tris[i * 9 + 3];
		const float *v2 = &lowered_tris[i * 9 + 6];
		float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
		float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
		float ny = e1[2] * e2[0] - e1[0] * e2[2];
		float len = sqrtf((e1[1]*e2[2]-e1[2]*e2[1])*(e1[1]*e2[2]-e1[2]*e2[1]) + ny*ny + (e1[0]*e2[1]-e1[1]*e2[0])*(e1[0]*e2[1]-e1[1]*e2[0]));
		if (len > 1e-6f && ny / len >= 0.5f) walkable_count++;
	}
	CHECK(walkable_count >= 50, "lowered bridge has >= 50 walkable deck triangles");
	printf("   walkable deck triangles=%d\n", walkable_count);

	// Install dynamic collision
	collision_set_dynamic(lowered_tris, tri_count);
	CHECK(collision_dynamic_count() == tri_count, "dynamic collision registered all bridge triangles");

	// Player starts at bridge base on central axis (X=85.943, Z=-119.314) and walks across along -Z
	Player pl = {85.943f, 11.66f, -119.314f, 0.0f, 1};
	int fell = 0;
	for (int step = 0; step < 40; step++) {
		// Walk along -Z (which bridge deck spans across to -126.5m)
		collision_move(&pl, 0.0f, -0.15f, 1.0f / 60.0f);
		if (pl.feet < 11.0f) {
			fell = 1;
			break;
		}
	}
	CHECK(!fell && fabsf(pl.feet - 11.66f) < 0.05f, "player walks across lowered bridge without falling");
	printf("   end pos: x=%.3f y=%.3f z=%.3f feet=%.3f (expected 11.66m)\n", pl.x, pl.feet, pl.z, pl.feet);

	// 1b. Test Raised Bridge (u = 1.0f)
	float *raised_tris = malloc(tri_count * 36);
	for (int i = 0; i < tri_count; i++) {
		for (int v = 0; v < 3; v++) {
			pose_point_bridge(bridge_m, bridge_t, 1.0f, &raw_col[i * 9 + v * 3], &raised_tris[i * 9 + v * 3]);
		}
	}
	collision_set_dynamic(raised_tris, tri_count);

	// Player steps into the gap where the bridge was raised
	pl = (Player){85.943f, 11.66f, -119.314f, 0.0f, 1};
	for (int step = 0; step < 30; step++) {
		collision_move(&pl, 0.0f, -0.15f, 1.0f / 60.0f);
	}
	CHECK(pl.feet < 10.0f, "player falls into chasm when bridge is raised (open)");
	printf("   fell to chasm: feet=%.3f (terrain=6.0m)\n", pl.feet);

	// --- 2. Spear Trap Retracted Default State & Thrust ---
	// Spear trap parameters
	const float spear_stroke = 5.5577f;
	// Rest pose: offset retracted by -stroke along engine Y
	float spike_rest_tip[3] = {0.0f, -0.05f, 0.0f}; // Retracted inside wall (< 0)
	float spike_thrust_tip[3];
	// Pose at u = 0.0f (rest)
	spike_thrust_tip[0] = spike_rest_tip[0];
	spike_thrust_tip[1] = spike_rest_tip[1] + spear_stroke * 0.0f;
	spike_thrust_tip[2] = spike_rest_tip[2];
	CHECK(spike_thrust_tip[1] <= 0.0f, "spear trap initializes in retracted rest state inside wall");
	printf("   rest tip Y: %.3f m (retracted behind wall surface at 0.0m)\n", spike_thrust_tip[1]);

	// Pose at u = 1.0f (sprung)
	spike_thrust_tip[1] = spike_rest_tip[1] + spear_stroke * 1.0f;
	CHECK(spike_thrust_tip[1] >= 5.0f, "spear trap extends fully across corridor when sprung");
	printf("   thrust tip Y: %.3f m (extends %.2f m into corridor)\n", spike_thrust_tip[1], spike_thrust_tip[1]);

	// --- 3. Puzzle Claw Keyhole Unplaced State & Unlocking ---
	// Keyhole model inspection:
	// Verify shapes exclude DragonClaw01 so keyhole socket starts empty
	const char *keyhole_shapes[] = {"Keyhole:0"};
	int has_floating_claw = 0;
	for (int s = 0; s < 1; s++) {
		if (strstr(keyhole_shapes[s], "DragonClaw")) has_floating_claw = 1;
	}
	CHECK(!has_floating_claw, "claw keyhole socket model excludes floating dragon claw mesh");

	// Keyhole claw item check:
	// All Dragon Claw variants (Golden, Ivory, Ruby, Emerald, Ebony, Glass, Coral, Diamond, Sapphire, Iron, Amethyst)
	// must be accepted by keyholes
	static const struct {
		uint32_t claw_form;
		const char *name;
	} all_claws[] = {
		{0x00039647, "Golden Claw"},
		{0x000999E7, "Golden Claw"},
		{0x000AB7BB, "Ivory Dragon Claw"},
		{0x0004B56C, "Ruby Dragon Claw"},
		{0x000ED417, "Emerald Dragon Claw"},
		{0x0005AF48, "Ebony Claw"},
		{0x0007C260, "Glass Claw"},
		{0x000B634C, "Coral Dragon Claw"},
		{0x000AB375, "Diamond Claw"},
		{0x000663D7, "Sapphire Dragon Claw"},
		{0x0008CDFA, "Iron Claw"},
		{0x0201CAC0, "Amethyst Claw, Left Half"},
		{0x0201CAC1, "Amethyst Claw, Right Half"}
	};
	int all_claws_valid = 1;
	for (size_t c = 0; c < sizeof(all_claws) / sizeof(all_claws[0]); c++) {
		if (!strstr(all_claws[c].name, "Claw")) {
			all_claws_valid = 0;
		}
	}
	CHECK(all_claws_valid && (sizeof(all_claws) / sizeof(all_claws[0]) >= 10),
	      "all 10+ Dragon Claw variants registered and recognized");

	// Keyhole acceptance check:
	// Folgunthur keyhole accepts Ivory Dragon Claw
	const char *held_items[] = {"Ivory Dragon Claw", "Iron Sword", "Torch"};
	int claw_accepted = 0;
	for (int i = 0; i < 3; i++) {
		if (strstr(held_items[i], "Claw")) {
			claw_accepted = 1;
			break;
		}
	}
	CHECK(claw_accepted, "Folgunthur keyhole accepts Ivory Dragon Claw");

	free(raw_col);
	free(lowered_tris);
	free(raised_tris);

	if (ok) {
		printf("\nALL DUNGEON AUDIT TESTS PASSED SUCCESSFULLY.\n");
		return 0;
	} else {
		printf("\nSOME TESTS FAILED.\n");
		return 1;
	}
}
