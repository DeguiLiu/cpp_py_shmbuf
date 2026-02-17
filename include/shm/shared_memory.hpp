#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shm {

enum class Error : uint8_t {
  kOk = 0,
  kCreationFailed,
  kMappingFailed,
  kOpenFailed,
  kTruncateFailed,
};

/// @brief Cross-platform shared memory wrapper (POSIX shm_open / Win32 CreateFileMapping).
///
/// Extracted from libsharedmemory (github/kyr0), simplified for C++14:
///   - No exceptions, error via enum
///   - RAII resource management
///   - Supports create (producer) and open (consumer) modes
///   - Move-only (non-copyable)
class SharedMemory {
 public:
  /// @param name    Shared memory name (alphanumeric, no leading '/').
  /// @param size    Size in bytes. For consumer (create=false), pass 0 to auto-detect.
  /// @param create  true = producer creates; false = consumer opens existing.
  /// @param persist true = keep shm after destruction; false = unlink on close.
  SharedMemory(const char* name, std::size_t size, bool create, bool persist = true)
      : size_(size), persist_(persist), create_(create) {
    NormalizeName(name);
    error_ = CreateOrOpen();
  }

  ~SharedMemory() { Close(); }

  // Move-only
  SharedMemory(SharedMemory&& other) noexcept { MoveFrom(other); }
  SharedMemory& operator=(SharedMemory&& other) noexcept {
    if (this != &other) {
      Close();
      MoveFrom(other);
    }
    return *this;
  }
  SharedMemory(const SharedMemory&) = delete;
  SharedMemory& operator=(const SharedMemory&) = delete;

  void* Data() const noexcept { return data_; }
  std::size_t Size() const noexcept { return size_; }
  bool Valid() const noexcept { return data_ != nullptr; }
  Error GetError() const noexcept { return error_; }
  const char* Name() const noexcept { return name_; }

  /// @brief Unlink the shared memory object (POSIX only).
  void Destroy() noexcept {
#if !defined(_WIN32)
    if (name_[0] != '\0') {
      shm_unlink(name_);
    }
#endif
  }

 private:
  /// @brief Normalize name for platform conventions.
  void NormalizeName(const char* name) noexcept {
    std::size_t len = std::strlen(name);
#if defined(_WIN32)
    // Windows: strip leading '/' if present
    const char* src = (name[0] == '/') ? name + 1 : name;
    len = std::strlen(src);
    if (len > sizeof(name_) - 1) { len = sizeof(name_) - 1; }
    std::memcpy(name_, src, len);
    name_[len] = '\0';
#else
    // POSIX: ensure leading '/'
    if (name[0] == '/') {
      if (len > sizeof(name_) - 1) { len = sizeof(name_) - 1; }
      std::memcpy(name_, name, len);
      name_[len] = '\0';
    } else {
      if (len > sizeof(name_) - 2) { len = sizeof(name_) - 2; }
      name_[0] = '/';
      std::memcpy(name_ + 1, name, len);
      name_[len + 1] = '\0';
    }
#endif
  }

  Error CreateOrOpen() noexcept {
#if defined(_WIN32)
    return create_ ? WinCreate() : WinOpen();
#else
    return create_ ? PosixCreate() : PosixOpen();
#endif
  }

#if defined(_WIN32)
  Error WinCreate() noexcept {
    const DWORD hi = static_cast<DWORD>((size_ >> 32) & 0xFFFFFFFF);
    const DWORD lo = static_cast<DWORD>(size_ & 0xFFFFFFFF);
    handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                 PAGE_READWRITE, hi, lo, name_);
    if (!handle_) { return Error::kCreationFailed; }
    return MapView();
  }

  Error WinOpen() noexcept {
    handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name_);
    if (!handle_) { return Error::kOpenFailed; }
    return MapView();
  }

  Error MapView() noexcept {
    data_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, size_);
    if (!data_) {
      CloseHandle(handle_);
      handle_ = nullptr;
      return Error::kMappingFailed;
    }
    return Error::kOk;
  }
#else
  Error PosixCreate() noexcept {
    // Remove stale segment (macOS refuses ftruncate on existing)
    shm_unlink(name_);

    fd_ = shm_open(name_, O_CREAT | O_RDWR, 0666);
    if (fd_ == -1) { return Error::kCreationFailed; }

    if (ftruncate(fd_, static_cast<off_t>(size_)) == -1) {
      ::close(fd_);
      fd_ = -1;
      shm_unlink(name_);
      return Error::kTruncateFailed;
    }
    return MapRegion();
  }

  Error PosixOpen() noexcept {
    fd_ = shm_open(name_, O_RDWR, 0666);
    if (fd_ == -1) { return Error::kOpenFailed; }

    // Auto-detect size if caller passed 0
    if (size_ == 0) {
      struct stat st {};
      if (fstat(fd_, &st) == 0 && st.st_size > 0) {
        size_ = static_cast<std::size_t>(st.st_size);
      }
    }
    return MapRegion();
  }

  Error MapRegion() noexcept {
    // Both sides need PROT_WRITE (consumer writes tail index)
    data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED) {
      ::close(fd_);
      fd_ = -1;
      if (create_) { shm_unlink(name_); }
      data_ = nullptr;
      return Error::kMappingFailed;
    }
    return Error::kOk;
  }
#endif

  void Close() noexcept {
    if (!data_) { return; }

#if defined(_WIN32)
    UnmapViewOfFile(data_);
    if (handle_) { CloseHandle(handle_); handle_ = nullptr; }
#else
    munmap(data_, size_);
    if (fd_ != -1) { ::close(fd_); fd_ = -1; }
    if (!persist_ && create_) { Destroy(); }
#endif
    data_ = nullptr;
  }

  void MoveFrom(SharedMemory& other) noexcept {
    std::memcpy(name_, other.name_, sizeof(name_));
    data_ = other.data_;
    size_ = other.size_;
    persist_ = other.persist_;
    create_ = other.create_;
    error_ = other.error_;
#if defined(_WIN32)
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.data_ = nullptr;
    other.name_[0] = '\0';
  }

  // Members ordered by alignment (pointer, size_t, int, bool, char[])
  void* data_ = nullptr;
  std::size_t size_ = 0;
#if defined(_WIN32)
  HANDLE handle_ = nullptr;
#else
  int fd_ = -1;
#endif
  Error error_ = Error::kOk;
  bool persist_ = true;
  bool create_ = false;
  char name_[64] = {};  // 128 -> 64 (POSIX shm names are typically < 32 chars)
};

}  // namespace shm
