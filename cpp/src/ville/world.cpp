#include "ville/world.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace ville {

const char* sectorKindName(SectorKind k) {
  switch (k) {
    case SectorKind::Home: return "home";
    case SectorKind::Cafe: return "cafe";
    case SectorKind::Pub: return "pub";
    case SectorKind::Store: return "store";
    case SectorKind::Market: return "market";
    case SectorKind::Park: return "park";
    case SectorKind::College: return "college";
    case SectorKind::Dorm: return "dorm";
    case SectorKind::TownHall: return "town hall";
    case SectorKind::Office: return "office";
  }
  return "?";
}

namespace {

constexpr int kCellW = 28, kCellH = 20, kRoad = 2;

struct Plan {
  std::string name;
  SectorKind kind;
};

using Rooms = std::vector<std::pair<std::string, std::vector<std::string>>>;

// Room layouts per sector kind: the first entry is the ground-floor hall the
// front door opens into; the rest sit along the back wall.
Rooms roomsFor(SectorKind k, int bedrooms) {
  switch (k) {
    case SectorKind::Home: {
      Rooms r = {{"living room", {"couch", "tv", "bookshelf", "dining table"}},
                 {"kitchen", {"stove", "refrigerator", "kitchen sink", "kitchen table"}},
                 {"bathroom", {"toilet", "shower", "bathroom sink"}}};
      for (int i = 0; i < std::clamp(bedrooms, 1, 3); ++i)
        r.push_back({bedrooms > 1 ? "bedroom " + std::to_string(i + 1) : "bedroom", {"bed", "closet", "desk"}});
      return r;
    }
    case SectorKind::Cafe:
      return {{"cafe", {"cafe customer seating", "cafe customer seating", "cafe customer seating", "piano"}},
              {"counter", {"behind the cafe counter", "coffee machine"}},
              {"cafe kitchen", {"cooking area", "refrigerator"}}};
    case SectorKind::Pub:
      return {{"pub", {"bar customer seating", "bar customer seating", "pool table", "karaoke machine"}},
              {"bar", {"behind the bar counter", "beer taps"}}};
    case SectorKind::Store:
      return {{"supply store", {"supply store shelf", "supply store shelf", "behind the supply store counter"}},
              {"storage room", {"storage shelf"}}};
    case SectorKind::Market:
      return {{"grocery store", {"grocery shelf", "grocery shelf", "behind the grocery counter"}},
              {"pharmacy", {"pharmacy counter", "behind the pharmacy counter"}}};
    case SectorKind::College:
      return {{"hallway", {"bench"}},
              {"classroom", {"classroom student seating", "classroom student seating", "blackboard", "classroom podium"}},
              {"library", {"library table", "library table", "bookshelf"}},
              {"professor's office", {"desk", "bookshelf"}}};
    case SectorKind::Dorm:
      return {{"common room", {"common room sofa", "common room table", "tv"}},
              {"dorm kitchen", {"stove", "refrigerator"}},
              {"dorm bathroom", {"toilet", "shower"}},
              {"dorm room 1", {"bed", "desk"}},
              {"dorm room 2", {"bed", "desk"}},
              {"dorm room 3", {"bed", "desk"}}};
    case SectorKind::TownHall:
      return {{"town hall lobby", {"bench", "notice board"}},
              {"mayor's office", {"desk"}},
              {"meeting room", {"meeting table", "podium"}}};
    case SectorKind::Office:
      return {{"office", {"office desk", "office desk", "office desk", "coffee maker"}}, {"meeting room", {"meeting table"}}};
    case SectorKind::Park: return {};
  }
  return {};
}

// The paper's cast of places, in the order they're laid out.
const std::vector<Plan>& villePlaces() {
  static const std::vector<Plan> p = {
      {"Hobbs Cafe", SectorKind::Cafe},
      {"Lin family's house", SectorKind::Home},
      {"Oak Hill College", SectorKind::College},
      {"The Willows Market and Pharmacy", SectorKind::Market},
      {"Moreno family's house", SectorKind::Home},
      {"Johnson Park", SectorKind::Park},
      {"The Rose and Crown Pub", SectorKind::Pub},
      {"Dorm for Oak Hill College", SectorKind::Dorm},
      {"Moore family's house", SectorKind::Home},
      {"Harvey Oak Supply Store", SectorKind::Store},
      {"Isabella Rodriguez's apartment", SectorKind::Home},
      {"Town Hall", SectorKind::TownHall},
      {"Artist's co-living space", SectorKind::Home},
      {"Adam Smith's house", SectorKind::Home},
      {"Yuriko Yamamoto's house", SectorKind::Home},
      {"Tamara Taylor and Carmen Ortiz's house", SectorKind::Home},
      {"Arthur Burton's apartment", SectorKind::Home},
      {"Ryan Park's apartment", SectorKind::Home},
      {"Giorgio Rossi's apartment", SectorKind::Home},
      {"Carlos Gomez's apartment", SectorKind::Home},
  };
  return p;
}

uint32_t mix(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

}  // namespace

void World::fill(const Rect& r, Tile t) {
  for (int y = r.y; y < r.y + r.h; ++y)
    for (int x = r.x; x < r.x + r.w; ++x)
      if (x >= 0 && y >= 0 && x < w_ && y < h_) tiles_[y * w_ + x] = t;
}

int World::addSector(const std::string& name, SectorKind kind, const Rect& r, int doorX, int doorY) {
  Sector s;
  s.name = name;
  s.kind = kind;
  s.rect = r;
  s.doorX = doorX;
  s.doorY = doorY;
  sectors.push_back(std::move(s));
  int id = static_cast<int>(sectors.size()) - 1;
  for (int y = r.y; y < r.y + r.h; ++y)
    for (int x = r.x; x < r.x + r.w; ++x) sectorOf_[y * w_ + x] = id;
  return id;
}

void World::building(int sectorId, const Rooms& rooms) {
  const Rect r = sectors[sectorId].rect;
  // Outer walls, floor inside.
  fill(r, Tile::Wall);
  Rect inner{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
  fill(inner, Tile::Floor);
  // Hall along the front (bottom), back rooms side by side above it.
  int hallH = std::max(4, inner.h * 2 / 5);
  Rect hall{inner.x, inner.y + inner.h - hallH, inner.w, hallH};
  int backRooms = static_cast<int>(rooms.size()) - 1;
  int wallY = hall.y - 1;
  if (backRooms > 0) {
    for (int x = inner.x; x < inner.x + inner.w; ++x) tiles_[wallY * w_ + x] = Tile::Wall;
  }
  auto addArena = [&](const std::string& name, const Rect& ar, const std::vector<std::string>& objs) {
    Arena a;
    a.name = name;
    a.sector = sectorId;
    a.rect = ar;
    arenas.push_back(a);
    int aid = static_cast<int>(arenas.size()) - 1;
    sectors[sectorId].arenas.push_back(aid);
    for (int y = ar.y; y < ar.y + ar.h; ++y)
      for (int x = ar.x; x < ar.x + ar.w; ++x) arenaOf_[y * w_ + x] = aid;
    // Objects along the room's back wall, then its side walls.
    int placed = 0;
    for (auto& on : objs) {
      int ox, oy;
      int slots = std::max(1, (ar.w - 1) / 2);
      if (placed < slots) {
        ox = ar.x + 1 + placed * 2;
        oy = ar.y;
      } else {
        ox = ar.x + (placed % 2 ? ar.w - 1 : 0);
        oy = ar.y + 1 + (placed - slots);
      }
      ox = std::clamp(ox, ar.x, ar.x + ar.w - 1);
      oy = std::clamp(oy, ar.y, ar.y + ar.h - 1);
      GameObject o;
      o.name = on;
      o.arena = aid;
      o.x = ox;
      o.y = oy;
      objects.push_back(o);
      arenas[aid].objects.push_back(static_cast<int>(objects.size()) - 1);
      ++placed;
    }
  };
  addArena(rooms[0].first, hall, rooms[0].second);
  if (backRooms > 0) {
    int backH = wallY - inner.y;
    int roomW = (inner.w - (backRooms - 1)) / backRooms;
    int x = inner.x;
    for (int i = 0; i < backRooms; ++i) {
      int wdt = i == backRooms - 1 ? inner.x + inner.w - x : roomW;
      Rect ar{x, inner.y, wdt, backH};
      addArena(rooms[i + 1].first, ar, rooms[i + 1].second);
      // Doorway from the hall into this room, and a wall to the next room.
      tiles_[wallY * w_ + x + wdt / 2] = Tile::Door;
      if (i < backRooms - 1)
        for (int yy = inner.y; yy < wallY; ++yy) tiles_[yy * w_ + x + wdt] = Tile::Wall;
      x += wdt + 1;
    }
  }
  // Front door in the middle of the bottom wall.
  const Sector& s = sectors[sectorId];
  tiles_[s.doorY * w_ + s.doorX] = Tile::Door;
}

void World::park(int sectorId, uint32_t seed) {
  const Rect r = sectors[sectorId].rect;
  Arena a;
  a.name = "park";
  a.sector = sectorId;
  a.rect = r;
  arenas.push_back(a);
  int aid = static_cast<int>(arenas.size()) - 1;
  sectors[sectorId].arenas.push_back(aid);
  for (int y = r.y; y < r.y + r.h; ++y)
    for (int x = r.x; x < r.x + r.w; ++x) arenaOf_[y * w_ + x] = aid;
  // A pond, paths, a few trees, benches and a garden.
  Rect pond{r.x + r.w / 2 - 3, r.y + 3, 6, 4};
  fill(pond, Tile::Water);
  for (int x = r.x; x < r.x + r.w; ++x) tiles_[(r.y + r.h - 3) * w_ + x] = Tile::Plaza;
  for (int i = 0; i < 10; ++i) {
    uint32_t h = mix(seed + i * 7919u);
    int tx = r.x + 1 + static_cast<int>(h % (r.w - 2));
    int ty = r.y + 1 + static_cast<int>((h >> 8) % (r.h - 5));
    if (tiles_[ty * w_ + tx] == Tile::Grass && !pond.contains(tx, ty)) tiles_[ty * w_ + tx] = Tile::Tree;
  }
  const char* things[] = {"park bench", "park bench", "park garden", "picnic table"};
  for (int i = 0; i < 4; ++i) {
    GameObject o;
    o.name = things[i];
    o.arena = aid;
    o.x = r.x + 3 + i * (r.w - 6) / 3;
    o.y = r.y + r.h - 4;
    tiles_[o.y * w_ + o.x] = Tile::Plaza;
    objects.push_back(o);
    arenas[aid].objects.push_back(static_cast<int>(objects.size()) - 1);
  }
}

void World::generate(int population, uint32_t seed) {
  sectors.clear();
  arenas.clear();
  objects.clear();

  // Which sectors this town needs: the named Ville, then generated blocks.
  std::vector<Plan> plan = villePlaces();
  if (population > 25) {
    int extra = population - 25;
    int homes = (extra + 2) / 3;
    int cafes = std::max(1, extra / 70), pubs = std::max(1, extra / 90), stores = std::max(1, extra / 90);
    int markets = std::max(1, extra / 90), parks = std::max(1, extra / 160), offices = std::max(1, extra / 45);
    int colleges = extra / 400, dorms = extra / 250;
    std::vector<Plan> more;
    for (int i = 0; i < homes; ++i) more.push_back({"House " + std::to_string(i + 1), SectorKind::Home});
    for (int i = 0; i < cafes; ++i) more.push_back({"Cafe " + std::to_string(i + 2), SectorKind::Cafe});
    for (int i = 0; i < pubs; ++i) more.push_back({"Pub " + std::to_string(i + 2), SectorKind::Pub});
    for (int i = 0; i < stores; ++i) more.push_back({"Supply Store " + std::to_string(i + 2), SectorKind::Store});
    for (int i = 0; i < markets; ++i) more.push_back({"Market " + std::to_string(i + 2), SectorKind::Market});
    for (int i = 0; i < parks; ++i) more.push_back({"Park " + std::to_string(i + 2), SectorKind::Park});
    for (int i = 0; i < offices; ++i) more.push_back({"Office " + std::to_string(i + 1), SectorKind::Office});
    for (int i = 0; i < colleges; ++i) more.push_back({"College " + std::to_string(i + 2), SectorKind::College});
    for (int i = 0; i < dorms; ++i) more.push_back({"Dorm " + std::to_string(i + 2), SectorKind::Dorm});
    // Interleave so neighborhoods mix homes and businesses.
    for (size_t i = more.size(); i-- > 1;) std::swap(more[i], more[mix(seed + static_cast<uint32_t>(i)) % (i + 1)]);
    plan.insert(plan.end(), more.begin(), more.end());
  }

  int n = static_cast<int>(plan.size());
  int cols = std::max(5, static_cast<int>(std::ceil(std::sqrt(n * 1.4))));
  int rows = (n + cols - 1) / cols;
  w_ = cols * kCellW + kRoad;
  h_ = rows * kCellH + kRoad;
  tiles_.assign(w_ * h_, Tile::Grass);
  arenaOf_.assign(w_ * h_, -1);
  sectorOf_.assign(w_ * h_, -1);
  // Street grid
  for (int y = 0; y < h_; ++y)
    for (int x = 0; x < w_; ++x)
      if (x % kCellW < kRoad || y % kCellH < kRoad) tiles_[y * w_ + x] = Tile::Road;

  for (int i = n; i < rows * cols; ++i) {
    int cx = (i % cols) * kCellW + kRoad, cy = (i / cols) * kCellH + kRoad;
    for (int t = 0; t < 14; ++t) {
      uint32_t h = mix(seed * 31u + i * 977u + t);
      int tx = cx + 1 + static_cast<int>(h % (kCellW - kRoad - 2)), ty = cy + 1 + static_cast<int>((h >> 10) % (kCellH - kRoad - 2));
      tiles_[ty * w_ + tx] = Tile::Tree;
    }
  }
  for (int i = 0; i < n; ++i) {
    int cx = (i % cols) * kCellW + kRoad, cy = (i / cols) * kCellH + kRoad;
    int cellW = kCellW - kRoad, cellH = kCellH - kRoad;
    const Plan& p = plan[i];
    if (p.kind == SectorKind::Park) {
      Rect r{cx + 1, cy + 1, cellW - 2, cellH - 2};
      int id = addSector(p.name, p.kind, r, r.cx(), r.y + r.h - 1);
      park(id, seed + i);
      continue;
    }
    bool big = p.kind == SectorKind::College || p.kind == SectorKind::Dorm || p.kind == SectorKind::Cafe ||
               p.kind == SectorKind::Market || p.kind == SectorKind::TownHall;
    int bw = big ? cellW - 2 : cellW - 6, bh = big ? cellH - 4 : cellH - 6;
    Rect r{cx + (cellW - bw) / 2, cy + 1, bw, bh};
    int doorX = r.cx(), doorY = r.y + r.h - 1;
    int id = addSector(p.name, p.kind, r, doorX, doorY);
    int bedrooms = p.name == "Lin family's house" ? 3 : p.name == "Artist's co-living space" ? 3 : 2;
    building(id, roomsFor(p.kind, bedrooms));
    // A short walk from the door down to the street.
    for (int y = doorY + 1; y < cy + cellH; ++y) tiles_[y * w_ + doorX] = Tile::Plaza;
  }
}

int World::findSector(const std::string& name) const {
  for (size_t i = 0; i < sectors.size(); ++i)
    if (sectors[i].name == name) return static_cast<int>(i);
  return -1;
}

int World::findArena(int sector, const std::string& name) const {
  if (sector < 0) return -1;
  for (int a : sectors[sector].arenas)
    if (arenas[a].name.find(name) != std::string::npos) return a;
  return -1;
}

int World::findObject(int sector, const std::string& keyword) const {
  if (sector < 0) return -1;
  for (int a : sectors[sector].arenas)
    for (int o : arenas[a].objects)
      if (objects[o].name.find(keyword) != std::string::npos) return o;
  return -1;
}

std::string World::address(int object) const {
  const GameObject& o = objects[object];
  const Arena& a = arenas[o.arena];
  return "the Ville:" + sectors[a.sector].name + ":" + a.name + ":" + o.name;
}

std::pair<int, int> World::standTileFor(int object) const {
  const GameObject& o = objects[object];
  if (walkable(o.x, o.y)) return {o.x, o.y};
  static const int d[8][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  for (auto& dd : d)
    if (walkable(o.x + dd[0], o.y + dd[1])) return {o.x + dd[0], o.y + dd[1]};
  return {o.x, o.y};
}

std::pair<int, int> World::freeTileIn(int arena, uint32_t salt) const {
  const Rect& r = arenas[arena].rect;
  for (int tries = 0; tries < 30; ++tries) {
    uint32_t h = mix(salt + tries * 2654435761u);
    int x = r.x + static_cast<int>(h % std::max(1, r.w)), y = r.y + static_cast<int>((h >> 12) % std::max(1, r.h));
    if (walkable(x, y)) return {x, y};
  }
  return {r.cx(), r.cy()};
}

std::vector<std::pair<int16_t, int16_t>> World::path(int sx, int sy, int tx, int ty) const {
  std::vector<std::pair<int16_t, int16_t>> out;
  if (sx == tx && sy == ty) return out;
  if (!walkable(tx, ty)) return out;
  int n = w_ * h_;
  // Per-thread scratch reused across calls, invalidated by a stamp rather
  // than cleared -- paths run for thousands of agents per game-hour.
  thread_local std::vector<int> gScore, parent;
  thread_local std::vector<uint32_t> stamp;
  thread_local uint32_t cur = 0;
  if ((int)gScore.size() < n) {
    gScore.assign(n, 0);
    parent.assign(n, -1);
    stamp.assign(n, 0);
    cur = 0;
  }
  ++cur;
  auto hfn = [&](int i) { return std::abs(i % w_ - tx) + std::abs(i / w_ - ty); };
  using Node = std::pair<int, int>;  // (f, index)
  std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
  int s = sy * w_ + sx, t = ty * w_ + tx;
  stamp[s] = cur;
  gScore[s] = 0;
  parent[s] = -1;
  open.push({hfn(s), s});
  static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
  bool found = false;
  while (!open.empty()) {
    auto [f, i] = open.top();
    open.pop();
    if (i == t) {
      found = true;
      break;
    }
    if (f - hfn(i) > gScore[i]) continue;  // stale entry
    int x = i % w_, y = i / w_;
    for (int k = 0; k < 4; ++k) {
      int nx = x + dx[k], ny = y + dy[k];
      if (!walkable(nx, ny)) continue;
      int j = ny * w_ + nx;
      int g = gScore[i] + 1;
      if (stamp[j] != cur || g < gScore[j]) {
        stamp[j] = cur;
        gScore[j] = g;
        parent[j] = i;
        open.push({g + hfn(j), j});
      }
    }
  }
  if (!found) return out;
  for (int i = t; i != s; i = parent[i]) out.push_back({static_cast<int16_t>(i % w_), static_cast<int16_t>(i / w_)});
  std::reverse(out.begin(), out.end());
  return out;
}

}  // namespace ville
