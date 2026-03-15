#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <queue>
#include <sstream>
#include <functional> 
#include <math.h>
#include <memory>
#include <random>

using namespace std;

const char PLATFORM_CHAR = '#';

const char SNAKE_CHAR = 'S';

const int ESCAPE_ROUTES = 1;

const int MAX_DFS_DEPTH = 10; // DFS pathfinding depth limit
const int ENEMY_DISTANCE = 8;
const int INITIAL_ZONES_SPLITTING = 2;
const int FULL_SIM_DEPTH = 3;

const int BASE_BONUS = 2350; // 2000 - ok, 2500 - max, 2400 - optimal
const int PREVENTING_ENEMY_BONUS = 3000; // intercept battery from enemy, must outweigh exploration
const int LOOP_ON_PLACE_PENALTY = -3000;
const int ADDITIONAL_BONUS = 1000; // eat battery right now
const int FALL_PENALTY = 1400;    // penalty for any move that causes falling
const int SUPPORT_BONUS = 2500;  // bonus/penalty for support stability (long snakes only)
const int SUPPORT_TURNS_THRESHOLD = 2; // start caring about support when <= this many turns left
const int DEATH_FINE = -99999;
const int ENEMY_HEAD_FINE = -51000;
const int TRAPPED_FINE = -90000;

const double FALL_COST_FACTOR = 0.5;  // each row of falling adds 0.5 to BFS distance
const int DECAY_PER_STEP = (BASE_BONUS + 700) / MAX_DFS_DEPTH;  // smooth decay over full search radius
const int KEEP_DIRECTION_BONUS = 35;
const int OUT_OF_BOUNDS_PENALTY = -1000;
const int LOOP_PENALTY = -900; // -900, very strictly -> why should do something more than once?
const int MAX_LOOP_PENALTY = -7500; // cap: worse than loop, but better than death (-9000/-9999)
const int EXPLORING_BONUS = 1200; // soft guidance, doesn't outweigh battery
const int EXPLORING_CORRECT_BONUS = 200; //
const int COM_BONUS = 15;            // center of mass bonus/penalty
const int NEAREST_CLUSTER_BONUS = 10; // nearest cluster bonus/penalty
const int BODY_BLOCK_BONUS = 500;

const int MAX_FLOOD_FILL = 32;
const int MAX_AER = 6;
const int AER_ENEMY_DISTANCE = 3;
const int CHOSEN_LOCK_TURNS = 10; // how long a snake stays "chosen"
const int CHOSEN_START_TURN = 7; // don't activate before this turn
const int CLUSTER_DISTANCE = 5; // max distance between batteries in a cluster
const int CHOSEN_BONUS = 2500; // bonus for chosen snake moving toward target cluster
const int MAX_CHOSEN_SLOTS = 4; // max number of snakes with cluster assignments
const int ROUTE_FINDING_BONUS = 100; // escaping from traps, additionalRoutes * ROUTE_FINDING_BONUS, 50 - ok


class IScorer;
class SafetyScorer;
class BatteryScorer;
class DecisionMaker;


template<typename K>
ostream& operator<<(ostream& os, const vector<K>& v) {
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it != v.begin()) { os << ";"; }
        os << *it;
    }
    return os;
}

enum CellType {
    EMPTY,
    PLATFORM,
    BATTERY,
    MY_SNAKE,
    ENEMY_SNAKE,
    UNKNOWN
};


enum Direction {
    UP,
    RIGHT,
    DOWN,
    LEFT
};

string getDirectionName(const Direction direction) {
    switch(direction) {
        case UP: return "UP";
        case DOWN: return "DOWN";
        case RIGHT: return "RIGHT";
        case LEFT: return "LEFT";
    }
    return "UNKNOWN";
}


struct Point {
    int x, y;
    Point(int x=0, int y=0) : x(x), y(y) {}
    
    bool operator==(const Point& p) const { return x == p.x && y == p.y; }
    bool operator!=(const Point& p) const { return !(*this == p); }
    Point operator+(const Point& p) const { return Point(x + p.x, y + p.y); }
    bool operator<(const Point& p) const {  // needed for map
        if (x != p.x) return x < p.x;
        return y < p.y;
    }

    friend ostream& operator<<(ostream& os, const Point& p) {
        os << "{" << p.x << ";" << p.y << "}";
        return os;
    }

    int distanceTo(const Point& p) const {
        return abs(x - p.x) + abs(y - p.y);
    }


    Point middlePoint(const Point& p) const {
        return Point((x + p.x)/2, (y + p.y)/2);
    }

    int getClosestBattery(const vector<Point>& batteries) const {
        int dist = 9999;
        int minDist = 9999;
        int batteryId = 0;
        for (int i=0; i<batteries.size(); ++i) {
            dist = distanceTo(batteries[i]);
            if (dist < minDist) {
                minDist = dist;
                batteryId = i;
            }
        }
        return batteryId;
    }

};

const Point directories[4] = {{0,-1}, {1,0}, {0,1}, {-1,0}};


void markPoint(const Point& p) {
    if (p.x < 0 || p.y < 0) return;  // skip invalid marks
    cout << "MARK " << p.x << " " << p.y << ";";
}

Point getCustomPoint(const Point& head, int dir) {
    return head + directories[dir];
}

struct LevelMap {
    LevelMap(const int width, const int height) : height(height), width(width) {}
    void createLevelMap(const vector<string>& iLevelMap) {
        levelMap = iLevelMap;
    }

    void addPowerElement(const int x, const int y) {
        powerElements.push_back({x,y});
    }

    void copyPowerMap(const vector<Point>& pe) {
        powerElements = pe;
    }

    void removePowerElement(const Point& p) {
        powerElements.erase(std::remove_if(powerElements.begin(), powerElements.end(),
                            [p](const Point& point){ return point == p; }), powerElements.end());

    }

    vector<Point> getPowerElements() const {
        return powerElements;
    }

    void setElement(const Point& p, const char SNAKE_CHAR) {
        if (!isOutsideMap(p)) levelMap[p.y].replace(p.x, 1, 1, SNAKE_CHAR);
    }

    bool isOutsideMap(const Point& p) const {
        if (p.x < 0 || p.x >= width || 
        p.y < 0 || p.y >= height) {
            return true;  // outside map bounds
        }
        return false;
    }

    void clearPowerElements() {
        powerElements.clear();
    }

    bool isBattery(const Point& p) const {
        // Check against all known batteries
        for (const Point& battery : powerElements) {
            if (battery == p) return true;
        }
        return false;
    }

    void debug() const {
        for (const auto level : levelMap) {
            cerr << level << endl;
        }
    }

    int height;
    int width;
    vector<string> levelMap;

    protected:
    vector<Point> powerElements;
};


struct Snake {
    Snake(const int id, const bool isMy) : id(id), isMy(isMy), movingDirection(UP) { isDead = false; }
    Snake(const Snake& other) 
        : id(other.id), isMy(other.isMy), body(other.body), movingDirection(other.movingDirection),
          heatMap(other.heatMap) { isDead = false; }

    void setBody(const string& bodyString) {
        stringstream ss(bodyString);
        string t;
        body.clear();
        while (getline(ss, t, ':')) {
            int x = stoi(t);
            int y = stoi(t.substr(t.find(',') + 1));
            body.push_back(Point(x, y));
        }
    }

    void setBody(const vector<Point>& newBody) {
        body.clear();
        body = newBody;
    }

    void updateHeatMap() {
        heatMap[getHead()]++;
    }

    int getHeat() const {
        if (heatMap.count(getHead())) return heatMap.at(getHead());
        return 0;
    }


    int getHeat(const Point& p) const {
        if (heatMap.count(p)) return heatMap.at(p);
        return 0;
    }

    void setMovingDirection(const Direction& dir) { movingDirection = dir; }
    
    Direction getMovingDirection() const {
        return movingDirection;
    }

    vector<Point> getBody() const {
        return body;
    }

    Point getHead() const {
            return body.empty() ? Point(-1,-1) : body[0];
    }

    bool isAlive() const {
        return !isDead;  // or other logic
    }

    bool isMySnake() const {
        return isMy;
    }

    int getId() const { return id; }

    void debug() const {
        cerr << "S" << id << " H=" << getHead() << " " << getDirectionName(getMovingDirection())
             << " " << getBody() << endl;
    }

    void updateHead(const Point& p, bool isGrow) {
        body.insert(body.begin(), p);
        if (!isGrow) body.pop_back();
    }

    int isPartOfSnake(const Point& p) const {
        auto it = find(body.begin(), body.end(), p);
        if (it == body.end()) return -1;
        return (int)(it - body.begin());
    }

    int getClosestBattery(const vector<Point>& batteries) const {
        return getHead().getClosestBattery(batteries);
    }

    protected:
    map<Point, int> heatMap;

    int id;
    bool isMy;
    vector<Point> body;
    Direction movingDirection;
    bool isDead;
};


CellType whatInPoint(const Point& p, const LevelMap& levelMap, 
                     const map<int, Snake>& allSnakes) {
    
    if (levelMap.isOutsideMap(p)) {
        return UNKNOWN;  // outside map bounds
    }

    // For fictional cases
    if (levelMap.levelMap[p.y][p.x] == SNAKE_CHAR) {
        return MY_SNAKE;
    }

    // 1. Platform
    if (levelMap.levelMap[p.y][p.x] == PLATFORM_CHAR) {
        return PLATFORM;
    }
    
    // 2. Battery
    if (levelMap.isBattery(p)) {
        return BATTERY;
    }
    
    // 3. Snakes
    for (const auto& [id, snake] : allSnakes) {
        if (!snake.isAlive()) continue;
        
        for (const Point& part : snake.getBody()) {
            if (part == p) {
                if (snake.isMySnake()) return MY_SNAKE;
                else return ENEMY_SNAKE;
            }
        }
    }
    
    return EMPTY;
}

bool isSolidCollision(const Point& p, const LevelMap& levelMap,
                      const map<int, Snake>& allSnakes, int myId,
                      bool eatingBattery = false, int skipTailCount = 1) {
    CellType cell = whatInPoint(p, levelMap, allSnakes);
    if (cell == PLATFORM) return true;
    for (const auto& [id, snake] : allSnakes) {
        if (!snake.isAlive()) continue;
        int idx = snake.isPartOfSnake(p);
        if (idx < 0) continue;
        // Own tail segments that will move away (unless eating battery)
        if (id == myId && !eatingBattery && idx >= (int)snake.getBody().size() - skipTailCount) continue;
        return true;
    }
    return false;
}

const int MAX_TUNNEL_DEPTH = 6;

// Follow tunnel from entry point, return AER at exit. 0 = dead end.
int followTunnel(const Point& entry, const Point& prev, const LevelMap& levelMap,
                 const map<int, Snake>& allSnakes, int myId, bool eatingBattery = false) {
    Point current = entry;
    Point prevP = prev;
    for (int depth = 0; depth < MAX_TUNNEL_DEPTH; depth++) {
        int exits = 0;
        Point nextStep = Point(-1, -1);
        for (int dir = 0; dir < 4; dir++) {
            Point next = getCustomPoint(current, dir);
            if (next == prevP) continue;
            if (!isSolidCollision(next, levelMap, allSnakes, myId, eatingBattery)) {
                exits++;
                nextStep = next;
            }
        }
        if (exits == 0) return 0; // dead end
        if (exits > 1) {
            // Tunnel exit — count AER here
            int aer = 0;
            for (int dir = 0; dir < 4; dir++) {
                Point next = getCustomPoint(current, dir);
                if (!isSolidCollision(next, levelMap, allSnakes, myId, eatingBattery)) {
                    for (int d1 = 0; d1 < 4; ++d1) {
                        Point t = getCustomPoint(next, d1);
                        if (!isSolidCollision(t, levelMap, allSnakes, myId, eatingBattery)) {
                            ++aer;
                        }
                    }
                }
            }
            return min(aer, MAX_AER);
        }
        // exits == 1: continue through tunnel
        prevP = current;
        current = nextStep;
    }
    return 0; // tunnel too long
}

pair<int, int> countEscapeRoutes(const Point& from, const LevelMap& levelMap,
                                  const map<int, Snake>& allSnakes, int myId,
                                  bool eatingBattery = false) {
    int escapeRoutes = 0, additionalEscapeRoutes = 0;
    for (int dir = 0; dir < 4; dir++) {
        Point next = getCustomPoint(from, dir);
        if (isSolidCollision(next, levelMap, allSnakes, myId, eatingBattery)) continue;
        escapeRoutes++;
        additionalEscapeRoutes += followTunnel(next, from, levelMap, allSnakes, myId, eatingBattery);
    }

    if (additionalEscapeRoutes > MAX_AER) additionalEscapeRoutes = MAX_AER;
    return {escapeRoutes, additionalEscapeRoutes};
}

pair<int, int> findClosestEnemy(const Point& target, const map<int, Snake>& allSnakes) {
    int minDist = 9999;
    int enemyId = -1;
    for (const auto& [id, snake] : allSnakes) {
        if (snake.isMySnake()) continue;
        int d = target.distanceTo(snake.getHead());
        if (d < minDist) {
            minDist = d;
            enemyId = id;
        }
    }
    return {minDist, enemyId};
}

Snake fallingImitation(const Snake& snake, const LevelMap& levelMap, const map<int, Snake>& allSnakes) {
    int totalFallDistance=levelMap.height;
    Snake result(snake);
    vector<Point> snakeBody = snake.getBody();
    for (int i = 0; i < (int)snakeBody.size(); ++i) {
        int currentFallDistance=0;
        for (int y=1; y < levelMap.height; ++y) {
            const Point below(snakeBody[i].x, snakeBody[i].y+y);
            CellType ct = whatInPoint(below, levelMap, allSnakes);
            if (ct == CellType::EMPTY) continue;
            else if (ct == CellType::UNKNOWN) { currentFallDistance = levelMap.height; break;}
            else if (ct == MY_SNAKE && snake.isPartOfSnake(below) >= 0) continue;
            else { currentFallDistance = y-1; break; }
        }
        if (currentFallDistance < totalFallDistance) {
            totalFallDistance = currentFallDistance;
        }
    }
    for (int i=0; i<(int)snakeBody.size(); ++i) {
        snakeBody[i] = Point(snakeBody[i].x, snakeBody[i].y+totalFallDistance);
    }
    result.setBody(snakeBody);
    return result;
}

Point applyGravityConservative(const Point& pos, const LevelMap& levelMap) {
    for (int y = pos.y; y < levelMap.height; y++) {
        Point below(pos.x, y + 1);
        if (below.y >= levelMap.height) return Point(pos.x, levelMap.height); // fell off
        if (levelMap.levelMap[below.y][below.x] == PLATFORM_CHAR) return Point(pos.x, y);
        if (levelMap.isBattery(below)) return Point(pos.x, y);
    }
    return Point(pos.x, levelMap.height); // fell off
}


// Flood fill from a point, no gravity. Returns number of reachable cells.
// Used to detect dead ends: if reachable cells < snake body size, it's a trap.
// Uses static visited array with generation counter to avoid reallocation.
int floodFillCount(const Point& start, const LevelMap& levelMap,
                   const map<int, Snake>& allSnakes, int myId) {
    if (levelMap.isOutsideMap(start)) return 0;
    static vector<vector<int>> visited;
    static int generation = 0;
    if ((int)visited.size() != levelMap.height || (int)visited[0].size() != levelMap.width) {
        visited.assign(levelMap.height, vector<int>(levelMap.width, 0));
    }
    generation++;
    queue<Point> q;
    q.push(start);
    visited[start.y][start.x] = generation;
    int count = 0;
    while (!q.empty()) {
        Point p = q.front(); q.pop();
        count++;
        if (count >= MAX_FLOOD_FILL) return count;
        for (int dir = 0; dir < 4; dir++) {
            Point next = getCustomPoint(p, dir);
            if (levelMap.isOutsideMap(next)) continue;
            if (visited[next.y][next.x] == generation) continue;
            if (isSolidCollision(next, levelMap, allSnakes, myId, false, 2)) continue;
            visited[next.y][next.x] = generation;
            q.push(next);
        }
    }
    return count;
}

// Count how many steps a snake can take before losing support.
// Finds the first supported segment (closest to head), freeSteps = segments from there to tail.
int calcFreeSteps(const Snake& snake, const LevelMap& levelMap, const map<int, Snake>& allSnakes) {
    const auto& body = snake.getBody();
    // body[0] = head, body[n-1] = tail
    for (int i = 0; i < (int)body.size(); i++) {
        Point below(body[i].x, body[i].y + 1);
        if (below.y >= levelMap.height) continue;
        CellType ct = whatInPoint(below, levelMap, allSnakes);
        if (ct == PLATFORM || ct == BATTERY || ct == ENEMY_SNAKE) {
            return (int)body.size() - 1 - i;
        } else if (ct == MY_SNAKE && snake.isPartOfSnake(below) < 0) {
            return (int)body.size() - 1 - i;
        }
    }
    return 0; // no support at all
}

struct BatteryInfo {
    Point pos;
    int dist;
    int firstDir;
};

// DFS pathfinding result
struct DfsResult {
    int pathLen = -1;
    int firstDir = -1;
    vector<Point> path;
    Point target;
};

// DFS: find actual path from start to target, with full body gravity simulation
// Returns shortest path found within maxDepth, or pathLen=-1 if unreachable
DfsResult dfsPathToTarget(const Snake& snake, const Point& target,
                          const LevelMap& levelMap, const map<int, Snake>& allSnakes,
                          int maxDepth = MAX_DFS_DEPTH) {
    int myId = snake.getId();
    // One copy, erase self — no per-node copying
    map<int, Snake> dfsSnakes(allSnakes);
    dfsSnakes.erase(myId);

    vector<vector<bool>> visited(levelMap.height, vector<bool>(levelMap.width, false));
    DfsResult best;
    best.target = target;
    vector<Point> currentPath;

    function<void(const vector<Point>&, int, int)> dfs =
        [&](const vector<Point>& body, int depth, int firstDir) {
        Point head = body[0];
        if (head == target) {
            if (best.pathLen < 0 || depth < best.pathLen) {
                best.pathLen = depth;
                best.firstDir = firstDir;
                best.path = currentPath;
            }
            return;
        }
        if (depth >= maxDepth) return;
        if (best.pathLen >= 0 && depth >= best.pathLen) return;
        if (head.distanceTo(target) > maxDepth - depth) return;
        if (best.pathLen >= 0 && depth + head.distanceTo(target) >= best.pathLen) return;

        int dirs[4] = {0, 1, 2, 3};
        sort(dirs, dirs + 4, [&](int a, int b) {
            return getCustomPoint(head, a).distanceTo(target) < getCustomPoint(head, b).distanceTo(target);
        });

        for (int di = 0; di < 4; di++) {
            int dir = dirs[di];
            Point next = getCustomPoint(head, dir);
            if (levelMap.isOutsideMap(next)) continue;
            if (visited[next.y][next.x]) continue;
            bool eatingBattery = levelMap.isBattery(next);
            if (isSolidCollision(next, levelMap, dfsSnakes, myId, eatingBattery)) continue;

            // Build new body
            vector<Point> newBody;
            newBody.push_back(next);
            int keepCount = eatingBattery ? (int)body.size() : (int)body.size() - 1;
            for (int i = 0; i < keepCount; i++) newBody.push_back(body[i]);

            // Apply gravity via fallingImitation (no per-node copy)
            Snake tempSnake(snake);
            tempSnake.setBody(newBody);
            Snake fallen = fallingImitation(tempSnake, levelMap, dfsSnakes);
            vector<Point> settledBody = fallen.getBody();
            Point settledHead = settledBody[0];

            if (levelMap.isOutsideMap(settledHead)) continue;
            if (settledHead.y >= levelMap.height) continue;
            if (visited[settledHead.y][settledHead.x]) continue;

            visited[settledHead.y][settledHead.x] = true;
            currentPath.push_back(settledHead);
            dfs(settledBody, depth + 1, firstDir < 0 ? dir : firstDir);
            currentPath.pop_back();
            visited[settledHead.y][settledHead.x] = false;
        }
    };

    visited[snake.getHead().y][snake.getHead().x] = true;
    dfs(snake.getBody(), 0, -1);
    return best;
}

// Find all reachable batteries for a snake using DFS
// Filters by Manhattan distance, runs DFS for each candidate
vector<DfsResult> findBatteriesDfs(const Snake& snake, const LevelMap& levelMap,
                                    const map<int, Snake>& allSnakes) {
    vector<DfsResult> results;
    Point head = snake.getHead();
    int myId = snake.getId();
    auto batteries = levelMap.getPowerElements();

    // Sort batteries by Manhattan distance
    sort(batteries.begin(), batteries.end(), [&](const Point& a, const Point& b) {
        return head.distanceTo(a) < head.distanceTo(b);
    });

    for (const auto& bat : batteries) {
        int manDist = head.distanceTo(bat);
        if (manDist > MAX_DFS_DEPTH) break; // sorted, rest are farther

        DfsResult res = dfsPathToTarget(snake, bat, levelMap, allSnakes);
        if (res.pathLen >= 0) {
            results.push_back(res);
        }
    }

    // Sort results by path length
    sort(results.begin(), results.end(), [](const DfsResult& a, const DfsResult& b) {
        return a.pathLen < b.pathLen;
    });

    return results;
}

/* OLD BFS — commented out, replaced by DFS pathfinding
vector<BatteryInfo> findAllBatteriesGravity(const Snake& snake, const LevelMap& levelMap,
                                             int maxSteps, const map<int, Snake>& allSnakes) {
    vector<BatteryInfo> result;
    vector<vector<bool>> visited(levelMap.height, vector<bool>(levelMap.width, false));
    Point start = snake.getHead();
    auto checkBattery = [&](const Point& pos, int dist, int firstDir) {
        if (!levelMap.isOutsideMap(pos) && levelMap.isBattery(pos)) {
            result.push_back({pos, dist, firstDir});
        }
    };

    struct FullSimNode {
        Snake snake;
        int steps;
        int firstDir;
    };

    auto expandFullSim = [&](const vector<FullSimNode>& sources, bool isFirstDepth) {
        vector<FullSimNode> nextNodes;
        for (const auto& node : sources) {
            if (node.steps >= maxSteps) continue;
            Point head = node.snake.getHead();

            for (int dir = 0; dir < 4; dir++) {
                int firstDir = isFirstDepth ? dir : node.firstDir;
                Point newHead = getCustomPoint(head, dir);
                if (levelMap.isOutsideMap(newHead)) continue;
                if (isSolidCollision(newHead, levelMap, allSnakes, node.snake.getId())) continue;

                Snake moved(node.snake);
                moved.updateHead(newHead, levelMap.isBattery(newHead));
                map<int, Snake> tempSnakes(allSnakes);
                tempSnakes.erase(node.snake.getId());
                tempSnakes.insert(make_pair(moved.getId(), moved));
                Snake settled = fallingImitation(moved, levelMap, tempSnakes);
                Point settledHead = settled.getHead();

                if (levelMap.isOutsideMap(settledHead)) continue;
                if (visited[settledHead.y][settledHead.x]) continue;
                visited[settledHead.y][settledHead.x] = true;

                int fallDist = abs(settledHead.y - newHead.y);
                int stepCost = node.steps + 1 + (int)(fallDist * FALL_COST_FACTOR);

                checkBattery(newHead, stepCost, firstDir);
                if (settledHead != newHead) checkBattery(settledHead, stepCost, firstDir);

                nextNodes.push_back({settled, stepCost, firstDir});
            }
        }
        return nextNodes;
    };

    // --- Phase 1: full body simulation ---
    vector<FullSimNode> currentNodes = {{snake, 0, -1}};
    queue<tuple<Point, int, int>> phase2Queue;

    for (int depth = 1; depth <= min(FULL_SIM_DEPTH, maxSteps); depth++) {
        currentNodes = expandFullSim(currentNodes, depth == 1);
    }

    for (const auto& node : currentNodes) {
        phase2Queue.push({node.snake.getHead(), node.steps, node.firstDir});
    }

    // --- Phase 2: conservative BFS ---
    while (!phase2Queue.empty()) {
        auto [pos, dist, firstDir] = phase2Queue.front(); phase2Queue.pop();
        if (dist >= maxSteps) continue;

        for (int dir = 0; dir < 4; dir++) {
            Point newHead = getCustomPoint(pos, dir);
            if (levelMap.isOutsideMap(newHead)) continue;
            if (levelMap.levelMap[newHead.y][newHead.x] == PLATFORM_CHAR) continue;
            bool blocked = false;
            for (const auto& [id, s] : allSnakes) {
                if (s.isMySnake()) continue;
                if (s.isPartOfSnake(newHead) >= 0) { blocked = true; break; }
            }
            if (blocked) continue;

            Point settledPos = applyGravityConservative(newHead, levelMap);
            if (settledPos.y >= levelMap.height) continue;
            if (visited[settledPos.y][settledPos.x]) continue;
            visited[settledPos.y][settledPos.x] = true;

            int fallDist = abs(settledPos.y - newHead.y);
            int stepCost = dist + 1 + (int)(fallDist * FALL_COST_FACTOR);

            checkBattery(newHead, stepCost, firstDir);
            if (settledPos != newHead) checkBattery(settledPos, stepCost, firstDir);

            phase2Queue.push({settledPos, stepCost, firstDir});
        }
    }

    return result;
}

// Alternative BFS with support-aware gravity (freeSteps).
// At Phase 1 end, calculates how many steps the snake can take before losing support.
// Phase 2 skips gravity for those steps, then applies conservative gravity.
vector<BatteryInfo> findAllBatteriesFreeSteps(const Snake& snake, const LevelMap& levelMap,
                                              int maxSteps, const map<int, Snake>& allSnakes) {
    vector<BatteryInfo> result;
    vector<vector<bool>> visited(levelMap.height, vector<bool>(levelMap.width, false));
    auto checkBattery = [&](const Point& pos, int dist, int firstDir) {
        if (!levelMap.isOutsideMap(pos) && levelMap.isBattery(pos)) {
            result.push_back({pos, dist, firstDir});
        }
    };

    struct FullSimNode {
        Snake snake;
        int steps;
        int firstDir;
    };

    auto expandFullSim = [&](const vector<FullSimNode>& sources, bool isFirstDepth) {
        vector<FullSimNode> nextNodes;
        for (const auto& node : sources) {
            if (node.steps >= maxSteps) continue;
            Point head = node.snake.getHead();
            for (int dir = 0; dir < 4; dir++) {
                int firstDir = isFirstDepth ? dir : node.firstDir;
                Point newHead = getCustomPoint(head, dir);
                if (levelMap.isOutsideMap(newHead)) continue;
                if (isSolidCollision(newHead, levelMap, allSnakes, node.snake.getId())) continue;
                Snake moved(node.snake);
                moved.updateHead(newHead, levelMap.isBattery(newHead));
                map<int, Snake> tempSnakes(allSnakes);
                tempSnakes.erase(node.snake.getId());
                tempSnakes.insert(make_pair(moved.getId(), moved));
                Snake settled = fallingImitation(moved, levelMap, tempSnakes);
                Point settledHead = settled.getHead();
                if (levelMap.isOutsideMap(settledHead)) continue;
                if (visited[settledHead.y][settledHead.x]) continue;
                visited[settledHead.y][settledHead.x] = true;
                int fallDist = abs(settledHead.y - newHead.y);
                int stepCost = node.steps + 1 + (int)(fallDist * FALL_COST_FACTOR);
                checkBattery(newHead, stepCost, firstDir);
                if (settledHead != newHead) checkBattery(settledHead, stepCost, firstDir);
                nextNodes.push_back({settled, stepCost, firstDir});
            }
        }
        return nextNodes;
    };

    // --- Phase 1: full body simulation ---
    vector<FullSimNode> currentNodes = {{snake, 0, -1}};
    queue<tuple<Point, int, int, int>> phase2Queue; // pos, dist, firstDir, freeSteps

    for (int depth = 1; depth <= min(FULL_SIM_DEPTH, maxSteps); depth++) {
        currentNodes = expandFullSim(currentNodes, depth == 1);
    }

    for (const auto& node : currentNodes) {
        int freeSteps = calcFreeSteps(node.snake, levelMap, allSnakes);
        phase2Queue.push({node.snake.getHead(), node.steps, node.firstDir, freeSteps});
    }

    // --- Phase 2: BFS with support-aware gravity ---
    while (!phase2Queue.empty()) {
        auto [pos, dist, firstDir, freeSteps] = phase2Queue.front(); phase2Queue.pop();
        if (dist >= maxSteps) continue;

        for (int dir = 0; dir < 4; dir++) {
            Point newHead = getCustomPoint(pos, dir);
            if (levelMap.isOutsideMap(newHead)) continue;
            if (levelMap.levelMap[newHead.y][newHead.x] == PLATFORM_CHAR) continue;
            bool blocked = false;
            for (const auto& [id, s] : allSnakes) {
                if (s.isMySnake()) continue;
                if (s.isPartOfSnake(newHead) >= 0) { blocked = true; break; }
            }
            if (blocked) continue;

            Point settledPos = newHead;
            int nextFreeSteps = 0;
            // if (freeSteps > 0) {
            //     settledPos = newHead;
            //     nextFreeSteps = freeSteps - 1;
            // } else {
            //     settledPos = applyGravityConservative(newHead, levelMap);
            //     nextFreeSteps = 0;
            // }
            if (settledPos.y >= levelMap.height) continue;
            if (visited[settledPos.y][settledPos.x]) continue;
            visited[settledPos.y][settledPos.x] = true;

            int fallDist = abs(settledPos.y - newHead.y);
            int stepCost = dist + 1 + (int)(fallDist * FALL_COST_FACTOR);

            checkBattery(newHead, stepCost, firstDir);
            if (settledPos != newHead) checkBattery(settledPos, stepCost, firstDir);

            phase2Queue.push({settledPos, stepCost, firstDir, nextFreeSteps});
        }
    }

    return result;
}
END OLD BFS */

class IScorer {
    public:
        virtual ~IScorer() = default;
        virtual int evaluate(const Snake& snake, const LevelMap& levelMap,
                            const map<int, Snake>& allSnakes, int direction) = 0;

    protected:

        struct MoveContext {
            Snake snake;
            LevelMap levelMap;
            map<int, Snake> allSnakes;
        };

        MoveContext simulateMove(const Snake& snake, const Point& newHead,
                                 const LevelMap& levelMap, const map<int, Snake>& allSnakes,
                                 bool markTail = false) const {
            Snake newSnake(snake);
            newSnake.updateHead(newHead, levelMap.isBattery(newHead));
            LevelMap newLevelMap(levelMap);
            newLevelMap.createLevelMap(levelMap.levelMap);
            newLevelMap.copyPowerMap(levelMap.getPowerElements());
            map<int, Snake> newAllSnakes(allSnakes);
            newAllSnakes.erase(snake.getId());
            newAllSnakes.insert(make_pair(newSnake.getId(), newSnake));
            if (levelMap.isBattery(newHead)) {
                newLevelMap.removePowerElement(newHead);
                if (markTail) {
                    newLevelMap.setElement(newSnake.getBody().back(), SNAKE_CHAR);
                }
            }
            return {newSnake, newLevelMap, newAllSnakes};
        }

        bool hasSupportAfterMove(const Snake& snake, const Point& newHead,
                                int direction, const LevelMap& levelMap,
                                const map<int, Snake>& allSnakes) const {
            auto ctx = simulateMove(snake, newHead, levelMap, allSnakes, true);
            return !isSnakeFalling(ctx.snake, ctx.levelMap, ctx.allSnakes);
        }

        Snake simulateMoveAndFall(const Snake& snake, const Point& newHead,
                                   const LevelMap& levelMap, const map<int, Snake>& allSnakes) const {
            auto ctx = simulateMove(snake, newHead, levelMap, allSnakes);
            return fallingImitation(ctx.snake, ctx.levelMap, ctx.allSnakes);
        }

        bool isLoopOnPlace(const Snake& snake, const LevelMap& levelMap,
                        const map<int, Snake>& allSnakes, int direction) const {
            const Point newHead = getCustomPoint(snake.getHead(), direction);
            if (!hasSupportAfterMove(snake, newHead, direction, levelMap, allSnakes)) {
                auto ctx = simulateMove(snake, newHead, levelMap, allSnakes, true);
                Snake fallenSnake = fallingImitation(ctx.snake, ctx.levelMap, ctx.allSnakes);
                if (fallenSnake.getHead() == snake.getHead()) return true;
            }
            return false;
        }

        int getOutOfBoundsPenalty(const Point& p, const LevelMap& levelMap) {
            if (p.x >= 0 && p.x < levelMap.width && 
                p.y >= 0 && p.y < levelMap.height) {
                return 0;  // within map bounds
            }
            
            // Calculate how far out of bounds
            int dx = 0, dy = 0;
            if (p.x < 0) dx = -p.x;
            if (p.x >= levelMap.width) dx = p.x - (levelMap.width - 1);
            if (p.y < 0) dy = -p.y;
            if (p.y >= levelMap.height) dy = p.y - (levelMap.height - 1);
            
            int distance = max(dx, dy);
            // Quadratic penalty: further = much worse
            return OUT_OF_BOUNDS_PENALTY * (distance * distance);
        }

        pair<bool, int> willBeTrappedNextTurn(const Snake& snake, const LevelMap& levelMap,
                               const map<int, Snake>& allSnakes, int direction,
                               bool eatingBattery = false) {
            Point newHead = getCustomPoint(snake.getHead(), direction);
            auto ctx = simulateMove(snake, newHead, levelMap, allSnakes);
            auto [escapeRoutes, additionalEscapeRoutes] = countEscapeRoutes(newHead, ctx.levelMap, ctx.allSnakes, snake.getId());
            cerr<<"AER: " << additionalEscapeRoutes << ", ";
            return make_pair(escapeRoutes < ESCAPE_ROUTES || additionalEscapeRoutes < (ESCAPE_ROUTES + 1), additionalEscapeRoutes);
        }

        bool isSnakeFalling(const Snake& snake, const LevelMap& levelMap, const map<int, Snake>& allSnakes) const {
            for (const auto bodyPart : snake.getBody()) {
                const Point below = getCustomPoint(bodyPart, DOWN);
                CellType cellType = whatInPoint(below, levelMap, allSnakes);
                if (cellType == PLATFORM || cellType == BATTERY || cellType == ENEMY_SNAKE) return false;
                if (cellType == EMPTY) continue;
                if (cellType == MY_SNAKE && snake.isPartOfSnake(below) < 0) return false;
            }
            return true;
        }
};

class SafetyScorer : public IScorer {
    // Working normal
    private:
        // Count how many turns until the snake loses its last support point
        int countSupportTurnsLeft(const Snake& snake, const LevelMap& levelMap,
                                   const map<int, Snake>& allSnakes) const {
            const auto& body = snake.getBody();
            // Walk from head toward tail, find the support point closest to head
            for (int i = 0; i < (int)body.size(); i++) {
                Point below(body[i].x, body[i].y + 1);
                if (below.y >= levelMap.height) continue;
                CellType ct = whatInPoint(below, levelMap, allSnakes);
                if (ct == PLATFORM || ct == ENEMY_SNAKE) {
                    // Support found at index i. Turns left = elements after it (tail side)
                    return (int)body.size() - 1 - i;
                }
                if (ct == MY_SNAKE && snake.isPartOfSnake(below) < 0) {
                    return (int)body.size() - 1 - i;
                }
            }
            return 0; // no support at all
        }

        // Check if newHead has nearby support (platform directly below, not a long fall)
        bool hasNewSupport(const Point& newHead, const LevelMap& levelMap) const {
            // Check cells below newHead within a small range
            for (int dy = 0; dy <= 2; dy++) {
                Point below(newHead.x, newHead.y + 1 + dy);
                if (below.y >= levelMap.height) return false;
                if (levelMap.levelMap[below.y][below.x] == PLATFORM_CHAR) return true;
            }
            return false;
        }

        int calcAerBonus(int aer, const Point& head, const Point& target,
                         const map<int, Snake>& allSnakes) {
            int headDist = findClosestEnemy(head, allSnakes).first;
            int pointDist = findClosestEnemy(target, allSnakes).first;
            int enemyDist = pointDist;
            if (pointDist > headDist) enemyDist += 1;  // moving away, relax
            if (enemyDist <= AER_ENEMY_DISTANCE) {
                // Near enemy: penalize low escape routes, cap bonus
                if (aer < 3) return (aer - 3) * ROUTE_FINDING_BONUS;
                return 2 * ROUTE_FINDING_BONUS;
            }
            return min(aer, 2) * ROUTE_FINDING_BONUS;
        }

    public:
    
        // Evaluates direction safety: lower risk = higher score
        int evaluate(const Snake& snake, const LevelMap& levelMap, 
                    const map<int, Snake>& allSnakes, int direction) override {
            
            int totalBonus = 0;
            Point head = snake.getHead();
            Point newHead = getCustomPoint(head, direction);
            
            // 1. Solid collision check
            bool eatingBattery = levelMap.isBattery(newHead);
            if (isSolidCollision(newHead, levelMap, allSnakes, snake.getId(), eatingBattery)) {
                bool isEnemyHead = false;
                for (const auto& [id, s] : allSnakes) {
                    if (!s.isMySnake() && s.isAlive() && s.isPartOfSnake(newHead) == 0) {
                        isEnemyHead = true; break;
                    }
                }
                totalBonus += isEnemyHead ? ENEMY_HEAD_FINE : DEATH_FINE;
            } else if (!hasSupportAfterMove(snake, newHead, direction, levelMap, allSnakes)) {
                // 2. No support after move — snake will fall
                if ((int)snake.getBody().size() >= 5) totalBonus += -FALL_PENALTY;
                // Simulate fall and evaluate landing position
                Snake fallenSnake = simulateMoveAndFall(snake, newHead, levelMap, allSnakes);
                Point landedHead = fallenSnake.getHead();
                if (landedHead.y >= levelMap.height || levelMap.isOutsideMap(landedHead)) {
                    totalBonus += DEATH_FINE;  // fell off map
                } else {
                    auto [escapeRoutes, additionalEscape] = countEscapeRoutes(landedHead, levelMap, allSnakes, snake.getId(), eatingBattery);
                    if (escapeRoutes < ESCAPE_ROUTES || additionalEscape < (ESCAPE_ROUTES + 1)) {
                        totalBonus += TRAPPED_FINE;
                        //cerr << "[FALL_TRAP] ";
                    } else {
                        totalBonus += calcAerBonus(additionalEscape, head, landedHead, allSnakes);
                    }
                }
            } else {
                const auto result = willBeTrappedNextTurn(snake, levelMap, allSnakes, direction, eatingBattery);
                if (result.first) {
                    totalBonus += TRAPPED_FINE;
                    //cerr << "[TRAPPED] ";
                } else {
                    totalBonus += calcAerBonus(result.second, head, newHead, allSnakes);
                }
            }
            // Flood fill check: detect dead ends regardless of fall/no-fall
            if (totalBonus > DEATH_FINE) {
                int space = floodFillCount(newHead, levelMap, allSnakes, snake.getId());
                //cerr << "[FLOOD " << newHead << " " << space << "/" << snake.getBody().size() << "] ";
                if (space < (int)snake.getBody().size() * 3 / 2 + 1) {
                    totalBonus = TRAPPED_FINE;
                    //cerr << "[FLOOD_TRAP] ";
                }
            }
            totalBonus += getOutOfBoundsPenalty(newHead, levelMap);

            // Support stability: bonus/penalty based on future support (long snakes only)
            if (totalBonus > TRAPPED_FINE && (int)snake.getBody().size() >= 6) {
                int supportLeft = countSupportTurnsLeft(snake, levelMap, allSnakes);
                if (supportLeft < SUPPORT_TURNS_THRESHOLD) {
                    if (hasNewSupport(newHead, levelMap)) {
                        totalBonus += SUPPORT_BONUS;
                        //cerr << "[S+" << supportLeft << "] ";
                    } else {
                        totalBonus += -SUPPORT_BONUS;
                        //cerr << "[!!! " << supportLeft << "] ";
                    }
                }
            }

            //cerr << "Safety: " << totalBonus << ", ";
            return totalBonus;
        }
        
};

class BatteryScorer : public IScorer {
    public:

        void clearCache() { cachedSnakeId = -1; cachedDfs.clear(); claimedBatteries.clear(); }
        void setCache(int snakeId, const vector<DfsResult>& dfs) {
            cachedSnakeId = snakeId;
            cachedDfs = dfs;
        }
        void setEnemyBfs(const map<Point, int>* bfs) { enemyBfsDist = bfs; }
        int getEnemyDist(const Point& bat) const {
            if (!enemyBfsDist) return 9999;
            auto it = enemyBfsDist->find(bat);
            return it != enemyBfsDist->end() ? it->second : 9999;
        }

        void claimBattery(const Point& pos) { claimedBatteries.insert(pos); }
        const set<Point>& getClaimedBatteries() const { return claimedBatteries; }

        Point getBestBatteryForDir(const Snake& snake, const map<int, Snake>& allSnakes, int direction) {
            vector<DfsResult> inDir;
            for (const auto& r : cachedDfs) {
                if (r.firstDir == direction) inDir.push_back(r);
            }
            if (inDir.empty()) return Point(-1, -1);
            sort(inDir.begin(), inDir.end(),
                [](const DfsResult& a, const DfsResult& b) { return a.pathLen < b.pathLen; });
            for (const auto& r : inDir) {
                if (claimedBatteries.count(r.target)) continue;
                if (getEnemyDist(r.target) < r.pathLen) continue;
                return r.target;
            }
            return Point(-1, -1);
        }

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            cerr << "Battery: ";
            if (isLoopOnPlace(snake, levelMap, allSnakes, direction)) {
                cerr << "LOOP, ";
                return 0;
            }

            vector<DfsResult> inDir;
            for (const auto& r : cachedDfs) {
                if (r.firstDir == direction) inDir.push_back(r);
            }
            if (inDir.empty()) { cerr << "0, "; return 0; }
            sort(inDir.begin(), inDir.end(),
                [](const DfsResult& a, const DfsResult& b) { return a.pathLen < b.pathLen; });

            int totalBonus = 0;
            for (const auto& r : inDir) {
                if (claimedBatteries.count(r.target)) {
                    cerr << "[" << r.target << " CLAIMED] ";
                    continue;
                }
                int enemyDist = getEnemyDist(r.target);
                if (enemyDist < r.pathLen) {
                    cerr << "[" << r.target << " ENEMY(" << enemyDist << "<" << r.pathLen << ")] ";
                    continue;
                }
                cerr << "[" << r.target << " d=" << r.pathLen << " OK] ";
                totalBonus = BASE_BONUS - (r.pathLen - 1) * DECAY_PER_STEP;
                break;
            }
            cerr << totalBonus << ", ";
            return totalBonus;
        }

    private:
        int cachedSnakeId = -1;
        vector<DfsResult> cachedDfs;
        set<Point> claimedBatteries;
        const map<Point, int>* enemyBfsDist = nullptr;
};

struct BatteryCluster {
    vector<Point> batteries;
    Point center;

    void recalcCenter() {
        if (batteries.empty()) return;
        int tx = 0, ty = 0;
        for (const auto& b : batteries) { tx += b.x; ty += b.y; }
        center = Point(tx / (int)batteries.size(), ty / (int)batteries.size());
    }
};

class ExplorationScorer : public IScorer {
    // Working excellent
    public:
        ExplorationScorer(int zones = INITIAL_ZONES_SPLITTING) : numZones(zones) {}

        void setClusters(const vector<BatteryCluster>* c) { clustersPtr = c; }

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            int comB = centerOfMassBonus(snake, direction);
            int clB = nearestClusterBonus(snake, direction);
            int exploringScore = comB + clB;
            //cerr << "Exploring: " << exploringScore << "(c=" << comB << " cl=" << clB << "), ";
            return exploringScore;
        }

        void InitZones(const LevelMap& levelMap, const vector<int>& mySnakeIds, map<int, Snake>& allSnakes) {

            batteries.clear();
            initZonesBoundary(levelMap.levelMap.at(0).length());
            for (int i=0; i < numZones; ++i) {
                setBattariesInZone(i, levelMap);
                calculateCenter(i);
            }
            setZonesForSnakes(mySnakeIds, allSnakes);
            updateCenterOfMass(levelMap);
        }

    private:
        
        void setBattariesInZone(const int zone, const LevelMap& levelMap) {

            int zoneStart = zoneBoundaries.at(zone).first,
                zoneEnd = zoneBoundaries.at(zone).second;
            int totalBatteriesInZone = 0;
            for (auto battery : levelMap.getPowerElements()) {
                if (battery.x >= zoneStart && battery.x < zoneEnd ) {
                    batteries[zone].push_back(battery);
                    ++totalBatteriesInZone;
                }
            }
            if (!totalBatteriesInZone) {
                --numZones;
                int height = levelMap.levelMap.size();
                Point draft((zoneStart + zoneEnd)/2, height/2);
                batteries[zone].push_back(draft);
            }
        }

        // Init zone boundaries based on map width
        void initZonesBoundary(int mapWidth) {
            zoneBoundaries.clear();
            int zoneWidth = mapWidth / numZones;
            
            int startZone = 0;
            for (int i = 0; i < numZones; i++) {
                pair<int,int> bounds = {startZone, startZone + zoneWidth};
                zoneBoundaries.push_back(bounds);
                startZone += zoneWidth;
            }
        }

        void calculateCenter(const int zone) {
            auto batteriesVector = batteries.at(zone);

            int totalX = 0, totalY = 0, totalAmount = 0;
            for (int i = 0; i < batteriesVector.size(); ++i) {
                totalX += batteriesVector[i].x;
                totalY += batteriesVector[i].y;
                ++totalAmount;
            }
            
            zoneCenter[zone] = Point(totalX/totalAmount, totalY/totalAmount);
            // markPoint(zoneCenter.at(zone));
        }

        void setZonesForSnakes(const vector<int>& mySnakeIds, map<int, Snake> allSnakes) {
            snakesInZone.clear();
            snakeAssignment.clear();

            // Init snake count per zone
            for (int i = 0; i < (int)zoneBoundaries.size(); ++i) {
                snakesInZone[i] = 0;
            }

            // Greedy assignment: pick best (snake, zone) pair each iteration
            set<int> unassigned(mySnakeIds.begin(), mySnakeIds.end());
            while (!unassigned.empty()) {
                int bestSnakeId = -1;
                int bestZone = -1;
                double bestScore = -1;

                for (int snakeId : unassigned) {
                    auto it = allSnakes.find(snakeId);
                    if (it == allSnakes.end()) continue;
                    Point head = it->second.getHead();

                    for (int z = 0; z < (int)zoneBoundaries.size(); ++z) {
                        int battCount = batteries[z].size();
                        if (battCount == 0) continue;

                        // Value = batteries / (already assigned + 1), penalized by distance
                        double value = (double)battCount / (snakesInZone[z] + 1);
                        int dist = head.distanceTo(zoneCenter[z]);
                        double score = value / (dist + 1);

                        if (score > bestScore) {
                            bestScore = score;
                            bestSnakeId = snakeId;
                            bestZone = z;
                        }
                    }
                }

                if (bestSnakeId == -1) break;

                snakeAssignment[bestSnakeId] = bestZone;
                snakesInZone[bestZone]++;
                unassigned.erase(bestSnakeId);
            }

            // Fallback for unassigned (all zones empty) — zone 0
            for (int snakeId : unassigned) {
                snakeAssignment[snakeId] = 0;
                snakesInZone[0]++;
            }
        }

        vector<pair<int,int>> zoneBoundaries;  // zone X boundaries
        map<int, vector<Point>> batteries;
        map<int,Point> zoneCenter;
        int numZones;
        map<int,int> snakesInZone;
        map<int,int> snakeAssignment;  // snakeId -> assigned zone
        Point batteryCom;
        const vector<BatteryCluster>* clustersPtr = nullptr;

        void updateCenterOfMass(const LevelMap& levelMap) {
            auto allBats = levelMap.getPowerElements();
            if (!allBats.empty()) {
                int tx = 0, ty = 0;
                for (const auto& b : allBats) { tx += b.x; ty += b.y; }
                batteryCom = Point(tx / (int)allBats.size(), ty / (int)allBats.size());
            }
        }

        int nearestClusterBonus(const Snake& snake, int direction) {
            if (!clustersPtr || clustersPtr->empty()) return 0;
            Point head = snake.getHead();
            int bestDist = 9999;
            Point bestCenter;
            for (const auto& c : *clustersPtr) {
                if (c.batteries.empty()) continue;
                int d = head.distanceTo(c.center);
                if (d < bestDist) { bestDist = d; bestCenter = c.center; }
            }
            if (bestDist == 9999) return 0;
            int oldDist = head.distanceTo(bestCenter);
            int newDist = getCustomPoint(head, direction).distanceTo(bestCenter);
            if (newDist < oldDist) return NEAREST_CLUSTER_BONUS;
            if (newDist > oldDist) return -NEAREST_CLUSTER_BONUS;
            return 0;
        }

        int centerOfMassBonus(const Snake& snake, int direction) {
            int oldDist = snake.getHead().distanceTo(batteryCom);
            int newDist = getCustomPoint(snake.getHead(), direction).distanceTo(batteryCom);
            if (newDist < oldDist) return COM_BONUS;
            if (newDist > oldDist) return -COM_BONUS;
            return 0;
        }
};

class AntiLoopScorer : public IScorer {
    // Working normal
    private:
        map<int, pair<Point, Direction>> lastState;  // snakeId -> (position, direction)
        map<int, int> loopCount;  // consecutive loop count


    public:

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {

            int totalBonus = 0;
            {
                int id = snake.getId();
                Point currentHead = snake.getHead();
                if (lastState.count(id)) {
                    auto [lastPos, lastDir] = lastState[id];
                    if (lastPos == currentHead && lastDir == snake.getMovingDirection()) {
                        if (direction == lastDir) {
                            int penalty = LOOP_PENALTY * loopCount[id];
                            if (penalty < MAX_LOOP_PENALTY) penalty = MAX_LOOP_PENALTY;
                            totalBonus += penalty;
                        }
                    }
                }
            }
            {
                // Skip loop-on-place penalty if move eats a battery
                Point newHead = getCustomPoint(snake.getHead(), direction);
                if (!levelMap.isBattery(newHead)) {
                    totalBonus += isLoopOnPlace(snake, levelMap, allSnakes, direction) ? LOOP_ON_PLACE_PENALTY : 0;
                }
            }

            // Long snakes: reduce loop penalty (they have established routes)
            if ((int)snake.getBody().size() >= 6) totalBonus /= 3;

            //cerr << "Loop: " << totalBonus << ", ";
            return totalBonus;
        }

        void recordPosition(int id, Point headPos, const Direction& chosenDir) {

            if (headPos != lastState[id].first || chosenDir != lastState[id].second)
                loopCount[id] = 0;
            lastState[id] = {headPos, chosenDir};
        }

        void recordDirection(int id, const Direction& chosenDir, const Point& headPos) {

            if (headPos == lastState[id].first && chosenDir == lastState[id].second) {
                loopCount[id]++;
            } else {
                loopCount[id] = 0;
            }
        }

};

class HuntScorer : public IScorer {
    public:
        // Cache enemy BFS distances: for each battery, min BFS dist from any enemy
        map<Point, int> enemyBfsDist;

        void updateEnemyBfs(const LevelMap& levelMap, const map<int, Snake>& allSnakes) {
            enemyBfsDist.clear();
            // TODO: replace with DFS or Manhattan-based enemy distance
            // for (const auto& [id, snake] : allSnakes) {
            //     if (snake.isMySnake() || !snake.isAlive()) continue;
            //     auto batteries = findAllBatteriesFreeSteps(snake, levelMap, MAX_BFS_STEPS, allSnakes);
            //     for (const auto& b : batteries) {
            //         auto it = enemyBfsDist.find(b.pos);
            //         if (it == enemyBfsDist.end() || b.dist < it->second) {
            //             enemyBfsDist[b.pos] = b.dist;
            //         }
            //     }
            // }
        }

        int getEnemyDist(const Point& bat) const {
            auto it = enemyBfsDist.find(bat);
            return it != enemyBfsDist.end() ? it->second : 9999;
        }

        void setMyBatteries(const vector<BatteryInfo>& batteries) { myBatteries = batteries; }

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {

            int totalBonus = 0;

            // Filter batteries reachable in this direction
            for (const auto& b : myBatteries) {
                if (b.firstDir != direction) continue;
                if (b.dist > ENEMY_DISTANCE) continue;

                int enemyDist = getEnemyDist(b.pos);
                if (enemyDist > ENEMY_DISTANCE) continue;
                //cerr << "[" << b.pos << " myD=" << b.dist << " enD=" << enemyDist << "] ";

                // We're closer or equal — intercept bonus
                if (b.dist <= enemyDist) {
                    int interceptBonus = PREVENTING_ENEMY_BONUS / (b.dist + 1);
                    totalBonus += interceptBonus;
                }
            }

            //cerr << "Hunt:" << totalBonus << ", ";
            return totalBonus;
        }

    private:
        vector<BatteryInfo> myBatteries;
};

class BodyBlockScorer : public IScorer {
    public:
        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            //cerr << "Block:0, ";
            return 0;
        }
};

// Old clustering: by proximity to cluster center (no transitive chaining)
vector<BatteryCluster> clusterBatteriesOld(const vector<Point>& batteries, int threshold) {
    vector<BatteryCluster> clusters;
    for (const auto& bat : batteries) {
        int bestCluster = -1;
        int bestDist = 9999;
        for (int i = 0; i < (int)clusters.size(); i++) {
            int d = bat.distanceTo(clusters[i].center);
            if (d <= threshold && d < bestDist) {
                bestDist = d;
                bestCluster = i;
            }
        }
        if (bestCluster >= 0) {
            clusters[bestCluster].batteries.push_back(bat);
            clusters[bestCluster].recalcCenter();
        } else {
            clusters.push_back({{bat}, bat});
        }
    }
    // Merge clusters whose centers are close
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < (int)clusters.size() && !merged; i++) {
            for (int j = i + 1; j < (int)clusters.size() && !merged; j++) {
                if (clusters[i].center.distanceTo(clusters[j].center) <= threshold) {
                    for (const auto& bp : clusters[j].batteries)
                        clusters[i].batteries.push_back(bp);
                    clusters[i].recalcCenter();
                    clusters.erase(clusters.begin() + j);
                    merged = true;
                }
            }
        }
    }
    return clusters;
}

// Cluster batteries with max size limit
vector<BatteryCluster> clusterBatteries(const vector<Point>& batteries, int threshold) {
    int maxClusterSize = 1 + (int)batteries.size() / CLUSTER_DISTANCE;
    vector<Point> sorted = batteries;
    sort(sorted.begin(), sorted.end(), [](const Point& a, const Point& b) {
        return (a.x + a.y) != (b.x + b.y) ? (a.x + a.y) < (b.x + b.y) : a.x < b.x;
    });
    vector<BatteryCluster> clusters;
    for (const auto& bat : sorted) {
        int bestCluster = -1;
        int bestDist = 9999;
        for (int i = 0; i < (int)clusters.size(); i++) {
            if ((int)clusters[i].batteries.size() >= maxClusterSize) continue;
            int d = bat.distanceTo(clusters[i].center);
            if (d <= threshold && d < bestDist) {
                bestDist = d;
                bestCluster = i;
            }
        }
        if (bestCluster >= 0) {
            clusters[bestCluster].batteries.push_back(bat);
            clusters[bestCluster].recalcCenter();
        } else {
            clusters.push_back({{bat}, bat});
        }
    }
    // Merge clusters whose centers are close (respecting max size)
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < (int)clusters.size() && !merged; i++) {
            for (int j = i + 1; j < (int)clusters.size() && !merged; j++) {
                if (clusters[i].center.distanceTo(clusters[j].center) <= threshold
                    && (int)(clusters[i].batteries.size() + clusters[j].batteries.size()) <= maxClusterSize) {
                    for (const auto& bp : clusters[j].batteries)
                        clusters[i].batteries.push_back(bp);
                    clusters[i].recalcCenter();
                    clusters.erase(clusters.begin() + j);
                    merged = true;
                }
            }
        }
    }
    return clusters;
}

class ClusterScorer : public IScorer {
    struct Assignment {
        int snakeId = -1;
        int enemyId = -1;
        int lockTurnsLeft = 0;
        Point target;
        Point enemyHead;
        bool active = false;
        int clusterIdx = -1;
    };

    Assignment slots[MAX_CHOSEN_SLOTS];
    int turnCount = 0;
    string lastLog;
    vector<BatteryCluster> clusters;
    const map<Point, int>* enemyBfsDist = nullptr;

    // Find closest snake to com, excluding ids in `exclude`
    int findClosestSnake(const Point& com, const vector<int>& mySnakeIds,
                         const map<int, Snake>& allSnakes, const set<int>& exclude) {
        int bestDist = 9999, bestId = -1;
        for (int id : mySnakeIds) {
            if (exclude.count(id)) continue;
            auto it = allSnakes.find(id);
            if (it == allSnakes.end()) continue;
            if ((int)it->second.getBody().size() <= 4) continue;  // too short for chosen role
            int d = it->second.getHead().distanceTo(com);
            if (d < bestDist) { bestDist = d; bestId = id; }
        }
        return bestId;
    }

    // Find closest enemy to com, excluding ids in `exclude`
    Point findClosestEnemyHead(const Point& com, const map<int, Snake>& allSnakes,
                               const set<int>& exclude) {
        int bestDist = 9999;
        Point bestHead;
        for (const auto& [id, s] : allSnakes) {
            if (s.isMySnake() || !s.isAlive()) continue;
            if (exclude.count(id)) continue;
            int d = s.getHead().distanceTo(com);
            if (d < bestDist) { bestDist = d; bestHead = s.getHead(); }
        }
        return bestHead;
    }

    // Calculate CoM of battery list
    Point calcCoM(const vector<Point>& batteries) {
        int tx = 0, ty = 0;
        for (const auto& b : batteries) { tx += b.x; ty += b.y; }
        return Point(tx / (int)batteries.size(), ty / (int)batteries.size());
    }

    // Pick target: find best cluster → finalMid → closest battery
    Point pickTarget(const Point& snakeHead, const Point& enemyHead, const Point& mid,
                     const vector<BatteryCluster>& clusters, const vector<Point>& batteries,
                     const map<int, Snake>& allSnakes, const set<int>& usedClusters,
                     int& outClusterIdx) {
        // Best cluster closest to mid, skip taken clusters
        int bestDist = 9999;
        outClusterIdx = -1;
        for (int i = 0; i < (int)clusters.size(); i++) {
            if (usedClusters.count(i)) continue;
            int d = clusters[i].center.distanceTo(mid);
            if (d < bestDist) { bestDist = d; outClusterIdx = i; }
        }
        if (outClusterIdx < 0) return Point(-1, -1);

        Point clusterCenter = clusters[outClusterIdx].center;
        Point clusterMid = snakeHead.middlePoint(clusterCenter);
        Point finalMid = clusterMid.middlePoint(mid);

        // Closest battery to finalMid, skip if enemy grabs it
        Point bestBat = clusterCenter;
        int bestBatDist = 9999;
        for (const auto& b : batteries) {
            auto [enDist, enId] = findClosestEnemy(b, allSnakes);
            int myDist = snakeHead.distanceTo(b);
            if (enDist <= 1 && myDist > enDist) continue;
            int d = b.distanceTo(finalMid);
            if (d < bestBatDist) { bestBatDist = d; bestBat = b; }
        }
        return bestBat;
    }

    // Assign a slot: pick snake, enemy, cluster, target
    void assignSlot(int slotIdx, int lockDuration, const vector<Point>& batteries,
                    const vector<int>& mySnakeIds, const map<int, Snake>& allSnakes,
                    set<int>& usedSnakes, set<int>& usedEnemies) {
        auto& slot = slots[slotIdx];

        // Check if locked snake is alive
        if (slot.lockTurnsLeft > 0 && slot.snakeId >= 0) {
            auto it = allSnakes.find(slot.snakeId);
            if (it == allSnakes.end() || !it->second.isAlive())
                slot.lockTurnsLeft = 0;
        }

        // Force recalculation if snake already taken by higher-priority slot
        if (slot.lockTurnsLeft > 0 && usedSnakes.count(slot.snakeId)) {
            slot.lockTurnsLeft = 0;
        }

        // Collect batteries from clusters not taken by other slots
        set<int> usedClusters;
        for (int s = 0; s < MAX_CHOSEN_SLOTS; s++) {
            if (s == slotIdx) continue;
            if (slots[s].clusterIdx >= 0 && slots[s].lockTurnsLeft > 0)
                usedClusters.insert(slots[s].clusterIdx);
        }
        vector<Point> availBats;
        for (int ci = 0; ci < (int)clusters.size(); ci++) {
            if (usedClusters.count(ci)) continue;
            for (const auto& b : clusters[ci].batteries) availBats.push_back(b);
        }
        if (availBats.empty()) { slot.active = false; return; }

        // Recalculate if lock expired
        if (slot.lockTurnsLeft <= 0) {
            // mid = midpoint(CoM, enemy) — independent of snake
            Point com = calcCoM(availBats);
            Point enemyHead = findClosestEnemyHead(com, allSnakes, usedEnemies);
            Point mid = com.middlePoint(enemyHead);

            // Choose snake closest to mid (best interceptor)
            int snakeId = findClosestSnake(mid, mySnakeIds, allSnakes, usedSnakes);
            if (snakeId < 0) { slot.active = false; return; }

            slot.snakeId = snakeId;
            slot.lockTurnsLeft = lockDuration;
        } else {
            slot.lockTurnsLeft--;
        }

        if (slot.snakeId < 0) { slot.active = false; return; }
        usedSnakes.insert(slot.snakeId);
        if (availBats.empty()) { slot.active = false; return; }

        Point snakeHead = allSnakes.at(slot.snakeId).getHead();
        Point availCoM = calcCoM(availBats);
        Point enemyHead = findClosestEnemyHead(availCoM, allSnakes, usedEnemies);

        // Find enemy id for exclusion
        for (const auto& [id, s] : allSnakes) {
            if (!s.isMySnake() && s.isAlive() && s.getHead() == enemyHead) {
                usedEnemies.insert(id);
                slot.enemyId = id;
            }
        }
        slot.enemyHead = enemyHead;

        Point mid = snakeHead.middlePoint(enemyHead);
        int clusterIdx = -1;
        slot.target = pickTarget(snakeHead, enemyHead, mid, clusters, availBats, allSnakes, usedClusters, clusterIdx);
        slot.active = true;
        slot.clusterIdx = clusterIdx;
    }

    void initClusters(const vector<Point>& batteries, int maxClusters) {
        int clusterDist = CLUSTER_DISTANCE;
        clusters = clusterBatteries(batteries, clusterDist);
        while ((int)clusters.size() > maxClusters && clusterDist < 50) {
            clusterDist++;
            clusters = clusterBatteries(batteries, clusterDist);
        }
    }

    void removeEatenBatteries(const set<Point>& liveBats) {
        for (auto& c : clusters) {
            c.batteries.erase(
                remove_if(c.batteries.begin(), c.batteries.end(),
                    [&](const Point& b) { return !liveBats.count(b); }),
                c.batteries.end());
            if (!c.batteries.empty()) c.recalcCenter();
        }

        // Unlock slots whose cluster is empty
        for (int i = 0; i < MAX_CHOSEN_SLOTS; i++) {
            if (slots[i].clusterIdx >= 0 &&
                slots[i].clusterIdx < (int)clusters.size() &&
                clusters[slots[i].clusterIdx].batteries.empty()) {
                slots[i].lockTurnsLeft = 0;
            }
        }

        // Remove empty clusters, update slot indices
        for (int i = (int)clusters.size() - 1; i >= 0; i--) {
            if (clusters[i].batteries.empty()) {
                clusters.erase(clusters.begin() + i);
                for (int s = 0; s < MAX_CHOSEN_SLOTS; s++) {
                    if (slots[s].clusterIdx == i) slots[s].clusterIdx = -1;
                    else if (slots[s].clusterIdx > i) slots[s].clusterIdx--;
                }
            }
        }
    }

    public:
        void update(const LevelMap& levelMap, const vector<int>& mySnakeIds,
                    const map<int, Snake>& allSnakes) {
            for (int i = 0; i < MAX_CHOSEN_SLOTS; i++) slots[i].active = false;
            lastLog = "";
            turnCount++;
            auto batteries = levelMap.getPowerElements();
            if (batteries.empty()) return;

            if (clusters.empty()) {
                initClusters(batteries, (int)mySnakeIds.size() + 1);
                cerr << "Clusters: " << clusters.size() << endl;
                for (int i = 0; i < (int)clusters.size(); i++) {
                    cerr << "C" << i << ": " << clusters[i].batteries.size()
                         << " bats, center=" << clusters[i].center << " | ";
                    for (const auto& b : clusters[i].batteries) cerr << b << " ";
                    cerr << endl;
                }
            } else {
                set<Point> liveBats(batteries.begin(), batteries.end());
                removeEatenBatteries(liveBats);
            }

            if (turnCount < CHOSEN_START_TURN) return;

            set<int> usedSnakes, usedEnemies;

            // Assign slots: #0 = full lock, rest = half lock
            int numSlots = min((int)mySnakeIds.size(), min((int)clusters.size(), MAX_CHOSEN_SLOTS));
            for (int i = 0; i < numSlots; i++) {
                int lockDur = (i == 0) ? CHOSEN_LOCK_TURNS : CHOSEN_LOCK_TURNS / 2;
                assignSlot(i, lockDur, batteries, mySnakeIds, allSnakes,
                           usedSnakes, usedEnemies);
            }

            // Marks for primary only, log for all
            ostringstream oss;
            for (int i = 0; i < numSlots; i++) {
                if (!slots[i].active) continue;
                int enBfsDist = -1;
                // Enemy BFS dist to target
                if (enemyBfsDist) {
                    auto eit = enemyBfsDist->find(slots[i].target);
                    if (eit != enemyBfsDist->end()) enBfsDist = eit->second;
                }
                oss << endl << "[#" << i << " S" << slots[i].snakeId << "(lock=" << slots[i].lockTurnsLeft
                    << ") → " << slots[i].target << " enBfs=" << enBfsDist
                    << " vs S" << slots[i].enemyId << "=" << slots[i].enemyHead << "]";
            }
            lastLog = oss.str();
        }

        void setEnemyBfs(const map<Point, int>* bfs) { enemyBfsDist = bfs; }
        string getLog() const { return lastLog; }
        const vector<BatteryCluster>& getClusters() const { return clusters; }

        void outputMarks(const map<int, Snake>& allSnakes) {
            if (!slots[0].active) return;
            if (allSnakes.find(slots[0].snakeId) == allSnakes.end()) return;
            markPoint(slots[0].target);
            markPoint(allSnakes.at(slots[0].snakeId).getHead());
            markPoint(slots[0].enemyHead);
            markPoint(allSnakes.at(slots[0].snakeId).getHead().middlePoint(slots[0].enemyHead));
        }

        set<int> getChosenIds() const {
            set<int> ids;
            for (int i = 0; i < MAX_CHOSEN_SLOTS; i++) if (slots[i].active) ids.insert(slots[i].snakeId);
            return ids;
        }

        Point getTarget(int snakeId) const {
            for (int i = 0; i < MAX_CHOSEN_SLOTS; i++) {
                if (slots[i].active && slots[i].snakeId == snakeId) return slots[i].target;
            }
            return Point(-1, -1);
        }

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            // Check all slots
            for (int s = 0; s < MAX_CHOSEN_SLOTS; s++) {
                if (!slots[s].active || snake.getId() != slots[s].snakeId) continue;

                Point newHead = getCustomPoint(snake.getHead(), direction);
                int oldDist = snake.getHead().distanceTo(slots[s].target);
                int newDist = newHead.distanceTo(slots[s].target);

                int score = 0;
                if (newDist < oldDist) {
                    score = CHOSEN_BONUS / (newDist + 1);
                } else if (newDist > oldDist) {
                    score = -(CHOSEN_BONUS / (oldDist + 1));
                } else {
                    // Manhattan equal — Euclidean breaks tie
                    int dx1 = newHead.x - slots[s].target.x, dy1 = newHead.y - slots[s].target.y;
                    int dx0 = snake.getHead().x - slots[s].target.x, dy0 = snake.getHead().y - slots[s].target.y;
                    score = (dx0*dx0 + dy0*dy0) - (dx1*dx1 + dy1*dy1);
                }

                //cerr << "Chosen: " << score << ", ";
                return score;
            }
            return 0;
        }
};

struct SnakeDecision {
    int snakeId;
    Direction dir;
    Point targetCell;
    Point head;
    int scores[4];
    bool needsRecalc = false;
    SnakeDecision(int id, Direction d, Point h, const int* sc)
        : snakeId(id), dir(d), targetCell(h + directories[d]), head(h) {
        for (int i = 0; i < 4; i++) scores[i] = sc[i];
    }
};

class DecisionMaker {
    vector<shared_ptr<IScorer>> scorers;
    shared_ptr<AntiLoopScorer> antiLoop;
    shared_ptr<ExplorationScorer> exploration;
    shared_ptr<BatteryScorer> batteryScorer;
    shared_ptr<HuntScorer> huntScorer;
    shared_ptr<ClusterScorer> clusterScorer;
    shared_ptr<BodyBlockScorer> bodyBlockScorer;
    int lastScores[4] = {};
    set<Point> blockedCells;
    map<int, vector<DfsResult>> snakeDfsPaths; // snakeId → reachable batteries via DFS

    public:
        const int* getLastScores() const { return lastScores; }
        void setBlockedCells(const set<Point>& cells) { blockedCells = cells; }
        void clearBlockedCells() { blockedCells.clear(); }

        DecisionMaker() {
            srand(time(nullptr));

            scorers.push_back(make_shared<SafetyScorer>());
            batteryScorer = make_shared<BatteryScorer>();
            scorers.push_back(batteryScorer);
            huntScorer = make_shared<HuntScorer>();
            scorers.push_back(huntScorer);

            exploration = make_shared<ExplorationScorer>();
            scorers.push_back(exploration);

            antiLoop = make_shared<AntiLoopScorer>();
            scorers.push_back(antiLoop);

            clusterScorer = make_shared<ClusterScorer>();
            scorers.push_back(clusterScorer);

            bodyBlockScorer = make_shared<BodyBlockScorer>();
            scorers.push_back(bodyBlockScorer);


        }
        

        Direction decide(const Snake& snake, const LevelMap& levelMap, 
                        const map<int, Snake>& allSnakes) {
            int bestScore = -1000000;


            Direction bestDir = snake.getMovingDirection();  // default: keep going
            antiLoop->recordPosition(snake.getId(), snake.getHead(), snake.getMovingDirection());
            // Feed DFS results to BatteryScorer
            const auto& dfsPaths = getDfsPaths(snake.getId());
            batteryScorer->setCache(snake.getId(), dfsPaths);
            vector<int> decisions(4);
            // Try all 4 directions
            for (int dir = 0; dir < 4; dir++) {
                int totalScore = 0;
                cerr << snake.getHead() + directories[dir] << " ";
                
                // Check if direction is blocked by conflict resolution
                Point dirTarget = snake.getHead() + directories[dir];
                if (blockedCells.count(dirTarget)) {
                    decisions[dir] = DEATH_FINE;
                    //cerr << "[BLOCKED] total: " << DEATH_FINE << endl;
                    continue;
                }

                int safetyTotalScore    = scorers[0]->evaluate(snake, levelMap, allSnakes, dir);
                int batteryTotalScore   = scorers[1]->evaluate(snake, levelMap, allSnakes, dir);
                int huntTotalScore      = scorers[2]->evaluate(snake, levelMap, allSnakes, dir);
                int exploreTotalScore   = scorers[3]->evaluate(snake, levelMap, allSnakes, dir);
                int antiloopTotalScore  = scorers[4]->evaluate(snake, levelMap, allSnakes, dir);
                int chosenTotalScore    = scorers[5]->evaluate(snake, levelMap, allSnakes, dir);
                int blockTotalScore     = scorers[6]->evaluate(snake, levelMap, allSnakes, dir);

                totalScore = safetyTotalScore + antiloopTotalScore + huntTotalScore + blockTotalScore;
                {
                    // Gradual weight reduction for exploration/battery with heat
                    int heat = snake.getHeat();
                    int divisor = max(1, heat);  // heat 0→/1, 1→/1, 2→/2, 3→/3...
                    totalScore += exploreTotalScore / divisor;
                    totalScore += batteryTotalScore / divisor;
                }
                totalScore += chosenTotalScore;
               
                cerr << "total: " << totalScore << endl;
                decisions[dir] = totalScore;
            }

            // Save scores for conflict resolution
            for (int i = 0; i < 4; i++) lastScores[i] = decisions[i];

            // Tiebreaker: equal scores — Euclidean to cluster target, or Manhattan to nearest battery
            Point head = snake.getHead();
            Point clusterTarget = clusterScorer->getTarget(snake.getId());
            bool hasClusterTarget = !(clusterTarget == Point(-1, -1));

            Point targetBat = clusterTarget;
            if (!hasClusterTarget) {
                vector<Point> bats = levelMap.getPowerElements();
                int closestBatId = head.getClosestBattery(bats);
                targetBat = bats.empty() ? head : bats[closestBatId];
            }

            int bestTiebreak = 999999;
            for (int dir = 0; dir < 4; dir++) {
                Point newHead = head + directories[dir];
                int tiebreak;
                if (hasClusterTarget) {
                    // Euclidean squared to cluster target (lower = better)
                    int dx = newHead.x - targetBat.x, dy = newHead.y - targetBat.y;
                    tiebreak = dx*dx + dy*dy;
                } else {
                    tiebreak = newHead.distanceTo(targetBat);
                }

                if (decisions[dir] > bestScore) {
                    bestScore = decisions[dir];
                    bestTiebreak = tiebreak;
                    bestDir = static_cast<Direction>(dir);
                } else if (decisions[dir] == bestScore) {
                    if (tiebreak < bestTiebreak) {
                        bestTiebreak = tiebreak;
                        bestDir = static_cast<Direction>(dir);
                    }
                }
            }
            

            antiLoop->recordDirection(snake.getId(), bestDir, snake.getHead());

            // Claim battery so other snakes don't chase the same one
            Point claimed = batteryScorer->getBestBatteryForDir(snake, allSnakes, bestDir);
            if (!(claimed == Point(-1, -1))) {
                batteryScorer->claimBattery(claimed);
                //cerr << "Target battery: " << claimed << " " << endl;
            }

            //cerr << getDirectionName(bestDir)[0] << endl  << endl;

            return bestDir;
        }

        void resolveConflicts(vector<SnakeDecision>& decisions, const LevelMap& levelMap,
                              map<int, Snake>& allSnakes) {
            for (int i = 0; i < (int)decisions.size(); i++) {
                for (int j = i + 1; j < (int)decisions.size(); j++) {
                    if (!(decisions[i].targetCell == decisions[j].targetCell)) continue;
                    cerr << "[CONFLICT S" << decisions[i].snakeId << " & S" << decisions[j].snakeId
                         << " at " << decisions[i].targetCell << "]" << endl;
                    bool resolved = false;
                    for (int victim : {i, j}) {
                        if (resolved) break;
                        auto& dec = decisions[victim];
                        auto it = allSnakes.find(dec.snakeId);
                        if (it == allSnakes.end()) continue;
                        setBlockedCells({dec.targetCell});
                        Direction newDir = decide(it->second, levelMap, allSnakes);
                        clearBlockedCells();
                        if (getLastScores()[newDir] > DEATH_FINE) {
                            dec.dir = newDir;
                            dec.targetCell = dec.head + directories[newDir];
                            const int* sc = getLastScores();
                            for (int k = 0; k < 4; k++) dec.scores[k] = sc[k];
                            cerr << "[REROUTED S" << dec.snakeId << " → " << dec.targetCell << "]" << endl;
                            resolved = true;
                        }
                    }
                    if (!resolved) cerr << "[CONFLICT UNRESOLVED]" << endl;
                }
            }
        }

        void update(LevelMap& levelMap, const vector<int>& mySnakeIds, map<int, Snake>& allSnakes) {
            exploration->InitZones(levelMap, mySnakeIds, allSnakes);
            batteryScorer->clearCache();
            huntScorer->updateEnemyBfs(levelMap, allSnakes);
            batteryScorer->setEnemyBfs(&huntScorer->enemyBfsDist);
            clusterScorer->setEnemyBfs(&huntScorer->enemyBfsDist);
            clusterScorer->update(levelMap, mySnakeIds, allSnakes);
            exploration->setClusters(&clusterScorer->getClusters());

            // Compute DFS paths for all my snakes
            snakeDfsPaths.clear();
            for (int id : mySnakeIds) {
                auto it = allSnakes.find(id);
                if (it == allSnakes.end() || !it->second.isAlive()) continue;
                snakeDfsPaths[id] = findBatteriesDfs(it->second, levelMap, allSnakes);
                cerr << "DFS S" << id << ": " << snakeDfsPaths[id].size() << " bats" << endl;
            }
        }

        const vector<DfsResult>& getDfsPaths(int snakeId) const {
            static const vector<DfsResult> empty;
            auto it = snakeDfsPaths.find(snakeId);
            return it != snakeDfsPaths.end() ? it->second : empty;
        }

        string getChosenLog() const { return clusterScorer->getLog(); }
        void outputMarks(const map<int, Snake>& allSnakes) { clusterScorer->outputMarks(allSnakes); }
};


class GameState {
    public:
        map<int, Snake> allSnakes;
        LevelMap levelMap;    
        vector<int> mySnakeIds;
        
        GameState(int width, int height) : levelMap(width, height) {

        }
        
        // Init snakes at game start
        void initSnakes(int snakebotsPerPlayer) {
            // My snakes
            for (int i = 0; i < snakebotsPerPlayer; i++) {
                int id;
                cin >> id; cin.ignore();
                allSnakes.emplace(id, Snake(id, true));
                mySnakeIds.push_back(id);
            }
            
            // Opponent snakes
            for (int i = 0; i < snakebotsPerPlayer; i++) {
                int id;
                cin >> id; cin.ignore();
                allSnakes.emplace(id, Snake(id, false));
            }
        }
        
        // Update snake state each turn
        void updateSnakes(int snakebotCount) {
            // Temp set of alive IDs this turn
            set<int> aliveIds;
            
            // Read all alive snakes
            for (int i = 0; i < snakebotCount; i++) {
                int id;
                string body;
                cin >> id >> body; cin.ignore();
                aliveIds.insert(id);
                
                auto it = allSnakes.find(id);
                if (it != allSnakes.end()) {
                    it->second.setBody(body);
                    it->second.updateHeatMap();
                }
            }
            
            // Remove dead snakes
            for (auto it = allSnakes.begin(); it != allSnakes.end(); ) {
                if (aliveIds.find(it->first) == aliveIds.end()) {
                    // Dead — remove
                    if (it->second.isMySnake()) {
                        // Remove from mySnakeIds
                        mySnakeIds.erase(
                            remove(mySnakeIds.begin(), mySnakeIds.end(), it->first),
                            mySnakeIds.end()
                        );
                    }
                    it = allSnakes.erase(it);
                } else {
                    ++it;
                }
            }

        }
        
        // Get all snakes (for collision checks)
        const map<int, Snake>& getAllSnakes() const { return allSnakes; }
        
        // Get my snakes only (for decision making)
        vector<reference_wrapper<Snake>> getMySnakes() {
            vector<reference_wrapper<Snake>> result;
            for (int id : mySnakeIds) {
                auto it = allSnakes.find(id);
                if (it != allSnakes.end() && it->second.isAlive()) {
                    result.push_back(ref(it->second));
                }
            }
            return result;
        }
};


int main() {
    int my_id;
    cin >> my_id; cin.ignore();
    int width, height;
    cin >> width >> height; cin.ignore();
    
    GameState state(width, height);
    
    // Read initial map
    vector<string> rowMap;
    for (int i = 0; i < height; i++) {
        string row;
        getline(cin, row);
        rowMap.push_back(row);
    }
    state.levelMap.createLevelMap(rowMap);
    
    int snakebots_per_player;
    cin >> snakebots_per_player; cin.ignore();
    
    // Init snakes
    state.initSnakes(snakebots_per_player);
    state.levelMap.debug();
    DecisionMaker decisionMaker;
    int v=0;
    while (1) {
        int power_source_count;
        cin >> power_source_count; cin.ignore();
        
        state.levelMap.clearPowerElements();
        for (int i = 0; i < power_source_count; i++) {
            int x, y;
            cin >> x >> y; cin.ignore();
            state.levelMap.addPowerElement(x, y);
        }
        
        int snakebot_count;
        cin >> snakebot_count; cin.ignore();
        
        // Update all snakes
        state.updateSnakes(snakebot_count);
        
        // Make decisions for my snakes
        string output;

        // Turn log: batteries and opponent heads
        cerr << "Step " << v++ << endl;
        cerr << "B: ";
        for (const auto& b : state.levelMap.getPowerElements()) cerr << b << " ";
        cerr << endl << "E: ";
        for (const auto& [id, s] : state.allSnakes) {
            if (!s.isMySnake() && s.isAlive()) cerr << "S" << id << "=" << s.getHead() << " ";
        }
        cerr << endl;

        decisionMaker.update(state.levelMap, state.mySnakeIds, state.allSnakes);

        // Phase 1: collect all decisions
        vector<SnakeDecision> decisions;
        for (auto& snakeRef : state.getMySnakes()) {
            Snake& snake = snakeRef.get();
            snake.debug();
            Direction dir = decisionMaker.decide(snake, state.levelMap, state.allSnakes);
            decisions.emplace_back(snake.getId(), dir, snake.getHead(), decisionMaker.getLastScores());
        }

        // Phase 2: resolve conflicts
        decisionMaker.resolveConflicts(decisions, state.levelMap, state.allSnakes);

        // Phase 3: apply decisions and output
        for (auto& dec : decisions) {
            auto it = state.allSnakes.find(dec.snakeId);
            if (it == state.allSnakes.end()) continue;
            Snake& snake = it->second;
            snake.setMovingDirection(dec.dir);
            output += to_string(dec.snakeId) + " " + getDirectionName(dec.dir) + ";";

            Point newHead = snake.getHead() + directories[dec.dir];
            bool eatsBattery = state.levelMap.isBattery(newHead);
            snake.updateHead(newHead, eatsBattery);
            if (eatsBattery) {
                state.levelMap.removePowerElement(newHead);
            }
        }

        decisionMaker.outputMarks(state.allSnakes);
        if (output.empty()) output = "WAIT";
        //cerr << decisionMaker.getChosenLog() << endl;
        cout << output << endl;
    }
}