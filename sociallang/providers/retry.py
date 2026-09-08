from __future__ import annotations

import time
from typing import Callable, TypeVar

T = TypeVar("T")

RETRYABLE_STATUS = {408, 409, 425, 429, 500, 502, 503, 504}


def with_backoff(fn: Callable[[], T], attempts: int = 4, base_delay: float = 1.5) -> T:
    """Runs fn() with exponential backoff. fn should raise on failure."""
    last_exc: Exception | None = None
    for attempt in range(attempts):
        try:
            return fn()
        except Exception as exc:  # noqa: BLE001 - deliberately broad, this is a generic retry wrapper
            last_exc = exc
            if attempt == attempts - 1:
                break
            time.sleep(base_delay * (2**attempt))
    assert last_exc is not None
    raise last_exc
