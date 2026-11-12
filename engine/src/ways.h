#ifndef WAYS_H
#define WAYS_H

// The way across Tamriel, coarse (pack/navmesh/ways.txt, converter/waygraph.py): where each
// cell's walkable ground crosses into the next, and how far apart by that ground. The navmesh
// is only loaded about you; this is how a far place's way is found (Clairvoyance: up the 7000
// Steps, not at the cliff below High Hrothgar).

void ways_init(const char *pack_dir);

// The crossings near from (within reach, metres) best to head for on the way to goal (x, z):
// their numbers, those whose way on is shortest (counting the straight distance to them)
// first; how many (0: no graph, or none near)
int ways_toward(const float from[3], const float goal[2], float reach, int *out, int max);
// A crossing's place (engine metres) and how far its way on to the goal is (metres)
void ways_node(int node, float pos[3]);
float ways_left(int node);

#endif
