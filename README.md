# Snake Challenge 2026 — Winter Contest

AI bot for the Winter 2026 Snake Challenge. A competitive multi-bot snake game on a 2D grid with gravity — snakes collect power sources (batteries) to grow and outlast the opponent.

**Result:** Silver league, 665th place.

## Build & Run

```bash
g++ -std=c++17 main.cpp -o snake_ai
./snake_ai < input.txt
```

Single-file C++17 project. Reads game state from stdin, outputs movement commands to stdout, debug info to stderr.

## How the Algorithm Works

### Architecture

The bot uses a **multi-scorer decision system** combined with **cluster-based target selection** and **DFS pathfinding**.

```
stdin → GameState (parse level, snakes, batteries)
     → ClusterScorer (cluster batteries, pick strategic target per snake)
     → BatteryScorer (short-range DFS to nearby batteries)
     → DecisionMaker (evaluate 4 directions per snake)
       → SafetyScorer     (collision, death, trap, support detection)
       → BatteryScorer     (nearby battery bonus, DFS depth ≤ 8)
       → ClusterScorer     (strategic target bonus via DFS with midpoints)
     → stdout (movement commands)
```

### Battery Clustering

At game start, batteries are grouped into clusters using a greedy algorithm:

1. **Phase 1:** Iterate batteries. For each unused battery, start a new cluster and add nearby batteries within `CLUSTER_DISTANCE` (checking distance to cluster center of mass). Max `MAX_CLUSTER_SIZE` per cluster.
2. **Phase 2:** Remaining batteries are assigned to the nearest existing cluster by center of mass distance.
3. **Auto-tuning:** If too few clusters, decrease distance threshold. If too many (> `MAX_CLUSTER_COUNT = cbrt(width * height)`), increase it. If most clusters are full, decrease once more.

Each turn, eaten batteries are removed and cluster centers are recalculated.

### Target Selection (ClusterScorer)

For each snake:

1. Calculate **center of mass** (CoM) of all remaining batteries.
2. Find **midpoint** between snake head and CoM.
3. Pick the **closest cluster** to this midpoint.
4. From that cluster, pick the **closest battery** to the snake.
5. Run **DFS with midpoints** to build a path:
   - If target is far (> `MAX_AVAILABLE_DISTANCE`), recursively halve the distance to find an intermediate waypoint.
   - DFS runs to the waypoint. If unreachable, halve again (up to 5 attempts).

### Short-Range Battery Collection (BatteryScorer)

Independently, BatteryScorer checks the 3 closest batteries (Manhattan ≤ 5) using DFS (depth ≤ 8). If a battery is reachable in < 8 steps, it gets `BASE_BONUS`. This **overrides** the cluster target — short-range opportunities take priority.

### Battery Pre-Assignment

After computing all DFS paths, batteries are pre-assigned to snakes: sorted by path length (shortest first), each battery goes to the closest snake. Other snakes skip assigned batteries in scoring.

### DFS Pathfinding

DFS simulates full snake body movement with gravity:

- Builds new body after each step (head moves, tail follows or grows on battery).
- Applies gravity: `calcFallDistance` finds how far the body falls until hitting a platform or another snake.
- Supports **out-of-bounds start** — snakes that extended beyond the grid can DFS back.
- **Battery contact optimization:** If the target cell is a battery, the path is registered before gravity (since batteries are eaten on contact).
- **Performance scaling:** Snakes with body length ≥ 15 get reduced DFS depth.

### Safety Scoring

SafetyScorer evaluates each direction for survival:

- **Solid collision detection** — platforms, snake bodies, enemy heads.
- **Fall analysis** — simulates post-move gravity, checks landing safety.
- **Escape route counting (AER)** — tunnel following to detect dead ends.
- **Flood fill** — counts reachable cells to detect traps (space < 1.5x body size = trapped).
- **Support stability** — for long snakes (≥ 6), bonus/penalty based on proximity to platform support.
- **Out-of-bounds penalty** — quadratic penalty for positions outside the grid.

### Game Physics

- **Gravity:** Snakes fall if no body part rests on a solid surface (platform or another snake).
- **Head collision:** Moving into a platform destroys the head; snake survives if ≥ 3 parts remain.
- **Battery eating:** Battery consumed on head contact (before gravity). Snake grows by one segment.
- **Border rules:** Snakes can extend beyond the grid. Death only from falling out of the playing area.

## Key Constants

| Constant | Value | Description |
|---|---|---|
| `MAX_DFS_DEPTH` | 9 | Default DFS search depth |
| `MAX_AVAILABLE_DISTANCE` | 8 | Max distance for DFS intermediate targets |
| `CLUSTER_DISTANCE` | 5 | Max distance between batteries in a cluster |
| `MAX_CLUSTER_SIZE` | 6 | Max batteries per cluster |
| `BASE_BONUS` | 2350 | Score bonus for reachable battery direction |
| `DEATH_FINE` | -99999 | Penalty for certain death |
| `TRAPPED_FINE` | -90000 | Penalty for entering a trap |
| `SUPPORT_BONUS` | 2500 | Bonus/penalty for support stability |

## Lessons Learned

- **BFS + scorers outperformed DFS as primary pathfinding** — BFS explores broadly in one pass, DFS is expensive per-target.
- **Cluster-based targeting significantly improved results** — choosing targets by CoM/cluster proximity beats nearest-battery greedy.
- **DFS is best for short-range** (< 8 steps) precise paths, not as a general navigation strategy.
- **Gravity simulation is O(body^2)** per DFS step — long snakes cause timeouts on large maps.
- **Trampoline system** (using ally snakes as platforms) was attempted but proved too complex to stabilize within contest time.
