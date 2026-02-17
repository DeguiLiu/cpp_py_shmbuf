#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "shm/byte_ring_buffer.hpp"
#include "shm/shm_channel.hpp"

using Clock = std::chrono::high_resolution_clock;

// ---- Benchmark 1: ByteRingBuffer throughput (in-process, same memory) ----

static void bench_ring_throughput(uint32_t msg_size, uint32_t iterations) {
  // Buffer: 64MB data area
  constexpr uint32_t kBufSize = 64 * 1024 * 1024;
  std::vector<uint8_t> mem(shm::kHeaderSize + kBufSize, 0);
  std::vector<char> payload(msg_size, 'X');
  std::vector<char> out(msg_size);

  shm::ByteRingBuffer ring(mem.data(), shm::kHeaderSize + kBufSize, true);

  // Warm up
  for (uint32_t i = 0; i < 100; ++i) {
    ring.Write(payload.data(), msg_size);
    ring.Read(out.data(), msg_size);
  }

  // Benchmark write
  auto t0 = Clock::now();
  uint32_t written = 0;
  for (uint32_t i = 0; i < iterations; ++i) {
    if (ring.Write(payload.data(), msg_size)) {
      ++written;
    } else {
      // Drain to make space
      while (ring.HasData()) ring.Read(out.data(), msg_size);
      ring.Write(payload.data(), msg_size);
      ++written;
    }
  }
  // Drain remaining
  while (ring.HasData()) ring.Read(out.data(), msg_size);
  auto t1 = Clock::now();

  double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  double total_bytes = static_cast<double>(written) * msg_size;
  double throughput_gbps = (total_bytes / (1024.0 * 1024.0 * 1024.0)) / (elapsed_ms / 1000.0);
  double msg_per_sec = written / (elapsed_ms / 1000.0);

  std::printf("  msg_size=%6u  iterations=%7u  time=%.1fms  throughput=%.2f GB/s  %.0f msg/s\n",
              msg_size, written, elapsed_ms, throughput_gbps, msg_per_sec);
}

// ---- Benchmark 2: Cross-thread SPSC (producer thread + consumer thread) ----

struct ThreadBenchResult {
  uint64_t total_bytes = 0;
  uint32_t msg_count = 0;
  double elapsed_ms = 0;
};

static void bench_cross_thread(uint32_t msg_size, uint32_t iterations) {
  constexpr uint32_t kBufSize = 64 * 1024 * 1024;
  std::vector<uint8_t> mem(shm::kHeaderSize + kBufSize, 0);
  shm::ByteRingBuffer ring(mem.data(), shm::kHeaderSize + kBufSize, true);

  std::vector<char> payload(msg_size, 'Y');
  ThreadBenchResult result{};

  auto t0 = Clock::now();

  // Consumer thread
  std::thread consumer([&]() {
    std::vector<char> out(msg_size);
    uint32_t count = 0;
    while (count < iterations) {
      uint32_t len = ring.Read(out.data(), msg_size);
      if (len > 0) {
        ++count;
      }
    }
    result.msg_count = count;
  });

  // Producer (main thread)
  uint32_t sent = 0;
  while (sent < iterations) {
    if (ring.Write(payload.data(), msg_size)) {
      ++sent;
    }
  }

  consumer.join();
  auto t1 = Clock::now();

  result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  result.total_bytes = static_cast<uint64_t>(result.msg_count) * msg_size;

  double throughput_gbps = (static_cast<double>(result.total_bytes) / (1024.0 * 1024.0 * 1024.0))
                           / (result.elapsed_ms / 1000.0);
  double msg_per_sec = result.msg_count / (result.elapsed_ms / 1000.0);

  std::printf("  msg_size=%6u  msgs=%7u  time=%.1fms  throughput=%.2f GB/s  %.0f msg/s\n",
              msg_size, result.msg_count, result.elapsed_ms, throughput_gbps, msg_per_sec);
}

// ---- Benchmark 3: ShmChannel (cross-process simulation via threads) ----

static void bench_shm_channel(uint32_t msg_size, uint32_t iterations) {
  const char* name = "bench_shm";
  shm::RemoveSharedMemory(name);

  uint32_t capacity = 64 * 1024 * 1024;
  shm::ShmProducer producer(name, capacity);
  if (!producer.IsValid()) {
    std::printf("  SKIP: failed to create shm\n");
    return;
  }

  shm::ShmConsumer consumer(name, capacity + shm::kHeaderSize);
  if (!consumer.IsValid()) {
    std::printf("  SKIP: failed to open shm\n");
    producer.Destroy();
    return;
  }

  std::vector<char> payload(msg_size, 'Z');
  std::vector<char> out(msg_size);

  auto t0 = Clock::now();

  // Consumer thread
  std::thread reader([&]() {
    uint32_t count = 0;
    while (count < iterations) {
      uint32_t len = consumer.Read(out.data(), msg_size);
      if (len > 0) ++count;
    }
  });

  // Producer (main thread)
  uint32_t sent = 0;
  while (sent < iterations) {
    if (producer.Write(payload.data(), msg_size)) ++sent;
  }

  reader.join();
  auto t1 = Clock::now();

  double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  double total_bytes = static_cast<double>(iterations) * msg_size;
  double throughput_gbps = (total_bytes / (1024.0 * 1024.0 * 1024.0)) / (elapsed_ms / 1000.0);

  std::printf("  msg_size=%6u  msgs=%7u  time=%.1fms  throughput=%.2f GB/s\n",
              msg_size, iterations, elapsed_ms, throughput_gbps);

  producer.Destroy();
}

// ---- Benchmark 4: Latency (round-trip) ----

static void bench_latency(uint32_t msg_size) {
  // Buffer must fit at least one message (header + payload + 4B length prefix)
  uint32_t kBufSize = 1024 * 1024;
  if (msg_size + 16 > kBufSize) { kBufSize = msg_size * 2 + 64; }
  // Round up to power of 2
  uint32_t v = kBufSize - 1;
  v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
  kBufSize = v + 1;
  std::vector<uint8_t> mem(shm::kHeaderSize + kBufSize, 0);
  shm::ByteRingBuffer ring(mem.data(), shm::kHeaderSize + kBufSize, true);

  std::vector<char> payload(msg_size, 'L');
  std::vector<char> out(msg_size);

  constexpr uint32_t kRounds = 100000;

  // Warm up
  for (uint32_t i = 0; i < 1000; ++i) {
    ring.Write(payload.data(), msg_size);
    ring.Read(out.data(), msg_size);
  }

  auto t0 = Clock::now();
  for (uint32_t i = 0; i < kRounds; ++i) {
    ring.Write(payload.data(), msg_size);
    ring.Read(out.data(), msg_size);
  }
  auto t1 = Clock::now();

  double elapsed_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
  double per_op_ns = elapsed_ns / kRounds;

  std::printf("  msg_size=%6u  rounds=%u  avg_latency=%.0f ns (write+read)\n",
              msg_size, kRounds, per_op_ns);
}

int main() {
  std::printf("=== Benchmark 1: ByteRingBuffer Throughput (single-thread) ===\n");
  bench_ring_throughput(64, 1000000);
  bench_ring_throughput(1024, 500000);
  bench_ring_throughput(4096, 200000);
  bench_ring_throughput(1920 * 1080 * 3, 100);  // 1080p frame

  std::printf("\n=== Benchmark 2: Cross-Thread SPSC ===\n");
  bench_cross_thread(64, 1000000);
  bench_cross_thread(1024, 500000);
  bench_cross_thread(4096, 200000);
  bench_cross_thread(1920 * 1080 * 3, 100);

  std::printf("\n=== Benchmark 3: ShmChannel (shared memory) ===\n");
  bench_shm_channel(64, 1000000);
  bench_shm_channel(1024, 500000);
  bench_shm_channel(4096, 200000);
  bench_shm_channel(1920 * 1080 * 3, 100);

  std::printf("\n=== Benchmark 4: Write+Read Latency ===\n");
  bench_latency(64);
  bench_latency(1024);
  bench_latency(4096);
  bench_latency(65536);  // 64KB (practical upper bound for latency test)

  return 0;
}
