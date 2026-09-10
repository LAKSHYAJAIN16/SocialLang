from __future__ import annotations

from dataclasses import dataclass, field


# --- top level -------------------------------------------------------------

@dataclass
class SimDecl:
    name: str
    agents_min: int
    agents_max: int
    roles: list["RoleDecl"] = field(default_factory=list)
    memory_decls: list["MemoryDecl"] = field(default_factory=list)
    fns: list["FnDecl"] = field(default_factory=list)
    phases: list["PhaseDecl"] = field(default_factory=list)
    win_condition: "Block | None" = None
    loop: "Block | None" = None
    world: "WorldDecl | None" = None


@dataclass
class LocationTypeDecl:
    """One kind of place in the world (e.g. `location Cafe { ... }`). The interpreter
    procedurally scatters `count` concrete instances of it at simulation start -- the
    program declares what exists and how many, not where each one sits.
    """
    name: str
    tag: str | None
    capacity: int | None
    count: int


@dataclass
class WorldDecl:
    width: int
    height: int
    location_types: list["LocationTypeDecl"] = field(default_factory=list)


@dataclass
class MemoryDecl:
    """A named, in-language memory pattern: a function of (events, *params) -> list,
    called each turn to build the slice of the shared event log a given agent sees.
    """
    name: str
    params: list[str]
    body: "Block"


@dataclass
class RoleDecl:
    name: str
    team: str
    memory_name: str | None
    memory_args: list["Expr"]
    sees: str  # "teammates" | "all" | "none"
    count: int | None  # None means "remainder"


@dataclass
class FnDecl:
    name: str
    params: list[str]
    body: "Block"


@dataclass
class PhaseDecl:
    name: str
    body: "Block"


# --- statements --------------------------------------------------------

@dataclass
class Block:
    statements: list["Stmt"]


@dataclass
class LetStmt:
    name: str
    value: "Expr"


@dataclass
class AssignStmt:
    target: "Expr"  # Identifier or Index
    value: "Expr"


@dataclass
class IfStmt:
    condition: "Expr"
    then_block: Block
    else_block: "Block | IfStmt | None"


@dataclass
class WhileStmt:
    condition: "Expr"
    body: Block


@dataclass
class ForStmt:
    var: str
    iterable: "Expr"
    body: Block


@dataclass
class ReturnStmt:
    value: "Expr | None"


@dataclass
class BreakStmt:
    pass


@dataclass
class RunStmt:
    phase_name: str


@dataclass
class ExprStmt:
    expr: "Expr"


Stmt = (
    LetStmt | AssignStmt | IfStmt | WhileStmt | ForStmt | ReturnStmt | BreakStmt | RunStmt | ExprStmt
)


# --- expressions -------------------------------------------------------

@dataclass
class NumberLit:
    value: float


@dataclass
class StringLit:
    value: str


@dataclass
class BoolLit:
    value: bool


@dataclass
class NullLit:
    pass


@dataclass
class ListLit:
    items: list["Expr"]


@dataclass
class DictLit:
    pairs: list[tuple["Expr", "Expr"]]


@dataclass
class Identifier:
    name: str


@dataclass
class BinaryOp:
    op: str
    left: "Expr"
    right: "Expr"


@dataclass
class UnaryOp:
    op: str
    operand: "Expr"


@dataclass
class Call:
    callee: "Expr"
    args: list["Expr"]
    kwargs: dict[str, "Expr"]


@dataclass
class Attr:
    obj: "Expr"
    name: str


@dataclass
class Index:
    obj: "Expr"
    key: "Expr"


Expr = (
    NumberLit | StringLit | BoolLit | NullLit | ListLit | DictLit | Identifier
    | BinaryOp | UnaryOp | Call | Attr | Index
)
