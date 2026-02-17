#pragma once

#include "byte_ring_buffer.hpp"
#include "shared_memory.hpp"

namespace shm {

/// @brief High-level shared memory producer.
/// Creates shared memory and writes length-prefixed messages.
class ShmProducer {
 public:
  /// @param name      Shared memory name (alphanumeric, no leading '/').
  /// @param capacity  Desired data area size in bytes.
  ///                  Actual capacity is rounded down to power of 2.
  ShmProducer(const char* name, uint32_t capacity)
      : shm_(name, capacity + kHeaderSize, /*create=*/true, /*persist=*/true),
        ring_(nullptr) {
    if (shm_.Valid()) {
      ring_ = new (ring_storage_) ByteRingBuffer(
          shm_.Data(), static_cast<uint32_t>(shm_.Size()), /*is_producer=*/true);
    }
  }

  ~ShmProducer() {
    if (ring_) {
      ring_->~ByteRingBuffer();
      ring_ = nullptr;
    }
  }

  ShmProducer(const ShmProducer&) = delete;
  ShmProducer& operator=(const ShmProducer&) = delete;

  /// @brief Write a message to shared memory.
  bool Write(const void* data, uint32_t len) {
    return ring_ ? ring_->Write(data, len) : false;
  }

  bool IsValid() const { return ring_ != nullptr; }
  uint32_t WriteableBytes() const { return ring_ ? ring_->WriteableBytes() : 0; }
  uint32_t Capacity() const { return ring_ ? ring_->Capacity() : 0; }

  /// @brief Remove the shared memory object.
  void Destroy() { shm_.Destroy(); }

 private:
  SharedMemory shm_;
  ByteRingBuffer* ring_;
  alignas(ByteRingBuffer) char ring_storage_[sizeof(ByteRingBuffer)]{};
};

/// @brief High-level shared memory consumer.
/// Opens existing shared memory and reads length-prefixed messages.
class ShmConsumer {
 public:
  /// @param name  Shared memory name (must match producer).
  /// @param size  Expected total shared memory size (header + capacity).
  ///             If 0, a reasonable default is used.
  explicit ShmConsumer(const char* name, uint32_t size = 0)
      : shm_(name, size > 0 ? size : kDefaultSize, /*create=*/false, /*persist=*/true),
        ring_(nullptr) {
    if (shm_.Valid()) {
      ring_ = new (ring_storage_) ByteRingBuffer(
          shm_.Data(), static_cast<uint32_t>(shm_.Size()), /*is_producer=*/false);
    }
  }

  ~ShmConsumer() {
    if (ring_) {
      ring_->~ByteRingBuffer();
      ring_ = nullptr;
    }
  }

  ShmConsumer(const ShmConsumer&) = delete;
  ShmConsumer& operator=(const ShmConsumer&) = delete;

  /// @brief Read one message from shared memory.
  /// @return Payload length, or 0 if no data.
  uint32_t Read(void* out, uint32_t max_len) {
    return ring_ ? ring_->Read(out, max_len) : 0;
  }

  bool HasData() const { return ring_ && ring_->HasData(); }
  bool IsValid() const { return ring_ != nullptr; }
  uint32_t ReadableBytes() const { return ring_ ? ring_->ReadableBytes() : 0; }
  uint32_t Capacity() const { return ring_ ? ring_->Capacity() : 0; }

 private:
  static constexpr uint32_t kDefaultSize = 1920 * 1080 * 3 + kHeaderSize;
  SharedMemory shm_;
  ByteRingBuffer* ring_;
  alignas(ByteRingBuffer) char ring_storage_[sizeof(ByteRingBuffer)]{};
};

/// @brief Static helper to remove shared memory by name.
inline void RemoveSharedMemory(const char* name) {
  // Create a temporary SharedMemory just to call Destroy
  SharedMemory tmp(name, 16, /*create=*/false, /*persist=*/false);
  tmp.Destroy();
}

}  // namespace shm
