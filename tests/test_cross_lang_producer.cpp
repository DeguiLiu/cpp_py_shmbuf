// Cross-language test: C++ producer writes, then exits.
// Run Python consumer after to verify data integrity.
#include <cstdio>
#include <cstring>
#include "shm/shm_channel.hpp"

int main() {
  const char* name = "test_cross_lang";
  shm::RemoveSharedMemory(name);

  shm::ShmProducer prod(name, 4096);
  if (!prod.IsValid()) {
    std::fprintf(stderr, "Failed to create shm\n");
    return 1;
  }

  // Write 5 messages
  const char* msgs[] = {"hello_from_cpp", "message_2", "cross_language_test", "1234567890", "end"};
  for (int i = 0; i < 5; ++i) {
    uint32_t len = static_cast<uint32_t>(std::strlen(msgs[i]));
    prod.Write(msgs[i], len);
    std::printf("C++ wrote: %s\n", msgs[i]);
  }

  std::printf("C++ producer done. Run Python consumer now.\n");
  // Don't destroy - let Python read it
  return 0;
}
