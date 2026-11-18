/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Lang-ecmascript.
 *
 * Ghoti.io Lang-ecmascript is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version 3
 * as published by the Free Software Foundation.
 *
 * Ghoti.io Lang-ecmascript is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// The input stream and the source made from one.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../test_helpers.h"

namespace {

// A reader that hands out its bytes a few at a time and can fail after a
// given number of calls.
struct Trickle {
  const unsigned char * data;
  size_t length;
  size_t position;
  size_t step;
  int fail_after;  // calls before it fails; negative never
  int calls;
  int closed;
};

GLECMA_Result TrickleRead(void * context, void * buffer, size_t capacity,
    size_t * out_read) {
  auto * t = static_cast<Trickle *>(context);
  if (t->fail_after >= 0 && t->calls++ >= t->fail_after) {
    return GLECMA_ERR_IO;
  }
  size_t left = t->length - t->position;
  size_t n = left < t->step ? left : t->step;
  if (n > capacity) {
    n = capacity;
  }
  std::memcpy(buffer, t->data + t->position, n);
  t->position += n;
  *out_read = n;
  return GLECMA_OK;
}

void TrickleClose(void * context) { static_cast<Trickle *>(context)->closed++; }

// A memory stream borrows its bytes, so the text is kept for the whole run.
GLECMA_Stream * Memory(const std::string & text) {
  static std::deque<std::string> kept;
  kept.push_back(text);
  GLECMA_Stream * stream = nullptr;
  EXPECT_EQ(glecma_stream_create_memory(kept.back().data(), kept.back().size(),
                nullptr, &stream),
      GLECMA_OK);
  return stream;
}

// Every observable of a source, as one string, so that two sources are equal
// when their strings are.
std::string Observe(const GLECMA_Source * s) {
  std::string out = std::to_string(glecma_source_units(s)) + "/" +
      std::to_string(glecma_source_code_points(s)) + "/" +
      std::to_string(glecma_source_lines(s)) + "/" +
      std::to_string(glecma_source_encoding(s));
  for (size_t i = 0; i <= glecma_source_units(s); i++) {
    GLECMA_Location where;
    if (glecma_source_location(s, i, &where) == GLECMA_OK) {
      out += ";" + std::to_string(i) + ":" + std::to_string(where.line) + ":" +
          std::to_string(where.column);
    } else {
      out += ";" + std::to_string(i) + ":x";
    }
  }
  return out;
}

const char * const kCorpus[] = {
  "", "x", "a\nb\r\nc\rd", "caf\xC3\xA9 \xE2\x80\xA8 \xF0\x9F\x98\x80 end",
  ("\xEF\xBB\xBF" "bom"), "line1\xE2\x80\xA9line2",
};

}  // namespace

TEST(Stream, MemoryStreamReadsInPiecesAndReportsItsEnd) {
  GLECMA_Stream * stream = Memory("hello world");
  char buffer[8];
  size_t got = 0;
  ASSERT_EQ(glecma_stream_read(stream, buffer, 5, &got), GLECMA_OK);
  EXPECT_EQ(got, 5u);
  EXPECT_EQ(std::memcmp(buffer, "hello", 5), 0);
  EXPECT_EQ(glecma_stream_tell(stream), 5u);
  EXPECT_FALSE(glecma_stream_eof(stream));
  ASSERT_EQ(glecma_stream_read(stream, buffer, 8, &got), GLECMA_OK);
  EXPECT_EQ(got, 6u);
  EXPECT_FALSE(glecma_stream_eof(stream));  // the end is seen by a read of 0
  ASSERT_EQ(glecma_stream_read(stream, buffer, 8, &got), GLECMA_OK);
  EXPECT_EQ(got, 0u);
  EXPECT_TRUE(glecma_stream_eof(stream));
  size_t size = 0;
  ASSERT_EQ(glecma_stream_size(stream, &size), GLECMA_OK);
  EXPECT_EQ(size, 11u);
  ASSERT_EQ(glecma_stream_seek(stream, 6), GLECMA_OK);
  EXPECT_FALSE(glecma_stream_eof(stream));
  ASSERT_EQ(glecma_stream_read(stream, buffer, 8, &got), GLECMA_OK);
  EXPECT_EQ(std::string(buffer, got), "world");
  EXPECT_EQ(glecma_stream_seek(stream, 12), GLECMA_ERR_INVALID);
  glecma_stream_destroy(stream);
}

TEST(Stream, ArgumentsAreChecked) {
  GLECMA_Stream * stream = nullptr;
  EXPECT_EQ(glecma_stream_create_memory(nullptr, 1, nullptr, &stream),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_stream_create_memory("", 0, nullptr, nullptr),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_stream_create_file(nullptr, nullptr, &stream),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_stream_create_callback(nullptr, nullptr, nullptr, nullptr,
                &stream),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_stream_create_file("/nonexistent/glecma-no-such-file",
                nullptr, &stream),
      GLECMA_ERR_IO);
  EXPECT_EQ(stream, nullptr);
  glecma_stream_destroy(nullptr);  // does nothing
  char buffer[1];
  size_t got = 0;
  EXPECT_EQ(glecma_stream_read(nullptr, buffer, 1, &got), GLECMA_ERR_INVALID);
  GLECMA_Stream * memory = Memory("x");
  EXPECT_EQ(glecma_stream_read(memory, nullptr, 1, &got), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_stream_read(memory, buffer, 0, &got), GLECMA_OK);
  EXPECT_EQ(got, 0u);
  glecma_stream_destroy(memory);
}

TEST(Stream, FileStreamSeeksAndReadsWhatWasWritten) {
  const std::string path_text = (std::filesystem::temp_directory_path() /
      ("glecma-stream-" + std::to_string(reinterpret_cast<uintptr_t>(&path_text)))).string();
  const char * path = path_text.c_str();
  FILE * f = std::fopen(path, "wb");
  ASSERT_NE(f, nullptr);
  std::fputs("0123456789", f);
  std::fclose(f);
  GLECMA_Stream * stream = nullptr;
  ASSERT_EQ(glecma_stream_create_file(path, nullptr, &stream), GLECMA_OK);
  size_t size = 0;
  ASSERT_EQ(glecma_stream_size(stream, &size), GLECMA_OK);
  EXPECT_EQ(size, 10u);
  ASSERT_EQ(glecma_stream_seek(stream, 4), GLECMA_OK);
  char buffer[16];
  size_t got = 0;
  ASSERT_EQ(glecma_stream_read(stream, buffer, 16, &got), GLECMA_OK);
  EXPECT_EQ(std::string(buffer, got), "456789");
  EXPECT_EQ(glecma_stream_tell(stream), 10u);
  glecma_stream_destroy(stream);
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_stream_create_file(path, nullptr, &stream), GLECMA_OK);
  ASSERT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  EXPECT_EQ(glecma_source_units(source), 10u);
  glecma_source_release(source);
  glecma_stream_destroy(stream);
  std::remove(path);
}

TEST(Stream, CallbackStreamHasNoSizeAndCannotSeek) {
  Trickle t{reinterpret_cast<const unsigned char *>("abc"), 3, 0, 1, -1, 0, 0};
  GLECMA_Stream * stream = nullptr;
  ASSERT_EQ(glecma_stream_create_callback(&TrickleRead, &TrickleClose, &t,
                nullptr, &stream),
      GLECMA_OK);
  size_t size = 0;
  EXPECT_EQ(glecma_stream_size(stream, &size), GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(glecma_stream_seek(stream, 0), GLECMA_ERR_UNSUPPORTED);
  glecma_stream_destroy(stream);
  EXPECT_EQ(t.closed, 1);
}

TEST(Stream, ASourceFromAStreamEqualsTheSourceFromTheSameBytes) {
  for (const char * text : kCorpus) {
    std::string bytes(text);
    GLECMA_Source * direct = nullptr;
    ASSERT_EQ(glecma_source_create_utf8(bytes.data(), bytes.size(), nullptr,
                  nullptr, nullptr, &direct),
        GLECMA_OK);
    // From memory, and from a reader that returns one byte at a time.
    GLECMA_Stream * memory = Memory(bytes);
    GLECMA_Source * from_memory = nullptr;
    ASSERT_EQ(glecma_source_create_stream(memory, GLECMA_ENCODING_UTF8, nullptr,
                  nullptr, nullptr, &from_memory),
        GLECMA_OK);
    Trickle t{reinterpret_cast<const unsigned char *>(bytes.data()),
        bytes.size(), 0, 1, -1, 0, 0};
    GLECMA_Stream * slow = nullptr;
    ASSERT_EQ(glecma_stream_create_callback(&TrickleRead, &TrickleClose, &t,
                  nullptr, &slow),
        GLECMA_OK);
    GLECMA_Source * from_slow = nullptr;
    ASSERT_EQ(glecma_source_create_stream(slow, GLECMA_ENCODING_UTF8, nullptr,
                  nullptr, nullptr, &from_slow),
        GLECMA_OK);
    EXPECT_EQ(Observe(direct), Observe(from_memory)) << text;
    EXPECT_EQ(Observe(direct), Observe(from_slow)) << text;
    EXPECT_TRUE(glecma_stream_eof(memory));
    glecma_source_release(direct);
    glecma_source_release(from_memory);
    glecma_source_release(from_slow);
    glecma_stream_destroy(memory);
    glecma_stream_destroy(slow);
  }
}

TEST(Stream, Utf16StreamEqualsTheBufferSourceAndAnOddCountIsCorrupt) {
  std::vector<uint16_t> units = {'a', '\n', 0xD83D, 0xDE00, 0xD800, 'z'};
  GLECMA_Source * direct = nullptr;
  ASSERT_EQ(glecma_source_create_utf16(units.data(), units.size(), nullptr,
                nullptr, nullptr, &direct),
      GLECMA_OK);
  GLECMA_Stream * stream = nullptr;
  ASSERT_EQ(glecma_stream_create_memory(units.data(),
                units.size() * sizeof(uint16_t), nullptr, &stream),
      GLECMA_OK);
  GLECMA_Source * from_stream = nullptr;
  ASSERT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF16, nullptr,
                nullptr, nullptr, &from_stream),
      GLECMA_OK);
  EXPECT_EQ(Observe(direct), Observe(from_stream));
  glecma_source_release(direct);
  glecma_source_release(from_stream);
  glecma_stream_destroy(stream);

  glecma_test::CountingAllocator counting;
  GLECMA_Stream * odd = nullptr;
  ASSERT_EQ(glecma_stream_create_memory(units.data(),
                units.size() * sizeof(uint16_t) - 1, nullptr, &odd),
      GLECMA_OK);
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  GLECMA_Source * none = nullptr;
  EXPECT_EQ(glecma_source_create_stream(odd, GLECMA_ENCODING_UTF16, nullptr,
                counting.get(), &failure, &none),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(none, nullptr);
  EXPECT_EQ(failure.location.offset, units.size() - 1);
  EXPECT_EQ(failure.location.line, 2u);
  EXPECT_EQ(counting.live(), 0u);
  glecma_stream_destroy(odd);
}

TEST(Stream, IllFormedUtf8IsTheSameFailureAsFromABuffer) {
  const std::string bad = "ok\n\xC0\xAF tail";
  GLECMA_Failure a = GLECMA_FAILURE_INIT, b = GLECMA_FAILURE_INIT;
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(bad.data(), bad.size(), nullptr, nullptr,
                &a, &source),
      GLECMA_ERR_CORRUPT);
  GLECMA_Stream * stream = Memory(bad);
  ASSERT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                nullptr, &b, &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(a.location.offset, b.location.offset);
  EXPECT_EQ(a.location.line, b.location.line);
  EXPECT_EQ(a.location.column, b.location.column);
  EXPECT_STREQ(a.message, b.message);
  glecma_stream_destroy(stream);
}

TEST(Stream, TheSizeCapIsCheckedAsBytesArriveAndNothingSurvives) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.source_units = 100;
  for (size_t length : {size_t{99}, size_t{100}, size_t{101}, size_t{5000}}) {
    std::string text(length, 'a');
    glecma_test::CountingAllocator counting;
    GLECMA_Stream * stream = Memory(text);
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Source * source = nullptr;
    GLECMA_Result r = glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8,
        &limits, counting.get(), &failure, &source);
    if (length <= 100) {
      ASSERT_EQ(r, GLECMA_OK) << length;
      EXPECT_EQ(glecma_source_units(source), length);
      glecma_source_release(source);
    } else {
      ASSERT_EQ(r, GLECMA_ERR_LIMIT) << length;
      EXPECT_STREQ(failure.limit, "source_units");
      // It stopped one byte past the cap, which is how it knew: not at the
      // end of the stream, and not a chunk later.
      EXPECT_LE(glecma_stream_tell(stream), static_cast<size_t>(limits.source_units) + 1);
    }
    EXPECT_EQ(counting.live(), 0u) << length;
    glecma_stream_destroy(stream);
  }
}

TEST(Stream, AStreamThatFailsIsAnIoErrorAndNothingLeaks) {
  for (int fail_after : {0, 1, 3}) {
    glecma_test::CountingAllocator counting;
    Trickle t{reinterpret_cast<const unsigned char *>("0123456789"), 10, 0, 2,
        fail_after, 0, 0};
    GLECMA_Stream * stream = nullptr;
    ASSERT_EQ(glecma_stream_create_callback(&TrickleRead, &TrickleClose, &t,
                  counting.get(), &stream),
        GLECMA_OK);
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    GLECMA_Source * source = nullptr;
    EXPECT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                  counting.get(), &failure, &source),
        GLECMA_ERR_IO);
    EXPECT_EQ(source, nullptr);
    EXPECT_EQ(failure.result, static_cast<uint32_t>(GLECMA_ERR_IO));
    // The stream stays failed.
    char buffer[1];
    size_t got = 0;
    EXPECT_EQ(glecma_stream_read(stream, buffer, 1, &got), GLECMA_ERR_IO);
    glecma_stream_destroy(stream);
    EXPECT_EQ(counting.live(), 0u);
    EXPECT_EQ(t.closed, 1);
  }
}

TEST(Stream, EveryAllocationOfAStreamSourceMayFailWithNothingLeaked) {
  const std::string text = "let x = 1;\nlet y = \"\xC3\xA9\";\n";
  size_t total = 0;
  {
    glecma_test::CountingAllocator counting;
    GLECMA_Stream * stream = Memory(text);
    GLECMA_Source * source = nullptr;
    ASSERT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                  counting.get(), nullptr, &source),
        GLECMA_OK);
    total = counting.calls();
    glecma_source_release(source);
    glecma_stream_destroy(stream);
  }
  ASSERT_GT(total, 0u);
  for (size_t i = 0; i < total; i++) {
    glecma_test::CountingAllocator counting;
    counting.fail_at(i);
    GLECMA_Stream * stream = Memory(text);
    GLECMA_Source * source = nullptr;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                  counting.get(), &failure, &source),
        GLECMA_ERR_OOM)
        << i;
    EXPECT_EQ(source, nullptr);
    EXPECT_EQ(counting.live(), 0u) << i;
    glecma_stream_destroy(stream);
  }
}

TEST(Stream, ABadEncodingAFailureTooSmallAndANullStreamAreRefused) {
  GLECMA_Stream * stream = Memory("x");
  GLECMA_Source * source = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_source_create_stream(stream, static_cast<GLECMA_Encoding>(9),
                nullptr, nullptr, &failure, &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_create_stream(nullptr, GLECMA_ENCODING_UTF8, nullptr,
                nullptr, &failure, &source),
      GLECMA_ERR_INVALID);
  GLECMA_Failure small = GLECMA_FAILURE_INIT;
  small.size = 4;
  EXPECT_EQ(glecma_source_create_stream(stream, GLECMA_ENCODING_UTF8, nullptr,
                nullptr, &small, &source),
      GLECMA_ERR_INVALID);
  glecma_stream_destroy(stream);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
