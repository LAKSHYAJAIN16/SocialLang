"""Introspects a parsed sim program and exports its roles and memory patterns ("models
and model patterns") as plain JSON -- for tooling that wants the game's shape without
parsing SocialLang itself: the visualizer, external analysis scripts, or a future UI.
"""

from __future__ import annotations

from .lang.ast_nodes import SimDecl


def export_schema(sim: SimDecl) -> dict:
    return {
        "name": sim.name,
        "agents": {"min": sim.agents_min, "max": sim.agents_max},
        "roles": [
            {
                "name": r.name,
                "team": r.team,
                "count": r.count if r.count is not None else "remainder",
                "sees": r.sees,
                "memory": {
                    "pattern": r.memory_name,
                    "args": [_expr_to_plain(a) for a in r.memory_args],
                } if r.memory_name else None,
            }
            for r in sim.roles
        ],
        "memory_patterns": [
            {"name": m.name, "params": m.params, "custom": True} for m in sim.memory_decls
        ] + [
            {"name": name, "params": params, "custom": False}
            for name, params in (("full_history", []), ("recent", ["n"]), ("generative", ["k"]))
            if name not in {m.name for m in sim.memory_decls}  # custom decl shadows the native kind of the same name
        ],
        "phases": [p.name for p in sim.phases],
        "functions": [{"name": f.name, "params": f.params} for f in sim.fns],
        "has_win_condition": sim.win_condition is not None,
    }


def _expr_to_plain(expr) -> object:
    """Best-effort literal-only rendering for memory-strategy args in role decls
    (e.g. the `20` in `memory: recent(20)`) -- non-literal expressions render as None
    since evaluating them requires a running interpreter.
    """
    cls_name = type(expr).__name__
    if cls_name == "NumberLit":
        return expr.value
    if cls_name == "StringLit":
        return expr.value
    if cls_name == "BoolLit":
        return expr.value
    return None
