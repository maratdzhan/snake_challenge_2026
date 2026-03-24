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

const int MAX_DFS_DEPTH = 9; // DFS pathfinding depth limit
const int DEPTH_INCREASING = 0;
const int SNAKE_EFFECTIVE_LENGTH = 15;

int MAX_CLUSTER_COUNT = 10;
const int CLUSTER_DISTANCE = 5; // max distance between batteries in a cluster
const int MAX_CLUSTER_SIZE = 6; // max batteries per cluster
const int MAX_AVAILABLE_DISTANCE = 8; // max distance for intermediate DFS targets

const int BASE_BONUS = 2350; // 2000 - ok, 2500 - max, 2400 - optimal
const int FALL_PENALTY = 1400;    // penalty for any move that causes falling
const int SUPPORT_BONUS = 2500;  // bonus/penalty for support stability (long snakes only)
const int SUPPORT_TURNS_THRESHOLD = 2; // start caring about support when <= this many turns left
const int DEATH_FINE = -99999;
const int ENEMY_HEAD_FINE = -51000;
const int TRAPPED_FINE = -90000;

const int OUT_OF_BOUNDS_PENALTY = -1000;

const int MAX_FLOOD_FILL = 32;
const int MAX_TUNNEL_DEPTH = 6;
const int MAX_AER = 6;
const int AER_ENEMY_DISTANCE = 3;
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

    int getPlatformTop(int x) const {
        if (x < 0 || x >= width) return -1;
        int platformY = -1;
        for (int y = height - 1; y >= 0; y--) {
            if (levelMap[y][x] == PLATFORM_CHAR) { platformY = y; break; }
        }
        if (platformY < 0) return -1;
        while (platformY > 0 && levelMap[platformY - 1][x] == PLATFORM_CHAR) platformY--;
        return platformY;
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
    void setBlocker(bool v) { isBlocker = v; }
    bool getBlocker() const { return isBlocker; }

    void debug() const {
        cerr << "S" << id << " H=" << getHead() << " " << getDirectionName(getMovingDirection())
             << " " << getBody();
        if (isBlocker) cerr << " [BLK]";
        cerr << endl;
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
    bool isBlocker = false;
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
        // Tail segments will move away (own snake: unless eating battery; any snake: last segment)
        if (id == myId && !eatingBattery && idx >= (int)snake.getBody().size() - skipTailCount) continue;
        if (id != myId && idx == (int)snake.getBody().size() - 1) continue;
        return true;
    }
    return false;
}


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
            int additionalEscapeRoutes = 0;
            for (int dir = 0; dir < 4; dir++) {
                Point next = getCustomPoint(current, dir);
                if (!isSolidCollision(next, levelMap, allSnakes, myId, eatingBattery)) {
                    for (int d1 = 0; d1 < 4; ++d1) {
                        Point t = getCustomPoint(next, d1);
                        if (!isSolidCollision(t, levelMap, allSnakes, myId, eatingBattery)) {
                            ++additionalEscapeRoutes;
                        }
                    }
                }
            }
            return min(additionalEscapeRoutes, MAX_AER);
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

// Returns {distance, id, head} of closest enemy to target
struct EnemyInfo { int dist = 9999; int id = -1; Point head = Point(-1,-1); };
EnemyInfo findClosestEnemy(const Point& target, const map<int, Snake>& allSnakes,
                           const set<int>& exclude = {}) {
    EnemyInfo best;
    for (const auto& [id, snake] : allSnakes) {
        if (snake.isMySnake() || !snake.isAlive()) continue;
        if (exclude.count(id)) continue;
        int d = target.distanceTo(snake.getHead());
        if (d < best.dist) {
            best.dist = d;
            best.id = id;
            best.head = snake.getHead();
        }
    }
    return best;
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

// Fast version for DFS: works with body directly, uses occupied set instead of allSnakes
int calcFallDistance(const vector<Point>& body, const LevelMap& levelMap, const set<Point>& occupied) {
    int totalFall = levelMap.height;
    for (const auto& bp : body) {
        int fall = 0;
        for (int y = 1; y < levelMap.height; y++) {
            int belowY = bp.y + y;
            if (belowY >= levelMap.height) { fall = levelMap.height; break; }
            char cell = levelMap.levelMap[belowY][bp.x];
            if (cell == PLATFORM_CHAR) { fall = y - 1; break; }
            Point belowP(bp.x, belowY);
            if (occupied.count(belowP)) { fall = y - 1; break; }
            // Check if it's own body segment
            bool ownBody = false;
            for (const auto& ob : body) {
                if (ob == belowP) { ownBody = true; break; }
            }
            if (ownBody) continue;
            // Empty — keep falling
        }
        totalFall = min(totalFall, fall);
    }
    return totalFall;
}

void applyFall(vector<Point>& body, int fallDist) {
    for (auto& p : body) p.y += fallDist;
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

// DFS: find shortest path from snake to target on given map, with gravity simulation
DfsResult dfsPathToTarget(const Snake& snake, const Point& target,
                          const LevelMap& levelMap, const map<int, Snake>& allSnakes,
                          int maxDepth = MAX_DFS_DEPTH) {

    if (levelMap.isOutsideMap(target)) return DfsResult();
    if ((int)snake.getBody().size() >= SNAKE_EFFECTIVE_LENGTH) maxDepth = max(1, maxDepth - 1);
    int myId = snake.getId();
    map<int, Snake> dfsSnakes(allSnakes);
    dfsSnakes.erase(myId);

    set<Point> occupied;
    for (const auto& [id, s] : dfsSnakes) {
        if (!s.isAlive()) continue;
        const auto& body = s.getBody();
        for (int i = 0; i < (int)body.size() - 1; i++)
            occupied.insert(body[i]);
    }

    const int MARGIN = 3;
    int vw = levelMap.width + 2 * MARGIN;
    int vh = levelMap.height + 2 * MARGIN;
    vector<vector<bool>> visited(vh, vector<bool>(vw, false));

    // Helpers to convert to/from visited coords
    auto vx = [&](int x) { return x + MARGIN; };
    auto vy = [&](int y) { return y + MARGIN; };
    auto validV = [&](const Point& p) {
        int px = vx(p.x), py = vy(p.y);
        return px >= 0 && px < vw && py >= 0 && py < vh;
    };

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
            if (!validV(next) || visited[vy(next.y)][vx(next.x)]) continue;
            if (levelMap.levelMap[next.y][next.x] == PLATFORM_CHAR ||
                occupied.count(next)) continue;
            bool eatingBattery = levelMap.isBattery(next);

            // Battery target reached on contact (before gravity)
            if (next == target && eatingBattery) {
                int fd = firstDir < 0 ? dir : firstDir;
                if (best.pathLen < 0 || (depth + 1) < best.pathLen) {
                    best.pathLen = depth + 1;
                    best.firstDir = fd;
                    best.path = currentPath;
                    best.path.push_back(next);
                }
                continue;
            }

            vector<Point> newBody;
            newBody.push_back(next);
            int keepCount = eatingBattery ? (int)body.size() : (int)body.size() - 1;
            for (int i = 0; i < keepCount; i++) newBody.push_back(body[i]);

            int fallDist = calcFallDistance(newBody, levelMap, occupied);
            vector<Point> settledBody = newBody;
            applyFall(settledBody, fallDist);
            Point settledHead = settledBody[0];

            if (levelMap.isOutsideMap(settledHead)) continue;
            if (settledHead.y >= levelMap.height) continue;

            if (validV(settledHead) && !visited[vy(settledHead.y)][vx(settledHead.x)]) {
                visited[vy(settledHead.y)][vx(settledHead.x)] = true;
                currentPath.push_back(settledHead);
                dfs(settledBody, depth + 1, firstDir < 0 ? dir : firstDir);
                currentPath.pop_back();
                visited[vy(settledHead.y)][vx(settledHead.x)] = false;
            }
        }
    };

    Point startHead = snake.getHead();
    if (validV(startHead)) visited[vy(startHead.y)][vx(startHead.x)] = true;
    dfs(snake.getBody(), 0, -1);
    return best;
}

// Find all reachable batteries for a snake using DFS
// Filters by Manhattan distance, runs DFS for each candidate
vector<DfsResult> findBatteriesDfs(const Snake& snake, const LevelMap& levelMap,
                                    const map<int, Snake>& allSnakes,
                                    int dfsDepth = MAX_DFS_DEPTH) {
    vector<DfsResult> results;
    Point head = snake.getHead();
    int myId = snake.getId();
    auto batteries = levelMap.getPowerElements();

    // Sort batteries by Manhattan distance
    sort(batteries.begin(), batteries.end(), [&](const Point& a, const Point& b) {
        return head.distanceTo(a) < head.distanceTo(b);
    });

    int maxManDist = 5;
    int checked = 0;
    for (const auto& bat : batteries) {
        int manDist = head.distanceTo(bat);
        if (manDist > maxManDist) break; // sorted, rest are farther
        if (checked >= 3) break; // limit DFS calls
        checked++;

        DfsResult res = dfsPathToTarget(snake, bat, levelMap, allSnakes, MAX_AVAILABLE_DISTANCE);
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

        int calcAerBonus(int additionalEscapeRoutes, const Point& head, const Point& target,
                         const map<int, Snake>& allSnakes) {
            int headDist = findClosestEnemy(head, allSnakes).dist;
            int pointDist = findClosestEnemy(target, allSnakes).dist;
            int enemyDist = pointDist;
            if (pointDist > headDist) enemyDist += 1;  // moving away, relax
            if (enemyDist <= AER_ENEMY_DISTANCE) {
                // Near enemy: penalize low escape routes, cap bonus
                if (additionalEscapeRoutes < 3) return (additionalEscapeRoutes - 3) * ROUTE_FINDING_BONUS;
                return 2 * ROUTE_FINDING_BONUS;
            }
            return min(additionalEscapeRoutes, 2) * ROUTE_FINDING_BONUS;
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
                    } else {
                        totalBonus += calcAerBonus(additionalEscape, head, landedHead, allSnakes);
                    }
                }
            } else {
                const auto result = willBeTrappedNextTurn(snake, levelMap, allSnakes, direction, eatingBattery);
                if (result.first) {
                    totalBonus += TRAPPED_FINE;
                } else {
                    totalBonus += calcAerBonus(result.second, head, newHead, allSnakes);
                }
            }
            // Flood fill check: detect dead ends regardless of fall/no-fall
            if (totalBonus > DEATH_FINE) {
                int space = floodFillCount(newHead, levelMap, allSnakes, snake.getId());
                if (space < (int)snake.getBody().size() * 3 / 2 + 1) {
                    totalBonus = TRAPPED_FINE;
                }
            }
            totalBonus += getOutOfBoundsPenalty(newHead, levelMap);

            // Support stability: bonus/penalty based on future support (long snakes only)
            if (totalBonus > TRAPPED_FINE && (int)snake.getBody().size() >= 6) {
                int supportLeft = countSupportTurnsLeft(snake, levelMap, allSnakes);
                if (supportLeft < SUPPORT_TURNS_THRESHOLD) {
                    if (hasNewSupport(newHead, levelMap)) {
                        totalBonus += SUPPORT_BONUS;
                    } else {
                        totalBonus += -SUPPORT_BONUS;
                    }
                }
            }

            return totalBonus;
        }
        
};

class BatteryScorer : public IScorer {
    public:

        void clearCache() { cachedSnakeId = -1; cachedDfs.clear(); claimedBatteries.clear(); assignedBatteries.clear(); }

        void setCache(int snakeId, const vector<DfsResult>& dfs) {
            cachedSnakeId = snakeId;
            cachedDfs = dfs;
        }

        // Pre-assign battery to specific snake — only that snake can score it
        void assignBattery(const Point& pos, int snakeId) { assignedBatteries[pos] = snakeId; }

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
                if (isAssignedToOther(r.target, cachedSnakeId)) continue;
                if (getEnemyDist(r.target) < r.pathLen) continue;
                return r.target;
            }
            return Point(-1, -1);
        }

        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            vector<DfsResult> inDir;
            for (const auto& r : cachedDfs) {
                if (r.firstDir == direction) inDir.push_back(r);
            }
            if (inDir.empty()) return 0;
            sort(inDir.begin(), inDir.end(),
                [](const DfsResult& a, const DfsResult& b) { return a.pathLen < b.pathLen; });

            int totalBonus = 0;
            for (const auto& r : inDir) {
                if (r.pathLen >= MAX_AVAILABLE_DISTANCE) continue;  // only short paths
                if (claimedBatteries.count(r.target)) continue;
                if (isAssignedToOther(r.target, snake.getId())) continue;
                int enemyDist = getEnemyDist(r.target);
                if (enemyDist < r.pathLen) continue;
                totalBonus = BASE_BONUS;
                break;
            }
            return totalBonus;
        }

    private:
        bool isAssignedToOther(const Point& bat, int myId) const {
            auto it = assignedBatteries.find(bat);
            if (it == assignedBatteries.end()) return false;
            return it->second != myId;
        }

        int cachedSnakeId = -1;
        vector<DfsResult> cachedDfs;
        set<Point> claimedBatteries;
        map<Point, int> assignedBatteries;  // battery → assigned snakeId
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

vector<BatteryCluster> clusterBatteries(const vector<Point>& batteries, int threshold, int maxClusterSize = MAX_CLUSTER_SIZE) {
    map<Point, bool> used;
    for (const auto& b : batteries) used[b] = false;

    vector<BatteryCluster> clusters;

    // Phase 1: build clusters from seeds (skip loners)
    for (const auto& bat : batteries) {
        if (used[bat]) continue;
        BatteryCluster c;
        c.batteries.push_back(bat);
        c.center = bat;

        // Add nearby unused batteries (check distance to CoM)
        for (const auto& other : batteries) {
            if (used[other] || other == bat) continue;
            if ((int)c.batteries.size() >= maxClusterSize) break;
            if (other.distanceTo(c.center) <= threshold) {
                c.batteries.push_back(other);
                c.recalcCenter();
            }
        }

        for (const auto& b : c.batteries) used[b] = true;
        clusters.push_back(c);
    }

    // Phase 2: assign remaining batteries to nearest cluster by CoM (no threshold)
    for (const auto& bat : batteries) {
        if (used[bat]) continue;
        int bestCluster = -1;
        int bestDist = 9999;
        for (int i = 0; i < (int)clusters.size(); i++) {
            if ((int)clusters[i].batteries.size() >= maxClusterSize) continue;
            int d = bat.distanceTo(clusters[i].center);
            if (d < bestDist) { bestDist = d; bestCluster = i; }
        }
        if (bestCluster >= 0) {
            clusters[bestCluster].batteries.push_back(bat);
            clusters[bestCluster].recalcCenter();
            used[bat] = true;
        } else {
            // All clusters full — new cluster
            clusters.push_back({{bat}, bat});
            used[bat] = true;
        }
    }

    return clusters;
}

class ClusterScorer : public IScorer {

    
    int turnCount = 0;
    string lastLog;
    vector<BatteryCluster> clusters;
    bool clustersInitialized = false;

    // Calculate CoM of battery list
    Point calcCoM(const vector<Point>& batteries) {
        if (batteries.empty()) return Point(-1, -1);
        int tx = 0, ty = 0;
        for (const auto& b : batteries) { tx += b.x; ty += b.y; }
        return Point(tx / (int)batteries.size(), ty / (int)batteries.size());
    }


    void initClusters(const vector<Point>& batteries) {
        int dist = CLUSTER_DISTANCE;
        int minClusters = 4;
        int prevCount = 0;
        // Too few → decrease distance
        for (int i = 0; i < 10; i++) {
            clusters = clusterBatteries(batteries, dist);
            int count = (int)clusters.size();
            if (count >= minClusters || dist <= 1) break;
            if (count <= prevCount) break;
            prevCount = count;
            dist--;
            minClusters++;
        }
        // Too many → increase distance
        while ((int)clusters.size() > MAX_CLUSTER_COUNT && dist < 50) {
            dist++;
            clusters = clusterBatteries(batteries, dist);
        }
        // Too many full clusters → decrease distance once
        {
            int fullCount = 0;
            for (const auto& c : clusters)
                if ((int)c.batteries.size() >= MAX_CLUSTER_SIZE) fullCount++;
            if (fullCount > (int)clusters.size() / 2 && dist > 1) {
                dist--;
                clusters = clusterBatteries(batteries, dist);
            }
        }
        cerr << "CLUSTER_TUNED dist=" << dist << " max=" << MAX_CLUSTER_COUNT << " got=" << clusters.size() << endl;
    }

    void removeEatenBatteries(const set<Point>& liveBats) {
        for (auto& c : clusters) {
            c.batteries.erase(
                remove_if(c.batteries.begin(), c.batteries.end(),
                    [&](const Point& b) { return !liveBats.count(b); }),
                c.batteries.end());
            if (!c.batteries.empty()) c.recalcCenter();
        }


        // Remove empty clusters, update slot indices
        for (int i = (int)clusters.size() - 1; i >= 0; i--) {
            if (clusters[i].batteries.empty()) {
                clusters.erase(clusters.begin() + i);
            }
        }
    }

    // Find reachable intermediate point between snake and target
    DfsResult findPathWithMidpoints(const Snake& snake, const Point& target,
                                     const LevelMap& levelMap, const map<int, Snake>& allSnakes) {
        Point head = snake.getHead();
        Point current = target;

        // Narrow down to reachable distance
        while (head.distanceTo(current) > MAX_AVAILABLE_DISTANCE) {
            current = head.middlePoint(current);
        }

        // Try DFS, if fails — halve again
        for (int attempts = 0; attempts < 5; attempts++) {
            DfsResult path = dfsPathToTarget(snake, current, levelMap, allSnakes);
            if (path.pathLen >= 0) {
                return path;
            }
            if (head.distanceTo(current) <= 2) {
                return path; // d=-1, unreachable
            }
            current = head.middlePoint(current);
        }
        return DfsResult();
    }

    public:

       // snakeId → target battery + DFS result
        map<int, pair<Point, DfsResult>> snakeTargets;
        Point lastCoM = Point(-1, -1);

        void update(const LevelMap& levelMap, const vector<int>& mySnakeIds,
                    const map<int, Snake>& allSnakes) {
            snakeTargets.clear();
            auto batteries = levelMap.getPowerElements();
            if (batteries.empty()) return;

            // Init clusters once, then only remove eaten batteries
            if (!clustersInitialized) {
                initClusters(batteries);
                clustersInitialized = true;
                cerr << "CL: (" << clusters.size() << "):" << endl;
                for (int i = 0; i < (int)clusters.size(); i++) {
                    cerr << "  C" << i << " center=" << clusters[i].center
                         << " bats=" << clusters[i].batteries.size() << ":";
                    for (const auto& b : clusters[i].batteries) cerr << " " << b;
                    cerr << endl;
                }
            } else {
                set<Point> liveBats(batteries.begin(), batteries.end());
                removeEatenBatteries(liveBats);
            }
            if (clusters.empty()) return;

            Point com = calcCoM(batteries);
            lastCoM = com;

            for (int id : mySnakeIds) {
                auto it = allSnakes.find(id);
                if (it == allSnakes.end() || !it->second.isAlive()) continue;

                Point head = it->second.getHead();
                Point mid = head.middlePoint(com);

                // Closest cluster to mid
                int bestCluster = -1;
                int bestDist = 9999;
                for (int i = 0; i < (int)clusters.size(); i++) {
                    int d = clusters[i].center.distanceTo(mid);
                    if (d < bestDist) { bestDist = d; bestCluster = i; }
                }
                if (bestCluster < 0) continue;

                // Closest battery in cluster to snake
                Point bestBat(-1, -1);
                int bestBatDist = 9999;
                for (const auto& b : clusters[bestCluster].batteries) {
                    int d = head.distanceTo(b);
                    if (d < bestBatDist) { bestBatDist = d; bestBat = b; }
                }
                if (bestBat == Point(-1, -1)) continue;

                // DFS to target (with midpoints if far)
                DfsResult path = findPathWithMidpoints(it->second, bestBat, levelMap, allSnakes);
                snakeTargets[id] = {bestBat, path};
                cerr << "CLUSTER S" << id << " -> " << bestBat << " (cluster " << bestCluster
                     << ", d=" << path.pathLen << ")" << endl;
            }
        }


        int evaluate(const Snake& snake, const LevelMap& levelMap,
                    const map<int, Snake>& allSnakes, int direction) override {
            auto it = snakeTargets.find(snake.getId());
            if (it == snakeTargets.end()) return 0;
            const auto& [target, path] = it->second;
            if (path.pathLen < 0) return 0;
            if (path.firstDir == direction) return BASE_BONUS;
            return 0;
        }

};


class DecisionMaker {
    vector<shared_ptr<IScorer>> scorers;
    shared_ptr<BatteryScorer> batteryScorer;
    shared_ptr<ClusterScorer> clusterScorer;
    map<int, vector<DfsResult>> snakeDfsPaths; // snakeId → reachable batteries via DFS
    int currentDfsDepth = MAX_DFS_DEPTH;
    int turnCounter = 0;

    public:
        void onSnakeDied() { currentDfsDepth += DEPTH_INCREASING; cerr << "DFS depth -> " << currentDfsDepth << endl; }

        DecisionMaker() {
            srand(time(nullptr));

            scorers.push_back(make_shared<SafetyScorer>());
            batteryScorer = make_shared<BatteryScorer>();
            scorers.push_back(batteryScorer);

            clusterScorer = make_shared<ClusterScorer>();
            scorers.push_back(clusterScorer);



        }
        

        Direction decide(const Snake& snake, const LevelMap& levelMap,
                        const map<int, Snake>& allSnakes) {
            int bestScore = -1000000;


            Direction bestDir = snake.getMovingDirection();  // default: keep going
            // Feed DFS results to BatteryScorer
            const auto& dfsPaths = getDfsPaths(snake.getId());
            batteryScorer->setCache(snake.getId(), dfsPaths);
            vector<int> decisions(4);
            // Try all 4 directions
            for (int dir = 0; dir < 4; dir++) {
                int totalScore = 0;

                int safetyTotalScore    = scorers[0]->evaluate(snake, levelMap, allSnakes, dir);
                int batteryTotalScore   = scorers[1]->evaluate(snake, levelMap, allSnakes, dir);
                int clusterTotalScore   = scorers[2]->evaluate(snake, levelMap, allSnakes, dir);

                // Prefer battery if short path exists, otherwise cluster
                int pathScore = (batteryTotalScore > 0) ? batteryTotalScore : clusterTotalScore;
                totalScore = safetyTotalScore + pathScore;
               
                decisions[dir] = totalScore;
            }

            // Save scores for conflict resolution

            // Tiebreaker: equal scores — Euclidean to cluster target, or Manhattan to nearest battery
            Point head = snake.getHead();
            vector<Point> bats = levelMap.getPowerElements();
            int closestBatId = head.getClosestBattery(bats);
            Point targetBat = bats.empty() ? head : bats[closestBatId];

            int bestTiebreak = 999999;
            for (int dir = 0; dir < 4; dir++) {
                Point newHead = head + directories[dir];
                int tiebreak = newHead.distanceTo(targetBat);

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
            

            // Claim battery so other snakes don't chase the same one
            Point claimed = batteryScorer->getBestBatteryForDir(snake, allSnakes, bestDir);
            if (!(claimed == Point(-1, -1))) {
                batteryScorer->claimBattery(claimed);
                cerr << "S" << snake.getId() << " -> bat " << claimed << endl;
            } else {
                cerr << "S" << snake.getId() << " -> no target" << endl;
            }


            return bestDir;
        }



        void computeDfsPaths(const LevelMap& map, const vector<int>& mySnakeIds,
                             const std::map<int, Snake>& allSnakes) {
            snakeDfsPaths.clear();
            for (int id : mySnakeIds) {
                auto it = allSnakes.find(id);
                if (it == allSnakes.end() || !it->second.isAlive()) continue;
                snakeDfsPaths[id] = findBatteriesDfs(it->second, map, allSnakes, currentDfsDepth);
            }
        }

        bool allDfsEmpty() const {
            for (const auto& [id, paths] : snakeDfsPaths) {
                if (!paths.empty()) return false;
            }
            return true;
        }

        void logDfsPaths() const {
            for (const auto& [id, paths] : snakeDfsPaths) {
                cerr << "DFS S" << id << ": " << paths.size() << " bats";
                for (const auto& r : paths) {
                    cerr << " [" << r.target << " d=" << r.pathLen << "]";
                }
                cerr << endl;
            }
        }

        void update(LevelMap& levelMap, const vector<int>& mySnakeIds, map<int, Snake>& allSnakes) {
            batteryScorer->clearCache();
            clusterScorer->update(levelMap, mySnakeIds, allSnakes);
            turnCounter++;

            // Compute DFS on real map
            computeDfsPaths(levelMap, mySnakeIds, allSnakes);

            // Step 6: Pre-assign batteries — closest snake (by DFS path) gets each battery
            {
                struct Claim { int snakeId; Point battery; int pathLen; };
                vector<Claim> claims;
                for (const auto& [id, paths] : snakeDfsPaths) {
                    for (const auto& r : paths) {
                        claims.push_back({id, r.target, r.pathLen});
                    }
                }
                sort(claims.begin(), claims.end(), [](const Claim& a, const Claim& b) {
                    return a.pathLen < b.pathLen;
                });
                set<Point> taken;
                set<int> assigned;
                for (const auto& c : claims) {
                    if (taken.count(c.battery)) continue;
                    if (assigned.count(c.snakeId)) continue;
                    taken.insert(c.battery);
                    assigned.insert(c.snakeId);
                    batteryScorer->assignBattery(c.battery, c.snakeId);
                    cerr << "ASSIGN S" << c.snakeId << " -> " << c.battery << " (d=" << c.pathLen << ")" << endl;
                }
            }

            logDfsPaths();
        }

        const vector<DfsResult>& getDfsPaths(int snakeId) const {
            static const vector<DfsResult> empty;
            auto it = snakeDfsPaths.find(snakeId);
            return it != snakeDfsPaths.end() ? it->second : empty;
        }

        void outputMarks(const map<int, Snake>& allSnakes) {
            markPoint(clusterScorer->lastCoM);
            for (const auto& [id, tp] : clusterScorer->snakeTargets) {
                markPoint(tp.first);
            }
        }
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
        
        bool mySnakeDied = false;
        bool didMySnakeDie() { bool r = mySnakeDied; mySnakeDied = false; return r; }

        // Update snake state each turn
        void updateSnakes(int snakebotCount) {
            mySnakeDied = false;
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
                        mySnakeDied = true;
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
    MAX_CLUSTER_COUNT = pow(width*height, 1.0/3.0)+1;
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
        if (state.didMySnakeDie()) decisionMaker.onSnakeDied();

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

        for (auto& snakeRef : state.getMySnakes()) {
            Snake& snake = snakeRef.get();
            snake.debug();
            Direction dir = decisionMaker.decide(snake, state.levelMap, state.allSnakes);
            snake.setMovingDirection(dir);
            output += to_string(snake.getId()) + " " + getDirectionName(dir) + ";";

            Point newHead = snake.getHead() + directories[dir];
            bool eatsBattery = state.levelMap.isBattery(newHead);
            snake.updateHead(newHead, eatsBattery);
            if (eatsBattery) {
                state.levelMap.removePowerElement(newHead);
            }
        }

        decisionMaker.outputMarks(state.allSnakes);
        if (output.empty()) output = "WAIT";
        cout << output << endl;
    }
}