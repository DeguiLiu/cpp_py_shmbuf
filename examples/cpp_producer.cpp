#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "shm/shm_channel.hpp"

// Minimal C++ producer example (no OpenCV dependency).
// Writes numbered frames to shared memory.

static constexpr uint32_t kFrameSize = 1920 * 1080 * 3;  // 1080p BGR
static constexpr uint32_t kNumFrames = 10;                // buffer 10 frames

int main(int argc, char* argv[]) {
  const char* shm_name = "shm_video";
  if (argc > 1) shm_name = argv[1];

  // Clean up any leftover
  shm::RemoveSharedMemory(shm_name);

  // Create producer with capacity for ~10 frames
  uint32_t capacity = kFrameSize * kNumFrames;
  shm::ShmProducer producer(shm_name, capacity);

  if (!producer.IsValid()) {
    std::fprintf(stderr, "Failed to create shared memory: %s\n", shm_name);
    return 1;
  }

  std::printf("Producer ready: %s (capacity: %u bytes)\n",
              shm_name, producer.Capacity());
  std::printf("Waiting for consumer... Press Ctrl+C to stop.\n");

  // Simulate frame production
  std::vector<uint8_t> frame(kFrameSize);
  uint32_t frame_idx = 0;

  while (true) {
    // Fill frame with pattern (simulate camera capture)
    uint8_t val = static_cast<uint8_t>(frame_idx & 0xFF);
    std::memset(frame.data(), val, kFrameSize);

    // Stamp frame index in first 4 bytes
    std::memcpy(frame.data(), &frame_idx, sizeof(frame_idx));

    if (producer.Write(frame.data(), kFrameSize)) {
      if (frame_idx % 100 == 0) {
        std::printf("Wrote frame %u\n", frame_idx);
      }
      ++frame_idx;
    } else {
      // Buffer full, skip frame (consumer too slow)
    }

    // ~30 FPS
    usleep(33000);
  }

  producer.Destroy();
  return 0;
}
