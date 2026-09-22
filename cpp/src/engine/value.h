// Runtime values for the SocialLang interpreter.
//
// Built for simulation throughput, not generality: agents, locations, and
// events are plain indices into the interpreter's own tables (no per-object
// heap allocation, no refcount churn when a 5,000-agent list is copied), and
// only strings, lists, and dicts live on the heap behind one shared_ptr. A
// Value is 32 bytes and copying one is at most a refcount bump.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sl {

enum class VT : uint8_t { Null, Bool, Num, Str, List, Dict, Agent, Loc, Event };

struct Heap {
  virtual ~Heap() = default;
};

struct DictObj;

struct Value {
  VT t = VT::Null;
  union {
    bool b;
    double n;
    int32_t idx;
  };
  std::shared_ptr<Heap> h;

  Value() : n(0) {}

  static Value boolean(bool v) { Value r; r.t = VT::Bool; r.b = v; return r; }
  static Value num(double v) { Value r; r.t = VT::Num; r.n = v; return r; }
  static Value str(std::string s);
  static Value list(std::vector<Value> items = {});
  static Value dict();
  static Value agent(int i) { Value r; r.t = VT::Agent; r.idx = i; return r; }
  static Value loc(int i) { Value r; r.t = VT::Loc; r.idx = i; return r; }
  static Value event(int i) { Value r; r.t = VT::Event; r.idx = i; return r; }

  bool isNull() const { return t == VT::Null; }
  const std::string& s() const;
  std::vector<Value>& l() const;
  DictObj& d() const;
};

struct StrObj : Heap {
  std::string v;
};

struct ListObj : Heap {
  std::vector<Value> v;
};

// Dict key semantics match the reference engines' Map: strings, numbers,
// bools, null, and agent/location/event references compare by value; lists
// and dicts by identity.
struct KeyHash {
  size_t operator()(const Value& v) const;
};
struct KeyEq {
  bool operator()(const Value& a, const Value& b) const;
};

// Insertion-ordered (iteration order is part of the language: `tally` breaks
// ties by first-seen). Small dicts -- the overwhelmingly common case, e.g.
// plan steps -- are a linear scan; the hash index is only built past a
// handful of entries.
struct DictObj : Heap {
  std::vector<std::pair<Value, Value>> entries;

  Value* find(const Value& key);
  void set(const Value& key, Value value);
  size_t size() const { return entries.size(); }

 private:
  static constexpr size_t kIndexThreshold = 8;
  std::unordered_map<Value, size_t, KeyHash, KeyEq> index_;
};

bool truthy(const Value& v);
bool valuesEqual(const Value& a, const Value& b);
std::string numToString(double n);

}  // namespace sl
