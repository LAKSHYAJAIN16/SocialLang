
/* ============================================================================
   SocialLang engine -- ported from sociallang/lang/{lexer,parser,interpreter,
   memory}.py and sociallang/providers/mock_provider.py. See DESIGN.md in the
   repo for the authoritative language reference; this is the same grammar and
   the same builtins, running client-side with a mock LLM provider (a public
   page can't hold real API keys).
============================================================================ */

const KEYWORDS = new Set([
  "sim", "agents", "memory", "role", "fn", "phase", "win_condition", "loop", "world",
  "let", "if", "else", "while", "for", "in", "return", "run", "break",
  "true", "false", "null", "and", "or", "not",
]);

const SYMBOLS = [
  "..", "==", "!=", "<=", ">=",
  "{", "}", "(", ")", "[", "]", ",", ":", ".", "+", "-", "*", "/", "%",
  "<", ">", "=",
];

class LexError extends Error {}

function isDigit(ch) { return ch >= "0" && ch <= "9"; }
function isAlpha(ch) { return (ch >= "a" && ch <= "z") || (ch >= "A" && ch <= "Z"); }
function isAlnum(ch) { return isAlpha(ch) || isDigit(ch); }

function tokenize(source) {
  const tokens = [];
  let i = 0, line = 1;
  const n = source.length;

  while (i < n) {
    const ch = source[i];
    if (ch === "\n") { line += 1; i += 1; continue; }
    if (ch === " " || ch === "\t" || ch === "\r") { i += 1; continue; }
    if (ch === "/" && i + 1 < n && source[i + 1] === "/") {
      while (i < n && source[i] !== "\n") i += 1;
      continue;
    }
    if (ch === '"') {
      let j = i + 1, buf = "";
      while (j < n && source[j] !== '"') {
        if (source[j] === "\\" && j + 1 < n) {
          const esc = source[j + 1];
          const map = { n: "\n", t: "\t", '"': '"', "\\": "\\" };
          buf += Object.prototype.hasOwnProperty.call(map, esc) ? map[esc] : esc;
          j += 2; continue;
        }
        buf += source[j]; j += 1;
      }
      if (j >= n) throw new LexError(`unterminated string starting at line ${line}`);
      tokens.push({ kind: "STRING", value: buf, line });
      i = j + 1; continue;
    }
    if (isDigit(ch)) {
      let j = i;
      while (j < n && isDigit(source[j])) j += 1;
      if (j < n && source[j] === "." && j + 1 < n && isDigit(source[j + 1])) {
        j += 1;
        while (j < n && isDigit(source[j])) j += 1;
      }
      tokens.push({ kind: "NUMBER", value: source.slice(i, j), line });
      i = j; continue;
    }
    if (isAlpha(ch) || ch === "_") {
      let j = i;
      while (j < n && (isAlnum(source[j]) || source[j] === "_")) j += 1;
      const word = source.slice(i, j);
      tokens.push({ kind: KEYWORDS.has(word) ? "KEYWORD" : "IDENT", value: word, line });
      i = j; continue;
    }
    let matched = null;
    for (const s of SYMBOLS) { if (source.startsWith(s, i)) { matched = s; break; } }
    if (matched) { tokens.push({ kind: "SYMBOL", value: matched, line }); i += matched.length; continue; }
    throw new LexError(`unexpected character ${JSON.stringify(ch)} at line ${line}`);
  }
  tokens.push({ kind: "EOF", value: "", line });
  return tokens;
}

class ParseError extends Error {}

class Parser {
  constructor(tokens) { this.tokens = tokens; this.pos = 0; }
  peek() { return this.tokens[this.pos]; }
  advance() { const t = this.tokens[this.pos]; if (t.kind !== "EOF") this.pos += 1; return t; }
  check(kind, value) { const t = this.peek(); return t.kind === kind && (value === undefined || t.value === value); }
  match(kind, value) { if (this.check(kind, value)) return this.advance(); return null; }
  expect(kind, value) {
    const t = this.match(kind, value);
    if (t === null) {
      const cur = this.peek();
      const expected = value !== undefined ? value : kind;
      throw new ParseError(`line ${cur.line}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(cur.value)} (${cur.kind})`);
    }
    return t;
  }
  isKw(w) { return this.check("KEYWORD", w); }

  parseProgram() {
    this.expect("KEYWORD", "sim");
    const name = this.expect("IDENT").value;
    this.expect("SYMBOL", "{");
    const sim = { kind: "SimDecl", name, agentsMin: 0, agentsMax: 0, roles: [], memoryDecls: [], fns: [], phases: [], winCondition: null, loop: null, world: null };
    while (!this.check("SYMBOL", "}")) {
      if (this.isKw("agents")) {
        this.advance(); this.expect("SYMBOL", ":");
        let lo = Math.trunc(parseFloat(this.expect("NUMBER").value));
        let hi = lo;
        if (this.match("SYMBOL", "..")) hi = Math.trunc(parseFloat(this.expect("NUMBER").value));
        sim.agentsMin = lo; sim.agentsMax = hi;
      } else if (this.isKw("memory")) sim.memoryDecls.push(this.parseMemoryDecl());
      else if (this.isKw("role")) sim.roles.push(this.parseRoleDecl());
      else if (this.isKw("fn")) sim.fns.push(this.parseFnDecl());
      else if (this.isKw("phase")) sim.phases.push(this.parsePhaseDecl());
      else if (this.isKw("win_condition")) { this.advance(); sim.winCondition = this.parseBlock(); }
      else if (this.isKw("loop")) { this.advance(); sim.loop = this.parseBlock(); }
      else if (this.isKw("world")) sim.world = this.parseWorldDecl();
      else { const cur = this.peek(); throw new ParseError(`line ${cur.line}: unexpected token ${JSON.stringify(cur.value)} in sim body`); }
    }
    this.expect("SYMBOL", "}");
    return sim;
  }

  parseMemoryDecl() {
    this.advance();
    const name = this.expect("IDENT").value;
    const params = [];
    if (this.match("SYMBOL", "(")) {
      while (!this.check("SYMBOL", ")")) { params.push(this.expect("IDENT").value); if (!this.match("SYMBOL", ",")) break; }
      this.expect("SYMBOL", ")");
    }
    const body = this.parseBlock();
    return { kind: "MemoryDecl", name, params, body };
  }

  parseRoleDecl() {
    this.advance();
    const name = this.expect("IDENT").value;
    this.expect("SYMBOL", "{");
    let team = name, memoryName = null, memoryArgs = [], sees = "none", count = null;
    while (!this.check("SYMBOL", "}")) {
      const fieldName = this.advance().value;
      this.expect("SYMBOL", ":");
      if (fieldName === "team") team = this.expect("STRING").value;
      else if (fieldName === "memory") {
        memoryName = this.expect("IDENT").value;
        if (this.match("SYMBOL", "(")) {
          while (!this.check("SYMBOL", ")")) { memoryArgs.push(this.parseExpr()); if (!this.match("SYMBOL", ",")) break; }
          this.expect("SYMBOL", ")");
        }
      } else if (fieldName === "sees") sees = this.advance().value;
      else if (fieldName === "count") {
        const tok = this.peek();
        if (tok.kind === "IDENT" && tok.value === "remainder") { this.advance(); count = null; }
        else count = Math.trunc(parseFloat(this.expect("NUMBER").value));
      } else throw new ParseError(`unknown role field ${JSON.stringify(fieldName)}`);
      this.match("SYMBOL", ",");
    }
    this.expect("SYMBOL", "}");
    return { kind: "RoleDecl", name, team, memoryName, memoryArgs, sees, count };
  }

  parseWorldDecl() {
    this.advance(); this.expect("SYMBOL", "{");
    let width = 0, height = 0;
    const locationTypes = [];
    while (!this.check("SYMBOL", "}")) {
      if (this.peek().kind === "IDENT" && this.peek().value === "location") { locationTypes.push(this.parseLocationTypeDecl()); continue; }
      const fieldName = this.advance().value;
      this.expect("SYMBOL", ":");
      if (fieldName === "width") width = Math.trunc(parseFloat(this.expect("NUMBER").value));
      else if (fieldName === "height") height = Math.trunc(parseFloat(this.expect("NUMBER").value));
      else throw new ParseError(`unknown world field ${JSON.stringify(fieldName)}`);
      this.match("SYMBOL", ",");
    }
    this.expect("SYMBOL", "}");
    return { kind: "WorldDecl", width, height, locationTypes };
  }

  parseLocationTypeDecl() {
    this.advance();
    const name = this.expect("IDENT").value;
    this.expect("SYMBOL", "{");
    let tag = null, capacity = null, count = 1;
    while (!this.check("SYMBOL", "}")) {
      const fieldName = this.advance().value;
      this.expect("SYMBOL", ":");
      if (fieldName === "tag") tag = this.expect("STRING").value;
      else if (fieldName === "capacity") capacity = Math.trunc(parseFloat(this.expect("NUMBER").value));
      else if (fieldName === "count") count = Math.trunc(parseFloat(this.expect("NUMBER").value));
      else throw new ParseError(`unknown location field ${JSON.stringify(fieldName)}`);
      this.match("SYMBOL", ",");
    }
    this.expect("SYMBOL", "}");
    return { kind: "LocationTypeDecl", name, tag, capacity, count };
  }

  parseFnDecl() {
    this.advance();
    const name = this.expect("IDENT").value;
    this.expect("SYMBOL", "(");
    const params = [];
    while (!this.check("SYMBOL", ")")) { params.push(this.expect("IDENT").value); if (!this.match("SYMBOL", ",")) break; }
    this.expect("SYMBOL", ")");
    const body = this.parseBlock();
    return { kind: "FnDecl", name, params, body };
  }

  parsePhaseDecl() {
    this.advance();
    const name = this.expect("IDENT").value;
    const body = this.parseBlock();
    return { kind: "PhaseDecl", name, body };
  }

  parseBlock() {
    this.expect("SYMBOL", "{");
    const statements = [];
    while (!this.check("SYMBOL", "}")) statements.push(this.parseStatement());
    this.expect("SYMBOL", "}");
    return { kind: "Block", statements };
  }

  parseStatement() {
    if (this.isKw("let")) {
      this.advance();
      const name = this.expect("IDENT").value;
      this.expect("SYMBOL", "=");
      return { kind: "LetStmt", name, value: this.parseExpr() };
    }
    if (this.isKw("if")) return this.parseIf();
    if (this.isKw("while")) {
      this.advance();
      const condition = this.parseExpr();
      return { kind: "WhileStmt", condition, body: this.parseBlock() };
    }
    if (this.isKw("for")) {
      this.advance();
      const varName = this.expect("IDENT").value;
      this.expect("KEYWORD", "in");
      const iterable = this.parseExpr();
      return { kind: "ForStmt", varName, iterable, body: this.parseBlock() };
    }
    if (this.isKw("return")) {
      this.advance();
      if (this.check("SYMBOL", "}")) return { kind: "ReturnStmt", value: null };
      return { kind: "ReturnStmt", value: this.parseExpr() };
    }
    if (this.isKw("break")) { this.advance(); return { kind: "BreakStmt" }; }
    if (this.isKw("run")) { this.advance(); return { kind: "RunStmt", phaseName: this.expect("IDENT").value }; }
    const expr = this.parseExpr();
    if (this.match("SYMBOL", "=")) return { kind: "AssignStmt", target: expr, value: this.parseExpr() };
    return { kind: "ExprStmt", expr };
  }

  parseIf() {
    this.advance();
    const condition = this.parseExpr();
    const thenBlock = this.parseBlock();
    let elseBlock = null;
    if (this.match("KEYWORD", "else")) elseBlock = this.isKw("if") ? this.parseIf() : this.parseBlock();
    return { kind: "IfStmt", condition, thenBlock, elseBlock };
  }

  parseExpr() { return this.parseOr(); }
  parseOr() { let l = this.parseAnd(); while (this.match("KEYWORD", "or")) l = { kind: "BinaryOp", op: "or", left: l, right: this.parseAnd() }; return l; }
  parseAnd() { let l = this.parseNot(); while (this.match("KEYWORD", "and")) l = { kind: "BinaryOp", op: "and", left: l, right: this.parseNot() }; return l; }
  parseNot() { if (this.match("KEYWORD", "not")) return { kind: "UnaryOp", op: "not", operand: this.parseNot() }; return this.parseComparison(); }
  static CMP_OPS = new Set(["==", "!=", "<", ">", "<=", ">="]);
  parseComparison() {
    let l = this.parseAdditive();
    while (this.peek().kind === "SYMBOL" && Parser.CMP_OPS.has(this.peek().value)) { const op = this.advance().value; l = { kind: "BinaryOp", op, left: l, right: this.parseAdditive() }; }
    return l;
  }
  parseAdditive() {
    let l = this.parseMultiplicative();
    while (this.peek().kind === "SYMBOL" && (this.peek().value === "+" || this.peek().value === "-")) { const op = this.advance().value; l = { kind: "BinaryOp", op, left: l, right: this.parseMultiplicative() }; }
    return l;
  }
  parseMultiplicative() {
    let l = this.parseUnary();
    while (this.peek().kind === "SYMBOL" && ["*", "/", "%"].includes(this.peek().value)) { const op = this.advance().value; l = { kind: "BinaryOp", op, left: l, right: this.parseUnary() }; }
    return l;
  }
  parseUnary() { if (this.match("SYMBOL", "-")) return { kind: "UnaryOp", op: "-", operand: this.parseUnary() }; return this.parsePostfix(); }

  parsePostfix() {
    let expr = this.parsePrimary();
    for (;;) {
      if (this.match("SYMBOL", ".")) {
        const tok = this.peek();
        if (tok.kind !== "IDENT" && tok.kind !== "KEYWORD") throw new ParseError(`line ${tok.line}: expected an attribute name after '.', got ${JSON.stringify(tok.value)}`);
        expr = { kind: "Attr", obj: expr, name: this.advance().value };
      } else if (this.match("SYMBOL", "(")) {
        const args = [], kwargs = {};
        while (!this.check("SYMBOL", ")")) {
          const next = this.tokens[this.pos + 1];
          if (this.peek().kind === "IDENT" && next && next.kind === "SYMBOL" && next.value === "=") {
            const key = this.advance().value; this.advance(); kwargs[key] = this.parseExpr();
          } else args.push(this.parseExpr());
          if (!this.match("SYMBOL", ",")) break;
        }
        this.expect("SYMBOL", ")");
        expr = { kind: "Call", callee: expr, args, kwargs };
      } else if (this.match("SYMBOL", "[")) {
        const key = this.parseExpr(); this.expect("SYMBOL", "]");
        expr = { kind: "Index", obj: expr, key };
      } else break;
    }
    return expr;
  }

  parsePrimary() {
    const tok = this.peek();
    if (tok.kind === "NUMBER") { this.advance(); return { kind: "NumberLit", value: parseFloat(tok.value) }; }
    if (tok.kind === "STRING") { this.advance(); return { kind: "StringLit", value: tok.value }; }
    if (tok.kind === "KEYWORD" && tok.value === "true") { this.advance(); return { kind: "BoolLit", value: true }; }
    if (tok.kind === "KEYWORD" && tok.value === "false") { this.advance(); return { kind: "BoolLit", value: false }; }
    if (tok.kind === "KEYWORD" && tok.value === "null") { this.advance(); return { kind: "NullLit" }; }
    if (tok.kind === "IDENT") { this.advance(); return { kind: "Identifier", name: tok.value }; }
    if (this.match("SYMBOL", "(")) { const inner = this.parseExpr(); this.expect("SYMBOL", ")"); return inner; }
    if (this.match("SYMBOL", "[")) {
      const items = [];
      while (!this.check("SYMBOL", "]")) { items.push(this.parseExpr()); if (!this.match("SYMBOL", ",")) break; }
      this.expect("SYMBOL", "]");
      return { kind: "ListLit", items };
    }
    if (this.match("SYMBOL", "{")) {
      const pairs = [];
      while (!this.check("SYMBOL", "}")) {
        const key = this.parseExpr(); this.expect("SYMBOL", ":"); const value = this.parseExpr();
        pairs.push([key, value]);
        if (!this.match("SYMBOL", ",")) break;
      }
      this.expect("SYMBOL", "}");
      return { kind: "DictLit", pairs };
    }
    throw new ParseError(`line ${tok.line}: unexpected token ${JSON.stringify(tok.value)} (${tok.kind})`);
  }
}

function parse(source) { return new Parser(tokenize(source)).parseProgram(); }

const IMPORTANT_WORDS = new Set(["eliminate", "eliminated", "kill", "killed", "vote", "voted", "accuse", "accused", "win", "won", "lose", "lost", "betray", "betrayed", "suspicious", "suspect", "mafia", "investigate", "investigated", "protect", "protected", "die", "died", "lied", "lying", "trust", "distrust", "reveal", "revealed"]);
const STOPWORDS = new Set(["a", "an", "the", "is", "are", "was", "were", "be", "been", "being", "to", "of", "in", "on", "at", "for", "and", "or", "but", "not", "this", "that", "it", "as", "by", "with", "who", "what", "when", "where", "why", "how", "do", "does", "did", "you", "your", "i"]);
function findWords(text) { return String(text).toLowerCase().match(/[a-z']+/g) || []; }
function heuristicImportance(text) {
  const words = findWords(text);
  if (words.length === 0) return 0.0;
  const hits = words.filter((w) => IMPORTANT_WORDS.has(w)).length;
  const density = hits / words.length;
  const lengthFactor = Math.min(words.length / 40.0, 1.0);
  return Math.max(0.0, Math.min(1.0, 0.3 * lengthFactor + 0.7 * Math.min(density * 5, 1.0)));
}
function lexicalRelevance(query, text) {
  const q = new Set(findWords(query).filter((w) => !STOPWORDS.has(w)));
  const t = new Set(findWords(text).filter((w) => !STOPWORDS.has(w)));
  if (q.size === 0 || t.size === 0) return 0.0;
  let inter = 0; for (const w of q) if (t.has(w)) inter += 1;
  return inter / new Set([...q, ...t]).size;
}
function recencyScore(eventSeq, nowSeq, decay = 0.995) { return Math.pow(decay, Math.max(nowSeq - eventSeq, 0)); }
function retrieveMemory(events, query, nowSeq, k) {
  const scored = events.map((e) => [recencyScore(e.seq, nowSeq) + e.importance + lexicalRelevance(query, e.text), e]);
  scored.sort((a, b) => b[0] - a[0]);
  const top = scored.slice(0, Math.max(Math.trunc(k), 0)).map((p) => p[1]);
  top.sort((a, b) => a.seq - b.seq);
  return top;
}

// Cheap close condition for converse()'s turn loop -- mirrors _looks_like_farewell
// in sociallang/lang/interpreter.py.
const FAREWELL_WORDS = ["bye", "goodbye", "good bye", "see you", "farewell", "gotta go", "have to go", "talk later", "talk to you later", "catch you later"];
function looksLikeFarewell(line) {
  const lowered = line.toLowerCase();
  return FAREWELL_WORDS.some((w) => lowered.includes(w));
}

class ProviderResponse { constructor(text) { this.text = text; } }
const OPTIONS_RE = /Respond with exactly one of:\s*(.+)/;
const FREE_TEXT_POOL = ["I'm not sure yet, let's see how this plays out.", "Something about this doesn't add up to me.", "I'll go along with the group for now.", "Let's hear more before anyone decides anything."];
class MockProvider {
  complete(_systemPrompt, userPrompt) {
    const match = OPTIONS_RE.exec(userPrompt);
    if (match) {
      const options = match[1].split(",").map((s) => s.trim()).filter((s) => s.length > 0);
      if (options.length > 0) return new ProviderResponse(options[Math.floor(Math.random() * options.length)]);
    }
    return new ProviderResponse(FREE_TEXT_POOL[Math.floor(Math.random() * FREE_TEXT_POOL.length)]);
  }
}

class SeededRandom {
  constructor(seed) { this._state = (seed === undefined || seed === null ? Math.floor(Math.random() * 4294967296) : seed) >>> 0; }
  _next() {
    this._state = (this._state + 0x6d2b79f5) >>> 0;
    let t = this._state;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }
  random() { return this._next(); }
  uniform(a, b) { return a + (b - a) * this._next(); }
  randint(a, b) { return a + Math.floor(this._next() * (b - a + 1)); }
  choice(list) { return list[Math.floor(this._next() * list.length)]; }
  shuffle(list) { for (let i = list.length - 1; i > 0; i -= 1) { const j = Math.floor(this._next() * (i + 1)); const t = list[i]; list[i] = list[j]; list[j] = t; } return list; }
  sample(list, k) { const c = list.slice(); this.shuffle(c); return c.slice(0, k); }
}

class SLRuntimeError extends Error {}
class ReturnSignal { constructor(value) { this.value = value; } }
class BreakSignal {}

class Agent {
  constructor(seat, roleName, team, modelKey, provider) {
    this.seat = seat; this.roleName = roleName; this.team = team; this.modelKey = modelKey; this.provider = provider;
    this.alive = true; this.deathCause = null; this.x = null; this.y = null; this.locationId = null;
    // Generative Agents (Park et al. 2023) state, beyond the memory stream itself --
    // mirrors sociallang/lang/interpreter.py's Agent dataclass field-for-field.
    this.persona = ""; this.plan = []; this.planCursor = 0;
    this.subplan = []; this.subplanCursor = 0; this.lastReflectSeq = 0;
  }
}
class Location {
  constructor(id, typeName, tag, capacity, x, y) { this.id = id; this.typeName = typeName; this.tag = tag; this.capacity = capacity; this.x = x; this.y = y; }
}
class EventRec {
  constructor(seq, round, kind, text, author, visibleTo, importance = 0.0) {
    this.seq = seq; this.round = round; this.kind = kind; this.text = text; this.author = author; this.visibleTo = visibleTo; this.importance = importance;
  }
}
class Env {
  constructor(parent = null) { this.vars = new Map(); this.parent = parent; }
  get(name) { let e = this; while (e) { if (e.vars.has(name)) return e.vars.get(name); e = e.parent; } throw new SLRuntimeError(`undefined variable '${name}'`); }
  setExisting(name, value) { let e = this; while (e) { if (e.vars.has(name)) { e.vars.set(name, value); return; } e = e.parent; } throw new SLRuntimeError(`assignment to undefined variable '${name}' (use 'let' to declare it first)`); }
  define(name, value) { this.vars.set(name, value); }
}
function isTruthy(v) {
  if (v === null || v === undefined) return false;
  if (Array.isArray(v)) return v.length > 0;
  if (v instanceof Map) return v.size > 0;
  if (typeof v === "string") return v.length > 0;
  return Boolean(v);
}
function valuesEqual(left, right) {
  if (left instanceof Agent || right instanceof Agent) return left instanceof Agent && right instanceof Agent && left.seat === right.seat;
  if (left instanceof Location || right instanceof Location) return left instanceof Location && right instanceof Location && left.id === right.id;
  // Python's == is structural for lists/dicts (two distinct-but-equal-content
  // lists compare equal); matching that instead of falling back to JS reference
  // equality for Array/Map.
  if (Array.isArray(left) && Array.isArray(right)) {
    if (left.length !== right.length) return false;
    for (let i = 0; i < left.length; i += 1) if (!valuesEqual(left[i], right[i])) return false;
    return true;
  }
  if (left instanceof Map && right instanceof Map) {
    if (left.size !== right.size) return false;
    for (const [k, v] of left) {
      if (!right.has(k) || !valuesEqual(v, right.get(k))) return false;
    }
    return true;
  }
  return left === right;
}

// Python-style list indexing: obj[-1] means "last element." Negative indices
// count from the end (still bounds-checked), matching how `obj[int(key)]`
// behaves on a native Python list in the reference interpreter.
function pyListIndex(list, key) {
  let idx = Math.trunc(key);
  if (idx < 0) idx += list.length;
  if (idx < 0 || idx >= list.length) throw new SLRuntimeError("list index out of range");
  return idx;
}

function assignAgents(sim, roster, rng) {
  const rolesByName = new Map(sim.roles.map((r) => [r.name, r]));
  const remainderRoles = sim.roles.filter((r) => r.count === null);
  if (remainderRoles.length > 1) throw new SLRuntimeError("at most one role may use `count: remainder`");
  const explicitTotal = sim.roles.filter((r) => r.count !== null).reduce((s, r) => s + r.count, 0);
  let remainderCount = 0;
  if (remainderRoles.length > 0) {
    const drawnTotal = sim.agentsMax > 0 ? rng.randint(sim.agentsMin, sim.agentsMax) : explicitTotal;
    remainderCount = Math.max(drawnTotal - explicitTotal, 0);
  }
  const roleNameSequence = [];
  for (const r of sim.roles) { const n = r.count !== null ? r.count : remainderCount; for (let i = 0; i < n; i += 1) roleNameSequence.push(r.name); }
  rng.shuffle(roleNameSequence);
  const total = roleNameSequence.length;
  if (total === 0) throw new SLRuntimeError("sim has no agents configured -- check role `count`s and `agents:`");
  const modelKeys = Array.from(roster.keys());
  if (modelKeys.length === 0) throw new SLRuntimeError("roster is empty -- no runnable models available");
  const chosenKeys = modelKeys.length >= total ? rng.sample(modelKeys, total) : Array.from({ length: total }, () => rng.choice(modelKeys));
  const agents = [];
  for (let i = 0; i < total; i += 1) {
    const roleName = roleNameSequence[i], modelKey = chosenKeys[i];
    const provider = roster.get(modelKey), role = rolesByName.get(roleName);
    agents.push(new Agent(`P${i + 1}`, roleName, role.team, modelKey, provider));
  }
  return { agents, rolesByName };
}

// Free functions (not Interpreter methods) so the Sandbox UI can preview/edit a
// world's layout before a run even starts, using the exact same placement logic
// the interpreter itself uses -- one source of truth, no risk of the preview
// drifting from what actually happens when you hit Run.
function scatterPoint(placed, w, h, rng, minSpacing = 1.0, maxAttempts = 20) {
  if (w <= 0 || h <= 0) return [0.0, 0.0];
  const spacing2 = minSpacing * minSpacing;
  for (let a = 0; a < maxAttempts; a += 1) {
    const x = rng.uniform(0, w), y = rng.uniform(0, h);
    let ok = true;
    for (const loc of placed.values()) { const dx = x - loc.x, dy = y - loc.y; if (dx * dx + dy * dy < spacing2) { ok = false; break; } }
    if (ok) return [x, y];
  }
  return [rng.uniform(0, w), rng.uniform(0, h)];
}

function setupWorldLocations(worldDecl, rng) {
  const locations = new Map();
  const seenNames = new Set();
  const { width: w, height: h, locationTypes } = worldDecl;
  for (const lt of locationTypes) {
    if (seenNames.has(lt.name)) {
      throw new SLRuntimeError(`world { }: duplicate location type '${lt.name}' -- location type names must be unique (their instances share an id prefix, so a second declaration silently overwrites the first)`);
    }
    seenNames.add(lt.name);
    for (let i = 0; i < lt.count; i += 1) {
      const locId = `${lt.name}_${i}`;
      const [x, y] = scatterPoint(locations, w, h, rng);
      locations.set(locId, new Location(locId, lt.name, lt.tag, lt.capacity, x, y));
    }
  }
  return locations;
}

class Interpreter {
  constructor(sim, agents, rolesByName, opts = {}) {
    this.sim = sim; this.agents = agents; this.roles = rolesByName;
    this.sink = opts.sink || null;
    this.events = []; this.publicEvents = []; this.privateEvents = new Map();
    this.seq = 0; this.round = 0; this.runLog = [];
    this.rng = opts.rng || new SeededRandom(opts.seed);
    this.userFns = new Map(sim.fns.map((f) => [f.name, f]));
    this.phases = new Map(sim.phases.map((p) => [p.name, p]));
    this.memoryDecls = new Map(sim.memoryDecls.map((m) => [m.name, m]));
    this.globalEnv = new Env();
    this.globalEnv.define("agents", this.agents.slice());
    this.builtins = this._makeBuiltins();
    this.world = sim.world;
    // opts.worldLocations lets a caller (the Sandbox UI, after the user drags
    // markers around) override the procedurally-scattered layout with specific
    // positions -- everything downstream (nearby(), move_to(), agents_at()) just
    // reads from this.worldLocations either way, so a dragged layout genuinely
    // changes gameplay, not just the picture.
    this.worldLocations = opts.worldLocations
      ? new Map(opts.worldLocations)
      : (this.world ? setupWorldLocations(this.world, this.rng) : new Map());
  }

  // Advances exactly one round (one execution of sim.loop's body) and returns
  // { done, winner }. Exists as its own method -- not just inlined into run()'s
  // for-loop -- so a caller (the Sandbox's interactive Step mode) can drive one
  // round at a time on a live Interpreter instance, e.g. to let a location drag
  // between steps actually change what the NEXT round's nearby()/move_to() see.
  stepOnce() {
    if (!this.sim.loop) throw new SLRuntimeError("sim has no `loop` block");
    this.round += 1;
    this.globalEnv.define("round", this.round);
    let done = false, winner = null;
    try { this.execBlock(this.sim.loop, new Env(this.globalEnv)); }
    catch (e) {
      if (e instanceof ReturnSignal) { done = true; winner = e.value; }
      else if (e instanceof BreakSignal) { done = true; }
      else throw e;
    }
    if (this.sink) this.sink.onAgents(this._agentsSnapshot());
    return { done, winner };
  }

  run(maxRounds = 200, onProgress = null) {
    if (this.sink) { if (this.world) this.sink.onWorld(this._worldSnapshot()); this.sink.onAgents(this._agentsSnapshot()); }
    let winner = null;
    for (let i = 0; i < maxRounds; i += 1) {
      const step = this.stepOnce();
      if (onProgress) onProgress(this.round);
      if (step.done) { winner = step.winner; break; }
    }
    if (this.sink) this.sink.onDone(winner, this.round);
    return {
      winner, rounds: this.round, log: this.runLog, agents: this._agentsSnapshot(),
      world: this.world ? { width: this.world.width, height: this.world.height, locations: this._worldSnapshot() } : null,
    };
  }

  // plan entries are Maps (see evalExpr's DictLit case) since that's how every
  // in-language dict literal is represented -- converted to plain objects here
  // so a snapshot is always ordinary JSON-shaped data for any consumer (this
  // sandbox included), not a mix of Maps and objects depending on how deep you
  // look.
  _agentsSnapshot() {
    return this.agents.map((a) => ({
      seat: a.seat, role: a.roleName, team: a.team, model: a.modelKey,
      alive: a.alive, deathCause: a.deathCause, x: a.x, y: a.y, locationId: a.locationId,
      persona: a.persona, subplan: a.subplan.slice(), subplanCursor: a.subplanCursor,
      plan: a.plan.map((step) => (step instanceof Map ? Object.fromEntries(step) : step)),
      planCursor: a.planCursor,
    }));
  }
  _worldSnapshot() { return Array.from(this.worldLocations.values()).map((loc) => ({ id: loc.id, type: loc.typeName, tag: loc.tag, capacity: loc.capacity, x: loc.x, y: loc.y })); }
  getAgentsSnapshot() { return this._agentsSnapshot(); }
  getWorldSnapshot() { return this.world ? { width: this.world.width, height: this.world.height, locations: this._worldSnapshot() } : null; }

  execBlock(block, env) { for (const stmt of block.statements) this.execStmt(stmt, env); }

  execStmt(stmt, env) {
    switch (stmt.kind) {
      case "LetStmt": env.define(stmt.name, this.evalExpr(stmt.value, env)); return;
      case "AssignStmt": this._assign(stmt.target, this.evalExpr(stmt.value, env), env); return;
      case "IfStmt":
        if (isTruthy(this.evalExpr(stmt.condition, env))) this.execBlock(stmt.thenBlock, new Env(env));
        else if (stmt.elseBlock !== null) { if (stmt.elseBlock.kind === "IfStmt") this.execStmt(stmt.elseBlock, env); else this.execBlock(stmt.elseBlock, new Env(env)); }
        return;
      case "WhileStmt":
        while (isTruthy(this.evalExpr(stmt.condition, env))) { try { this.execBlock(stmt.body, new Env(env)); } catch (e) { if (e instanceof BreakSignal) break; throw e; } }
        return;
      case "ForStmt": {
        const iterable = this.evalExpr(stmt.iterable, env);
        const items = iterable instanceof Map ? Array.from(iterable.keys()) : iterable.slice();
        for (const item of items) {
          const loopEnv = new Env(env); loopEnv.define(stmt.varName, item);
          try { this.execBlock(stmt.body, loopEnv); } catch (e) { if (e instanceof BreakSignal) break; throw e; }
        }
        return;
      }
      case "ReturnStmt": throw new ReturnSignal(stmt.value !== null ? this.evalExpr(stmt.value, env) : null);
      case "BreakStmt": throw new BreakSignal();
      case "RunStmt": {
        const phase = this.phases.get(stmt.phaseName);
        if (!phase) throw new SLRuntimeError(`no such phase '${stmt.phaseName}'`);
        this.execBlock(phase.body, new Env(this.globalEnv));
        return;
      }
      case "ExprStmt": this.evalExpr(stmt.expr, env); return;
      default: throw new SLRuntimeError(`unknown statement ${stmt.kind}`);
    }
  }

  _assign(target, value, env) {
    if (target.kind === "Identifier") { env.setExisting(target.name, value); return; }
    if (target.kind === "Index") {
      const obj = this.evalExpr(target.obj, env), key = this.evalExpr(target.key, env);
      if (obj instanceof Map) obj.set(key, value);
      else if (Array.isArray(obj)) { obj[pyListIndex(obj, key)] = value; }
      else throw new SLRuntimeError("cannot index-assign into this value");
      return;
    }
    throw new SLRuntimeError("invalid assignment target");
  }

  evalExpr(node, env) {
    switch (node.kind) {
      case "NumberLit": return node.value;
      case "StringLit": return node.value;
      case "BoolLit": return node.value;
      case "NullLit": return null;
      case "ListLit": return node.items.map((i) => this.evalExpr(i, env));
      case "DictLit": { const m = new Map(); for (const [k, v] of node.pairs) m.set(this.evalExpr(k, env), this.evalExpr(v, env)); return m; }
      case "Identifier": return env.get(node.name);
      case "UnaryOp":
        if (node.op === "not") return !isTruthy(this.evalExpr(node.operand, env));
        if (node.op === "-") return -this.evalExpr(node.operand, env);
        throw new SLRuntimeError(`unknown unary operator ${node.op}`);
      case "BinaryOp": return this._evalBinop(node, env);
      case "Attr": return this._evalAttr(node, env);
      case "Index": {
        const obj = this.evalExpr(node.obj, env), key = this.evalExpr(node.key, env);
        if (obj instanceof Map) return obj.has(key) ? obj.get(key) : null;
        if (Array.isArray(obj)) { return obj[pyListIndex(obj, key)]; }
        throw new SLRuntimeError("cannot index this value");
      }
      case "Call": return this._evalCall(node, env);
      default: throw new SLRuntimeError(`cannot evaluate ${node.kind}`);
    }
  }

  _evalBinop(node, env) {
    if (node.op === "and") { const l = this.evalExpr(node.left, env); return isTruthy(l) ? this.evalExpr(node.right, env) : l; }
    if (node.op === "or") { const l = this.evalExpr(node.left, env); return isTruthy(l) ? l : this.evalExpr(node.right, env); }
    const left = this.evalExpr(node.left, env), right = this.evalExpr(node.right, env);
    switch (node.op) {
      case "+":
        if (typeof left === "string" || typeof right === "string") {
          if (typeof left === "string" && typeof right === "string") return left + right;
          throw new SLRuntimeError("cannot '+' a string with a non-string -- use str() to convert first");
        }
        return left + right;
      case "-": return left - right;
      case "*": return left * right;
      case "/": return left / right;
      case "%": return ((left % right) + right) % right; // Python floor-mod: sign follows the divisor, not JS's (sign follows the dividend)
      case "==": return valuesEqual(left, right);
      case "!=": return !valuesEqual(left, right);
      case "<": return left < right;
      case ">": return left > right;
      case "<=": return left <= right;
      case ">=": return left >= right;
      default: throw new SLRuntimeError(`unknown operator ${node.op}`);
    }
  }

  _evalAttr(node, env) {
    const obj = this.evalExpr(node.obj, env);
    let mapping;
    if (obj instanceof Agent) mapping = { seat: obj.seat, role: obj.roleName, team: obj.team, alive: obj.alive, model: obj.modelKey, death_cause: obj.deathCause, x: obj.x, y: obj.y, location: obj.locationId ? (this.worldLocations.get(obj.locationId) || null) : null, persona: obj.persona, plan: obj.plan, subplan: obj.subplan };
    else if (obj instanceof EventRec) mapping = { text: obj.text, author: obj.author, kind: obj.kind, seq: obj.seq, round: obj.round, importance: obj.importance };
    else if (obj instanceof Location) mapping = { id: obj.id, type: obj.typeName, tag: obj.tag, capacity: obj.capacity, x: obj.x, y: obj.y };
    else throw new SLRuntimeError(`'.${node.name}' is not valid on this kind of value`);
    if (!(node.name in mapping)) throw new SLRuntimeError(`no attribute '${node.name}' here`);
    return mapping[node.name];
  }

  _evalCall(node, env) {
    if (node.callee.kind !== "Identifier") throw new SLRuntimeError("only named functions can be called");
    const name = node.callee.name;
    const args = node.args.map((a) => this.evalExpr(a, env));
    const kwargs = {};
    for (const k of Object.keys(node.kwargs)) kwargs[k] = this.evalExpr(node.kwargs[k], env);
    if (Object.prototype.hasOwnProperty.call(this.builtins, name)) return this.builtins[name](args, kwargs);
    if (this.userFns.has(name)) return this._callUserFn(name, args);
    throw new SLRuntimeError(`undefined function '${name}'`);
  }

  _callUserFn(name, args) {
    const fn = this.userFns.get(name);
    const env = new Env(this.globalEnv);
    for (let i = 0; i < fn.params.length; i += 1) if (i < args.length) env.define(fn.params[i], args[i]);
    try { this.execBlock(fn.body, env); } catch (e) { if (e instanceof ReturnSignal) return e.value; throw e; }
    return null;
  }

  _visibleEventsFor(agent) {
    const priv = this.privateEvents.get(agent.seat);
    if (!priv || priv.length === 0) return this.publicEvents.slice();
    const merged = []; let i = 0, j = 0;
    while (i < this.publicEvents.length && j < priv.length) {
      if (this.publicEvents[i].seq <= priv[j].seq) { merged.push(this.publicEvents[i]); i += 1; }
      else { merged.push(priv[j]); j += 1; }
    }
    while (i < this.publicEvents.length) { merged.push(this.publicEvents[i]); i += 1; }
    while (j < priv.length) { merged.push(priv[j]); j += 1; }
    return merged;
  }

  _appendEvent(kind, text, author, visibleTo, importance = null) {
    this.seq += 1;
    const imp = importance !== null ? importance : heuristicImportance(text);
    const e = new EventRec(this.seq, this.round, kind, text, author, visibleTo, imp);
    this.events.push(e);
    if (visibleTo === null) this.publicEvents.push(e);
    else for (const seat of visibleTo) { if (!this.privateEvents.has(seat)) this.privateEvents.set(seat, []); this.privateEvents.get(seat).push(e); }
    const entry = { seq: e.seq, round: e.round, kind, text, author, visibleTo: visibleTo && visibleTo.size > 0 ? Array.from(visibleTo).sort() : null };
    this.runLog.push(entry);
    if (this.sink) this.sink.onEvent(entry);
    return e;
  }

  static _renderEvent(e) { return `${e.author ? `[${e.author}] ` : ""}${e.text}`; }

  _callMemory(name, argExprs, events, query) {
    const args = argExprs.map((a) => this.evalExpr(a, this.globalEnv));
    const userDecl = this.memoryDecls.get(name);
    if (userDecl) {
      const env = new Env(this.globalEnv);
      env.define("events", events); env.define("query", query);
      for (let i = 0; i < userDecl.params.length; i += 1) if (i < args.length) env.define(userDecl.params[i], args[i]);
      try { this.execBlock(userDecl.body, env); } catch (e) { if (e instanceof ReturnSignal) return Array.isArray(e.value) ? e.value : events; throw e; }
      return events;
    }
    if (name === "full_history") return events;
    if (name === "recent") { const n = args.length > 0 ? Math.trunc(args[0]) : 10; return n > 0 ? events.slice(Math.max(events.length - n, 0)) : []; }
    if (name === "generative") { const k = args.length > 0 ? Math.trunc(args[0]) : 8; return retrieveMemory(events, query, this.seq, k); }
    throw new SLRuntimeError(`unknown memory strategy '${name}' (not a native kind, no \`memory ${name}(...) { }\` declared)`);
  }

  buildContextFor(agent, query) {
    const role = this.roles.get(agent.roleName);
    const visible = this._visibleEventsFor(agent);
    const selected = role.memoryName === null ? visible : this._callMemory(role.memoryName, role.memoryArgs, visible, query);
    const lines = selected.map((e) => Interpreter._renderEvent(e));
    return lines.length > 0 ? lines.join("\n") : "(nothing has happened yet)";
  }

  _stringify(v) {
    if (v instanceof Agent) return v.seat;
    if (v instanceof Location) return v.id;
    if (v instanceof EventRec) return v.text;
    if (typeof v === "boolean") return v ? "true" : "false";
    if (v === null || v === undefined) return "null";
    return String(v);
  }

  _identityFor(agent, role) {
    let identity = `You are ${agent.seat}. Your role is ${role.name} (${role.team} team).`;
    if (agent.persona) identity += ` ${agent.persona}`;
    if (role.sees === "teammates") {
      const teammates = this.agents.filter((a) => a !== agent && a.roleName === role.name).map((a) => a.seat);
      if (teammates.length > 0) identity += ` Your teammates are: ${teammates.join(", ")}.`;
    }
    return identity;
  }

  // Shared by the Generative Agents builtins below (make_plan/decompose_step/
  // react/converse) so their prompt construction can't drift from ask()'s -- same
  // role as _prompt_pieces in the Python reference.
  _promptPieces(agent, prompt) {
    const context = this.buildContextFor(agent, prompt);
    const role = this.roles.get(agent.roleName);
    const identity = this._identityFor(agent, role);
    const userPrompt = `What has happened so far:\n${context}\n\nNow: ${prompt}`;
    return [identity, userPrompt];
  }

  _biAsk(args, kwargs) {
    const [agent, prompt] = args;
    const temperature = kwargs.temperature !== undefined ? kwargs.temperature : 0.9;
    const maxTokens = kwargs.max_tokens !== undefined ? kwargs.max_tokens : 500;
    const context = this.buildContextFor(agent, prompt);
    const role = this.roles.get(agent.roleName);
    const identity = this._identityFor(agent, role);
    const userPrompt = `What has happened so far:\n${context}\n\nNow: ${prompt}`;
    const resp = agent.provider.complete(identity, userPrompt, temperature, maxTokens);
    const text = resp.text || "";
    this._appendEvent("ask", `(asked: ${prompt}) ${text}`, agent.seat, new Set([agent.seat]));
    return text;
  }

  static _matchOption(text, options, labels, rng) {
    const norm = text.trim().replace(/^[."']+|[."']+$/g, "").toLowerCase();
    for (let i = 0; i < options.length; i += 1) if (labels[i].toLowerCase() === norm) return options[i];
    for (let i = 0; i < options.length; i += 1) if (text.toLowerCase().includes(labels[i].toLowerCase())) return options[i];
    return options.length > 0 ? rng.choice(options) : null;
  }

  _biAskChoice(args, kwargs) {
    const [agent, prompt, options] = args;
    const labels = options.map((o) => this._stringify(o));
    const fullPrompt = `${prompt}\n\nRespond with exactly one of: ${labels.join(", ")}`;
    const text = this._biAsk([agent, fullPrompt], kwargs);
    return Interpreter._matchOption(text, options, labels, this.rng);
  }

  _biAskAll(args, kwargs) {
    const [agentsList, prompt] = args;
    const temperature = kwargs.temperature !== undefined ? kwargs.temperature : 0.9;
    const maxTokens = kwargs.max_tokens !== undefined ? kwargs.max_tokens : 500;
    const results = new Map();
    for (const agent of agentsList) {
      const context = this.buildContextFor(agent, prompt);
      const role = this.roles.get(agent.roleName);
      const identity = this._identityFor(agent, role);
      const userPrompt = `What has happened so far:\n${context}\n\nNow: ${prompt}`;
      const resp = agent.provider.complete(identity, userPrompt, temperature, maxTokens);
      const text = resp.text || "";
      this._appendEvent("ask", `(asked: ${prompt}) ${text}`, agent.seat, new Set([agent.seat]));
      results.set(agent, text);
    }
    return results;
  }

  _biAskChoiceAll(args, kwargs) {
    const [agentsList, prompt, options] = args;
    const labels = options.map((o) => this._stringify(o));
    const fullPrompt = `${prompt}\n\nRespond with exactly one of: ${labels.join(", ")}`;
    const texts = this._biAskAll([agentsList, fullPrompt], kwargs);
    const results = new Map();
    for (const agent of agentsList) results.set(agent, Interpreter._matchOption(texts.get(agent), options, labels, this.rng));
    return results;
  }

  _biBroadcast(args) {
    if (args.length === 1) this._appendEvent("broadcast", this._stringify(args[0]), null, null);
    else {
      const [agent, text] = args;
      const author = agent instanceof Agent ? agent.seat : this._stringify(agent);
      this._appendEvent("broadcast", this._stringify(text), author, null);
    }
  }
  _biWhisper(args) {
    const [who, text] = args;
    const agentsList = Array.isArray(who) ? who : [who];
    this._appendEvent("whisper", this._stringify(text), null, new Set(agentsList.map((a) => a.seat)));
  }
  _biRemember(args) { const [agent, text] = args; this._appendEvent("note", this._stringify(text), agent.seat, new Set([agent.seat])); }

  _biReflect(args) {
    const agent = args[0];
    const visible = this._visibleEventsFor(agent);
    const top = retrieveMemory(visible, "", this.seq, 15);
    const context = top.map((e) => Interpreter._renderEvent(e)).join("\n");
    const role = this.roles.get(agent.roleName);
    const identity = `You are ${agent.seat}. Your role is ${role.name} (${role.team} team).`;
    const userPrompt = "Reflect on what has happened so far and state 1-3 higher-level insights or conclusions you can draw, one per line, no numbering.\n\n" + context;
    const resp = agent.provider.complete(identity, userPrompt);
    const insights = (resp.text || "").split("\n").map((l) => l.replace(/^[-* ]+|[-* ]+$/g, "").trim()).filter((l) => l.length > 0);
    for (const insight of insights.slice(0, 3)) this._appendEvent("reflection", insight, agent.seat, new Set([agent.seat]), 0.9);
    return insights;
  }

  // Threshold-triggered counterpart to reflect() -- fires only once the sum of
  // importance scores of this agent's memories since its last reflection crosses
  // `threshold`, matching Park et al. 2023 instead of a script-decided cadence.
  // Reflection events are written back at importance 0.9 (see _biReflect), so
  // they count toward the *next* threshold sum and get retrieved again by the
  // same scoring -- reflecting on reflections, without a separate tree structure.
  _biMaybeReflect(args, kwargs) {
    const agent = args[0];
    const threshold = kwargs.threshold !== undefined ? kwargs.threshold : (args.length > 1 ? args[1] : 4.0);
    const visible = this._visibleEventsFor(agent);
    const newEvents = visible.filter((e) => e.seq > agent.lastReflectSeq);
    const total = newEvents.reduce((s, e) => s + e.importance, 0);
    if (total < threshold) return [];
    const insights = this._biReflect([agent]);
    agent.lastReflectSeq = this.seq;
    return insights;
  }

  _biSetPersona(args) { const [agent, text] = args; agent.persona = this._stringify(text); }

  // Top level of the paper's recursive plan decomposition: a rough, broad-strokes
  // schedule generated from the agent's persona and memory context, stored on the
  // agent and also written into its own memory stream. decompose_step() below
  // does the next level down, on demand rather than all at once.
  _biMakePlan(args, kwargs) {
    const agent = args[0];
    const goal = args.length > 1 ? args[1] : (kwargs.goal !== undefined ? kwargs.goal : "Plan what you'll do.");
    const steps = kwargs.steps !== undefined ? Math.trunc(kwargs.steps) : 6;
    const prompt = `${goal}\n\nSketch a rough plan of about ${steps} broad-strokes steps for what you'll do, in order. Respond with exactly one step per line, formatted as 'time: activity' (e.g. '9am: eat breakfast at home'). No numbering, no extra commentary.`;
    const [identity, userPrompt] = this._promptPieces(agent, prompt);
    const resp = agent.provider.complete(identity, userPrompt, 0.7, 300);
    const plan = [];
    for (let line of (resp.text || "").split("\n")) {
      line = line.replace(/^[-* ]+|[-* ]+$/g, "").trim();
      if (!line) continue;
      const idx = line.indexOf(":");
      const step = new Map();
      if (idx !== -1) { step.set("time", line.slice(0, idx).trim()); step.set("activity", line.slice(idx + 1).trim()); }
      else { step.set("time", ""); step.set("activity", line); }
      plan.push(step);
    }
    if (plan.length === 0) plan.push(new Map([["time", ""], ["activity", goal]]));
    agent.plan = steps > 0 ? plan.slice(0, steps) : plan;
    agent.planCursor = 0;
    agent.subplan = []; agent.subplanCursor = 0;
    const summary = agent.plan.map((s) => (s.get("time") ? `${s.get("time")}: ${s.get("activity")}` : s.get("activity"))).join("; ");
    this._appendEvent("plan", `Plan: ${summary}`, agent.seat, new Set([agent.seat]), 0.5);
    return agent.plan;
  }

  _biCurrentStep(args) {
    const agent = args[0];
    if (agent.plan.length === 0 || agent.planCursor >= agent.plan.length) return null;
    return agent.plan[agent.planCursor];
  }

  // Second level of recursive decomposition: turns one broad-strokes plan step
  // into a handful of concrete, few-minutes-each actions. Defaults to the agent's
  // own current_step() if none is given.
  _biDecomposeStep(args, kwargs) {
    const agent = args[0];
    const step = args.length > 1 ? args[1] : this._biCurrentStep([agent]);
    const chunks = kwargs.chunks !== undefined ? Math.trunc(kwargs.chunks) : 4;
    if (step === null || step === undefined) return [];
    const activity = step instanceof Map ? step.get("activity") : this._stringify(step);
    const prompt = `Your current broad-strokes plan step is: '${activity}'. Break it down into about ${chunks} smaller, concrete, sequential actions (a few minutes each). Respond with exactly one action per line, no numbering.`;
    const [identity, userPrompt] = this._promptPieces(agent, prompt);
    const resp = agent.provider.complete(identity, userPrompt, 0.7, 250);
    let actions = (resp.text || "").split("\n").map((l) => l.replace(/^[-* ]+|[-* ]+$/g, "").trim()).filter((l) => l.length > 0);
    if (actions.length === 0) actions = [activity];
    agent.subplan = chunks > 0 ? actions.slice(0, chunks) : actions;
    agent.subplanCursor = 0;
    return agent.subplan;
  }

  _biCurrentAction(args) {
    const agent = args[0];
    if (agent.subplan.length > 0 && agent.subplanCursor < agent.subplan.length) return agent.subplan[agent.subplanCursor];
    const step = this._biCurrentStep([agent]);
    return step ? step.get("activity") : null;
  }

  // Moves to the next fine-grained action if decompose_step() populated one,
  // otherwise to the next broad-strokes step.
  _biAdvancePlan(args) {
    const agent = args[0];
    if (agent.subplan.length > 0 && agent.subplanCursor < agent.subplan.length - 1) { agent.subplanCursor += 1; return; }
    agent.subplan = []; agent.subplanCursor = 0;
    if (agent.plan.length > 0 && agent.planCursor < agent.plan.length - 1) agent.planCursor += 1;
  }

  // The paper's reacting mechanism: decide whether to keep following the current
  // plan or interrupt it given something just observed. Returns null for
  // "continue as planned", or a one-sentence new immediate action otherwise.
  _biReact(args, kwargs) {
    const agent = args[0];
    const observation = args[1];
    const current = this._biCurrentAction([agent]) || "nothing in particular";
    const prompt = `You observe: ${observation}\nYour current planned action is: ${current}\n\nDecide: continue with your current planned action, or react to what you just observed? If you continue, respond with exactly: CONTINUE\nIf you react, respond with one sentence describing your new immediate action instead.`;
    const [identity, userPrompt] = this._promptPieces(agent, prompt);
    const resp = agent.provider.complete(identity, userPrompt, 0.8, 80);
    const text = (resp.text || "").trim();
    this._appendEvent("ask", `(observed: ${observation}) ${text}`, agent.seat, new Set([agent.seat]));
    if (text.replace(/\.+$/, "").toUpperCase() === "CONTINUE") return null;
    return text;
  }

  // The paper's dialogue-generation mechanism: two agents that have met exchange
  // turns (each a real ask() against that agent's own model/persona/memory),
  // alternating until one says something that reads like a goodbye or max_turns
  // exchanges pass. Every line is a `dialogue` event visible to both.
  _biConverse(args, kwargs) {
    const a = args[0], b = args[1];
    const topic = args.length > 2 ? args[2] : (kwargs.topic !== undefined ? kwargs.topic : "");
    const maxTurns = kwargs.max_turns !== undefined ? Math.trunc(kwargs.max_turns) : 4;
    const seats = new Set([a.seat, b.seat]);
    const transcript = [];
    let speaker = a, other = b;
    let prompt = `You run into ${other.seat}.` + (topic ? ` Topic on your mind: ${topic}.` : "") + " Say one thing to them, one sentence.";
    for (let i = 0; i < Math.max(maxTurns, 0) * 2; i += 1) {
      const [identity, userPrompt] = this._promptPieces(speaker, prompt);
      const resp = speaker.provider.complete(identity, userPrompt, 0.9, 100);
      const line = (resp.text || "").trim();
      if (!line) break;
      this._appendEvent("dialogue", `${speaker.seat}: ${line}`, speaker.seat, new Set(seats));
      transcript.push(`${speaker.seat}: ${line}`);
      if (looksLikeFarewell(line)) break;
      [speaker, other] = [other, speaker];
      prompt = `${other.seat} just said to you: "${line}" Respond with one sentence.`;
    }
    return transcript;
  }

  _biEliminate(args, kwargs) {
    const agent = args[0];
    const cause = args.length > 1 ? args[1] : kwargs.cause;
    agent.alive = false;
    agent.deathCause = cause !== undefined && cause !== null ? this._stringify(cause) : null;
    this._appendEvent("broadcast", `${agent.seat} was eliminated${agent.deathCause ? ` (${agent.deathCause})` : ""}.`, null, null, 0.8);
  }

  _biTally(args) {
    const votes = args[0];
    if (!votes || votes.size === 0) return null;
    const buckets = new Map();
    for (const target of votes.values()) { const key = target instanceof Agent ? target.seat : target; if (!buckets.has(key)) buckets.set(key, []); buckets.get(key).push(target); }
    let bestCount = 0; for (const list of buckets.values()) bestCount = Math.max(bestCount, list.length);
    const winners = []; for (const list of buckets.values()) if (list.length === bestCount) winners.push(list[0]);
    return this.rng.choice(winners);
  }

  _biSpawnAgentsAt(args, kwargs) {
    const agentsList = args[0];
    const tag = args.length > 1 ? args[1] : kwargs.tag;
    const candidates = Array.from(this.worldLocations.values()).filter((loc) => tag === undefined || tag === null || loc.tag === tag);
    if (candidates.length === 0) throw new SLRuntimeError(`spawn_agents_at: no locations match${tag ? ` tag '${tag}'` : ""}`);
    for (const agent of agentsList) { const loc = this.rng.choice(candidates); agent.locationId = loc.id; agent.x = loc.x; agent.y = loc.y; }
  }
  _biMoveTo(args) { const [agent, loc] = args; agent.locationId = loc.id; agent.x = loc.x; agent.y = loc.y; }
  _biNearby(args, kwargs) {
    const agent = args[0];
    const radius = args.length > 1 ? args[1] : (kwargs.radius !== undefined ? kwargs.radius : 5.0);
    if (agent.x === null) return [];
    const r2 = radius * radius;
    return this.agents.filter((ag) => ag !== agent && ag.x !== null && (ag.x - agent.x) ** 2 + (ag.y - agent.y) ** 2 <= r2);
  }
  _biPrint(value) {
    // Streamed to the sink too (see runCurrent's sink.onEvent capturing
    // roundFrames-adjacent state), matching the Python reference -- print()
    // output used to only reach the final log, silently missing from a live view
    // of the same run.
    const entry = { seq: null, round: this.round, kind: "print", text: this._stringify(value), author: null, visibleTo: null };
    this.runLog.push(entry);
    if (this.sink) this.sink.onEvent(entry);
  }

  _checkWin() {
    if (!this.sim.winCondition) return null;
    const env = new Env(this.globalEnv);
    try { this.execBlock(this.sim.winCondition, env); } catch (e) { if (e instanceof ReturnSignal) return e.value; throw e; }
    return null;
  }

  _makeBuiltins() {
    return {
      ask: (a, k) => this._biAsk(a, k),
      ask_choice: (a, k) => this._biAskChoice(a, k),
      ask_all: (a, k) => this._biAskAll(a, k),
      ask_choice_all: (a, k) => this._biAskChoiceAll(a, k),
      broadcast: (a) => this._biBroadcast(a),
      whisper: (a) => this._biWhisper(a),
      remember: (a) => this._biRemember(a),
      reflect: (a) => this._biReflect(a),
      maybe_reflect: (a, k) => this._biMaybeReflect(a, k),
      set_persona: (a) => this._biSetPersona(a),
      make_plan: (a, k) => this._biMakePlan(a, k),
      current_step: (a) => this._biCurrentStep(a),
      decompose_step: (a, k) => this._biDecomposeStep(a, k),
      current_action: (a) => this._biCurrentAction(a),
      advance_plan: (a) => this._biAdvancePlan(a),
      react: (a, k) => this._biReact(a, k),
      converse: (a, k) => this._biConverse(a, k),
      alive: () => this.agents.filter((ag) => ag.alive),
      all_agents: () => this.agents.slice(),
      with_role: (a) => this.agents.filter((ag) => ag.roleName === a[0]),
      team_of: (a) => a[0].team,
      eliminate: (a, k) => this._biEliminate(a, k),
      tally: (a) => this._biTally(a),
      count: (a) => (a[0] instanceof Map ? a[0].size : a[0].length),
      last: (a) => { const n = Math.trunc(a[1]); return n > 0 ? a[0].slice(Math.max(a[0].length - n, 0)) : []; },
      random_choice: (a) => (a[0] && a[0].length > 0 ? this.rng.choice(a[0]) : null),
      str: (a) => this._stringify(a[0]),
      print: (a) => this._biPrint(a[0]),
      check_win: () => this._checkWin(),
      locations: () => Array.from(this.worldLocations.values()),
      locations_by_tag: (a) => Array.from(this.worldLocations.values()).filter((loc) => loc.tag === a[0]),
      spawn_agents_at: (a, k) => this._biSpawnAgentsAt(a, k),
      move_to: (a) => this._biMoveTo(a),
      location_of: (a) => (a[0].locationId ? (this.worldLocations.get(a[0].locationId) || null) : null),
      agents_at: (a) => this.agents.filter((ag) => ag.locationId === a[0].id),
      nearby: (a, k) => this._biNearby(a, k),
    };
  }
}

function runSource(source, roster, opts = {}) {
  const sim = parse(source);
  // Two independently seeded RNGs, matching the Python reference's run_source
  // (a fresh random.Random(seed) for assign_agents, and Interpreter.__init__
  // building its own separate random.Random(seed) from the same seed value) --
  // not one shared, continued stream. Otherwise world-scatter/tally/tie-break
  // draws would depend on how many random calls assignAgents happened to make
  // (which varies with population size), instead of only on `seed`.
  const assignRng = opts.rng || new SeededRandom(opts.seed);
  const { agents, rolesByName } = assignAgents(sim, roster, assignRng);
  const interp = new Interpreter(sim, agents, rolesByName, {
    seed: opts.seed, sink: opts.sink, worldLocations: opts.worldLocations,
  });
  return { interp, result: interp.run(opts.maxRounds || 200, opts.onProgress) };
}

export { tokenize, parse, LexError, ParseError, SLRuntimeError, Interpreter, Agent, Location, EventRec, assignAgents, runSource, SeededRandom, MockProvider, setupWorldLocations };
