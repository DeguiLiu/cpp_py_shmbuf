#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace shm {

/// @brief Shared memory ring buffer header (POD, 16 bytes).
/// Stored at the beginning of the shared memory region.
/// Both C++ and Python read/write this structure via raw bytes.
struct RingHeader {
  uint32_t head;      // Producer write position (monotonically increasing)
  uint32_t tail;      // Consumer read position (monotonically increasing)
  uint32_t capacity;  // Data area size (must be power of 2)
  uint32_t reserved;  // Alignment padding
};

static constexpr uint32_t kHeaderSize = 16;
static_assert(sizeof(RingHeader) == kHeaderSize, "RingHeader must be 16 bytes");

/// @brief SPSC byte-level ring buffer for cross-language IPC.
///
/// Design inspired by spsc::Ringbuffer (gitee/liudegui/ringbuffer):
///   - Monotonically increasing head/tail indices (no buf_full flag)
///   - Power-of-2 capacity with bitmask wrap-around
///   - Memory fences for cross-process visibility
///
/// Memory layout in shared memory:
///   [0..15]  : RingHeader (head, tail, capacity, reserved)
///   [16..N]  : Data area (circular buffer)
///
/// Message format: [4-byte length (LE)][payload]
///
/// Thread/process safety: SPSC only (one producer, one consumer).
class ByteRingBuffer {
 public:
  /// @brief Bind to an existing shared memory region.
  /// @param shm_base  Pointer to the start of shared memory.
  /// @param total_size  Total shared memory size (header + data).
  /// @param is_producer  If true, initialize header on first use.
  ByteRingBuffer(void* shm_base, uint32_t total_size, bool is_producer)
      : header_(static_cast<RingHeader*>(shm_base)),
        data_(static_cast<uint8_t*>(shm_base) + kHeaderSize),
        is_producer_(is_producer) {
    if (is_producer) {
      // Producer initializes the header
      uint32_t data_size = total_size - kHeaderSize;
      // Round down to power of 2
      uint32_t cap = RoundDownPow2(data_size);
      header_->head = 0;
      header_->tail = 0;
      header_->capacity = cap;
      header_->reserved = 0;
      std::atomic_thread_fence(std::memory_order_release);
    } else {
      // Consumer reads capacity set by producer
      std::atomic_thread_fence(std::memory_order_acquire);
    }
    mask_ = header_->capacity - 1;
  }

  // ---- Producer API ----

  /// @brief Write a length-prefixed message: [4B len][payload].
  /// @return true if successful, false if not enough space.
  bool Write(const void* data, uint32_t len) {
    const uint32_t total = len + 4;  // 4-byte length prefix
    if (WriteableBytes() < total) {
      return false;
    }

    const uint32_t head = header_->head;

    // Write length prefix (little-endian)
    WriteRaw(head, &len, 4);
    // Write payload
    WriteRaw(head + 4, data, len);

    // Release fence: ensure data is visible before updating head
    std::atomic_thread_fence(std::memory_order_release);
    header_->head = head + total;
    return true;
  }

  /// @brief Available bytes for writing.
  uint32_t WriteableBytes() const {
    const uint32_t head = header_->head;
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t tail = header_->tail;
    return header_->capacity - (head - tail);
  }

  // ---- Consumer API ----

  /// @brief Read one length-prefixed message.
  /// @param[out] out  Buffer to receive payload.
  /// @param max_len  Maximum payload size.
  /// @return Payload length, or 0 if no data available.
  uint32_t Read(void* out, uint32_t max_len) {
    const uint32_t tail = header_->tail;
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t head = header_->head;

    const uint32_t available = head - tail;
    if (available < 4) {
      return 0;
    }

    // Read length prefix
    uint32_t msg_len = 0;
    ReadRaw(tail, &msg_len, 4);

    if (msg_len == 0 || available < msg_len + 4) {
      return 0;  // Incomplete message
    }

    if (msg_len > max_len) {
      // Message too large for output buffer; skip it
      std::atomic_thread_fence(std::memory_order_release);
      header_->tail = tail + msg_len + 4;
      return 0;
    }

    // Read payload
    ReadRaw(tail + 4, out, msg_len);

    // Release fence: ensure reads complete before updating tail
    std::atomic_thread_fence(std::memory_order_release);
    header_->tail = tail + msg_len + 4;
    return msg_len;
  }

  /// @brief Available bytes for reading.
  uint32_t ReadableBytes() const {
    const uint32_t tail = header_->tail;
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t head = header_->head;
    return head - tail;
  }

  /// @brief Check if there is at least one complete message.
  bool HasData() const { return ReadableBytes() >= 4; }

  /// @brief Get data area capacity.
  uint32_t Capacity() const { return header_->capacity; }

 private:
  /// @brief Write bytes into the circular data area.
  void WriteRaw(uint32_t pos, const void* src, uint32_t len) {
    const uint32_t offset = pos & mask_;
    const uint32_t first = header_->capacity - offset;

    if (first >= len) {
      std::memcpy(data_ + offset, src, len);
    } else {
      std::memcpy(data_ + offset, src, first);
      std::memcpy(data_, static_cast<const uint8_t*>(src) + first, len - first);
    }
  }

  /// @brief Read bytes from the circular data area.
  void ReadRaw(uint32_t pos, void* dst, uint32_t len) const {
    const uint32_t offset = pos & mask_;
    const uint32_t first = header_->capacity - offset;

    if (first >= len) {
      std::memcpy(dst, data_ + offset, len);
    } else {
      std::memcpy(dst, data_ + offset, first);
      std::memcpy(static_cast<uint8_t*>(dst) + first, data_, len - first);
    }
  }

  /// @brief Round down to the nearest power of 2.
  static uint32_t RoundDownPow2(uint32_t v) {
    if (v == 0) return 0;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return (v >> 1) + 1;
  }

  RingHeader* header_;
  uint8_t* data_;
  uint32_t mask_ = 0;
  bool is_producer_;
};

}  // namespace shm
