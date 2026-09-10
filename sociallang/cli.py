from __future__ import annotations

import argparse
import json
import os
import random
import sys

from dotenv import load_dotenv

from .lang.interpreter import SLRuntimeError, assign_agents, Interpreter
from .lang.lexer import LexError
from .lang.parser import ParseError, parse
from .providers.embeddings import GoogleEmbeddingProvider, HashEmbeddingProvider, OpenAICompatEmbeddingProvider
from .providers.factory import ModelSpec, build_provider, filter_roster_by_vendor, load_runnable_roster
from .results import ResultsLogger
from .schema_export import export_schema
from .visualize import render_html

DEFAULT_MODELS_CONFIG = "config/models.yaml"
DEFAULT_RESULTS_DIR = "results"
DEFAULT_OPENAI_EMBEDDING_MODEL = "text-embedding-3-small"
DEFAULT_GOOGLE_EMBEDDING_MODEL = "text-embedding-004"


def _load_roster(args: argparse.Namespace) -> dict:
    roster = load_runnable_roster(args.models, require_keys=not args.mock_only)
    if args.mock_only or not roster:
        if not roster:
            print("[run] no models had usable API keys -- falling back to mock-only roster")
        mock_spec = ModelSpec(key="mock-random", display_name="Mock", provider="mock", model_id="mock-random")
        roster = {"mock-random": (mock_spec, build_provider(mock_spec))}
    if args.vendor:
        roster = filter_roster_by_vendor(roster, args.vendor)
        if not roster:
            print(f"[run] no enabled models found for vendor '{args.vendor}' -- aborting")
            sys.exit(1)
    return roster


def _build_embedder(args: argparse.Namespace):
    """Relevance backend for generative(k) memory. 'lexical' keeps the old zero-config
    Jaccard-overlap behavior; 'hash' (the default) is a real, offline, deterministic
    embedding-space relevance score with no API key needed; 'openai'/'google' call out
    to a real embedding API and fall back to 'hash' if the matching key is missing.
    """
    if args.embeddings == "lexical":
        return None
    if args.embeddings == "hash":
        return HashEmbeddingProvider()
    if args.embeddings == "openai":
        api_key = os.environ.get("OPENAI_API_KEY")
        if not api_key:
            print("[run] --embeddings openai needs OPENAI_API_KEY -- falling back to 'hash'")
            return HashEmbeddingProvider()
        return OpenAICompatEmbeddingProvider(
            args.embedding_model or DEFAULT_OPENAI_EMBEDDING_MODEL, api_key, "https://api.openai.com/v1"
        )
    if args.embeddings == "google":
        api_key = os.environ.get("GEMINI_API_KEY")
        if not api_key:
            print("[run] --embeddings google needs GEMINI_API_KEY -- falling back to 'hash'")
            return HashEmbeddingProvider()
        return GoogleEmbeddingProvider(args.embedding_model or DEFAULT_GOOGLE_EMBEDDING_MODEL, api_key)
    raise ValueError(f"unknown --embeddings choice '{args.embeddings}'")


def _build_importance_provider(args: argparse.Namespace, roster: dict):
    """None keeps the old zero-config lexical-heuristic importance score; otherwise
    picks the roster model that will rate each memory's importance (1-10) at write
    time, per Park et al.'s original design -- see memory.llm_importance.
    """
    if not args.llm_importance:
        return None
    if args.importance_model:
        if args.importance_model not in roster:
            print(f"[run] --importance-model '{args.importance_model}' not in the roster -- aborting")
            sys.exit(1)
        return roster[args.importance_model][1]
    return next(iter(roster.values()))[1]


def _parse_or_die(path: str):
    with open(path, "r", encoding="utf-8") as f:
        source = f.read()
    try:
        return source, parse(source)
    except (LexError, ParseError) as exc:
        print(f"[error] {path}: {exc}")
        sys.exit(1)


def cmd_run(args: argparse.Namespace) -> None:
    load_dotenv()
    _source, sim = _parse_or_die(args.file)
    roster = _load_roster(args)
    print(f"[run] roster: {', '.join(roster.keys())}")
    embedder = _build_embedder(args)
    importance_provider = _build_importance_provider(args, roster)

    logger = None if args.no_save else ResultsLogger(args.out)

    for i in range(args.games):
        seed = args.seed + i if args.seed is not None else None
        agents, roles_by_name = assign_agents(sim, roster, random.Random(seed))
        interp = Interpreter(
            sim, agents, roles_by_name, seed=seed, embedder=embedder, importance_provider=importance_provider
        )
        result = interp.run(max_rounds=args.max_rounds)

        print(
            f"[run] game {i + 1}/{args.games}: winner={result['winner']} rounds={result['rounds']} "
            f"agents={len(result['agents'])}"
        )
        if logger is not None:
            run_id = logger.save_run(sim.name, i, result)
            print(f"[run] saved: {os.path.join(args.out, run_id)}.json / .html")


def cmd_schema(args: argparse.Namespace) -> None:
    _source, sim = _parse_or_die(args.file)
    schema = export_schema(sim)
    text = json.dumps(schema, indent=2)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"[schema] wrote {args.out}")
    else:
        print(text)


def cmd_visualize(args: argparse.Namespace) -> None:
    with open(args.run_json, "r", encoding="utf-8") as f:
        record = json.load(f)
    sim_name = record.get("sim_name", "sim")
    out_path = args.out or os.path.splitext(args.run_json)[0] + ".html"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(render_html(sim_name, record, show_private=args.show_private))
    print(f"[visualize] wrote {out_path}")


def cmd_check(args: argparse.Namespace) -> None:
    _source, sim = _parse_or_die(args.file)
    print(f"[check] '{args.file}' parses OK -- sim '{sim.name}', {len(sim.roles)} role(s), "
          f"{len(sim.phases)} phase(s), {len(sim.fns)} fn(s), {len(sim.memory_decls)} custom memory pattern(s)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="sociallang", description="Run SocialLang simulations")
    sub = parser.add_subparsers(dest="command", required=True)

    run_p = sub.add_parser("run", help="run a .sl simulation one or more times")
    run_p.add_argument("file", help="path to a .sl program, e.g. games/mafia.sl")
    run_p.add_argument("--games", type=int, default=1, help="how many independent runs to simulate")
    run_p.add_argument("--models", default=DEFAULT_MODELS_CONFIG, help="path to the model roster YAML")
    run_p.add_argument("--mock-only", action="store_true", help="ignore API keys, use only the mock random-play model")
    run_p.add_argument("--vendor", default=None, help="restrict the roster to one vendor (e.g. 'anthropic')")
    run_p.add_argument("--seed", type=int, default=None, help="base RNG seed (run i uses seed + i) for reproducible runs")
    run_p.add_argument("--max-rounds", type=int, default=200, help="safety cap on loop iterations")
    run_p.add_argument("--out", default=DEFAULT_RESULTS_DIR, help="directory to save run JSON/HTML into")
    run_p.add_argument("--no-save", action="store_true", help="don't write result files, just print the outcome")
    run_p.add_argument(
        "--embeddings", choices=["lexical", "hash", "openai", "google"], default="hash",
        help="relevance backend for generative(k) memory: 'lexical' (old Jaccard-overlap default), "
             "'hash' (offline deterministic embedding, no API key -- the default), 'openai'/'google' "
             "(real embedding API, needs the matching API key; falls back to 'hash' if missing)",
    )
    run_p.add_argument(
        "--embedding-model", default=None,
        help=f"embedding model id for --embeddings openai/google "
             f"(default: {DEFAULT_OPENAI_EMBEDDING_MODEL} / {DEFAULT_GOOGLE_EMBEDDING_MODEL})",
    )
    run_p.add_argument(
        "--llm-importance", action="store_true",
        help="rate each memory's importance with an LLM call (1-10) instead of the lexical heuristic",
    )
    run_p.add_argument(
        "--importance-model", default=None,
        help="roster key of the model that rates importance when --llm-importance is set "
             "(default: first available model in the roster)",
    )
    run_p.set_defaults(func=cmd_run)

    schema_p = sub.add_parser("schema", help="export a .sl program's roles/memory patterns as JSON")
    schema_p.add_argument("file", help="path to a .sl program")
    schema_p.add_argument("--out", default=None, help="write JSON here instead of stdout")
    schema_p.set_defaults(func=cmd_schema)

    viz_p = sub.add_parser("visualize", help="(re)generate the HTML replay for a saved run")
    viz_p.add_argument("run_json", help="path to a run_*.json file saved by `run`")
    viz_p.add_argument("--out", default=None, help="output .html path (default: alongside the input)")
    viz_p.add_argument("--show-private", action="store_true", help="include each agent's private ask() calls in the replay")
    viz_p.set_defaults(func=cmd_visualize)

    check_p = sub.add_parser("check", help="parse a .sl program and report its shape without running it")
    check_p.add_argument("file")
    check_p.set_defaults(func=cmd_check)

    return parser


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.func(args)
    except SLRuntimeError as exc:
        print(f"[error] runtime error: {exc}")
        sys.exit(1)


if __name__ == "__main__":
    main()
