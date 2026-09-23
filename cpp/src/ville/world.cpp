#include "ville/world.h"

#include <algorithm>
#include <cctype>
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

SectorKind sectorKindFromName(const std::string& name, SectorKind fallback) {
  for (int k = 0; k <= static_cast<int>(SectorKind::Office); ++k)
    if (name == sectorKindName(static_cast<SectorKind>(k))) return static_cast<SectorKind>(k);
  return fallback;
}

namespace {

constexpr int kCellW = 28, kCellH = 20, kRoad = 2;

struct Plan {
  std::string name;
  SectorKind kind;
};

using Rooms = std::vector<std::pair<std::string, std::vector<std::string>>>;

}  // namespace

// A building's rooms from its effective spec: its own rooms if it declares
// them, else its type's, then `bedrooms` copies of the type's bedroom.
std::vector<std::pair<std::string, std::vector<std::string>>> roomsFromSpec(const BuildingSpec& b) {
  Rooms rooms;
  for (auto& r : b.rooms) rooms.push_back({r.name.empty() ? "room" : r.name, r.objects});
  if (b.hasBedroom) {
    int n = b.bedrooms >= 0 ? b.bedrooms : 1;
    for (int i = 0; i < n; ++i)
      rooms.push_back({n > 1 ? b.bedroom.name + " " + std::to_string(i + 1) : b.bedroom.name, b.bedroom.objects});
  }
  if (rooms.empty()) rooms.push_back({b.kind.empty() ? "room" : b.kind, {}});
  return rooms;
}

namespace {

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
  s.key = name;
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

void World::park(int sectorId, uint32_t seed, const std::vector<std::string>& things) {
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
  int count = static_cast<int>(things.size());
  for (int i = 0; i < count; ++i) {
    GameObject o;
    o.name = things[i];
    o.arena = aid;
    o.x = r.x + 3 + i * (r.w - 6) / std::max(1, count - 1);
    o.y = r.y + r.h - 4;
    tiles_[o.y * w_ + o.x] = Tile::Plaza;
    objects.push_back(o);
    arenas[aid].objects.push_back(static_cast<int>(objects.size()) - 1);
  }
}

void World::generate(const TownSpec& spec) {
  sectors.clear();
  arenas.clear();
  objects.clear();
  const EnvironmentSpec& env = spec.env;

  // The buildings the environment declares, in order, then generated ones:
  // one building of each type per N generated residents (generate.one_per).
  std::vector<BuildingSpec> plan = env.buildings;
  int extra = env.generate.residents;
  if (extra > 0) {
    std::vector<BuildingSpec> more;
    for (auto& [kind, per] : env.generate.onePer) {
      if (per <= 0) continue;
      int count = std::max(1, (extra + per - 1) / per);
      std::string label = kind;
      if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
      for (int i = 0; i < count; ++i) {
        BuildingSpec b;
        b.kind = kind;
        b.name = (kind == "home" ? std::string("House") : label) + " " + std::to_string(i + 1);
        more.push_back(b);
      }
    }
    // Interleave so neighborhoods mix homes and businesses.
    for (size_t i = more.size(); i-- > 1;) std::swap(more[i], more[mix(env.seed + static_cast<uint32_t>(i)) % (i + 1)]);
    plan.insert(plan.end(), more.begin(), more.end());
  }

  int n = static_cast<int>(plan.size());
  int cols = std::max(5, static_cast<int>(std::ceil(std::sqrt(std::max(n, 1) * 1.4))));
  int rows = std::max(1, (n + cols - 1) / cols);
  w_ = cols * kCellW + kRoad;
  h_ = rows * kCellH + kRoad;
  tiles_.assign(w_ * h_, Tile::Grass);
  arenaOf_.assign(w_ * h_, -1);
  sectorOf_.assign(w_ * h_, -1);
  for (int y = 0; y < h_; ++y)
    for (int x = 0; x < w_; ++x)
      if (x % kCellW < kRoad || y % kCellH < kRoad) tiles_[y * w_ + x] = Tile::Road;
  // Empty lots at the end of the last row get a few trees.
  for (int i = n; i < rows * cols; ++i) {
    int cx = (i % cols) * kCellW + kRoad, cy = (i / cols) * kCellH + kRoad;
    for (int t = 0; t < 14; ++t) {
      uint32_t h = mix(env.seed * 31u + i * 977u + t);
      int tx = cx + 1 + static_cast<int>(h % (kCellW - kRoad - 2)), ty = cy + 1 + static_cast<int>((h >> 10) % (kCellH - kRoad - 2));
      tiles_[ty * w_ + tx] = Tile::Tree;
    }
  }

  for (int i = 0; i < n; ++i) {
    int cx = (i % cols) * kCellW + kRoad, cy = (i / cols) * kCellH + kRoad;
    int cellW = kCellW - kRoad, cellH = kCellH - kRoad;
    BuildingSpec b = spec.styleFor(plan[i]);  // style < type < this building
    SectorKind kind = sectorKindFromName(b.kind, SectorKind::Home);
    auto finish = [&](int id) {
      sectors[id].floorColor = b.floorColor;
      sectors[id].wallColor = b.wallColor;
    };
    if (kind == SectorKind::Park) {
      Rect r{cx + 1, cy + 1, cellW - 2, cellH - 2};
      int id = addSector(b.name, kind, r, r.cx(), r.y + r.h - 1);
      park(id, env.seed + i, b.parkObjects);
      finish(id);
      continue;
    }
    int size = b.size >= 0 ? b.size : 1;
    int bw = size == 2 ? cellW - 2 : size == 0 ? cellW - 10 : cellW - 6;
    int bh = size == 2 ? cellH - 4 : size == 0 ? cellH - 8 : cellH - 6;
    Rect r{cx + (cellW - bw) / 2, cy + 1, bw, bh};
    int doorX = r.cx(), doorY = r.y + r.h - 1;
    int id = addSector(b.name, kind, r, doorX, doorY);
    Rooms rooms = roomsFromSpec(b);
    // Each back room needs at least 3 tiles of width.
    size_t maxRooms = static_cast<size_t>((bw - 2 + 1) / 4) + 1;
    if (rooms.size() > maxRooms) rooms.resize(maxRooms);
    building(id, rooms);
    finish(id);
    // A short walk from the door down to the street.
    for (int y = doorY + 1; y < cy + cellH; ++y) tiles_[y * w_ + doorX] = Tile::Plaza;
  }
}

int World::findSector(const std::string& name) const {
  for (size_t i = 0; i < sectors.size(); ++i)
    if (sectors[i].key == name) return static_cast<int>(i);
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
