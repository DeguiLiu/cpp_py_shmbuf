# cpp_py_shmbuf

[![CI](https://github.com/DeguiLiu/cpp_py_shmbuf/actions/workflows/ci.yml/badge.svg)](https://github.com/DeguiLiu/cpp_py_shmbuf/actions/workflows/ci.yml)
[![Code Coverage](https://github.com/DeguiLiu/cpp_py_shmbuf/actions/workflows/coverage.yml/badge.svg)](https://github.com/DeguiLiu/cpp_py_shmbuf/actions/workflows/coverage.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

[English](README.md) | **中文**

跨语言共享内存 IPC: C++ 和 Python 通过无锁 SPSC 环形缓冲区在 POSIX/Win32 共享内存上通信。零拷贝、零依赖、header-only。

## 架构

```
+------------------+     共享内存 (shm_open / CreateFileMapping)     +------------------+
|  C++ Producer    | =============================================>  |  Python Consumer |
|  (ShmProducer)   |     SPSC 字节环形缓冲 (lock-free)               |  (ShmConsumer)   |
+------------------+                                                  +------------------+
```

## 特性

- Header-only C++14, 零外部依赖 (无 Boost)
- 跨平台: Linux, macOS, Windows
- 跨语言: C++ 和 Python 共享相同的环形缓冲协议
- 无锁 SPSC 设计, 借鉴 [ringbuffer](https://gitee.com/liudegui/ringbuffer)
- 单调递增索引 (消除 buf_full flag 竞态)
- 内存屏障保证跨进程可见性 (`atomic_thread_fence`)
- 长度前缀消息: `[4字节 LE 长度][载荷]`

## 环形缓冲协议

共享内存布局 (16 字节头部 + 数据区):

| 偏移 | 大小 | 描述 |
|------|------|------|
| 0 | 4 字节 | head 索引 (uint32 LE, 生产者写) |
| 4 | 4 字节 | tail 索引 (uint32 LE, 消费者写) |
| 8 | 4 字节 | capacity (uint32 LE, 2 的幂, 生产者初始化) |
| 12 | 4 字节 | 保留 |
| 16 | N 字节 | 数据区 (环形缓冲) |

head 和 tail 单调递增, 实际偏移 = `index & (capacity - 1)`。

## 快速开始

### 构建 (C++)

```bash
cmake -B build -DSHM_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

### C++ 生产者

```cpp
#include "shm/shm_channel.hpp"

shm::ShmProducer producer("my_channel", 1024 * 1024);  // 1MB
producer.Write(data, len);
```

### Python 消费者

```python
from shm_channel import ShmConsumer

consumer = ShmConsumer("my_channel")
data = consumer.read()
```

### 跨语言演示

```bash
# 终端 1: C++ 生产者
./build/cpp_producer

# 终端 2: Python 消费者
cd py && python consumer_demo.py
```

## 性能 (x86-64, GCC 13, -O2)

| 消息大小 | 吞吐量 (单线程) | 跨线程 | 延迟 (写+读) |
|---------|----------------|--------|-------------|
| 64 B | 2.1 GB/s, 35M msg/s | 0.5 GB/s, 9M msg/s | 11 ns |
| 1 KB | 3.2 GB/s, 3.4M msg/s | 3.9 GB/s, 4.1M msg/s | 52 ns |
| 4 KB | 3.2 GB/s, 830K msg/s | 5.7 GB/s, 1.5M msg/s | 169 ns |
| 6 MB (1080p) | 2.5 GB/s, 423 msg/s | 4.4 GB/s, 763 msg/s | - |

1080p 帧 (6MB) 跨线程吞吐 4.4 GB/s, 理论支持 700+ FPS。30 FPS 1080p 仅需 ~180 MB/s 带宽, 占用 CPU < 1%。

## 项目结构

```
include/shm/
  shared_memory.hpp      -- 跨平台共享内存 (源自 libsharedmemory)
  byte_ring_buffer.hpp   -- SPSC 字节环形缓冲 (借鉴 ringbuffer)
  shm_channel.hpp        -- 高层 API: ShmProducer / ShmConsumer
py/
  byte_ring_buffer.py    -- Python SPSC 字节环形缓冲
  shm_channel.py         -- Python ShmProducer / ShmConsumer
  consumer_demo.py       -- OpenCV 视频消费者演示
tests/
  test_byte_ring_buffer.cpp   -- C++ 单元测试 (51 tests)
  test_cross_lang_producer.cpp -- 跨语言测试 (C++ 端)
  test_cross_lang_consumer.py  -- 跨语言测试 (Python 端)
  benchmark.cpp               -- 性能基准测试
examples/
  cpp_producer.cpp       -- C++ 视频帧生产者
docs/
  design_zh.md           -- 设计文档
```

## 依赖

- C++14 编译器 (GCC, Clang, MSVC)
- Python 3.8+ (Python 消费者)
- CMake 3.10+
- 无外部依赖

## 致谢

- [libsharedmemory](https://github.com/kyr0/libsharedmemory) -- 跨平台共享内存抽象
- [ringbuffer](https://gitee.com/liudegui/ringbuffer) -- 无锁 SPSC 环形缓冲设计

## 许可证

MIT License
