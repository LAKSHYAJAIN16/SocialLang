from __future__ import annotations

from .ast_nodes import (
    Attr, AssignStmt, BinaryOp, Block, BoolLit, BreakStmt, Call, DictLit, Expr, ExprStmt,
    FnDecl, ForStmt, Identifier, IfStmt, Index, LetStmt, ListLit, MemoryDecl, NullLit,
    NumberLit, PhaseDecl, ReturnStmt, RoleDecl, RunStmt, SimDecl, Stmt, StringLit, UnaryOp,
    WhileStmt,
)
from .lexer import Token, tokenize


class ParseError(Exception):
    pass


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    # -- token stream helpers --

    def _peek(self) -> Token:
        return self.tokens[self.pos]

    def _advance(self) -> Token:
        tok = self.tokens[self.pos]
        if tok.kind != "EOF":
            self.pos += 1
        return tok

    def _check(self, kind: str, value: str | None = None) -> bool:
        tok = self._peek()
        return tok.kind == kind and (value is None or tok.value == value)

    def _match(self, kind: str, value: str | None = None) -> Token | None:
        if self._check(kind, value):
            return self._advance()
        return None

    def _expect(self, kind: str, value: str | None = None) -> Token:
        tok = self._match(kind, value)
        if tok is None:
            cur = self._peek()
            expected = value or kind
            raise ParseError(f"line {cur.line}: expected {expected!r}, got {cur.value!r} ({cur.kind})")
        return tok

    def _is_kw(self, word: str) -> bool:
        return self._check("KEYWORD", word)

    # -- entry point --

    def parse_program(self) -> SimDecl:
        self._expect("KEYWORD", "sim")
        name = self._expect("IDENT").value
        self._expect("SYMBOL", "{")

        sim = SimDecl(name=name, agents_min=0, agents_max=0)
        while not self._check("SYMBOL", "}"):
            if self._is_kw("agents"):
                self._advance()
                self._expect("SYMBOL", ":")
                lo = int(float(self._expect("NUMBER").value))
                hi = lo
                if self._match("SYMBOL", ".."):
                    hi = int(float(self._expect("NUMBER").value))
                sim.agents_min, sim.agents_max = lo, hi
            elif self._is_kw("memory"):
                sim.memory_decls.append(self._parse_memory_decl())
            elif self._is_kw("role"):
                sim.roles.append(self._parse_role_decl())
            elif self._is_kw("fn"):
                sim.fns.append(self._parse_fn_decl())
            elif self._is_kw("phase"):
                sim.phases.append(self._parse_phase_decl())
            elif self._is_kw("win_condition"):
                self._advance()
                sim.win_condition = self._parse_block()
            elif self._is_kw("loop"):
                self._advance()
                sim.loop = self._parse_block()
            else:
                cur = self._peek()
                raise ParseError(f"line {cur.line}: unexpected token {cur.value!r} in sim body")
        self._expect("SYMBOL", "}")
        return sim

    def _parse_memory_decl(self) -> MemoryDecl:
        self._advance()  # 'memory'
        name = self._expect("IDENT").value
        params: list[str] = []
        if self._match("SYMBOL", "("):
            while not self._check("SYMBOL", ")"):
                params.append(self._expect("IDENT").value)
                if not self._match("SYMBOL", ","):
                    break
            self._expect("SYMBOL", ")")
        body = self._parse_block()
        return MemoryDecl(name=name, params=params, body=body)

    def _parse_role_decl(self) -> RoleDecl:
        self._advance()  # 'role'
        name = self._expect("IDENT").value
        self._expect("SYMBOL", "{")

        team = name
        memory_name: str | None = None
        memory_args: list[Expr] = []
        sees = "none"
        count: int | None = None

        while not self._check("SYMBOL", "}"):
            field_name = self._advance().value  # IDENT or KEYWORD (e.g. "memory")
            self._expect("SYMBOL", ":")
            if field_name == "team":
                team = self._expect("STRING").value
            elif field_name == "memory":
                memory_name = self._expect("IDENT").value
                if self._match("SYMBOL", "("):
                    while not self._check("SYMBOL", ")"):
                        memory_args.append(self._parse_expr())
                        if not self._match("SYMBOL", ","):
                            break
                    self._expect("SYMBOL", ")")
            elif field_name == "sees":
                sees = self._advance().value
            elif field_name == "count":
                tok = self._peek()
                if tok.kind == "IDENT" and tok.value == "remainder":
                    self._advance()
                    count = None
                else:
                    count = int(float(self._expect("NUMBER").value))
            else:
                raise ParseError(f"unknown role field {field_name!r}")
            self._match("SYMBOL", ",")

        self._expect("SYMBOL", "}")
        return RoleDecl(name=name, team=team, memory_name=memory_name, memory_args=memory_args, sees=sees, count=count)

    def _parse_fn_decl(self) -> FnDecl:
        self._advance()  # 'fn'
        name = self._expect("IDENT").value
        self._expect("SYMBOL", "(")
        params: list[str] = []
        while not self._check("SYMBOL", ")"):
            params.append(self._expect("IDENT").value)
            if not self._match("SYMBOL", ","):
                break
        self._expect("SYMBOL", ")")
        body = self._parse_block()
        return FnDecl(name=name, params=params, body=body)

    def _parse_phase_decl(self) -> PhaseDecl:
        self._advance()  # 'phase'
        name = self._expect("IDENT").value
        body = self._parse_block()
        return PhaseDecl(name=name, body=body)

    # -- statements --

    def _parse_block(self) -> Block:
        self._expect("SYMBOL", "{")
        statements: list[Stmt] = []
        while not self._check("SYMBOL", "}"):
            statements.append(self._parse_statement())
        self._expect("SYMBOL", "}")
        return Block(statements)

    def _parse_statement(self) -> Stmt:
        if self._is_kw("let"):
            self._advance()
            name = self._expect("IDENT").value
            self._expect("SYMBOL", "=")
            value = self._parse_expr()
            return LetStmt(name, value)
        if self._is_kw("if"):
            return self._parse_if()
        if self._is_kw("while"):
            self._advance()
            cond = self._parse_expr()
            body = self._parse_block()
            return WhileStmt(cond, body)
        if self._is_kw("for"):
            self._advance()
            var = self._expect("IDENT").value
            self._expect("KEYWORD", "in")
            iterable = self._parse_expr()
            body = self._parse_block()
            return ForStmt(var, iterable, body)
        if self._is_kw("return"):
            self._advance()
            if self._check("SYMBOL", "}"):
                return ReturnStmt(None)
            return ReturnStmt(self._parse_expr())
        if self._is_kw("break"):
            self._advance()
            return BreakStmt()
        if self._is_kw("run"):
            self._advance()
            name = self._expect("IDENT").value
            return RunStmt(name)

        expr = self._parse_expr()
        if self._match("SYMBOL", "="):
            value = self._parse_expr()
            return AssignStmt(expr, value)
        return ExprStmt(expr)

    def _parse_if(self) -> IfStmt:
        self._advance()  # 'if'
        cond = self._parse_expr()
        then_block = self._parse_block()
        else_block = None
        if self._match("KEYWORD", "else"):
            if self._is_kw("if"):
                else_block = self._parse_if()
            else:
                else_block = self._parse_block()
        return IfStmt(cond, then_block, else_block)

    # -- expressions (precedence climbing) --

    def _parse_expr(self) -> Expr:
        return self._parse_or()

    def _parse_or(self) -> Expr:
        left = self._parse_and()
        while self._match("KEYWORD", "or"):
            left = BinaryOp("or", left, self._parse_and())
        return left

    def _parse_and(self) -> Expr:
        left = self._parse_not()
        while self._match("KEYWORD", "and"):
            left = BinaryOp("and", left, self._parse_not())
        return left

    def _parse_not(self) -> Expr:
        if self._match("KEYWORD", "not"):
            return UnaryOp("not", self._parse_not())
        return self._parse_comparison()

    _CMP_OPS = {"==", "!=", "<", ">", "<=", ">="}

    def _parse_comparison(self) -> Expr:
        left = self._parse_additive()
        while self._peek().kind == "SYMBOL" and self._peek().value in self._CMP_OPS:
            op = self._advance().value
            left = BinaryOp(op, left, self._parse_additive())
        return left

    def _parse_additive(self) -> Expr:
        left = self._parse_multiplicative()
        while self._peek().kind == "SYMBOL" and self._peek().value in ("+", "-"):
            op = self._advance().value
            left = BinaryOp(op, left, self._parse_multiplicative())
        return left

    def _parse_multiplicative(self) -> Expr:
        left = self._parse_unary()
        while self._peek().kind == "SYMBOL" and self._peek().value in ("*", "/", "%"):
            op = self._advance().value
            left = BinaryOp(op, left, self._parse_unary())
        return left

    def _parse_unary(self) -> Expr:
        if self._match("SYMBOL", "-"):
            return UnaryOp("-", self._parse_unary())
        return self._parse_postfix()

    def _parse_postfix(self) -> Expr:
        expr = self._parse_primary()
        while True:
            if self._match("SYMBOL", "."):
                name = self._expect("IDENT").value
                expr = Attr(expr, name)
            elif self._match("SYMBOL", "("):
                args: list[Expr] = []
                kwargs: dict[str, Expr] = {}
                while not self._check("SYMBOL", ")"):
                    if self._peek().kind == "IDENT" and self.tokens[self.pos + 1].kind == "SYMBOL" \
                            and self.tokens[self.pos + 1].value == "=":
                        key = self._advance().value
                        self._advance()  # '='
                        kwargs[key] = self._parse_expr()
                    else:
                        args.append(self._parse_expr())
                    if not self._match("SYMBOL", ","):
                        break
                self._expect("SYMBOL", ")")
                expr = Call(expr, args, kwargs)
            elif self._match("SYMBOL", "["):
                key = self._parse_expr()
                self._expect("SYMBOL", "]")
                expr = Index(expr, key)
            else:
                break
        return expr

    def _parse_primary(self) -> Expr:
        tok = self._peek()
        if tok.kind == "NUMBER":
            self._advance()
            return NumberLit(float(tok.value))
        if tok.kind == "STRING":
            self._advance()
            return StringLit(tok.value)
        if tok.kind == "KEYWORD" and tok.value == "true":
            self._advance()
            return BoolLit(True)
        if tok.kind == "KEYWORD" and tok.value == "false":
            self._advance()
            return BoolLit(False)
        if tok.kind == "KEYWORD" and tok.value == "null":
            self._advance()
            return NullLit()
        if tok.kind == "IDENT":
            self._advance()
            return Identifier(tok.value)
        if self._match("SYMBOL", "("):
            inner = self._parse_expr()
            self._expect("SYMBOL", ")")
            return inner
        if self._match("SYMBOL", "["):
            items: list[Expr] = []
            while not self._check("SYMBOL", "]"):
                items.append(self._parse_expr())
                if not self._match("SYMBOL", ","):
                    break
            self._expect("SYMBOL", "]")
            return ListLit(items)
        if self._match("SYMBOL", "{"):
            pairs: list[tuple[Expr, Expr]] = []
            while not self._check("SYMBOL", "}"):
                key = self._parse_expr()
                self._expect("SYMBOL", ":")
                value = self._parse_expr()
                pairs.append((key, value))
                if not self._match("SYMBOL", ","):
                    break
            self._expect("SYMBOL", "}")
            return DictLit(pairs)

        raise ParseError(f"line {tok.line}: unexpected token {tok.value!r} ({tok.kind})")


def parse(source: str) -> SimDecl:
    return Parser(tokenize(source)).parse_program()
