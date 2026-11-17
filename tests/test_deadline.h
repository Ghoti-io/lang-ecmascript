//
// SPDX-License-Identifier: LGPL-3.0-only
//
// Copyright (C) 2026 Corey Pennycuff
//
// This file is part of Ghoti.io Lang-ecmascript.
//
// Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License version 3
// as published by the Free Software Foundation.
//
// Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
// General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// A deadline for a test that guards against work growing without bound: a
// regression there is a parse that does not finish, which would hang the
// suite instead of failing it. If the scope is not left within the time, the
// process is ended with a message and a non-zero status.

#ifndef GHOTI_IO_GLECMA_TESTS_TEST_DEADLINE_H
#define GHOTI_IO_GLECMA_TESTS_TEST_DEADLINE_H

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace glecma_test {

class Deadline {
 public:
  explicit Deadline(std::chrono::seconds limit, const char * what)
      : watcher_([this, limit, what] {
          std::unique_lock<std::mutex> lock(mutex_);
          if (!cv_.wait_for(lock, limit, [this] { return done_; })) {
            std::fprintf(stderr, "deadline of %lld s passed in: %s\n",
                static_cast<long long>(limit.count()), what);
            std::_Exit(3);
          }
        }) {}

  Deadline(const Deadline &) = delete;
  Deadline & operator=(const Deadline &) = delete;

  ~Deadline() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      done_ = true;
    }
    cv_.notify_all();
    watcher_.join();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  std::thread watcher_;
};

}  // namespace glecma_test

#endif  // GHOTI_IO_GLECMA_TESTS_TEST_DEADLINE_H
