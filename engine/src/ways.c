#include "ways.h"
#include "fileio.h"
#include "gamelog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOAL_NEAR_M 60.0f // the crossings a goal's ground starts from: within this of it (a cell)

typedef struct {
	float pos[3];
	int first, count; // its ways: links[first .. first + count)
} Node;
typedef struct {
	int to;
	float cost;
} Link;

static Node *nodes;
static Link *links;
static int node_count;
static float *left;          // each crossing's way on to the goal (metres; huge: none)
static float goal_at[2] = {1e9f, 1e9f};
static int *heap, heap_n;    // Dijkstra's queue (node numbers, by left)

void ways_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%snavmesh/ways.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	int nn = 0, ne = 0;
	for (char *p = buf; *p; p++)
		if (p == buf || p[-1] == '\n')
			nn += *p == 'N', ne += *p == 'E';
	nodes = calloc(nn, sizeof(Node)), links = calloc(ne * 2, sizeof(Link)), left = malloc(nn * sizeof(float));
	heap = malloc((ne * 2 + nn) * sizeof(int));
	int *ea = malloc(ne * sizeof(int)), *eb = malloc(ne * sizeof(int));
	float *ec = malloc(ne * sizeof(float));
	if (!nodes || !links || !left || !heap || !ea || !eb || !ec) {
		free(buf), free(ea), free(eb), free(ec);
		node_count = 0;
		return;
	}
	int e = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		if (line[0] == 'N' && node_count < nn) {
			Node *n = &nodes[node_count++];
			sscanf(line + 2, "%f %f %f", &n->pos[0], &n->pos[1], &n->pos[2]);
		} else if (line[0] == 'E' && e < ne && sscanf(line + 2, "%d %d %f", &ea[e], &eb[e], &ec[e]) == 3 &&
		           ea[e] >= 0 && eb[e] >= 0 && ea[e] < nn && eb[e] < nn) {
			nodes[ea[e]].count++, nodes[eb[e]].count++, e++;
		}
	}
	for (int i = 0, at = 0; i < node_count; i++)
		nodes[i].first = at, at += nodes[i].count, nodes[i].count = 0;
	for (int k = 0; k < e; k++) {
		Node *a = &nodes[ea[k]], *b = &nodes[eb[k]];
		links[a->first + a->count++] = (Link){eb[k], ec[k]};
		links[b->first + b->count++] = (Link){ea[k], ec[k]};
	}
	free(buf), free(ea), free(eb), free(ec);
	game_log("ways: %d crossings, %d ways between", node_count, e);
}

static void push(int n) {
	int i = heap_n++;
	heap[i] = n;
	while (i > 0 && left[heap[(i - 1) / 2]] > left[heap[i]]) {
		const int up = (i - 1) / 2, t = heap[up];
		heap[up] = heap[i], heap[i] = t, i = up;
	}
}

static int pop(void) {
	const int top = heap[0];
	heap[0] = heap[--heap_n];
	for (int i = 0;;) {
		const int l = i * 2 + 1, r = l + 1;
		int m = i;
		if (l < heap_n && left[heap[l]] < left[heap[m]])
			m = l;
		if (r < heap_n && left[heap[r]] < left[heap[m]])
			m = r;
		if (m == i)
			break;
		const int t = heap[m];
		heap[m] = heap[i], heap[i] = t, i = m;
	}
	return top;
}

// Every crossing's way on to the goal (from the crossings about it, out along the ways)
static void spread(const float goal[2]) {
	goal_at[0] = goal[0], goal_at[1] = goal[1];
	heap_n = 0;
	int seeded = 0;
	for (int i = 0; i < node_count; i++) {
		const float d = hypotf(nodes[i].pos[0] - goal[0], nodes[i].pos[2] - goal[1]);
		left[i] = d < GOAL_NEAR_M ? d : 1e30f;
		if (d < GOAL_NEAR_M)
			push(i), seeded++;
	}
	if (!seeded) { // (no ground crossing near it: from the nearest crossing)
		int best = -1;
		float bd = 1e30f;
		for (int i = 0; i < node_count; i++) {
			const float d = hypotf(nodes[i].pos[0] - goal[0], nodes[i].pos[2] - goal[1]);
			if (d < bd)
				bd = d, best = i;
		}
		if (best >= 0)
			left[best] = bd, push(best);
	}
	while (heap_n > 0) {
		const int u = pop();
		for (int k = 0; k < nodes[u].count; k++) {
			const Link *l = &links[nodes[u].first + k];
			if (left[u] + l->cost < left[l->to])
				left[l->to] = left[u] + l->cost, push(l->to); // (a stale copy left in the queue: harmless)
		}
	}
}

int ways_toward(const float from[3], const float goal[2], float reach, int *out, int max) {
	if (!node_count || max <= 0)
		return 0;
	if (hypotf(goal[0] - goal_at[0], goal[1] - goal_at[1]) > 1.0f)
		spread(goal);
	int n = 0;
	float score[16];
	if (max > 16)
		max = 16;
	for (int i = 0; i < node_count; i++) {
		const float *p = nodes[i].pos;
		const float d = hypotf(p[0] - from[0], p[2] - from[2]);
		if (d > reach || left[i] > 1e29f)
			continue;
		const float s = left[i] + sqrtf(d * d + (p[1] - from[1]) * (p[1] - from[1]));
		int at = n < max ? n++ : max;
		if (at == max && s >= score[max - 1])
			continue;
		if (at == max)
			at = max - 1;
		while (at > 0 && score[at - 1] > s)
			score[at] = score[at - 1], out[at] = out[at - 1], at--;
		score[at] = s, out[at] = i;
	}
	return n;
}

void ways_node(int node, float pos[3]) {
	memcpy(pos, nodes[node].pos, sizeof(nodes[node].pos));
}

float ways_left(int node) {
	return left[node];
}
