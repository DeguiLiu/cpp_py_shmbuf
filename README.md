# cpp_py_shmbuf

Cross-language shared memory IPC: C++ and Python communicate via a lock-free SPSC ring buffer over POSIX/Win32 shared memory. Zero-copy, zero-dependency, header-only.

## Architecture

```
+------------------+     Shared Memory (shm_open / CreateFileMapping)     +------------------+
|  C++ Producer    | =================================================>  |  Python Consumer |
|  (ShmProducer)   |     SPSC Byte Ring Buffer (lock-free)                |  (ShmConsumer)   |
+------------------+                                                       +------------------+
```

## Features

- Header-only C++14, zero external dependencies (no Boost)
- Cross-platform: Linux, macOS, Windows
- Cross-language: C++ and Python share the same ring buffer protocol
- Lock-free SPSC design inspired by [ringbuffer](https://gitee.com/liudegui/ringbuffer)
- Monotonically increasing indices (no buf_full flag race condition)
- Memory fences for cross-process visibility (`atomic_thread_fence`)
- Length-prefixed messages: `[4-byte LE length][payload]`

## Ring Buffer Protocol

Shared memory layout (16-byte header + data area):

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 4 bytes | head index (uint32 LE, producer writes) |
| 4 | 4 bytes | tail index (uint32 LE, consumer writes) |
| 8 | 4 bytes | capacity (uint32 LE, power of 2, set by producer) |
| 12 | 4 bytes | reserved |
| 16 | N bytes | data area (circular buffer) |

Head and tail are monotonically increasing. Actual offset = `index & (capacity - 1)`.

## Quick Start

### Build (C++)

```bash
cmake -B build -DSHM_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

### C++ Producer

```cpp
#include "shm/shm_channel.hpp"

shm::ShmProducer producer("my_channel", 1024 * 1024);  // 1MB
producer.Write(data, len);
```

### Python Consumer

```python
from shm_channel import ShmConsumer

consumer = ShmConsumer("my_channel")
data = consumer.read()
```

### Cross-Language Demo

```bash
# Terminal 1: C++ producer
./build/cpp_producer

# Terminal 2: Python consumer
cd py && python consumer_demo.py
```

## Performance (x86-64, GCC 13, -O2)

| Message Size | Throughput (single-thread) | Cross-Thread | Latency (write+read) |
|-------------|---------------------------|--------------|----------------------|
| 64 B | 2.1 GB/s, 35M msg/s | 0.5 GB/s, 9M msg/s | 11 ns |
| 1 KB | 3.2 GB/s, 3.4M msg/s | 3.9 GB/s, 4.1M msg/s | 52 ns |
| 4 KB | 3.2 GB/s, 830K msg/s | 5.7 GB/s, 1.5M msg/s | 169 ns |
| 6 MB (1080p) | 2.5 GB/s, 423 msg/s | 4.4 GB/s, 763 msg/s | - |

## Project Structure

```
include/shm/
  shared_memory.hpp      -- Cross-platform shared memory (from libsharedmemory)
  byte_ring_buffer.hpp   -- SPSC byte ring buffer (inspired by ringbuffer)
  shm_channel.hpp        -- High-level API: ShmProducer / ShmConsumer
py/
  byte_ring_buffer.py    -- Python SPSC byte ring buffer
  shm_channel.py         -- Python ShmProducer / ShmConsumer
  consumer_demo.py       -- OpenCV video consumer demo
tests/
  test_byte_ring_buffer.cpp   -- C++ unit tests (51 tests)
  test_cross_lang_producer.cpp -- Cross-language test (C++ side)
  test_cross_lang_consumer.py  -- Cross-language test (Python side)
  benchmark.cpp               -- Performance benchmark
examples/
  cpp_producer.cpp       -- C++ video frame producer
docs/
  design_zh.md           -- Design document (Chinese)
```

## Requirements

- C++14 compiler (GCC, Clang, MSVC)
- Python 3.8+ (for Python consumer)
- CMake 3.10+
- No external dependencies

## Credits

- [libsharedmemory](https://github.com/kyr0/libsharedmemory) -- Cross-platform shared memory abstraction
- [ringbuffer](https://gitee.com/liudegui/ringbuffer) -- Lock-free SPSC ring buffer design

## License

MIT License
