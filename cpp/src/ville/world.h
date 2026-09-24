// A tile-based town: a tree of world > sector (building) > arena (room) >
// game object, over a collision grid agents walk on tile by tile.
//
// Everything about the town comes from its environment file; only the
// street-grid layout is computed, so a town can hold 25 residents or thousands.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ville/spec.h"

namespace ville {

enum class Tile : uint8_t { Grass, Road, Floor, Wall, Door, Tree, Water, Plaza };

enum class SectorKind : uint8_t { Home, Cafe, Pub, Store, Market, Park, College, Dorm, TownHall, Office };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  int cx() const { return x + w / 2; }
  int cy() const { return y + h / 2; }
};

struct GameObject {
  std::string name;  // "bed", "coffee machine", ...
  int arena = -1;
  int x = 0, y = 0;
  std::string state = "idle";  // "being used by Klaus Mueller", "brewing coffee", ...
};

struct Arena {
  std::string name;  // "kitchen", "bedroom", "cafe customer seating", ...
  int sector = -1;
  Rect rect;
  std::vector<int> objects;
};

struct Sector {
  std::string key;   // original name: what residents' homes / workplaces and town specs refer to
  std::string name;  // display name ("Hobbs Cafe", "Lin family's house", ...), may be customized
  unsigned floorColor = 0, wallColor = 0;  // 0xRRGGBB overrides, 0 = default
  SectorKind kind = SectorKind::Home;
  Rect rect;
  std::vector<int> arenas;
  int doorX = 0, doorY = 0;
};

class World {
 public:
  // Lays out the environment's buildings (and generated ones) on a street grid.
  void generate(const TownSpec& spec);

  std::string name = "Town";  // the environment's name, first part of every address

  int width() const { return w_; }
  int height() const { return h_; }
  Tile tile(int x, int y) const { return tiles_[y * w_ + x]; }
  bool walkable(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return false;
    Tile t = tiles_[y * w_ + x];
    return t != Tile::Wall && t != Tile::Tree && t != Tile::Water;
  }
  int arenaAt(int x, int y) const { return arenaOf_[y * w_ + x]; }
  int sectorAt(int x, int y) const {
    int a = arenaAt(x, y);
    return a >= 0 ? arenas[a].sector : sectorOf_[y * w_ + x];
  }

  // A* over the collision grid (4-connected). Returns the tiles to walk,
  // excluding the start; empty if unreachable or already there.
  std::vector<std::pair<int16_t, int16_t>> path(int sx, int sy, int tx, int ty) const;
  // Nearest walkable tile to (x, y) inside `arena`'s rect (the object tile
  // itself if it's walkable).
  std::pair<int, int> standTileFor(int object) const;
  std::pair<int, int> freeTileIn(int arena, uint32_t salt) const;

  std::vector<Sector> sectors;
  std::vector<Arena> arenas;
  std::vector<GameObject> objects;

  int findSector(const std::string& name) const;
  int findArena(int sector, const std::string& name) const;
  // First object in the sector whose name contains `keyword`.
  int findObject(int sector, const std::string& keyword) const;
  std::string address(int object) const;  // "Smallville:Hobbs Cafe:cafe:coffee machine"

 private:
  int w_ = 0, h_ = 0;
  std::vector<Tile> tiles_;
  std::vector<int> arenaOf_, sectorOf_;

  void fill(const Rect& r, Tile t);
  int addSector(const std::string& name, SectorKind kind, const Rect& r, int doorX, int doorY);
  void building(int sector, const std::vector<std::pair<std::string, std::vector<std::string>>>& rooms);
  void park(int sector, uint32_t seed, const std::vector<std::string>& objects);
};

const char* sectorKindName(SectorKind k);
SectorKind sectorKindFromName(const std::string& name, SectorKind fallback);
// A building's rooms from its effective spec: (room, objects), hall first.
std::vector<std::pair<std::string, std::vector<std::string>>> roomsFromSpec(const BuildingSpec& b);

}  // namespace ville
