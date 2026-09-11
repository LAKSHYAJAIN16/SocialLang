"""A live event-streaming bridge out of the interpreter, so an external viewer (the
Unity client under unity/, or any other WebSocket client -- a browser tab, a test)
can watch a simulation as it runs instead of only reading the final JSON.

Interpreter.run() stays fully synchronous (see sociallang/lang/interpreter.py); this
module runs an asyncio WebSocket server on a background thread and gives the
interpreter's calling thread a plain synchronous `LiveSink`-shaped API
(emit_event/emit_agents_snapshot/emit_world/emit_done) to push through it.

Message schema (one JSON object per WebSocket text frame), deliberately reusing the
same field shapes Interpreter.run()'s result dict and ResultsLogger already produce
(see results.py/visualize.py) rather than inventing a parallel one:

  {"type": "world", "width": <n>, "height": <n>, "locations": [{"id","type","tag",
    "capacity","x","y"}, ...]}                                    -- sent once, if the
                                                                       sim has a world {}
  {"type": "agents_snapshot", "agents": [{"seat","role","team","model","alive",
    "death_cause","x","y","location_id"}, ...]}                   -- sent at round start
                                                                       and after each round
  {"type": "event", "seq","round","kind","text","author","visible_to"}
                                                                    -- one per run_log entry
  {"type": "done", "winner": <any>, "rounds": <n>}                -- sent once, at the end
"""

from __future__ import annotations

import asyncio
import json
import threading
from typing import Any, Protocol


class LiveSink(Protocol):
    def emit_event(self, event: dict) -> None: ...
    def emit_agents_snapshot(self, agents: list[dict]) -> None: ...
    def emit_world(self, width: int, height: int, locations: list[dict]) -> None: ...
    def emit_done(self, winner: Any, rounds: int) -> None: ...


class WebSocketSink:
    """A LiveSink backed by a `websockets` server running on a background thread.
    Construct it, pass it as `Interpreter(..., sink=sink)`, and any client connected
    to ws://<host>:<port> receives every message as it's emitted. Call close() when
    done (e.g. after interp.run() returns).
    """

    def __init__(self, host: str = "localhost", port: int = 8765, ready_timeout: float = 5.0):
        import websockets  # imported lazily so the rest of the package has no hard dep

        self._websockets = websockets
        self.host = host
        self.port = port
        self._clients: set = set()
        self._server = None
        self._loop = asyncio.new_event_loop()
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()
        if not self._ready.wait(timeout=ready_timeout):
            raise RuntimeError(f"WebSocketSink server did not start within {ready_timeout}s")

    # -- background thread / event loop plumbing --

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_until_complete(self._serve())

    async def _serve(self) -> None:
        async def handler(ws) -> None:
            self._clients.add(ws)
            try:
                async for _msg in ws:
                    pass  # this bridge is push-only; incoming client messages are ignored
            finally:
                self._clients.discard(ws)

        self._server = await self._websockets.serve(handler, self.host, self.port)
        # port=0 asks the OS for a free port; resolve the actual bound port so
        # callers (and tests) can discover it via self.port after construction.
        self.port = self._server.sockets[0].getsockname()[1]
        self._ready.set()
        await self._server.wait_closed()

    async def _broadcast_async(self, data: str) -> None:
        if not self._clients:
            return
        stale = []
        for ws in list(self._clients):
            try:
                await ws.send(data)
            except Exception:
                stale.append(ws)
        for ws in stale:
            self._clients.discard(ws)

    def _broadcast_sync(self, message: dict) -> None:
        data = json.dumps(message, default=str)
        future = asyncio.run_coroutine_threadsafe(self._broadcast_async(data), self._loop)
        try:
            future.result(timeout=5)
        except TimeoutError:
            # A slow/stuck client (paused debugger, backed-up network) can make
            # ws.send() block past this timeout via websockets' own backpressure
            # handling. Don't let one misbehaving viewer crash the whole
            # simulation -- the broadcast keeps running on the event-loop thread
            # (and that client gets pruned on its next failed send in
            # _broadcast_async); we just stop waiting for it here.
            pass

    # -- LiveSink API (called from the interpreter's thread) --

    def emit_event(self, event: dict) -> None:
        self._broadcast_sync({"type": "event", **event})

    def emit_agents_snapshot(self, agents: list[dict]) -> None:
        self._broadcast_sync({"type": "agents_snapshot", "agents": agents})

    def emit_world(self, width: int, height: int, locations: list[dict]) -> None:
        self._broadcast_sync({"type": "world", "width": width, "height": height, "locations": locations})

    def emit_done(self, winner: Any, rounds: int) -> None:
        self._broadcast_sync({"type": "done", "winner": winner, "rounds": rounds})

    def close(self, timeout: float = 5.0) -> None:
        if self._server is not None:
            async def _shutdown() -> None:
                self._server.close()
                await self._server.wait_closed()

            future = asyncio.run_coroutine_threadsafe(_shutdown(), self._loop)
            future.result(timeout=timeout)
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join(timeout=timeout)
