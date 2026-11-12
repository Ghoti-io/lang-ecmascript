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

}  // namespace glecma_test

#endif  // GHOTI_IO_GLECMA_TESTS_TEST_HELPERS_H
