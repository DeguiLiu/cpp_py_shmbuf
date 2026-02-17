# -*- coding: utf-8 -*-
"""High-level shared memory channel for cross-language IPC.

Usage:
    # Producer (C++ or Python)
    producer = ShmProducer("my_channel", capacity=1024 * 1024)
    producer.write(b"hello world")

    # Consumer (C++ or Python)
    consumer = ShmConsumer("my_channel")
    data = consumer.read()
"""

from __future__ import annotations

from multiprocessing.shared_memory import SharedMemory
from typing import Optional

from byte_ring_buffer import ByteRingBuffer, HEADER_SIZE

__all__ = ["ShmProducer", "ShmConsumer"]


class ShmProducer:
    """Shared memory producer. Creates shared memory and writes messages.

    Args:
        name: Shared memory name (alphanumeric).
        capacity: Data area size in bytes.
    """

    __slots__ = ("_shm", "_ring", "_buf_view", "_name")

    def __init__(self, name: str, capacity: int) -> None:
        self._name = name
        total_size = capacity + HEADER_SIZE

        # Clean up any leftover segment
        try:
            old = SharedMemory(name=name, create=False)
            old.close()
            old.unlink()
        except FileNotFoundError:
            pass

        self._shm: Optional[SharedMemory] = SharedMemory(
            name=name, create=True, size=total_size
        )
        self._buf_view = self._shm.buf
        self._ring: Optional[ByteRingBuffer] = ByteRingBuffer(
            self._buf_view, is_producer=True
        )

    def write(self, data: bytes) -> bool:
        """Write a message to shared memory."""
        if self._ring is None:
            return False
        return self._ring.write(data)

    def writeable_bytes(self) -> int:
        return self._ring.writeable_bytes() if self._ring else 0

    @property
    def capacity(self) -> int:
        return self._ring.capacity if self._ring else 0

    @property
    def is_valid(self) -> bool:
        return self._shm is not None

    def close(self) -> None:
        """Release resources without unlinking shared memory."""
        self._ring = None
        self._buf_view = None
        if self._shm is not None:
            self._shm.close()
            self._shm = None

    def destroy(self) -> None:
        """Close and unlink shared memory."""
        self._ring = None
        self._buf_view = None
        if self._shm is not None:
            self._shm.close()
            try:
                self._shm.unlink()
            except FileNotFoundError:
                pass
            self._shm = None

    def __enter__(self) -> ShmProducer:
        return self

    def __exit__(self, *args: object) -> None:
        self.destroy()

    def __del__(self) -> None:
        self.close()


class ShmConsumer:
    """Shared memory consumer. Opens existing shared memory and reads messages.

    Args:
        name: Shared memory name (must match producer).
        size: Expected total size. Pass 0 to auto-detect.
    """

    __slots__ = ("_shm", "_ring", "_buf_view", "_name")

    def __init__(self, name: str, size: int = 0) -> None:
        self._name = name
        self._shm: Optional[SharedMemory] = SharedMemory(name=name, create=False)
        actual_size = size if size > 0 else self._shm.size
        self._buf_view = self._shm.buf[:actual_size]
        self._ring: Optional[ByteRingBuffer] = ByteRingBuffer(
            self._buf_view, is_producer=False
        )

    def read(self) -> Optional[bytes]:
        """Read one message from shared memory."""
        if self._ring is None:
            return None
        return self._ring.read()

    def has_data(self) -> bool:
        return self._ring.has_data() if self._ring else False

    def readable_bytes(self) -> int:
        return self._ring.readable_bytes() if self._ring else 0

    @property
    def capacity(self) -> int:
        return self._ring.capacity if self._ring else 0

    @property
    def is_valid(self) -> bool:
        return self._shm is not None

    def close(self) -> None:
        """Release resources."""
        self._ring = None
        self._buf_view = None
        if self._shm is not None:
            self._shm.close()
            self._shm = None

    def __enter__(self) -> ShmConsumer:
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
