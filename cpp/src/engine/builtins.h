// The builtin function table, shared by the parser's link pass (name -> id)
// and the interpreter's dispatch (id -> implementation). Same set, same names,
// same argument conventions as sociallang/lang/interpreter.py's _make_builtins.
#pragma once

#include <array>
#include <string_view>

namespace sl {

#define SL_BUILTINS(X)                                                        \
  X(ask) X(ask_choice) X(ask_all) X(ask_choice_all) X(broadcast) X(whisper)  \
  X(remember) X(reflect) X(maybe_reflect) X(set_persona) X(make_plan)        \
  X(current_step) X(decompose_step) X(current_action) X(advance_plan)       \
  X(react) X(converse) X(alive) X(all_agents) X(with_role) X(team_of)        \
  X(eliminate) X(tally) X(count) X(last) X(random_choice) X(str) X(print)    \
  X(check_win) X(locations) X(locations_by_tag) X(spawn_agents_at)           \
  X(move_to) X(location_of) X(agents_at) X(nearby)

enum Builtin : int {
#define SL_ENUM(name) B_##name,
  SL_BUILTINS(SL_ENUM)
#undef SL_ENUM
  B_COUNT_
};

inline constexpr std::array<std::string_view, B_COUNT_> kBuiltinNames = {
#define SL_NAME(name) #name,
    SL_BUILTINS(SL_NAME)
#undef SL_NAME
};

}  // namespace sl
