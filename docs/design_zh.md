# cpp_py_shmbuf 设计文档

版本: 2.0 | 日期: 2026-02-17

## 1. 现状分析

### 1.1 现有架构问题

| 问题 | 影响 | 严重度 |
|------|------|--------|
| Boost.Interprocess 依赖 | 编译慢、部署复杂、仅 C++ 端需要 | 高 |
| `__sync_synchronize` 全屏障 | 过时 API、ARM 上性能差、语义不精确 | 高 |
| buf_full flag 竞态 | 生产者/消费者同时读写 flag，无原子保护 | 高 |
| 手写 SPSC ring buffer | 无 cache-line 对齐、无批量操作、wrap-around 逻辑复杂 | 中 |
| Python `mprt_monkeypatch` | hack 式修复 Python resource_tracker bug | 低 |
| 仅 Linux/macOS | 不支持 Windows | 中 |

### 1.2 可用组件

| 组件 | 来源 | 用途 |
|------|------|------|
| libsharedmemory `Memory` | github/kyr0 | 跨平台共享内存 (shm_open / CreateFileMapping) |
| ringbuffer `Ringbuffer` | gitee/liudegui | Lock-free SPSC 设计模式 (atomic head/tail, cache-line padding) |
| hsm-cpp `StateMachine` | gitee/liudegui | 连接状态管理 |
| mem_pool `FixedPool` | gitee/liudegui | 消息帧缓冲池 |

## 2. 设计决策

### 2.1 跨语言 atomic 约束

C++ `std::atomic` 在共享内存中对 Python 不可见。Python 只能通过 `struct.pack/unpack` 读写原始字节。

解决方案: **原始字节 + 约定协议**

```
共享内存布局 (字节级协议):
  [0..3]   : head index  (uint32_t LE, producer 写, consumer 读)
  [4..7]   : tail index  (uint32_t LE, consumer 写, producer 读)
  [8..11]  : capacity    (uint32_t LE, 创建时写入, 只读)
  [12..15] : reserved    (对齐填充)
  [16..N]  : data area   (环形缓冲区)
```

内存序保证:
- C++ 端: 使用 `std::atomic_thread_fence(acquire/release)` 在读写索引前后插入屏障
- Python 端: CPython GIL + `struct.pack_into` 保证 4 字节对齐写入的原子性 (x86/ARM 上 aligned uint32 写入天然原子)
- 关键: head/tail 使用单调递增索引 (不回绕), 通过 `& mask` 计算实际偏移, 消除 buf_full flag

### 2.2 消除 buf_full flag

现有设计用 `buf_full` flag 区分 "空" 和 "满" (head == tail 时)。这个 flag 被两端同时读写，存在竞态。

新设计采用 ringbuffer 的方案: **单调递增索引 + power-of-2 容量**

```
available_to_read  = head - tail
available_to_write = capacity - (head - tail)
empty: head == tail
full:  head - tail == capacity
```

索引自然溢出 (uint32_t wrap at 4GB)，对于实际缓冲区大小 (MB 级) 完全安全。

### 2.3 组件集成策略

| 组件 | 集成方式 | 说明 |
|------|----------|------|
| libsharedmemory | 提取 `Memory` 类, 简化 | 仅用其跨平台 shm_open/CreateFileMapping 封装 |
| ringbuffer | 借鉴设计模式, 不直接使用 | 原库是模板类存储 T 元素; 我们需要字节级环形缓冲 |
| hsm-cpp | 可选集成 | 管理 Producer/Consumer 连接状态 |
| mem_pool | 不集成 | 视频帧直接写入共享内存, 无需额外缓冲池 |

### 2.4 为什么不直接用 ringbuffer?

`spsc::Ringbuffer<T>` 是模板类，存储固定大小的 T 元素。但视频流场景需要:
1. 字节级操作 (变长消息: 4B header + payload)
2. 共享内存中的布局必须是 POD (无 C++ 对象)
3. Python 端需要直接读写原始字节

因此: 借鉴 ringbuffer 的 **设计模式** (单调递增索引、power-of-2 mask、cache-line 概念)，但实现为字节级环形缓冲。

## 3. 新架构

```
+------------------+     Cross-platform Shared Memory     +------------------+
|  C++ Producer    | ==================================>  |  Python Consumer |
|  (ShmProducer)   |     SPSC Byte Ring Buffer            |  (ShmConsumer)   |
+------------------+                                       +------------------+
        |                                                          |
        v                                                          v
  SharedMemory (C++)                                    SharedMemory (Python)
  - shm_open / CreateFileMapping                        - multiprocessing.shared_memory
  - ByteRingBuffer (atomic fences)                      - ByteRingBuffer (struct.pack)
```

## 4. 文件结构

```
cpp_py_shmbuf_sample/
  include/
    shm/
      shared_memory.hpp      -- 跨平台共享内存 (from libsharedmemory, simplified)
      byte_ring_buffer.hpp   -- SPSC 字节环形缓冲 (inspired by ringbuffer)
      shm_channel.hpp        -- 高层 API: ShmProducer / ShmConsumer
  examples/
    cpp_producer.cpp         -- C++ 视频帧生产者
    cpp_consumer.cpp         -- C++ 消费者 (验证 C++ 端读取)
  py/
    shm_channel.py           -- Python 高层 API: ShmProducer / ShmConsumer
    byte_ring_buffer.py      -- Python SPSC 字节环形缓冲
    consumer_demo.py         -- Python 视频帧消费者
  tests/
    test_byte_ring_buffer.cpp
    test_shm_channel.cpp
  CMakeLists.txt
  README.md
```

## 5. 接口设计

### 5.1 C++ SharedMemory (from libsharedmemory)

```cpp
namespace shm {

enum class Error : uint8_t {
  kOk = 0,
  kCreationFailed,
  kMappingFailed,
  kOpenFailed,
};

class SharedMemory {
 public:
  SharedMemory(const char* name, std::size_t size, bool create, bool persist = true);
  ~SharedMemory();

  void* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  bool valid() const noexcept { return data_ != nullptr; }
  Error error() const noexcept { return error_; }

  void destroy();  // shm_unlink / close handle

 private:
  // ... platform-specific handles
};

}  // namespace shm
```

### 5.2 C++ ByteRingBuffer

```cpp
namespace shm {

// 共享内存中的环形缓冲区头部 (POD, 16 bytes)
struct RingHeader {
  uint32_t head;      // producer 写入位置 (单调递增)
  uint32_t tail;      // consumer 读取位置 (单调递增)
  uint32_t capacity;  // 数据区大小 (power of 2)
  uint32_t reserved;  // 对齐填充
};

static constexpr uint32_t kHeaderSize = sizeof(RingHeader);  // 16

class ByteRingBuffer {
 public:
  // 绑定到已有的共享内存区域
  ByteRingBuffer(void* shm_base, uint32_t total_size, bool is_producer);

  // Producer API
  bool Write(const void* data, uint32_t len);       // 写入 [4B len][payload]
  uint32_t WriteableBytes() const;

  // Consumer API
  uint32_t Read(void* out, uint32_t max_len);        // 读取一条消息, 返回 payload 长度
  uint32_t ReadableBytes() const;
  bool HasData() const;

 private:
  RingHeader* header_;
  uint8_t* data_;       // header_ + kHeaderSize
  uint32_t mask_;       // capacity - 1
  bool is_producer_;

  void WriteBytes(const void* src, uint32_t len);    // 内部: 处理 wrap-around
  void ReadBytes(void* dst, uint32_t len);            // 内部: 处理 wrap-around
};

}  // namespace shm
```

### 5.3 C++ ShmChannel (高层 API)

```cpp
namespace shm {

class ShmProducer {
 public:
  ShmProducer(const char* name, uint32_t capacity);
  ~ShmProducer();

  bool Write(const void* data, uint32_t len);
  bool IsValid() const;

 private:
  SharedMemory shm_;
  ByteRingBuffer ring_;
};

class ShmConsumer {
 public:
  explicit ShmConsumer(const char* name);
  ~ShmConsumer();

  uint32_t Read(void* out, uint32_t max_len);
  bool HasData() const;
  bool IsValid() const;

 private:
  SharedMemory shm_;
  ByteRingBuffer ring_;
};

}  // namespace shm
```

### 5.4 Python ByteRingBuffer

```python
class ByteRingBuffer:
    """SPSC byte ring buffer, compatible with C++ ByteRingBuffer."""

    HEADER_SIZE = 16  # head(4) + tail(4) + capacity(4) + reserved(4)

    def __init__(self, buf: memoryview, is_producer: bool = False):
        ...

    def write(self, data: bytes) -> bool:
        """Write length-prefixed message: [4B len][payload]"""
        ...

    def read(self) -> Optional[bytes]:
        """Read one length-prefixed message."""
        ...

    def has_data(self) -> bool: ...
    def readable_bytes(self) -> int: ...
    def writeable_bytes(self) -> int: ...
```

### 5.5 Python ShmChannel

```python
class ShmProducer:
    def __init__(self, name: str, capacity: int): ...
    def write(self, data: bytes) -> bool: ...

class ShmConsumer:
    def __init__(self, name: str): ...
    def read(self) -> Optional[bytes]: ...
    def has_data(self) -> bool: ...
```

## 6. 内存序协议

### 6.1 C++ Producer 写入

```cpp
bool ByteRingBuffer::Write(const void* data, uint32_t len) {
  uint32_t head = header_->head;                    // relaxed (own variable)
  std::atomic_thread_fence(std::memory_order_acquire);
  uint32_t tail = header_->tail;                    // acquire fence before read

  uint32_t available = mask_ + 1 - (head - tail);
  uint32_t total = len + 4;  // 4B header
  if (available < total) return false;

  WriteBytes(&len, 4);                              // length prefix
  WriteBytes(data, len);                            // payload

  std::atomic_thread_fence(std::memory_order_release);
  header_->head = head + total;                     // release fence before write
  return true;
}
```

### 6.2 Python Consumer 读取

```python
def read(self) -> Optional[bytes]:
    tail = self._read_u32(4)   # own variable
    # acquire: Python GIL + aligned read = atomic on x86/ARM
    head = self._read_u32(0)

    available = head - tail
    if available < 4:
        return None

    msg_len = self._read_data_u32(tail)
    if available < msg_len + 4:
        return None

    data = self._read_data(tail + 4, msg_len)

    # release: write tail after reading data
    self._write_u32(4, tail + msg_len + 4)
    return data
```

### 6.3 跨语言原子性保证

| 操作 | x86-64 | ARM | 保证 |
|------|--------|-----|------|
| aligned uint32 read | 原子 | 原子 (ARMv6+) | 硬件保证 |
| aligned uint32 write | 原子 | 原子 (ARMv6+) | 硬件保证 |
| C++ acquire fence | mfence/lfence | dmb ish | 编译器+硬件 |
| Python struct.pack_into | GIL 保护 | GIL 保护 | CPython 实现 |

关键假设:
1. 同架构 (不跨字节序)
2. CPython (有 GIL)
3. ARMv6+ 或 x86 (aligned access 原子)

## 7. 对比现有方案

| 维度 | 旧版本 | 当前版本 |
|------|--------|----------|
| 共享内存 | Boost.Interprocess | POSIX shm_open / Win32 CreateFileMapping |
| 依赖 | Boost, OpenCV | 零依赖 (header-only) |
| 内存序 | `__sync_synchronize` (全屏障) | `atomic_thread_fence(acquire/release)` |
| 空/满判断 | buf_full flag (竞态) | 单调递增索引 (无竞态) |
| 索引回绕 | 手动 if/else | `& mask` 位运算 |
| 平台 | Linux/macOS | Linux/macOS/Windows |
| Python hack | mprt_monkeypatch | 不需要 (persist=true) |
| 批量操作 | 无 | memcpy 连续块 |
| 消息协议 | [4B len][payload] | [4B len][payload] (兼容) |

## 8. 资源预算

| 资源 | 大小 | 说明 |
|------|------|------|
| RingHeader | 16 B | head + tail + capacity + reserved |
| 数据区 (1080p x 100帧) | ~593 MB | 1920x1080x3 x 100 (power-of-2 向上取整) |
| 数据区 (1080p x 10帧) | ~64 MB | 实际推荐大小 |
| C++ 头文件 | ~400 行 | 3 个头文件 |
| Python 模块 | ~200 行 | 2 个文件 |

## 9. 实现计划

| 步骤 | 文件 | 说明 |
|------|------|------|
| 1 | `include/shm/shared_memory.hpp` | 从 libsharedmemory 提取+简化 Memory 类 |
| 2 | `include/shm/byte_ring_buffer.hpp` | SPSC 字节环形缓冲 (借鉴 ringbuffer 设计) |
| 3 | `include/shm/shm_channel.hpp` | 高层 ShmProducer/ShmConsumer |
| 4 | `py/byte_ring_buffer.py` | Python 环形缓冲 (兼容 C++ 布局) |
| 5 | `py/shm_channel.py` | Python ShmProducer/ShmConsumer |
| 6 | `tests/test_byte_ring_buffer.cpp` | C++ 单元测试 |
| 7 | `examples/cpp_producer.cpp` | 视频帧生产者示例 |
| 8 | `py/consumer_demo.py` | Python 消费者示例 |
| 9 | `README.md` | 更新文档 |
