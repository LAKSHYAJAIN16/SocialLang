from __future__ import annotations

import heapq
import operator
import random
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from typing import Any, Callable

from . import memory
from .ast_nodes import (
    AssignStmt, Attr, BinaryOp, Block, BoolLit, BreakStmt, Call, DictLit, ExprStmt, ForStmt,
    Identifier, IfStmt, Index, LetStmt, ListLit, NullLit, NumberLit, ReturnStmt, RunStmt,
    SimDecl, StringLit, UnaryOp, WhileStmt,
)
from .parser import parse


class SLRuntimeError(Exception):
    pass


class ReturnSignal(Exception):
    def __init__(self, value: Any):
        self.value = value


class BreakSignal(Exception):
    pass


@dataclass
class Event:
    seq: int
    round: int
    kind: str  # broadcast | whisper | note | ask | reflection
    text: str
    author: str | None
    visible_to: set[str] | None  # None means public
    importance: float = 0.0
    embedding: list[float] | None = None  # cached by memory._embedding_relevances, see there


@dataclass
class Agent:
    seat: str
    role_name: str
    team: str
    model_key: str
    provider: Any
    alive: bool = True
    death_cause: str | None = None
    x: float | None = None
    y: float | None = None
    location_id: str | None = None

    def __hash__(self) -> int:
        return hash(self.seat)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Agent) and other.seat == self.seat

    def __repr__(self) -> str:
        return self.seat


@dataclass
class Location:
    """One procedurally-placed instance of a `world { location <Type> { ... } }`
    declaration -- e.g. `Cafe_0` at (12.3, 44.1). See Interpreter._setup_world.
    """
    id: str
    type_name: str
    tag: str | None
    capacity: int | None
    x: float
    y: float

    def __hash__(self) -> int:
        return hash(self.id)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Location) and other.id == self.id

    def __repr__(self) -> str:
        return self.id


class Env:
    def __init__(self, parent: "Env | None" = None):
        self.vars: dict[str, Any] = {}
        self.parent = parent

    def get(self, name: str) -> Any:
        env: Env | None = self
        while env is not None:
            if name in env.vars:
                return env.vars[name]
            env = env.parent
        raise SLRuntimeError(f"undefined variable '{name}'")

    def set_existing(self, name: str, value: Any) -> None:
        env: Env | None = self
        while env is not None:
            if name in env.vars:
                env.vars[name] = value
                return
            env = env.parent
        raise SLRuntimeError(f"assignment to undefined variable '{name}' (use 'let' to declare it first)")

    def define(self, name: str, value: Any) -> None:
        self.vars[name] = value


def _truthy(v: Any) -> bool:
    if v is None:
        return False
    if isinstance(v, (list, dict, str)):
        return len(v) > 0
    return bool(v)


def assign_agents(sim: SimDecl, roster: dict, rng: random.Random) -> tuple[list[Agent], dict]:
    roles_by_name = {r.name: r for r in sim.roles}
    remainder_roles = [r for r in sim.roles if r.count is None]
    if len(remainder_roles) > 1:
        raise SLRuntimeError("at most one role may use `count: remainder`")

    explicit_total = sum(r.count for r in sim.roles if r.count is not None)
    if remainder_roles:
        drawn_total = rng.randint(sim.agents_min, sim.agents_max) if sim.agents_max > 0 else explicit_total
        remainder_count = max(drawn_total - explicit_total, 0)
    else:
        remainder_count = 0

    role_name_sequence: list[str] = []
    for r in sim.roles:
        n = r.count if r.count is not None else remainder_count
        role_name_sequence.extend([r.name] * n)
    rng.shuffle(role_name_sequence)

    total = len(role_name_sequence)
    if total == 0:
        raise SLRuntimeError("sim has no agents configured -- check role `count`s and `agents:`")

    model_keys = list(roster.keys())
    if not model_keys:
        raise SLRuntimeError("roster is empty -- no runnable models available")
    chosen_keys = rng.sample(model_keys, total) if len(model_keys) >= total else [
        rng.choice(model_keys) for _ in range(total)
    ]

    agents = []
    for i, (role_name, model_key) in enumerate(zip(role_name_sequence, chosen_keys), start=1):
        _spec, provider = roster[model_key]
        role = roles_by_name[role_name]
        agents.append(
            Agent(seat=f"P{i}", role_name=role_name, team=role.team, model_key=model_key, provider=provider)
        )
    return agents, roles_by_name


class Interpreter:
    """Tree-walking interpreter for a parsed `sim` program. One instance runs exactly
    one simulation: construct with the already-cast roster of agents, call run().
    """

    _CMP_OPS: dict[str, Callable[[Any, Any], bool]] = {
        "<": operator.lt, ">": operator.gt, "<=": operator.le, ">=": operator.ge,
    }

    def __init__(
        self,
        sim: SimDecl,
        agents: list[Agent],
        roles_by_name: dict,
        seed: int | None = None,
        embedder: Any = None,
        importance_provider: Any = None,
        sink: Any = None,
    ):
        self.sim = sim
        self.agents = agents
        self.roles = roles_by_name
        # Optional LiveSink (see sociallang/engine/live.py) -- duck-typed
        # (emit_event/emit_agents_snapshot/emit_world/emit_done) so this module keeps
        # no hard dependency on the engine package, same pattern as embedder/
        # importance_provider above.
        self.sink = sink
        self.events: list[Event] = []
        # Per-agent visibility index maintained incrementally in _append_event, so
        # _visible_events_for is O(that agent's own visible count) instead of O(total
        # events) -- the fix that keeps ask()/reflect() cheap as the event log and
        # agent count both grow into the thousands over a long run.
        self._public_events: list[Event] = []
        self._private_events: dict[str, list[Event]] = {}
        self.seq = 0
        self.round = 0
        self.run_log: list[dict] = []
        self.rng = random.Random(seed)
        # Both optional and duck-typed (EmbeddingProvider.embed / ChatProvider.complete)
        # -- see memory.retrieve's `embedder` param and memory.llm_importance -- so this
        # module still has no hard dependency on sociallang/providers.
        self.embedder = embedder
        self.importance_provider = importance_provider

        self.user_fns = {f.name: f for f in sim.fns}
        self.phases = {p.name: p for p in sim.phases}
        self.memory_decls = {m.name: m for m in sim.memory_decls}

        self.global_env = Env()
        self.global_env.define("agents", list(agents))
        self.builtins = self._make_builtins()

        self.world = sim.world
        self.world_locations: dict[str, Location] = {}
        if self.world is not None:
            self._setup_world()

    # -- world / spatial setup --

    def _setup_world(self) -> None:
        """Procedurally scatter `count` instances of each declared location type
        across the world's grid, using the same seeded RNG as everything else so a
        run is fully reproducible under --seed. Rejection-sampling with a minimum
        spacing keeps instances from landing on top of each other; falls back to an
        unchecked random point after a bounded number of attempts so a dense world
        (many locations relative to grid area) can't spin forever.
        """
        w, h = self.world.width, self.world.height
        seen_names: set[str] = set()
        for lt in self.world.location_types:
            if lt.name in seen_names:
                raise SLRuntimeError(
                    f"world {{ }}: duplicate location type '{lt.name}' -- location type names must be unique "
                    "(their instances share an id prefix, so a second declaration silently overwrites the first)"
                )
            seen_names.add(lt.name)
            for i in range(lt.count):
                loc_id = f"{lt.name}_{i}"
                x, y = self._scatter_point(w, h)
                self.world_locations[loc_id] = Location(
                    id=loc_id, type_name=lt.name, tag=lt.tag, capacity=lt.capacity, x=x, y=y,
                )

    def _scatter_point(self, w: int, h: int, min_spacing: float = 1.0, max_attempts: int = 20) -> tuple[float, float]:
        if w <= 0 or h <= 0:
            return 0.0, 0.0
        spacing2 = min_spacing * min_spacing
        for _ in range(max_attempts):
            x, y = self.rng.uniform(0, w), self.rng.uniform(0, h)
            if all((x - loc.x) ** 2 + (y - loc.y) ** 2 >= spacing2 for loc in self.world_locations.values()):
                return x, y
        return self.rng.uniform(0, w), self.rng.uniform(0, h)

    # -- public entrypoint --

    def run(self, max_rounds: int = 200) -> dict:
        if self.sim.loop is None:
            raise SLRuntimeError("sim has no `loop` block")
        if self.sink is not None:
            if self.world is not None:
                self.sink.emit_world(self.world.width, self.world.height, self._world_snapshot())
            self.sink.emit_agents_snapshot(self._agents_snapshot())
        winner = None
        for _ in range(max_rounds):
            self.round += 1
            self.global_env.define("round", float(self.round))
            try:
                self.exec_block(self.sim.loop, Env(self.global_env))
            except ReturnSignal as r:
                winner = r.value
                break
            except BreakSignal:
                break
            if self.sink is not None:
                self.sink.emit_agents_snapshot(self._agents_snapshot())
        if self.sink is not None:
            # A `return` inside the loop body (the common pattern -- see every
            # games/*.sl) raises ReturnSignal and skips the post-round snapshot
            # above, so always send one final snapshot here before "done" -- callers
            # shouldn't have to infer end-of-run agent state from event text alone.
            self.sink.emit_agents_snapshot(self._agents_snapshot())
            self.sink.emit_done(winner, self.round)
        return {
            "winner": winner,
            "rounds": self.round,
            "log": self.run_log,
            "agents": self._agents_snapshot(),
            "world": (
                {"width": self.world.width, "height": self.world.height, "locations": self._world_snapshot()}
                if self.world is not None else None
            ),
        }

    def _agents_snapshot(self) -> list[dict]:
        return [
            {"seat": a.seat, "role": a.role_name, "team": a.team, "model": a.model_key,
             "alive": a.alive, "death_cause": a.death_cause, "x": a.x, "y": a.y, "location_id": a.location_id}
            for a in self.agents
        ]

    def _world_snapshot(self) -> list[dict]:
        return [
            {"id": loc.id, "type": loc.type_name, "tag": loc.tag, "capacity": loc.capacity, "x": loc.x, "y": loc.y}
            for loc in self.world_locations.values()
        ]

    # -- statement execution --

    def exec_block(self, block: Block, env: Env) -> None:
        for stmt in block.statements:
            self.exec_stmt(stmt, env)

    def exec_stmt(self, stmt: Any, env: Env) -> None:
        if isinstance(stmt, LetStmt):
            env.define(stmt.name, self.eval_expr(stmt.value, env))
        elif isinstance(stmt, AssignStmt):
            self._assign(stmt.target, self.eval_expr(stmt.value, env), env)
        elif isinstance(stmt, IfStmt):
            if _truthy(self.eval_expr(stmt.condition, env)):
                self.exec_block(stmt.then_block, Env(env))
            elif stmt.else_block is not None:
                if isinstance(stmt.else_block, IfStmt):
                    self.exec_stmt(stmt.else_block, env)
                else:
                    self.exec_block(stmt.else_block, Env(env))
        elif isinstance(stmt, WhileStmt):
            while _truthy(self.eval_expr(stmt.condition, env)):
                try:
                    self.exec_block(stmt.body, Env(env))
                except BreakSignal:
                    break
        elif isinstance(stmt, ForStmt):
            iterable = self.eval_expr(stmt.iterable, env)
            items = list(iterable.keys()) if isinstance(iterable, dict) else list(iterable)
            for item in items:
                loop_env = Env(env)
                loop_env.define(stmt.var, item)
                try:
                    self.exec_block(stmt.body, loop_env)
                except BreakSignal:
                    break
        elif isinstance(stmt, ReturnStmt):
            raise ReturnSignal(self.eval_expr(stmt.value, env) if stmt.value is not None else None)
        elif isinstance(stmt, BreakStmt):
            raise BreakSignal()
        elif isinstance(stmt, RunStmt):
            phase = self.phases.get(stmt.phase_name)
            if phase is None:
                raise SLRuntimeError(f"no such phase '{stmt.phase_name}'")
            self.exec_block(phase.body, Env(self.global_env))
        elif isinstance(stmt, ExprStmt):
            self.eval_expr(stmt.expr, env)
        else:
            raise SLRuntimeError(f"unknown statement {stmt!r}")

    def _assign(self, target: Any, value: Any, env: Env) -> None:
        if isinstance(target, Identifier):
            env.set_existing(target.name, value)
        elif isinstance(target, Index):
            obj = self.eval_expr(target.obj, env)
            key = self.eval_expr(target.key, env)
            if isinstance(obj, dict):
                obj[key] = value
            elif isinstance(obj, list):
                obj[int(key)] = value
            else:
                raise SLRuntimeError("cannot index-assign into this value")
        else:
            raise SLRuntimeError("invalid assignment target")

    # -- expression evaluation --

    def eval_expr(self, node: Any, env: Env) -> Any:
        if isinstance(node, NumberLit):
            return node.value
        if isinstance(node, StringLit):
            return node.value
        if isinstance(node, BoolLit):
            return node.value
        if isinstance(node, NullLit):
            return None
        if isinstance(node, ListLit):
            return [self.eval_expr(i, env) for i in node.items]
        if isinstance(node, DictLit):
            return {self.eval_expr(k, env): self.eval_expr(v, env) for k, v in node.pairs}
        if isinstance(node, Identifier):
            return env.get(node.name)
        if isinstance(node, UnaryOp):
            if node.op == "not":
                return not _truthy(self.eval_expr(node.operand, env))
            if node.op == "-":
                return -self.eval_expr(node.operand, env)
            raise SLRuntimeError(f"unknown unary operator {node.op}")
        if isinstance(node, BinaryOp):
            return self._eval_binop(node, env)
        if isinstance(node, Attr):
            return self._eval_attr(node, env)
        if isinstance(node, Index):
            obj = self.eval_expr(node.obj, env)
            key = self.eval_expr(node.key, env)
            if isinstance(obj, dict):
                return obj.get(key)
            if isinstance(obj, list):
                return obj[int(key)]
            raise SLRuntimeError("cannot index this value")
        if isinstance(node, Call):
            return self._eval_call(node, env)
        raise SLRuntimeError(f"cannot evaluate {node!r}")

    def _eval_binop(self, node: BinaryOp, env: Env) -> Any:
        if node.op == "and":
            left = self.eval_expr(node.left, env)
            return self.eval_expr(node.right, env) if _truthy(left) else left
        if node.op == "or":
            left = self.eval_expr(node.left, env)
            return left if _truthy(left) else self.eval_expr(node.right, env)

        left = self.eval_expr(node.left, env)
        right = self.eval_expr(node.right, env)
        op = node.op
        if op == "+":
            if isinstance(left, str) or isinstance(right, str):
                if isinstance(left, str) and isinstance(right, str):
                    return left + right
                raise SLRuntimeError("cannot '+' a string with a non-string -- use str() to convert first")
            return left + right
        if op == "-":
            return left - right
        if op == "*":
            return left * right
        if op == "/":
            return left / right
        if op == "%":
            return left % right
        if op == "==":
            return self._values_equal(left, right)
        if op == "!=":
            return not self._values_equal(left, right)
        if op in self._CMP_OPS:
            return self._CMP_OPS[op](left, right)
        raise SLRuntimeError(f"unknown operator {op!r}")

    @staticmethod
    def _values_equal(left: Any, right: Any) -> bool:
        if isinstance(left, Agent) or isinstance(right, Agent):
            return isinstance(left, Agent) and isinstance(right, Agent) and left.seat == right.seat
        if isinstance(left, Location) or isinstance(right, Location):
            return isinstance(left, Location) and isinstance(right, Location) and left.id == right.id
        return left == right

    def _eval_attr(self, node: Attr, env: Env) -> Any:
        obj = self.eval_expr(node.obj, env)
        if isinstance(obj, Agent):
            mapping = {
                "seat": obj.seat, "role": obj.role_name, "team": obj.team,
                "alive": obj.alive, "model": obj.model_key, "death_cause": obj.death_cause,
                "x": obj.x, "y": obj.y, "location": self.world_locations.get(obj.location_id),
            }
        elif isinstance(obj, Event):
            mapping = {
                "text": obj.text, "author": obj.author, "kind": obj.kind,
                "seq": float(obj.seq), "round": float(obj.round), "importance": obj.importance,
            }
        elif isinstance(obj, Location):
            mapping = {
                "id": obj.id, "type": obj.type_name, "tag": obj.tag,
                "capacity": obj.capacity, "x": obj.x, "y": obj.y,
            }
        else:
            raise SLRuntimeError(f"'.{node.name}' is not valid on this kind of value")
        if node.name not in mapping:
            raise SLRuntimeError(f"no attribute '{node.name}' here")
        return mapping[node.name]

    def _eval_call(self, node: Call, env: Env) -> Any:
        if not isinstance(node.callee, Identifier):
            raise SLRuntimeError("only named functions can be called")
        name = node.callee.name
        args = [self.eval_expr(a, env) for a in node.args]
        kwargs = {k: self.eval_expr(v, env) for k, v in node.kwargs.items()}
        if name in self.builtins:
            return self.builtins[name](args, kwargs)
        if name in self.user_fns:
            return self._call_user_fn(name, args)
        raise SLRuntimeError(f"undefined function '{name}'")

    def _call_user_fn(self, name: str, args: list[Any]) -> Any:
        fn = self.user_fns[name]
        env = Env(self.global_env)
        for pname, val in zip(fn.params, args):
            env.define(pname, val)
        try:
            self.exec_block(fn.body, env)
        except ReturnSignal as r:
            return r.value
        return None

    # -- memory / events --

    def _visible_events_for(self, agent: Agent) -> list[Event]:
        private = self._private_events.get(agent.seat)
        if not private:
            return list(self._public_events)
        # Both lists are already in seq order (append-only), so this is a linear
        # merge over just this agent's own visible events, not a scan of the full
        # event log -- see the index maintained in _append_event.
        return list(heapq.merge(self._public_events, private, key=lambda e: e.seq))

    def _append_event(
        self, kind: str, text: str, author: str | None, visible_to: set[str] | None, importance: float | None = None
    ) -> Event:
        self.seq += 1
        if importance is not None:
            imp = importance
        elif self.importance_provider is not None:
            imp = memory.llm_importance(text, self.importance_provider)
        else:
            imp = memory.heuristic_importance(text)
        e = Event(seq=self.seq, round=self.round, kind=kind, text=text, author=author, visible_to=visible_to, importance=imp)
        self.events.append(e)
        if visible_to is None:
            self._public_events.append(e)
        else:
            for seat in visible_to:
                self._private_events.setdefault(seat, []).append(e)
        entry = {"seq": e.seq, "round": e.round, "kind": kind, "text": text, "author": author,
                 "visible_to": sorted(visible_to) if visible_to else None}
        self.run_log.append(entry)
        if self.sink is not None:
            self.sink.emit_event(entry)
        return e

    @staticmethod
    def _render_event(e: Event) -> str:
        prefix = f"[{e.author}] " if e.author else ""
        return f"{prefix}{e.text}"

    def _call_memory(self, name: str, arg_exprs: list[Any], events: list[Event], query: str) -> list[Event]:
        args = [self.eval_expr(a, self.global_env) for a in arg_exprs]

        user_decl = self.memory_decls.get(name)
        if user_decl is not None:
            env = Env(self.global_env)
            env.define("events", events)
            env.define("query", query)
            for pname, pval in zip(user_decl.params, args):
                env.define(pname, pval)
            try:
                self.exec_block(user_decl.body, env)
            except ReturnSignal as r:
                return r.value if isinstance(r.value, list) else events
            return events

        if name == "full_history":
            return events
        if name == "recent":
            n = int(args[0]) if args else 10
            return events[-n:] if n > 0 else []
        if name == "generative":
            k = int(args[0]) if args else 8
            return memory.retrieve(events, query, self.seq, k, embedder=self.embedder)
        raise SLRuntimeError(f"unknown memory strategy '{name}' (not a native kind, no `memory {name}(...) {{ }}` declared)")

    def build_context_for(self, agent: Agent, query: str) -> str:
        role = self.roles[agent.role_name]
        visible = self._visible_events_for(agent)
        selected = visible if role.memory_name is None else self._call_memory(role.memory_name, role.memory_args, visible, query)
        lines = [self._render_event(e) for e in selected]
        return "\n".join(lines) if lines else "(nothing has happened yet)"

    # -- builtins --

    def _stringify(self, v: Any) -> str:
        if isinstance(v, Agent):
            return v.seat
        if isinstance(v, Location):
            return v.id
        if isinstance(v, Event):
            return v.text
        if isinstance(v, bool):
            return "true" if v else "false"
        if v is None:
            return "null"
        if isinstance(v, float):
            return str(int(v)) if v.is_integer() else str(v)
        return str(v)

    def _identity_for(self, agent: Agent, role: "RoleDecl") -> str:
        identity = f"You are {agent.seat}. Your role is {role.name} ({role.team} team)."
        if role.sees == "teammates":
            teammates = [a.seat for a in self.agents if a is not agent and a.role_name == role.name]
            if teammates:
                identity += f" Your teammates are: {', '.join(teammates)}."
        return identity

    def _prompt_pieces(self, agent: Agent, prompt: str) -> tuple[str, str]:
        """Returns (identity, user_prompt) for one ask() call -- shared by the
        single-agent and bulk builtins so their prompt construction can't drift.
        """
        context = self.build_context_for(agent, prompt)
        role = self.roles[agent.role_name]
        identity = self._identity_for(agent, role)
        user_prompt = f"What has happened so far:\n{context}\n\nNow: {prompt}"
        return identity, user_prompt

    @staticmethod
    def _match_option(text: str, options: list[Any], labels: list[str], rng: random.Random) -> Any:
        """Shared by ask_choice and ask_choice_all: exact match first, then
        substring, then a random legal fallback -- never returns something
        outside `options`.
        """
        norm = text.strip().strip(".\"'").lower()
        for o, lab in zip(options, labels):
            if lab.lower() == norm:
                return o
        for o, lab in zip(options, labels):
            if lab.lower() in text.lower():
                return o
        return rng.choice(options) if options else None

    def _bi_ask(self, args: list[Any], kwargs: dict[str, Any]) -> str:
        agent: Agent = args[0]
        prompt: str = args[1]
        temperature = float(kwargs.get("temperature", 0.9))
        max_tokens = int(kwargs.get("max_tokens", 500))

        identity, user_prompt = self._prompt_pieces(agent, prompt)
        resp = agent.provider.complete(identity, user_prompt, temperature=temperature, max_tokens=max_tokens)
        text = resp.text or ""
        self._append_event(kind="ask", text=f"(asked: {prompt}) {text}", author=agent.seat, visible_to={agent.seat})
        return text

    def _bi_ask_choice(self, args: list[Any], kwargs: dict[str, Any]) -> Any:
        agent, prompt, options = args[0], args[1], args[2]
        labels = [self._stringify(o) for o in options]
        full_prompt = f"{prompt}\n\nRespond with exactly one of: {', '.join(labels)}"
        text = self._bi_ask([agent, full_prompt], kwargs)
        return self._match_option(text, options, labels, self.rng)

    def _bi_ask_all(self, args: list[Any], kwargs: dict[str, Any]) -> dict[Agent, str]:
        """Like ask(), but for many agents at once: builds every agent's prompt (pure,
        single-threaded, deterministic, via the same _prompt_pieces() ask() uses),
        then fires all provider.complete() calls concurrently via a thread pool --
        ChatProvider.complete is a stateless, blocking HTTP call with no shared
        mutable state per provider instance, so this is safe with zero provider-side
        changes. This is what makes a few-hundred-agent LLM-tier batch cost roughly
        one round-trip instead of N serial ones. Results are applied to the event
        log in agent-list order (Executor.map preserves input order), not completion
        order, so a run stays reproducible under --seed. Returns {agent: response_text}.
        """
        agents_list: list[Agent] = args[0]
        prompt: str = args[1]
        temperature = float(kwargs.get("temperature", 0.9))
        max_tokens = int(kwargs.get("max_tokens", 500))
        max_workers = max(int(kwargs.get("max_workers", 16)), 1)

        prepared = [(agent, *self._prompt_pieces(agent, prompt)) for agent in agents_list]

        def call_one(item: tuple[Agent, str, str]) -> str:
            _agent, identity, user_prompt = item
            resp = _agent.provider.complete(identity, user_prompt, temperature=temperature, max_tokens=max_tokens)
            return resp.text or ""

        if not prepared:
            return {}
        with ThreadPoolExecutor(max_workers=min(max_workers, len(prepared))) as ex:
            texts = list(ex.map(call_one, prepared))

        results: dict[Agent, str] = {}
        for (agent, _identity, _user_prompt), text in zip(prepared, texts):
            self._append_event(kind="ask", text=f"(asked: {prompt}) {text}", author=agent.seat, visible_to={agent.seat})
            results[agent] = text
        return results

    def _bi_ask_choice_all(self, args: list[Any], kwargs: dict[str, Any]) -> dict[Agent, Any]:
        agents_list: list[Agent] = args[0]
        prompt, options = args[1], args[2]
        labels = [self._stringify(o) for o in options]
        full_prompt = f"{prompt}\n\nRespond with exactly one of: {', '.join(labels)}"
        texts = self._bi_ask_all([agents_list, full_prompt], kwargs)
        return {agent: self._match_option(texts[agent], options, labels, self.rng) for agent in agents_list}

    def _bi_broadcast(self, args: list[Any]) -> None:
        if len(args) == 1:
            self._append_event(kind="broadcast", text=self._stringify(args[0]), author=None, visible_to=None)
        else:
            agent, text = args[0], args[1]
            author = agent.seat if isinstance(agent, Agent) else self._stringify(agent)
            self._append_event(kind="broadcast", text=self._stringify(text), author=author, visible_to=None)

    def _bi_whisper(self, args: list[Any]) -> None:
        who, text = args[0], args[1]
        agents_list = who if isinstance(who, list) else [who]
        seats = {a.seat for a in agents_list}
        self._append_event(kind="whisper", text=self._stringify(text), author=None, visible_to=seats)

    def _bi_remember(self, args: list[Any]) -> None:
        agent, text = args[0], args[1]
        self._append_event(kind="note", text=self._stringify(text), author=agent.seat, visible_to={agent.seat})

    def _bi_reflect(self, args: list[Any]) -> list[str]:
        agent: Agent = args[0]
        visible = self._visible_events_for(agent)
        top = memory.retrieve(visible, "", self.seq, 15, embedder=self.embedder)
        context = "\n".join(self._render_event(e) for e in top)
        role = self.roles[agent.role_name]
        identity = f"You are {agent.seat}. Your role is {role.name} ({role.team} team)."
        user_prompt = (
            "Reflect on what has happened so far and state 1-3 higher-level insights or "
            "conclusions you can draw, one per line, no numbering.\n\n" + context
        )
        resp = agent.provider.complete(identity, user_prompt)
        insights = [line.strip("-* ").strip() for line in (resp.text or "").splitlines() if line.strip()]
        for insight in insights[:3]:
            self._append_event(kind="reflection", text=insight, author=agent.seat, visible_to={agent.seat}, importance=0.9)
        return insights

    def _bi_eliminate(self, args: list[Any], kwargs: dict[str, Any]) -> None:
        agent: Agent = args[0]
        cause = args[1] if len(args) > 1 else kwargs.get("cause")
        agent.alive = False
        agent.death_cause = self._stringify(cause) if cause is not None else None
        text = f"{agent.seat} was eliminated" + (f" ({agent.death_cause})" if agent.death_cause else "") + "."
        self._append_event(kind="broadcast", text=text, author=None, visible_to=None, importance=0.8)

    def _bi_tally(self, args: list[Any]) -> Any:
        votes: dict[Any, Any] = args[0]
        if not votes:
            return None
        buckets: dict[Any, list[Any]] = {}
        for target in votes.values():
            key = target.seat if isinstance(target, Agent) else target
            buckets.setdefault(key, []).append(target)
        best_count = max(len(v) for v in buckets.values())
        winners = [v[0] for v in buckets.values() if len(v) == best_count]
        return self.rng.choice(winners)

    def _bi_spawn_agents_at(self, args: list[Any], kwargs: dict[str, Any]) -> None:
        agents_list: list[Agent] = args[0]
        tag = args[1] if len(args) > 1 else kwargs.get("tag")
        candidates = [loc for loc in self.world_locations.values() if tag is None or loc.tag == tag]
        if not candidates:
            raise SLRuntimeError("spawn_agents_at: no locations match" + (f" tag '{tag}'" if tag else ""))
        for agent in agents_list:
            loc = self.rng.choice(candidates)
            agent.location_id, agent.x, agent.y = loc.id, loc.x, loc.y

    def _bi_move_to(self, args: list[Any]) -> None:
        agent: Agent = args[0]
        loc: Location = args[1]
        agent.location_id, agent.x, agent.y = loc.id, loc.x, loc.y

    def _bi_nearby(self, args: list[Any], kwargs: dict[str, Any]) -> list[Agent]:
        agent: Agent = args[0]
        radius = float(args[1]) if len(args) > 1 else float(kwargs.get("radius", 5.0))
        if agent.x is None:
            return []
        r2 = radius * radius
        return [
            ag for ag in self.agents
            if ag is not agent and ag.x is not None and (ag.x - agent.x) ** 2 + (ag.y - agent.y) ** 2 <= r2
        ]

    def _bi_print(self, value: Any) -> None:
        text = self._stringify(value)
        entry = {"seq": None, "round": self.round, "kind": "print", "text": text, "author": None, "visible_to": None}
        self.run_log.append(entry)
        # Stream to the live sink too (see sociallang/engine/live.py) -- otherwise
        # print() output shows up when a saved run is replayed (it's right there in
        # the JSON's "log") but silently never reaches a --live viewer watching the
        # identical run, which is a confusing divergence between the two viewing modes.
        if self.sink is not None:
            self.sink.emit_event(entry)

    def _check_win(self) -> Any:
        if self.sim.win_condition is None:
            return None
        env = Env(self.global_env)
        try:
            self.exec_block(self.sim.win_condition, env)
        except ReturnSignal as r:
            return r.value
        return None

    def _make_builtins(self) -> dict[str, Callable[[list[Any], dict[str, Any]], Any]]:
        return {
            "ask": lambda a, k: self._bi_ask(a, k),
            "ask_choice": lambda a, k: self._bi_ask_choice(a, k),
            "ask_all": lambda a, k: self._bi_ask_all(a, k),
            "ask_choice_all": lambda a, k: self._bi_ask_choice_all(a, k),
            "broadcast": lambda a, k: self._bi_broadcast(a),
            "whisper": lambda a, k: self._bi_whisper(a),
            "remember": lambda a, k: self._bi_remember(a),
            "reflect": lambda a, k: self._bi_reflect(a),
            "alive": lambda a, k: [ag for ag in self.agents if ag.alive],
            "all_agents": lambda a, k: list(self.agents),
            "with_role": lambda a, k: [ag for ag in self.agents if ag.role_name == a[0]],
            "team_of": lambda a, k: a[0].team,
            "eliminate": lambda a, k: self._bi_eliminate(a, k),
            "tally": lambda a, k: self._bi_tally(a),
            "count": lambda a, k: len(a[0]),
            "last": lambda a, k: (a[0][-int(a[1]):] if int(a[1]) > 0 else []),
            "random_choice": lambda a, k: (self.rng.choice(a[0]) if a[0] else None),
            "str": lambda a, k: self._stringify(a[0]),
            "print": lambda a, k: self._bi_print(a[0]),
            "check_win": lambda a, k: self._check_win(),
            "locations": lambda a, k: list(self.world_locations.values()),
            "locations_by_tag": lambda a, k: [loc for loc in self.world_locations.values() if loc.tag == a[0]],
            "spawn_agents_at": lambda a, k: self._bi_spawn_agents_at(a, k),
            "move_to": lambda a, k: self._bi_move_to(a),
            "location_of": lambda a, k: self.world_locations.get(a[0].location_id),
            "agents_at": lambda a, k: [ag for ag in self.agents if ag.location_id == a[0].id],
            "nearby": lambda a, k: self._bi_nearby(a, k),
        }


def run_source(
    source: str,
    roster: dict,
    seed: int | None = None,
    max_rounds: int = 200,
    embedder: Any = None,
    importance_provider: Any = None,
    sink: Any = None,
) -> dict:
    sim = parse(source)
    rng = random.Random(seed)
    agents, roles_by_name = assign_agents(sim, roster, rng)
    interp = Interpreter(
        sim, agents, roles_by_name, seed=seed, embedder=embedder, importance_provider=importance_provider, sink=sink
    )
    return interp.run(max_rounds=max_rounds)
