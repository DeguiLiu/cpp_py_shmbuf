#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "shm/byte_ring_buffer.hpp"
#include "shm/shm_channel.hpp"

static int passed = 0;
static int failed = 0;

#define CHECK(name, got, expected)                                          \
  do {                                                                      \
    if ((got) == (expected)) {                                              \
      ++passed;                                                             \
    } else {                                                                \
      ++failed;                                                             \
      std::printf("  FAIL: %s (line %d)\n", name, __LINE__);               \
    }                                                                       \
  } while (0)

// ---- ByteRingBuffer tests ----

void test_basic_write_read() {
  std::printf("test_basic_write_read\n");
  std::vector<uint8_t> mem(16 + 64, 0);  // header + 64B data
  shm::ByteRingBuffer ring(mem.data(), 16 + 64, true);

  CHECK("capacity", ring.Capacity(), 64u);
  CHECK("initial readable", ring.ReadableBytes(), 0u);
  CHECK("initial writeable", ring.WriteableBytes(), 64u);

  bool ok = ring.Write("hello", 5);
  CHECK("write ok", ok, true);
  CHECK("readable after write", ring.ReadableBytes(), 9u);  // 4 + 5

  char out[64] = {};
  uint32_t len = ring.Read(out, sizeof(out));
  CHECK("read len", len, 5u);
  CHECK("read data", std::memcmp(out, "hello", 5) == 0, true);
  CHECK("readable after read", ring.ReadableBytes(), 0u);
}

void test_multiple_messages() {
  std::printf("test_multiple_messages\n");
  std::vector<uint8_t> mem(16 + 256, 0);
  shm::ByteRingBuffer ring(mem.data(), 16 + 256, true);

  const char* msgs[] = {"msg1", "message_two", "3"};
  uint32_t lens[] = {4, 11, 1};

  for (int i = 0; i < 3; ++i) {
    bool ok = ring.Write(msgs[i], lens[i]);
    CHECK("write", ok, true);
  }

  for (int i = 0; i < 3; ++i) {
    char out[64] = {};
    uint32_t len = ring.Read(out, sizeof(out));
    CHECK("read len", len, lens[i]);
    CHECK("read data", std::memcmp(out, msgs[i], lens[i]) == 0, true);
  }

  char out[64] = {};
  CHECK("empty", ring.Read(out, sizeof(out)), 0u);
}

void test_wrap_around() {
  std::printf("test_wrap_around\n");
  std::vector<uint8_t> mem(16 + 32, 0);  // 32B data area
  shm::ByteRingBuffer ring(mem.data(), 16 + 32, true);

  // Fill most of buffer: 4 + 20 = 24 bytes
  char fill[20];
  std::memset(fill, 'A', 20);
  bool ok = ring.Write(fill, 20);
  CHECK("fill write", ok, true);

  // Read it back to advance tail
  char out[32] = {};
  ring.Read(out, sizeof(out));

  // Now write wrapping around: 4 + 20 = 24 bytes
  std::memset(fill, 'B', 20);
  ok = ring.Write(fill, 20);
  CHECK("wrap write", ok, true);

  uint32_t len = ring.Read(out, sizeof(out));
  CHECK("wrap read len", len, 20u);
  CHECK("wrap read data", out[0] == 'B' && out[19] == 'B', true);
}

void test_full_buffer() {
  std::printf("test_full_buffer\n");
  std::vector<uint8_t> mem(16 + 16, 0);  // 16B data area
  shm::ByteRingBuffer ring(mem.data(), 16 + 16, true);

  // 4 + 12 = 16, exactly full
  char data[12];
  std::memset(data, 'X', 12);
  bool ok = ring.Write(data, 12);
  CHECK("full write", ok, true);
  CHECK("writeable after full", ring.WriteableBytes(), 0u);

  // Should reject
  ok = ring.Write("Y", 1);
  CHECK("reject on full", ok, false);

  // Read back
  char out[16] = {};
  uint32_t len = ring.Read(out, sizeof(out));
  CHECK("read full", len, 12u);
}

void test_producer_consumer_views() {
  std::printf("test_producer_consumer_views\n");
  std::vector<uint8_t> mem(16 + 64, 0);

  // Producer writes
  shm::ByteRingBuffer prod(mem.data(), 16 + 64, true);
  prod.Write("cross-lang", 10);

  // Consumer reads (same memory, different view)
  shm::ByteRingBuffer cons(mem.data(), 16 + 64, false);
  CHECK("consumer capacity", cons.Capacity(), 64u);
  CHECK("consumer has_data", cons.HasData(), true);

  char out[64] = {};
  uint32_t len = cons.Read(out, sizeof(out));
  CHECK("consumer read len", len, 10u);
  CHECK("consumer read data", std::memcmp(out, "cross-lang", 10) == 0, true);
}

void test_large_message() {
  std::printf("test_large_message\n");
  std::vector<uint8_t> mem(16 + 8192, 0);
  shm::ByteRingBuffer ring(mem.data(), 16 + 8192, true);

  // 4096 byte payload
  std::vector<char> large(4096);
  for (int i = 0; i < 4096; ++i) large[i] = static_cast<char>(i & 0xFF);

  bool ok = ring.Write(large.data(), 4096);
  CHECK("large write", ok, true);

  std::vector<char> out(4096);
  uint32_t len = ring.Read(out.data(), 4096);
  CHECK("large read len", len, 4096u);
  CHECK("large read data", std::memcmp(out.data(), large.data(), 4096) == 0, true);
}

void test_has_data() {
  std::printf("test_has_data\n");
  std::vector<uint8_t> mem(16 + 64, 0);
  shm::ByteRingBuffer ring(mem.data(), 16 + 64, true);

  CHECK("empty has_data", ring.HasData(), false);
  ring.Write("x", 1);
  CHECK("non-empty has_data", ring.HasData(), true);
}

void test_message_too_large_for_output() {
  std::printf("test_message_too_large_for_output\n");
  std::vector<uint8_t> mem(16 + 64, 0);
  shm::ByteRingBuffer ring(mem.data(), 16 + 64, true);

  ring.Write("hello world!", 12);

  // Try to read with too-small buffer -> message is skipped
  char out[4] = {};
  uint32_t len = ring.Read(out, 4);
  CHECK("too small read", len, 0u);
  // Message was skipped, buffer should be empty now
  CHECK("empty after skip", ring.HasData(), false);
}

void test_round_down_pow2() {
  std::printf("test_round_down_pow2\n");
  // 100 bytes data area -> rounds down to 64
  std::vector<uint8_t> mem(16 + 100, 0);
  shm::ByteRingBuffer ring(mem.data(), 16 + 100, true);
  CHECK("round down 100->64", ring.Capacity(), 64u);

  // 128 bytes data area -> stays 128
  std::vector<uint8_t> mem2(16 + 128, 0);
  shm::ByteRingBuffer ring2(mem2.data(), 16 + 128, true);
  CHECK("exact pow2 128", ring2.Capacity(), 128u);

  // 33 bytes data area -> rounds down to 32
  std::vector<uint8_t> mem3(16 + 33, 0);
  shm::ByteRingBuffer ring3(mem3.data(), 16 + 33, true);
  CHECK("round down 33->32", ring3.Capacity(), 32u);
}

// ---- ShmChannel tests (requires POSIX/Windows shared memory) ----

void test_shm_channel() {
  std::printf("test_shm_channel\n");
  const char* name = "test_shm_v2";

  // Clean up
  shm::RemoveSharedMemory(name);

  {
    shm::ShmProducer prod(name, 1024);
    CHECK("producer valid", prod.IsValid(), true);
    CHECK("producer capacity", prod.Capacity() > 0, true);

    bool ok = prod.Write("hello from C++", 14);
    CHECK("producer write", ok, true);

    shm::ShmConsumer cons(name, 1024 + shm::kHeaderSize);
    CHECK("consumer valid", cons.IsValid(), true);
    CHECK("consumer has_data", cons.HasData(), true);

    char out[64] = {};
    uint32_t len = cons.Read(out, sizeof(out));
    CHECK("consumer read len", len, 14u);
    CHECK("consumer read data", std::memcmp(out, "hello from C++", 14) == 0, true);

    prod.Destroy();
  }
}

void test_shm_multiple_messages() {
  std::printf("test_shm_multiple_messages\n");
  const char* name = "test_shm_multi";
  shm::RemoveSharedMemory(name);

  {
    shm::ShmProducer prod(name, 4096);
    prod.Write("msg1", 4);
    prod.Write("msg2", 4);
    prod.Write("msg3", 4);

    shm::ShmConsumer cons(name, 4096 + shm::kHeaderSize);
    char out[64] = {};

    uint32_t len = cons.Read(out, sizeof(out));
    CHECK("read msg1", len == 4 && std::memcmp(out, "msg1", 4) == 0, true);

    len = cons.Read(out, sizeof(out));
    CHECK("read msg2", len == 4 && std::memcmp(out, "msg2", 4) == 0, true);

    len = cons.Read(out, sizeof(out));
    CHECK("read msg3", len == 4 && std::memcmp(out, "msg3", 4) == 0, true);

    CHECK("empty", cons.HasData(), false);
    prod.Destroy();
  }
}

int main() {
  std::printf("=== ByteRingBuffer Tests ===\n");
  test_basic_write_read();
  test_multiple_messages();
  test_wrap_around();
  test_full_buffer();
  test_producer_consumer_views();
  test_large_message();
  test_has_data();
  test_message_too_large_for_output();
  test_round_down_pow2();

  std::printf("\n=== ShmChannel Tests ===\n");
  test_shm_channel();
  test_shm_multiple_messages();

  std::printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
