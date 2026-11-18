// SPDX-License-Identifier: LGPL-3.0-only
//
// Helpers shared by the unit tests.

#ifndef GHOTI_IO_GLECMA_TESTS_TEST_HELPERS_H
#define GHOTI_IO_GLECMA_TESTS_TEST_HELPERS_H

#include <ghoti.io/lang-ecmascript/macros.h>

#include <ghoti.io/lang-ecmascript/allocator.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

namespace glecma_test {

// An allocator that counts every request and every live block, and refuses
// the request whose zero-based index is fail_at. It is how a test sees that
// a refusal allocated nothing (calls stays 0) and that a failure freed
// everything (live returns to 0), and how a sweep fails each allocation of
// an operation in turn.
class CountingAllocator {
 public:
  CountingAllocator() {
    allocator_.ctx = this;
    allocator_.malloc_fn = &CountingAllocator::Malloc;
    allocator_.calloc_fn = &CountingAllocator::Calloc;
    allocator_.realloc_fn = &CountingAllocator::Realloc;
    allocator_.free_fn = &CountingAllocator::Free;
  }

  CountingAllocator(const CountingAllocator &) = delete;
  CountingAllocator & operator=(const CountingAllocator &) = delete;

  const GLECMA_Allocator * get() const { return &allocator_; }

  // Requests made so far, refused ones included.
  size_t calls() const { return calls_; }
  // Blocks handed out and not yet freed.
  size_t live() const { return live_; }
  // Refuse the request with this index (SIZE_MAX refuses none).
  void fail_at(size_t index) { fail_at_ = index; }
  void reset() {
    calls_ = 0;
    fail_at_ = SIZE_MAX;
  }

 private:
  bool refuse() { return calls_++ == fail_at_; }

  static void * Malloc(void * ctx, size_t size) {
    auto * self = static_cast<CountingAllocator *>(ctx);
    if (self->refuse()) {
      return nullptr;
    }
    void * p = std::malloc(size == 0 ? 1 : size);
    if (p != nullptr) {
      self->live_++;
    }
    return p;
  }

  static void * Calloc(void * ctx, size_t count, size_t size) {
    auto * self = static_cast<CountingAllocator *>(ctx);
    if (self->refuse()) {
      return nullptr;
    }
    if (size != 0 && count > SIZE_MAX / size) {
      return nullptr;
    }
    void * p = std::calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (p != nullptr) {
      self->live_++;
    }
    return p;
  }

  static void * Realloc(void * ctx, void * old, size_t size) {
    auto * self = static_cast<CountingAllocator *>(ctx);
    if (self->refuse()) {
      return nullptr;
    }
    void * p = std::realloc(old, size == 0 ? 1 : size);
    if (p != nullptr && old == nullptr) {
      self->live_++;
    }
    return p;
  }

  static void Free(void * ctx, void * p) {
    auto * self = static_cast<CountingAllocator *>(ctx);
    if (p != nullptr) {
      self->live_--;
      std::free(p);
    }
  }

  GLECMA_Allocator allocator_{};
  size_t calls_ = 0;
  size_t live_ = 0;
  size_t fail_at_ = SIZE_MAX;
};

// What `write` writes to a stream, as text, and what it returned: through a
// temporary file, because open_memstream is POSIX and a Windows C runtime has
// none.
template <typename F>
std::string Capture(F write, GLECMA_Result * result) {
  std::FILE * file = std::tmpfile();
  if (file == nullptr) {
    *result = GLECMA_ERR_IO;
    return std::string();
  }
  *result = write(file);
  std::fflush(file);
  std::rewind(file);
  std::string text;
  char chunk[4096];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0) {
    text.append(chunk, got);
  }
  std::fclose(file);
  return text;
}

// An allocator that meters what is live: the most bytes ever held at once
// (peak_bytes), the requests made (calls) and the bytes held now (live_bytes).
// Every block carries a header that says its size, so a realloc and a free
// are accounted exactly. It is how a sweep sees memory grow, deterministically,
// where a process's resident size would be a measurement of the allocator and
// the machine.
class MeteringAllocator {
 public:
  MeteringAllocator() { Reset(); }
  MeteringAllocator(const MeteringAllocator &) = delete;
  MeteringAllocator & operator=(const MeteringAllocator &) = delete;

  const GLECMA_Allocator * get() const { return &allocator_; }
  size_t peak_bytes() const { return peak_; }
  size_t live_bytes() const { return live_; }
  size_t calls() const { return calls_; }
  void Reset() {
    allocator_.ctx = this;
    allocator_.malloc_fn = &MeteringAllocator::Malloc;
    allocator_.calloc_fn = &MeteringAllocator::Calloc;
    allocator_.realloc_fn = &MeteringAllocator::Realloc;
    allocator_.free_fn = &MeteringAllocator::Free;
    peak_ = live_ = calls_ = 0;
  }

 private:
  // 16 bytes keeps the block the allocator returns aligned as malloc's is.
  struct alignas(16) Header {
    size_t size;
  };

  void Add(size_t bytes) {
    live_ += bytes;
    if (live_ > peak_) {
      peak_ = live_;
    }
  }

  static void * Malloc(void * ctx, size_t size) {
    auto * self = static_cast<MeteringAllocator *>(ctx);
    self->calls_++;
    if (size > SIZE_MAX - sizeof(Header)) {
      return nullptr;
    }
    auto * h = static_cast<Header *>(std::malloc(sizeof(Header) + size));
    if (h == nullptr) {
      return nullptr;
    }
    h->size = size;
    self->Add(size);
    return h + 1;
  }

  static void * Calloc(void * ctx, size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) {
      return nullptr;
    }
    void * p = Malloc(ctx, count * size);
    if (p != nullptr) {
      std::memset(p, 0, count * size);
    }
    return p;
  }

  static void * Realloc(void * ctx, void * old, size_t size) {
    auto * self = static_cast<MeteringAllocator *>(ctx);
    if (old == nullptr) {
      return Malloc(ctx, size);
    }
    self->calls_++;
    if (size > SIZE_MAX - sizeof(Header)) {
      return nullptr;
    }
    Header * h = static_cast<Header *>(old) - 1;
    const size_t before = h->size;
    auto * grown = static_cast<Header *>(std::realloc(h, sizeof(Header) + size));
    if (grown == nullptr) {
      return nullptr;
    }
    grown->size = size;
    self->live_ -= before;
    self->Add(size);
    return grown + 1;
  }

  static void Free(void * ctx, void * p) {
    auto * self = static_cast<MeteringAllocator *>(ctx);
    if (p == nullptr) {
      return;
    }
    Header * h = static_cast<Header *>(p) - 1;
    self->live_ -= h->size;
    std::free(h);
  }

  GLECMA_Allocator allocator_{};
  size_t peak_ = 0;
  size_t live_ = 0;
  size_t calls_ = 0;
};

}  // namespace glecma_test

#endif  // GHOTI_IO_GLECMA_TESTS_TEST_HELPERS_H
