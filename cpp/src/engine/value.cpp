#include "engine/value.h"

#include <charconv>
#include <cmath>
#include <functional>

namespace sl {

Value Value::str(std::string s) {
  Value r;
  r.t = VT::Str;
  auto o = std::make_shared<StrObj>();
  o->v = std::move(s);
  r.h = std::move(o);
  return r;
}

Value Value::list(std::vector<Value> items) {
  Value r;
  r.t = VT::List;
  auto o = std::make_shared<ListObj>();
  o->v = std::move(items);
  r.h = std::move(o);
  return r;
}

Value Value::dict() {
  Value r;
  r.t = VT::Dict;
  r.h = std::make_shared<DictObj>();
  return r;
}

const std::string& Value::s() const { return static_cast<StrObj*>(h.get())->v; }
std::vector<Value>& Value::l() const { return static_cast<ListObj*>(h.get())->v; }
DictObj& Value::d() const { return *static_cast<DictObj*>(h.get()); }

size_t KeyHash::operator()(const Value& v) const {
  switch (v.t) {
    case VT::Null: return 0x9e3779b9u;
    case VT::Bool: return v.b ? 1u : 2u;
    case VT::Num: return std::hash<double>()(v.n == 0 ? 0.0 : v.n);
    case VT::Str: return std::hash<std::string>()(v.s());
    case VT::Agent: return 0x1000003u ^ static_cast<size_t>(v.idx);
    case VT::Loc: return 0x2000005u ^ static_cast<size_t>(v.idx);
    case VT::Event: return 0x3000007u ^ static_cast<size_t>(v.idx);
    case VT::List:
    case VT::Dict: return std::hash<const void*>()(v.h.get());
  }
  return 0;
}

bool KeyEq::operator()(const Value& a, const Value& b) const {
  if (a.t != b.t) return false;
  switch (a.t) {
    case VT::Null: return true;
    case VT::Bool: return a.b == b.b;
    case VT::Num: return a.n == b.n;
    case VT::Str: return a.s() == b.s();
    case VT::Agent:
    case VT::Loc:
    case VT::Event: return a.idx == b.idx;
    case VT::List:
    case VT::Dict: return a.h == b.h;
  }
  return false;
}

Value* DictObj::find(const Value& key) {
  if (entries.size() <= kIndexThreshold) {
    KeyEq eq;
    for (auto& e : entries)
      if (eq(e.first, key)) return &e.second;
    return nullptr;
  }
  if (index_.size() != entries.size()) {
    index_.clear();
    for (size_t i = 0; i < entries.size(); ++i) index_.emplace(entries[i].first, i);
  }
  auto it = index_.find(key);
  return it == index_.end() ? nullptr : &entries[it->second].second;
}

void DictObj::set(const Value& key, Value value) {
  if (Value* existing = find(key)) {
    *existing = std::move(value);
    return;
  }
  entries.emplace_back(key, std::move(value));
  if (entries.size() > kIndexThreshold) index_.emplace(key, entries.size() - 1);
}

bool truthy(const Value& v) {
  switch (v.t) {
    case VT::Null: return false;
    case VT::Bool: return v.b;
    case VT::Num: return v.n != 0 && !std::isnan(v.n);
    case VT::Str: return !v.s().empty();
    case VT::List: return !v.l().empty();
    case VT::Dict: return v.d().size() > 0;
    default: return true;
  }
}

// Structural for lists and dicts (the Python reference's ==), by-value for
// everything else.
bool valuesEqual(const Value& a, const Value& b) {
  if (a.t != b.t) return false;
  switch (a.t) {
    case VT::List: {
      const auto& x = a.l();
      const auto& y = b.l();
      if (x.size() != y.size()) return false;
      for (size_t i = 0; i < x.size(); ++i)
        if (!valuesEqual(x[i], y[i])) return false;
      return true;
    }
    case VT::Dict: {
      auto& x = a.d();
      auto& y = b.d();
      if (x.size() != y.size()) return false;
      for (auto& [k, v] : x.entries) {
        Value* other = y.find(k);
        if (!other || !valuesEqual(v, *other)) return false;
      }
      return true;
    }
    default: return KeyEq()(a, b);
  }
}

// Matches JavaScript's Number#toString for the cases scripts actually hit:
// integers print without a decimal point, everything else shortest round-trip.
std::string numToString(double n) {
  if (std::isnan(n)) return "NaN";
  if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
  if (n == std::floor(n) && std::fabs(n) < 1e21) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, static_cast<long long>(n));
    return std::string(buf, r.ptr);
  }
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, n);
  return std::string(buf, r.ptr);
}

}  // namespace sl
