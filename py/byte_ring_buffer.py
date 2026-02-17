# -*- coding: utf-8 -*-
"""SPSC byte ring buffer for cross-language IPC via shared memory.

Compatible with C++ shm::ByteRingBuffer. Both sides use the same
shared memory layout:

  [0..3]   : head  (uint32 LE, producer writes, consumer reads)
  [4..7]   : tail  (uint32 LE, consumer writes, producer reads)
  [8..11]  : capacity (uint32 LE, set by producer, read-only after init)
  [12..15] : reserved
  [16..N]  : data area (circular buffer)

Message format: [4-byte length (LE)][payload]

Cross-language atomicity:
  - Aligned uint32 read/write is atomic on x86 and ARMv6+
  - CPython GIL provides additional serialization
  - head/tail are monotonically increasing; wrap via & mask
"""

from __future__ import annotations

import struct
from typing import Optional

__all__ = ["ByteRingBuffer", "HEADER_SIZE"]

HEADER_SIZE: int = 16
_U32 = struct.Struct("<I")


class ByteRingBuffer:
    """SPSC byte ring buffer operating on a shared memoryview.

    Args:
        buf: Shared memory region as memoryview.
        is_producer: If True, initialize header fields.
    """

    __slots__ = ("_buf", "_capacity", "_mask", "_is_producer")

    def __init__(self, buf: memoryview, *, is_producer: bool = False) -> None:
        self._buf = buf
        self._is_producer = is_producer

        if is_producer:
            cap = self._round_down_pow2(len(buf) - HEADER_SIZE)
            _U32.pack_into(self._buf, 0, 0)       # head = 0
            _U32.pack_into(self._buf, 4, 0)       # tail = 0
            _U32.pack_into(self._buf, 8, cap)     # capacity
            _U32.pack_into(self._buf, 12, 0)      # reserved
        else:
            cap = _U32.unpack_from(self._buf, 8)[0]

        self._capacity = cap
        self._mask = cap - 1

    # ---- Producer API ----

    def write(self, data: bytes) -> bool:
        """Write a length-prefixed message: [4B len][payload].

        Returns:
            True if written successfully, False if insufficient space.
        """
        msg_len = len(data)
        total = msg_len + 4
        if self.writeable_bytes() < total:
            return False

        head = _U32.unpack_from(self._buf, 0)[0]
        self._write_raw(head, _U32.pack(msg_len))
        self._write_raw(head + 4, data)
        _U32.pack_into(self._buf, 0, head + total)
        return True

    def writeable_bytes(self) -> int:
        """Available bytes for writing."""
        head = _U32.unpack_from(self._buf, 0)[0]
        tail = _U32.unpack_from(self._buf, 4)[0]
        return self._capacity - (head - tail)

    # ---- Consumer API ----

    def read(self) -> Optional[bytes]:
        """Read one length-prefixed message.

        Returns:
            Payload bytes, or None if no complete message available.
        """
        tail = _U32.unpack_from(self._buf, 4)[0]
        head = _U32.unpack_from(self._buf, 0)[0]

        available = head - tail
        if available < 4:
            return None

        msg_len = _U32.unpack(self._read_raw(tail, 4))[0]
        if msg_len == 0 or available < msg_len + 4:
            return None

        payload = self._read_raw(tail + 4, msg_len)
        _U32.pack_into(self._buf, 4, tail + msg_len + 4)
        return payload

    def readable_bytes(self) -> int:
        """Available bytes for reading."""
        tail = _U32.unpack_from(self._buf, 4)[0]
        head = _U32.unpack_from(self._buf, 0)[0]
        return head - tail

    def has_data(self) -> bool:
        """Check if at least one complete message header is available."""
        return self.readable_bytes() >= 4

    @property
    def capacity(self) -> int:
        """Data area capacity in bytes."""
        return self._capacity

    # ---- Internal ----

    def _write_raw(self, pos: int, data: bytes) -> None:
        length = len(data)
        offset = pos & self._mask
        base = HEADER_SIZE + offset
        first = self._capacity - offset

        if first >= length:
            self._buf[base:base + length] = data
        else:
            self._buf[base:base + first] = data[:first]
            self._buf[HEADER_SIZE:HEADER_SIZE + length - first] = data[first:]

    def _read_raw(self, pos: int, length: int) -> bytes:
        offset = pos & self._mask
        base = HEADER_SIZE + offset
        first = self._capacity - offset

        if first >= length:
            return bytes(self._buf[base:base + length])
        return (bytes(self._buf[base:base + first])
                + bytes(self._buf[HEADER_SIZE:HEADER_SIZE + length - first]))

    @staticmethod
    def _round_down_pow2(v: int) -> int:
        if v <= 0:
            return 0
        v |= v >> 1
        v |= v >> 2
        v |= v >> 4
        v |= v >> 8
        v |= v >> 16
        return (v >> 1) + 1


def _run_tests() -> None:
    """Correctness tests for ByteRingBuffer."""
    import sys

    passed = 0
    failed = 0

    def check(name: str, got: object, expected: object) -> None:
        nonlocal passed, failed
        if got == expected:
            passed += 1
        else:
            failed += 1
            print(f"  FAIL: {name} (got {got!r}, expected {expected!r})")

    # Test 1: basic write/read
    buf = bytearray(16 + 64)
    ring = ByteRingBuffer(memoryview(buf), is_producer=True)
    check("capacity", ring.capacity, 64)
    check("initial readable", ring.readable_bytes(), 0)
    check("initial writeable", ring.writeable_bytes(), 64)

    ok = ring.write(b"hello")
    check("write ok", ok, True)
    check("readable after write", ring.readable_bytes(), 9)

    data = ring.read()
    check("read data", data, b"hello")
    check("readable after read", ring.readable_bytes(), 0)

    # Test 2: multiple messages
    ring2 = ByteRingBuffer(memoryview(buf), is_producer=True)
    msgs = [b"msg1", b"message_two", b"3"]
    for m in msgs:
        check(f"write {m!r}", ring2.write(m), True)
    for m in msgs:
        check(f"read {m!r}", ring2.read(), m)
    check("empty after all", ring2.read(), None)

    # Test 3: wrap-around
    buf3 = bytearray(16 + 32)
    ring3 = ByteRingBuffer(memoryview(buf3), is_producer=True)
    ring3.write(b"A" * 20)
    ring3.read()
    check("wrap write", ring3.write(b"B" * 20), True)
    check("wrap read", ring3.read(), b"B" * 20)

    # Test 4: full buffer rejection
    buf4 = bytearray(16 + 16)
    ring4 = ByteRingBuffer(memoryview(buf4), is_producer=True)
    check("full write", ring4.write(b"X" * 12), True)
    check("reject on full", ring4.write(b"Y"), False)

    # Test 5: consumer view
    buf5 = bytearray(16 + 64)
    prod = ByteRingBuffer(memoryview(buf5), is_producer=True)
    prod.write(b"cross-lang")
    cons = ByteRingBuffer(memoryview(buf5), is_producer=False)
    check("consumer capacity", cons.capacity, 64)
    check("consumer has_data", cons.has_data(), True)
    check("consumer read", cons.read(), b"cross-lang")

    print(f"Results: {passed} passed, {failed} failed")
    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    _run_tests()
