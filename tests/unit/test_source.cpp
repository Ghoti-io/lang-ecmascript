// SPDX-License-Identifier: LGPL-3.0-only
//
// Source text: creation, validation, counts and locations. source.h and
// core.h are the contract each test cites; the encoding rules are the
// Unicode Standard's section 3.9 (Tables 3-7 and 3-8), and the line rules
// ECMA-262 17th edition 12.3 (Line Terminators).

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../test_helpers.h"
#include "source/source_internal.h"

namespace {

// A sentinel an output must still hold after a refusal, because outputs are
// written only on success.
GLECMA_Source * const kUntouched =
    reinterpret_cast<GLECMA_Source *>(static_cast<uintptr_t>(0xA5A5A5A5u));

constexpr uint32_t kLF = 0x0A;
constexpr uint32_t kCR = 0x0D;
constexpr uint32_t kLS = 0x2028;
constexpr uint32_t kPS = 0x2029;

// ---------------------------------------------------------------------------
// Reference encoders and the reference location model. Written from the
// definitions, independently of the library: UTF-8 by code point range, and
// locations from a list of line starts rather than from a cursor.
// ---------------------------------------------------------------------------

std::string Utf8(const std::vector<uint32_t> & code_points) {
  std::string out;
  for (uint32_t c : code_points) {
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

std::vector<uint16_t> Utf16(const std::vector<uint32_t> & code_points) {
  std::vector<uint16_t> out;
  for (uint32_t c : code_points) {
    if (c < 0x10000) {
      out.push_back(static_cast<uint16_t>(c));
    } else {
      out.push_back(static_cast<uint16_t>(0xD800 + ((c - 0x10000) >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
    }
  }
  return out;
}

size_t Utf8Length(uint32_t c) {
  return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
}

size_t Utf16Length(uint32_t c) {
  return c < 0x10000 ? 1 : 2;
}

struct Expected {
  std::vector<size_t> offsets;          // each code point's offset
  std::vector<GLECMA_Location> locations;  // each code point's location
  GLECMA_Location end;                  // the end of input
  size_t lines;
};

// ECMA-262 12.3: a line ends after LF, after CR not followed by LF, after
// CRLF (as one), after U+2028 and after U+2029. Line n starts at the n-th
// entry of `starts`; a column is the UTF-16 width of what precedes the code
// point on its line.
Expected Reference(const std::vector<uint32_t> & cps, bool utf16) {
  std::vector<size_t> starts = {0};
  for (size_t i = 0; i < cps.size(); ++i) {
    if (cps[i] == kCR && i + 1 < cps.size() && cps[i + 1] == kLF) {
      starts.push_back(i + 2);
      ++i;
    } else if (cps[i] == kLF || cps[i] == kCR || cps[i] == kLS ||
        cps[i] == kPS) {
      starts.push_back(i + 1);
    }
  }
  Expected e;
  e.lines = starts.size();
  size_t offset = 0;
  auto locate = [&](size_t index, size_t at) {
    size_t line = static_cast<size_t>(
        std::upper_bound(starts.begin(), starts.end(), index) - starts.begin());
    size_t column = 0;
    for (size_t k = starts[line - 1]; k < index; ++k) {
      column += Utf16Length(cps[k]);
    }
    return GLECMA_Location{at, line, column};
  };
  for (size_t i = 0; i < cps.size(); ++i) {
    e.offsets.push_back(offset);
    e.locations.push_back(locate(i, offset));
    offset += utf16 ? Utf16Length(cps[i]) : Utf8Length(cps[i]);
  }
  e.end = locate(cps.size(), offset);
  return e;
}

void ExpectLocation(const GLECMA_Location & got, const GLECMA_Location & want,
    const std::string & where) {
  EXPECT_EQ(got.offset, want.offset) << where;
  EXPECT_EQ(got.line, want.line) << where;
  EXPECT_EQ(got.column, want.column) << where;
}

GLECMA_Source * MakeUtf8(const std::string & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  return source;
}

GLECMA_Source * MakeUtf16(const std::vector<uint16_t> & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf16(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  return source;
}

GLECMA_Location At(const GLECMA_Source * source, size_t offset) {
  GLECMA_Location location = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
  EXPECT_EQ(glecma_source_location(source, offset, &location), GLECMA_OK)
      << "offset " << offset;
  return location;
}

// Walks the source with the internal cursor and checks every code point, and
// the end, against the public query and against the reference model; then
// checks that every offset that is not a code point's start is refused.
void Differential(const GLECMA_Source * source,
    const std::vector<uint32_t> & cps, bool utf16, const std::string & name) {
  Expected e = Reference(cps, utf16);
  ASSERT_EQ(glecma_source_code_points(source), cps.size()) << name;
  ASSERT_EQ(glecma_source_units(source), e.end.offset) << name;
  ASSERT_EQ(glecma_source_lines(source), e.lines) << name;

  GLECMA_SourceCursor cursor;
  glecma_source_cursor_start(&cursor, source);
  size_t index = 0;
  uint32_t code_point = 0;
  GLECMA_Location walked;
  while (glecma_source_cursor_next(&cursor, &code_point, &walked)) {
    ASSERT_LT(index, cps.size()) << name;
    std::string where = name + " code point " + std::to_string(index);
    EXPECT_EQ(code_point, cps[index]) << where;
    ExpectLocation(walked, e.locations[index], where);
    ExpectLocation(At(source, walked.offset), walked, where + " (query)");
    ++index;
  }
  EXPECT_EQ(index, cps.size()) << name;
  GLECMA_Location end;
  glecma_source_cursor_location(&cursor, &end);
  ExpectLocation(end, e.end, name + " end");
  ExpectLocation(At(source, end.offset), e.end, name + " end (query)");
  EXPECT_EQ(end.line, glecma_source_lines(source)) << name;

  std::vector<bool> boundary(e.end.offset + 1, false);
  for (size_t o : e.offsets) {
    boundary[o] = true;
  }
  boundary[e.end.offset] = true;
  for (size_t o = 0; o <= e.end.offset + 1; ++o) {
    GLECMA_Location location = {7, 7, 7};
    GLECMA_Result result = glecma_source_location(source, o, &location);
    if (o <= e.end.offset && boundary[o]) {
      EXPECT_EQ(result, GLECMA_OK) << name << " offset " << o;
    } else {
      EXPECT_EQ(result, GLECMA_ERR_INVALID) << name << " offset " << o;
      EXPECT_EQ(location.offset, 7u) << name << " offset " << o;
    }
  }
}

// Deterministic pseudo-random code points drawn from a palette that holds
// every terminator, every UTF-8 length, the BOM and NUL, so a few thousand
// of them cross several checkpoints with code points straddling the
// boundaries. Lone surrogates only where the encoding can carry them.
std::vector<uint32_t> Generated(uint32_t seed, size_t count, bool utf16) {
  static const uint32_t palette[] = {'a', 'z', ' ', kLF, kCR, kCR, kLS, kPS,
      0x00, 0xE9, 0x7FF, 0x800, 0x20AC, 0xFEFF, 0xFFFF, 0x10000, 0x1F600,
      0x10FFFF};
  static const uint32_t surrogates[] = {0xD800, 0xDBFF, 0xDC00, 0xDFFF};
  std::vector<uint32_t> out;
  uint32_t state = seed * 2654435761u + 1;
  while (out.size() < count) {
    state = state * 1103515245u + 12345u;
    uint32_t pick = (state >> 8) % (sizeof(palette) / sizeof(palette[0]) + 2);
    if (pick < sizeof(palette) / sizeof(palette[0])) {
      out.push_back(palette[pick]);
    } else if (utf16) {
      uint32_t s = surrogates[(state >> 20) % 4];
      // A high surrogate followed by a low one would pair; the reference
      // decodes lists of code points, so keep the list what the decoder
      // will see.
      if (s >= 0xDC00 && !out.empty() && out.back() >= 0xD800 &&
          out.back() <= 0xDBFF) {
        s = 'b';
      }
      out.push_back(s);
    } else {
      out.push_back('\n');
    }
  }
  return out;
}

// The corpus the differential walks: the empty source, every line
// terminator mixed, an astral code point, a leading BOM, and sources that
// stress what the checkpoints and the CR look-ahead have to get right.
std::vector<std::vector<uint32_t>> Corpus() {
  std::vector<std::vector<uint32_t>> corpus = {
      {},
      {'a', kCR, kLF, 'b', kCR, 'c', kLS, 'd', kPS, 'e', kLF, 'f'},
      {0x1F600, 'x'},
      {0xFEFF, 'a'},
      {kCR},
      {kCR, kCR, kLF, kLF},
      {kLF, kCR},
      {'#', '!', ' ', 'x', kLF, 'y'},
      {0x00, 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x10FFFF},
  };
  // A long line, so columns grow past one checkpoint interval.
  std::vector<uint32_t> long_line(3000, 'w');
  long_line.push_back(kLF);
  long_line.push_back('v');
  corpus.push_back(long_line);
  // A CRLF split by a checkpoint boundary in both encodings: the CR is the
  // last unit before 1024 and the LF the first after it.
  std::vector<uint32_t> split(1023, 'q');
  split.push_back(kCR);
  split.push_back(kLF);
  split.push_back('r');
  corpus.push_back(split);
  return corpus;
}

}  // namespace

// ---------------------------------------------------------------------------
// Creation, counts and locations
// ---------------------------------------------------------------------------

TEST(Source, EmptySourceHasNoCodePointsAndOneLine) {
  const char nothing[] = "unused";
  const uint16_t wide_nothing[] = {'u'};
  GLECMA_Source * sources[4] = {};
  ASSERT_EQ(glecma_source_create_utf8(nullptr, 0, nullptr, nullptr, nullptr,
                &sources[0]),
      GLECMA_OK);
  ASSERT_EQ(glecma_source_create_utf8(nothing, 0, nullptr, nullptr, nullptr,
                &sources[1]),
      GLECMA_OK);
  ASSERT_EQ(glecma_source_create_utf16(nullptr, 0, nullptr, nullptr, nullptr,
                &sources[2]),
      GLECMA_OK);
  ASSERT_EQ(glecma_source_create_utf16(wide_nothing, 0, nullptr, nullptr,
                nullptr, &sources[3]),
      GLECMA_OK);
  for (GLECMA_Source * source : sources) {
    EXPECT_EQ(glecma_source_units(source), 0u);
    EXPECT_EQ(glecma_source_code_points(source), 0u);
    EXPECT_EQ(glecma_source_lines(source), 1u);
    ExpectLocation(At(source, 0), GLECMA_Location{0, 1, 0}, "end of empty");
    GLECMA_Location location;
    EXPECT_EQ(glecma_source_location(source, 1, &location),
        GLECMA_ERR_INVALID);
    glecma_source_release(source);
  }
}

// a CRLF b CR c LS d PS e LF f: six lines, each terminator counted once.
TEST(Source, MixedTerminatorsPutFOnLineSix) {
  std::vector<uint32_t> cps = {'a', kCR, kLF, 'b', kCR, 'c', kLS, 'd', kPS,
      'e', kLF, 'f'};
  std::string utf8 = Utf8(cps);
  GLECMA_Source * narrow = MakeUtf8(utf8);
  ExpectLocation(At(narrow, utf8.size() - 1),
      GLECMA_Location{utf8.size() - 1, 6, 0}, "f, UTF-8");
  // The LF of the CRLF sits on the CR's line, one column on.
  ExpectLocation(At(narrow, 2), GLECMA_Location{2, 1, 2}, "LF of CRLF");
  ExpectLocation(At(narrow, 3), GLECMA_Location{3, 2, 0}, "b");
  EXPECT_EQ(glecma_source_lines(narrow), 6u);
  glecma_source_release(narrow);

  std::vector<uint16_t> utf16 = Utf16(cps);
  GLECMA_Source * wide = MakeUtf16(utf16);
  ExpectLocation(At(wide, 11), GLECMA_Location{11, 6, 0}, "f, UTF-16");
  EXPECT_EQ(glecma_source_lines(wide), 6u);
  glecma_source_release(wide);
}

// A code point above U+FFFF is two UTF-16 columns wide in either encoding.
TEST(Source, AstralCodePointIsTwoColumnsWide) {
  GLECMA_Source * narrow = MakeUtf8("\xF0\x9F\x98\x80x");
  ExpectLocation(At(narrow, 4), GLECMA_Location{4, 1, 2}, "x, UTF-8");
  EXPECT_EQ(glecma_source_code_points(narrow), 2u);
  glecma_source_release(narrow);
  GLECMA_Source * wide = MakeUtf16({0xD83D, 0xDE00, 'x'});
  ExpectLocation(At(wide, 2), GLECMA_Location{2, 1, 2}, "x, UTF-16");
  EXPECT_EQ(glecma_source_code_points(wide), 2u);
  glecma_source_release(wide);
}

// source.h: nothing is stripped. U+FEFF is ECMA-262 white space and stays a
// code point of the source.
TEST(Source, LeadingByteOrderMarkIsKept) {
  GLECMA_Source * narrow = MakeUtf8("\xEF\xBB\xBF" "a");
  EXPECT_EQ(glecma_source_units(narrow), 4u);
  EXPECT_EQ(glecma_source_code_points(narrow), 2u);
  ExpectLocation(At(narrow, 3), GLECMA_Location{3, 1, 1}, "a after BOM");
  GLECMA_SourceCursor cursor;
  glecma_source_cursor_start(&cursor, narrow);
  uint32_t first = 0;
  ASSERT_TRUE(glecma_source_cursor_next(&cursor, &first, nullptr));
  EXPECT_EQ(first, 0xFEFFu);
  glecma_source_release(narrow);

  GLECMA_Source * wide = MakeUtf16({0xFEFF, 'a'});
  EXPECT_EQ(glecma_source_code_points(wide), 2u);
  ExpectLocation(At(wide, 1), GLECMA_Location{1, 1, 1}, "a after BOM");
  glecma_source_release(wide);
}

// UTF16DecodeString: an unpaired surrogate is a code point of its own.
TEST(Source, LoneSurrogateIsACodePointOfItsOwn) {
  GLECMA_Source * source = MakeUtf16({0xD800, 0x61});
  EXPECT_EQ(glecma_source_code_points(source), 2u);
  GLECMA_SourceCursor cursor;
  glecma_source_cursor_start(&cursor, source);
  uint32_t cp = 0;
  GLECMA_Location location;
  ASSERT_TRUE(glecma_source_cursor_next(&cursor, &cp, &location));
  EXPECT_EQ(cp, 0xD800u);
  ExpectLocation(location, GLECMA_Location{0, 1, 0}, "lone high");
  ASSERT_TRUE(glecma_source_cursor_next(&cursor, &cp, &location));
  EXPECT_EQ(cp, 0x61u);
  ExpectLocation(location, GLECMA_Location{1, 1, 1}, "a");
  EXPECT_FALSE(glecma_source_cursor_next(&cursor, &cp, &location));
  glecma_source_release(source);
}

// The other unpaired shapes: a low surrogate first, a low before a high, and
// a high surrogate as the last unit. Each is one code point and one column.
TEST(Source, EveryUnpairedSurrogateShapeIsOneCodePoint) {
  struct Case {
    std::vector<uint16_t> units;
    std::vector<uint32_t> cps;
  };
  const Case cases[] = {
      {{0xDC00}, {0xDC00}},
      {{0xDC00, 0xD800}, {0xDC00, 0xD800}},
      {{'a', 0xDBFF}, {'a', 0xDBFF}},
      {{0xD800, 0xD800, 0xDC00}, {0xD800, 0x10000}},
  };
  for (const Case & c : cases) {
    GLECMA_Source * source = MakeUtf16(c.units);
    Differential(source, c.cps, true, "unpaired");
    glecma_source_release(source);
  }
}

TEST(Source, IllFormedUtf8IsCorruptAtTheFirstBadByte) {
  glecma_test::CountingAllocator counting;
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8("ab\xE0\x80" "c", 5, nullptr,
                counting.get(), &failure, &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ(counting.live(), 0u);
  EXPECT_EQ((GLECMA_Result)failure.result, GLECMA_ERR_CORRUPT);
  ASSERT_NE(failure.message, nullptr);
  EXPECT_STREQ(failure.message, "overlong UTF-8 encoding");
  EXPECT_EQ(failure.limit, nullptr);
  ExpectLocation(failure.location, GLECMA_Location{2, 1, 2}, "E0 80");
}

TEST(Source, EncodedSurrogateIsCorruptAtItsFirstByte) {
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8("\xED\xA0\x80", 3, nullptr, nullptr,
                &failure, &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(source, kUntouched);
  ExpectLocation(failure.location, GLECMA_Location{0, 1, 0}, "ED A0 80");
}

TEST(Source, TruncatedTailIsCorruptAtItsLeadByte) {
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8("a\xF0\x9F", 3, nullptr, nullptr,
                &failure, &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(source, kUntouched);
  ExpectLocation(failure.location, GLECMA_Location{1, 1, 1}, "F0 9F");
}

// core.h: the failure's location has the line and column of the bad byte,
// counted as for any other position: after a CRLF and an astral code point.
TEST(Source, CorruptLocationCountsLinesAndUtf16Columns) {
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  const char text[] = "x\r\n\xF0\x9F\x98\x80\xFF";
  ASSERT_EQ(glecma_source_create_utf8(text, sizeof(text) - 1, nullptr,
                nullptr, &failure, &source),
      GLECMA_ERR_CORRUPT);
  ExpectLocation(failure.location, GLECMA_Location{7, 2, 2}, "FF");
}

TEST(Source, OffsetInsideACodePointIsInvalid) {
  GLECMA_Source * narrow = MakeUtf8("\xF0\x9F\x98\x80");
  for (size_t offset : {1u, 2u, 3u}) {
    GLECMA_Location location = {9, 9, 9};
    EXPECT_EQ(glecma_source_location(narrow, offset, &location),
        GLECMA_ERR_INVALID)
        << offset;
    EXPECT_EQ(location.offset, 9u) << offset;
  }
  ExpectLocation(At(narrow, 0), GLECMA_Location{0, 1, 0}, "start");
  ExpectLocation(At(narrow, 4), GLECMA_Location{4, 1, 2}, "end");
  glecma_source_release(narrow);

  GLECMA_Source * wide = MakeUtf16({0xD83D, 0xDE00});
  GLECMA_Location location = {9, 9, 9};
  EXPECT_EQ(glecma_source_location(wide, 1, &location), GLECMA_ERR_INVALID);
  EXPECT_EQ(location.offset, 9u);
  ExpectLocation(At(wide, 2), GLECMA_Location{2, 1, 2}, "end");
  glecma_source_release(wide);
}

TEST(Source, BadArgumentsAreInvalidAndWriteNothing) {
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  glecma_test::CountingAllocator counting;
  EXPECT_EQ(glecma_source_create_utf8(nullptr, 1, nullptr, counting.get(),
                &failure, &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ((GLECMA_Result)failure.result, GLECMA_ERR_INVALID);
  EXPECT_EQ(failure.limit, nullptr);
  EXPECT_EQ(glecma_source_create_utf16(nullptr, 1, nullptr, counting.get(),
                &failure, &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ(glecma_source_create_utf8("a", 1, nullptr, counting.get(),
                &failure, nullptr),
      GLECMA_ERR_INVALID);
  EXPECT_EQ((GLECMA_Result)failure.result, GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_create_utf16(nullptr, 0, nullptr, counting.get(),
                &failure, nullptr),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(counting.calls(), 0u);

  GLECMA_Source * real = MakeUtf8("ab");
  GLECMA_Location location = {9, 9, 9};
  EXPECT_EQ(glecma_source_location(nullptr, 0, &location), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_location(real, 0, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_location(real, 3, &location), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_location(real, SIZE_MAX, &location),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(location.offset, 9u);
  glecma_source_release(real);
}

// source.h: an allocator with a NULL function is a bad argument, refused
// before it is called.
TEST(SourceArguments, AllocatorWithANullFunctionIsInvalid) {
  glecma_test::CountingAllocator counting;
  GLECMA_Allocator broken = *counting.get();
  broken.free_fn = nullptr;
  GLECMA_Source * source = kUntouched;
  EXPECT_EQ(glecma_source_create_utf8("a", 1, nullptr, &broken, nullptr,
                &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ(counting.calls(), 0u);
}

// source.h: a UTF-16 length whose byte size overflows size_t describes no
// buffer. It is refused even when the limit has been raised to allow it.
TEST(SourceArguments, Utf16LengthPastAnyBufferIsInvalid) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  ASSERT_EQ(glecma_limits_default(&limits), GLECMA_OK);
  limits.source_units = UINT64_MAX;
  const uint16_t unit = 'a';
  GLECMA_Source * source = kUntouched;
  glecma_test::CountingAllocator counting;
  EXPECT_EQ(glecma_source_create_utf16(&unit, SIZE_MAX / 2 + 1, &limits,
                counting.get(), nullptr, &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ(counting.calls(), 0u);
}

// source.h: failing each allocation of a create in turn returns OOM
// and leaves nothing allocated, until the create needs no more and succeeds.
TEST(Source, EachAllocationFailingIsOomAndLeaksNothing) {
  const std::string text(5000, 'z');
  const std::vector<uint16_t> wide(5000, 'z');
  for (int encoding = 0; encoding < 2; ++encoding) {
    glecma_test::CountingAllocator counting;
    size_t failures = 0;
    for (size_t n = 0;; ++n) {
      ASSERT_LT(n, 100u) << "the create never stopped allocating";
      counting.reset();
      counting.fail_at(n);
      GLECMA_Source * source = kUntouched;
      GLECMA_Failure failure = GLECMA_FAILURE_INIT;
      GLECMA_Result result = encoding == 0
          ? glecma_source_create_utf8(text.data(), text.size(), nullptr,
                counting.get(), &failure, &source)
          : glecma_source_create_utf16(wide.data(), wide.size(), nullptr,
                counting.get(), &failure, &source);
      EXPECT_EQ((GLECMA_Result)failure.result, result);
      if (result == GLECMA_OK) {
        EXPECT_LE(counting.calls(), n) << "succeeded although allocation "
                                       << n << " was refused";
        glecma_source_release(source);
        EXPECT_EQ(counting.live(), 0u);
        break;
      }
      EXPECT_EQ(result, GLECMA_ERR_OOM) << n;
      EXPECT_EQ(source, kUntouched) << n;
      EXPECT_EQ(counting.live(), 0u) << n;
      ++failures;
    }
    // A source is more than one block (struct, checkpoints, copy), so the
    // sweep has reached more than the first.
    EXPECT_GE(failures, 2u) << encoding;
  }
}

// The same sweep on input that turns out ill-formed: the scan runs after the
// allocations, so every exit frees them.
TEST(SourceAllocation, CorruptInputAfterAllocationLeaksNothing) {
  glecma_test::CountingAllocator counting;
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8("abc\x80", 4, nullptr, counting.get(),
                &failure, &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_GT(counting.calls(), 0u);
  EXPECT_EQ(counting.live(), 0u);
  EXPECT_EQ(source, kUntouched);
}

// ---------------------------------------------------------------------------
// The failure record and ownership
// ---------------------------------------------------------------------------

// core.h: the record is written on every return, success included, with the
// fields that do not apply cleared.
TEST(SourceFailure, SuccessWritesACleanRecord) {
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  std::memset(&failure, 0xFF, sizeof(failure));
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("a", 1, nullptr, nullptr, &failure,
                &source),
      GLECMA_OK);
  EXPECT_EQ((GLECMA_Result)failure.result, GLECMA_OK);
  EXPECT_EQ(failure.message, nullptr);
  EXPECT_EQ(failure.limit, nullptr);
  ExpectLocation(failure.location, GLECMA_Location{0, 0, 0}, "success");
  glecma_source_release(source);
}

// core.h: location is meaningful only for CORRUPT (and later SYNTAX); a bad
// argument leaves it zero.
TEST(SourceFailure, InvalidArgumentLeavesTheLocationZero) {
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  std::memset(&failure, 0xFF, sizeof(failure));
  GLECMA_Source * source = kUntouched;
  EXPECT_EQ(glecma_source_create_utf8(nullptr, 3, nullptr, nullptr, &failure,
                &source),
      GLECMA_ERR_INVALID);
  ExpectLocation(failure.location, GLECMA_Location{0, 0, 0}, "invalid");
  EXPECT_EQ(failure.limit, nullptr);
}

// source.h: the text is copied, so the caller may change or free its buffer
// once the call returns.
TEST(SourceOwnership, SourceKeepsItsOwnCopy) {
  std::string text = "a\nb";
  GLECMA_Source * narrow = MakeUtf8(text);
  text.assign("xyz");
  ExpectLocation(At(narrow, 2), GLECMA_Location{2, 2, 0}, "b");
  EXPECT_EQ(glecma_source_lines(narrow), 2u);
  glecma_source_release(narrow);

  std::vector<uint16_t> wide = {'a', '\n', 'b'};
  GLECMA_Source * copy = MakeUtf16(wide);
  wide.assign({'x', 'y', 'z'});
  ExpectLocation(At(copy, 2), GLECMA_Location{2, 2, 0}, "b");
  glecma_source_release(copy);
}

TEST(SourceAccessors, ReportTheEncodingAndAreZeroForNull) {
  GLECMA_Source * narrow = MakeUtf8("a");
  GLECMA_Source * wide = MakeUtf16({'a'});
  EXPECT_EQ(glecma_source_encoding(narrow), GLECMA_ENCODING_UTF8);
  EXPECT_EQ(glecma_source_encoding(wide), GLECMA_ENCODING_UTF16);
  EXPECT_EQ(glecma_source_encoding(nullptr), static_cast<GLECMA_Encoding>(0));
  EXPECT_EQ(glecma_source_units(nullptr), 0u);
  EXPECT_EQ(glecma_source_code_points(nullptr), 0u);
  EXPECT_EQ(glecma_source_lines(nullptr), 0u);
  glecma_source_release(nullptr);
  glecma_source_release(narrow);
  glecma_source_release(wide);
}

// A NUL byte is U+0000, not a terminator of the text.
TEST(SourceAccessors, NulIsAnOrdinaryCodePoint) {
  GLECMA_Source * source = MakeUtf8(std::string("a\0b", 3));
  EXPECT_EQ(glecma_source_code_points(source), 3u);
  ExpectLocation(At(source, 2), GLECMA_Location{2, 1, 2}, "b after NUL");
  glecma_source_release(source);
}

TEST(SourceDump, WritesOneSummaryLine) {
  GLECMA_Source * source = MakeUtf8("a\nb");
  FILE * out = std::tmpfile();
  ASSERT_NE(out, nullptr);
  ASSERT_EQ(glecma_source_dump(source, out), GLECMA_OK);
  std::rewind(out);
  char line[256] = {};
  ASSERT_NE(std::fgets(line, sizeof(line), out), nullptr);
  std::string text(line);
  EXPECT_NE(text.find("UTF-8"), std::string::npos) << text;
  EXPECT_NE(text.find("3 units"), std::string::npos) << text;
  EXPECT_NE(text.find("3 code points"), std::string::npos) << text;
  EXPECT_NE(text.find("2 lines"), std::string::npos) << text;
  std::fclose(out);
  EXPECT_EQ(glecma_source_dump(nullptr, stdout), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_source_dump(source, nullptr), GLECMA_ERR_INVALID);
  glecma_source_release(source);
}

// source.h: a stream that reports an error is ERR_IO. A stream opened only
// for reading refuses every write.
TEST(SourceDump, StreamThatRefusesWritesIsIo) {
#ifdef _WIN32
  // Under wine a write to a stream opened for reading is accepted and flushed without an error; whether a
  // Windows C runtime refuses it is for the run on a real Windows machine.
  GTEST_SKIP() << "wine's C runtime accepts a write to a read-only stream";
#endif
  GLECMA_Source * source = MakeUtf8("a");
  std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("glecma-dump-probe-" +
          std::to_string(reinterpret_cast<uintptr_t>(source)));
  FILE * create = std::fopen(path.string().c_str(), "w");
  ASSERT_NE(create, nullptr);
  std::fclose(create);
  FILE * read_only = std::fopen(path.string().c_str(), "r");
  ASSERT_NE(read_only, nullptr);
  EXPECT_EQ(glecma_source_dump(source, read_only), GLECMA_ERR_IO);
  std::fclose(read_only);
  std::filesystem::remove(path);
  glecma_source_release(source);
}

// ---------------------------------------------------------------------------
// UTF-8 against the Unicode Standard
// ---------------------------------------------------------------------------

// Table 3-8's example, "61 F1 80 80 E1 80 C2 62 80 63 80 BF 64": its
// maximal subparts are F1 80 80, E1 80, C2, 80, 80 and BF. Decoding from the
// start and skipping each maximal subpart must find exactly those, in that
// order, between the well-formed a, b, c and d.
TEST(Utf8, TableThreeEightSegmentsIntoItsMaximalSubparts) {
  const uint8_t bytes[] = {0x61, 0xF1, 0x80, 0x80, 0xE1, 0x80, 0xC2, 0x62,
      0x80, 0x63, 0x80, 0xBF, 0x64};
  struct Piece {
    size_t offset;
    size_t length;
    bool well_formed;
    uint32_t code_point;
  };
  const Piece want[] = {{0, 1, true, 0x61}, {1, 3, false, 0},
      {4, 2, false, 0}, {6, 1, false, 0}, {7, 1, true, 0x62},
      {8, 1, false, 0}, {9, 1, true, 0x63}, {10, 1, false, 0},
      {11, 1, false, 0}, {12, 1, true, 0x64}};
  size_t offset = 0;
  size_t i = 0;
  while (offset < sizeof(bytes)) {
    ASSERT_LT(i, sizeof(want) / sizeof(want[0]));
    uint32_t cp = 0;
    size_t length = 0;
    bool ok = glecma_source_utf8_decode(bytes + offset, sizeof(bytes) - offset,
        &cp, &length);
    EXPECT_EQ(offset, want[i].offset) << i;
    EXPECT_EQ(ok, want[i].well_formed) << i;
    EXPECT_EQ(length, want[i].length) << i;
    if (ok) {
      EXPECT_EQ(cp, want[i].code_point) << i;
    }
    offset += length;
    ++i;
  }
  EXPECT_EQ(i, sizeof(want) / sizeof(want[0]));

  // And through the public API, the first ill-formed subsequence is at 1.
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8(reinterpret_cast<const char *>(bytes),
                sizeof(bytes), nullptr, nullptr, &failure, &source),
      GLECMA_ERR_CORRUPT);
  ExpectLocation(failure.location, GLECMA_Location{1, 1, 1}, "Table 3-8");
}

// Named ill-formed shapes, each with the offset of its first bad byte and
// the reason the failure record gives: the classes Table 3-7 rules out.
TEST(Utf8, EachIllFormedClassIsCorruptAtItsStart) {
  const char * const continuation = "UTF-8 continuation byte with no lead byte";
  const char * const overlong = "overlong UTF-8 encoding";
  const char * const never = "byte that never occurs in UTF-8";
  const char * const truncated =
      "UTF-8 sequence truncated by the end of the input";
  const char * const surrogate = "UTF-8 encoding of a surrogate code point";
  const char * const above = "UTF-8 encoding of a value above U+10FFFF";
  const char * const missing = "UTF-8 sequence missing a continuation byte";
  struct Case {
    std::string bytes;
    size_t offset;
    const char * reason;
  };
  const Case cases[] = {
      {"\x80", 0, continuation},
      {"a\xBF", 1, continuation},
      {"\xC0\x80", 0, overlong},          // overlong NUL
      {"\xC1\xBF", 0, overlong},
      {"\xE0\x80\x80", 0, overlong},      // three-byte
      {"\xE0\x9F\xBF", 0, overlong},      // three-byte, top of range
      {"\xF0\x80\x80\x80", 0, overlong},  // four-byte
      {"\xF0\x8F\xBF\xBF", 0, overlong},  // four-byte, top of range
      {"\xED\xA0\x80", 0, surrogate},     // U+D800
      {"\xED\xBF\xBF", 0, surrogate},     // U+DFFF
      {"\xF4\x90\x80\x80", 0, above},     // U+110000
      {"\xF5\x80\x80\x80", 0, never},     // lead byte above F4
      {"\xFF", 0, never},
      {"\xFE", 0, never},
      {"\xC2", 0, truncated},              // two-byte
      {"\xE0", 0, truncated},              // three-byte, lead only
      {"\xE2\x82", 0, truncated},          // three-byte
      {"\xF0\x9F\x98", 0, truncated},      // four-byte
      {"\xC2" "a", 0, missing},
      {"\xE2\x82" "a", 0, missing},
      {"ok\xE2\x28\xA1", 2, missing},      // bad second byte
      // E0 and F0 followed by something that is no continuation byte at all
      // are not overlong forms: nothing was encoded.
      {"\xE0" "A", 0, missing},
      {"\xF0" "A", 0, missing},
      {std::string("\xE0\x00", 2), 0, missing},
      {"\xF0\xC0", 0, missing},
  };
  for (const Case & c : cases) {
    GLECMA_Source * source = kUntouched;
    GLECMA_Failure failure = GLECMA_FAILURE_INIT;
    EXPECT_EQ(glecma_source_create_utf8(c.bytes.data(), c.bytes.size(),
                  nullptr, nullptr, &failure, &source),
        GLECMA_ERR_CORRUPT)
        << c.reason;
    EXPECT_EQ(source, kUntouched);
    EXPECT_EQ(failure.location.offset, c.offset) << c.reason;
    ASSERT_NE(failure.message, nullptr) << c.reason;
    EXPECT_STREQ(failure.message, c.reason)
        << "input starting with byte at offset " << c.offset;
  }
}

// Table 3-7's boundaries, accepted: the first and last code point of every
// row, and each side of the surrogate gap.
TEST(Utf8, EveryRowOfTableThreeSevenIsAccepted) {
  const uint32_t edges[] = {0x00, 0x7F, 0x80, 0x7FF, 0x800, 0xFFF, 0x1000,
      0xCFFF, 0xD000, 0xD7FF, 0xE000, 0xFFFF, 0x10000, 0x3FFFF, 0x40000,
      0xFFFFF, 0x100000, 0x10FFFF};
  std::vector<uint32_t> cps(std::begin(edges), std::end(edges));
  GLECMA_Source * source = MakeUtf8(Utf8(cps));
  ASSERT_NE(source, nullptr);
  Differential(source, cps, false, "Table 3-7 edges");
  glecma_source_release(source);
}

// Every Unicode scalar value, encoded by the reference encoder, decodes to
// itself with its own length: the decoder accepts the whole of Table 3-7.
TEST(Utf8, EveryScalarValueRoundTrips) {
  for (uint32_t c = 0; c <= 0x10FFFF; ++c) {
    if (c >= 0xD800 && c <= 0xDFFF) {
      continue;
    }
    std::string bytes = Utf8({c});
    uint32_t got = 0;
    size_t length = 0;
    ASSERT_TRUE(glecma_source_utf8_decode(
        reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), &got,
        &length))
        << std::hex << c;
    ASSERT_EQ(got, c);
    ASSERT_EQ(length, bytes.size());
  }
}

// The decoder against an oracle built from the definition, not from Table
// 3-7: the well-formed sequences are exactly the encodings of the scalar
// values, and a maximal subpart is the longest prefix of the input that
// begins some encoding (at least one byte). Every input of up to four bytes
// drawn from the byte values at the edges of the table's ranges is checked,
// and every two-byte input exhaustively.
TEST(Utf8, DecoderMatchesTheEncodingsOfTheScalarValues) {
  // Keys are (length << 24 | bytes), for prefixes of lengths 1 to 3, and a
  // separate set of complete encodings (a four-byte encoding is complete by
  // being a four-byte prefix of one).
  std::unordered_set<uint64_t> prefixes;
  std::unordered_set<uint64_t> complete;
  auto key = [](const uint8_t * b, size_t n) {
    uint64_t k = n;
    for (size_t i = 0; i < n; ++i) {
      k = (k << 8) | b[i];
    }
    return k;
  };
  for (uint32_t c = 0; c <= 0x10FFFF; ++c) {
    if (c >= 0xD800 && c <= 0xDFFF) {
      continue;
    }
    std::string e = Utf8({c});
    const uint8_t * b = reinterpret_cast<const uint8_t *>(e.data());
    for (size_t n = 1; n <= e.size(); ++n) {
      prefixes.insert(key(b, n));
    }
    complete.insert(key(b, e.size()));
  }
  auto check = [&](const uint8_t * b, size_t n) {
    // The oracle: the longest prefix that is a complete encoding, else the
    // longest prefix that begins one.
    size_t longest_prefix = 0;
    size_t complete_length = 0;
    for (size_t k = 1; k <= n && k <= 4; ++k) {
      if (prefixes.count(key(b, k)) == 0) {
        break;
      }
      longest_prefix = k;
      if (complete.count(key(b, k)) != 0) {
        complete_length = k;
        break;
      }
    }
    uint32_t cp = 0;
    size_t length = 0;
    bool ok = glecma_source_utf8_decode(b, n, &cp, &length);
    if (complete_length != 0) {
      ASSERT_TRUE(ok) << std::hex << int(b[0]) << " n=" << n;
      ASSERT_EQ(length, complete_length) << std::hex << int(b[0]);
    } else {
      ASSERT_FALSE(ok) << std::hex << int(b[0]) << " n=" << n;
      ASSERT_EQ(length, longest_prefix == 0 ? 1 : longest_prefix)
          << std::hex << int(b[0]) << " " << int(n > 1 ? b[1] : 0);
    }
  };
  for (uint32_t x = 0; x < 0x10000; ++x) {
    uint8_t b[2] = {static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x)};
    check(b, 2);
    check(b, 1);
  }
  const uint8_t edges[] = {0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0,
      0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0,
      0xF1, 0xF3, 0xF4, 0xF5, 0xFF};
  for (uint8_t a : edges) {
    for (uint8_t b : edges) {
      for (uint8_t c : edges) {
        uint8_t three[3] = {a, b, c};
        check(three, 3);
        for (uint8_t d : edges) {
          uint8_t four[4] = {a, b, c, d};
          check(four, 4);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The differential: cursor against query against the reference model
// ---------------------------------------------------------------------------

TEST(SourceDifferential, CorpusInBothEncodingsAgreesEverywhere) {
  int n = 0;
  for (const std::vector<uint32_t> & cps : Corpus()) {
    GLECMA_Source * narrow = MakeUtf8(Utf8(cps));
    Differential(narrow, cps, false, "corpus " + std::to_string(n) + " UTF-8");
    glecma_source_release(narrow);
    GLECMA_Source * wide = MakeUtf16(Utf16(cps));
    Differential(wide, cps, true, "corpus " + std::to_string(n) + " UTF-16");
    glecma_source_release(wide);
    ++n;
  }
}

TEST(SourceDifferential, GeneratedSourcesAcrossManyCheckpointsAgree) {
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    std::vector<uint32_t> narrow_cps = Generated(seed, 3000, false);
    GLECMA_Source * narrow = MakeUtf8(Utf8(narrow_cps));
    Differential(narrow, narrow_cps, false,
        "generated " + std::to_string(seed) + " UTF-8");
    glecma_source_release(narrow);
    std::vector<uint32_t> wide_cps = Generated(seed, 3000, true);
    std::vector<uint16_t> units;
    for (uint32_t c : wide_cps) {
      std::vector<uint16_t> one = Utf16({c});
      units.insert(units.end(), one.begin(), one.end());
    }
    GLECMA_Source * wide = MakeUtf16(units);
    Differential(wide, wide_cps, true,
        "generated " + std::to_string(seed) + " UTF-16");
    glecma_source_release(wide);
  }
}

// A four-byte code point straddling the first checkpoint boundary: the
// checkpoint is placed after it, the bytes inside it are refused, and the
// location just past it is still exact.
TEST(SourceCheckpoints, CodePointStraddlingABoundaryIsHandled) {
  std::vector<uint32_t> cps(1022, 'a');
  cps.push_back(0x1F600);  // bytes 1022..1025
  cps.push_back('b');      // byte 1026
  std::string text = Utf8(cps);
  GLECMA_Source * source = MakeUtf8(text);
  ASSERT_EQ(source->checkpoint_count, 2u);
  EXPECT_EQ(source->checkpoints[1].offset, 1026u);
  for (size_t offset : {1023u, 1024u, 1025u}) {
    GLECMA_Location location;
    EXPECT_EQ(glecma_source_location(source, offset, &location),
        GLECMA_ERR_INVALID)
        << offset;
  }
  ExpectLocation(At(source, 1022), GLECMA_Location{1022, 1, 1022}, "emoji");
  ExpectLocation(At(source, 1026), GLECMA_Location{1026, 1, 1024}, "b");
  glecma_source_release(source);
}

// source.h and the design: one checkpoint per 1024 input units, so the
// table is a few percent of the copy, never a per-line or per-code-point
// array.
TEST(SourceCheckpoints, OneCheckpointPerIntervalOfInput) {
  const size_t sizes[] = {0, 1, 1023, 1024, 1025, 4096, 100000};
  for (size_t size : sizes) {
    GLECMA_Source * source = MakeUtf8(std::string(size, '\n'));
    EXPECT_EQ(source->checkpoint_count, size / 1024 + 1) << size;
    EXPECT_EQ(glecma_source_lines(source), size + 1) << size;
    glecma_source_release(source);
  }
}

// source_internal.h: checkpoint i is the first code-point boundary at or
// after i * 1024, which is what bounds a query's walk to one interval plus
// one code point. Each checkpoint's position is checked against the
// reference model's boundaries; its stored line and column are compared
// with a query at its own offset, which starts from that checkpoint and walks
// nothing, so that comparison only shows the two agree. That queries use the
// stored state is SourceCheckpoints.QueryStartsFromItsOwnCheckpoint.
TEST(SourceCheckpoints, EachCheckpointIsTheFirstBoundaryOfItsInterval) {
  for (uint32_t seed = 11; seed <= 13; ++seed) {
    std::vector<uint32_t> cps = Generated(seed, 4000, false);
    GLECMA_Source * source = MakeUtf8(Utf8(cps));
    Expected e = Reference(cps, false);
    std::vector<size_t> boundaries = e.offsets;
    boundaries.push_back(e.end.offset);
    ASSERT_EQ(source->checkpoint_count, e.end.offset / 1024 + 1);
    for (size_t i = 0; i < source->checkpoint_count; ++i) {
      const GLECMA_SourceCheckpoint & checkpoint = source->checkpoints[i];
      size_t first = *std::lower_bound(boundaries.begin(), boundaries.end(),
          i * 1024);
      EXPECT_EQ(checkpoint.offset, first) << "seed " << seed << " #" << i;
      GLECMA_Location stored = {checkpoint.offset, checkpoint.line,
          checkpoint.column};
      ExpectLocation(stored, At(source, checkpoint.offset),
          "checkpoint " + std::to_string(i));
    }
    glecma_source_release(source);
  }
}

// A query reads its interval's checkpoint, not the start of the source: a
// sentinel planted in checkpoint 2 of a one-line source shows up in every
// answer inside [2048, 3072), and in none before it.
TEST(SourceCheckpoints, QueryStartsFromItsOwnCheckpoint) {
  GLECMA_Source * source = MakeUtf8(std::string(3000, 'a'));
  ASSERT_EQ(source->checkpoint_count, 3u);
  ASSERT_EQ(source->checkpoints[2].offset, 2048u);
  source->checkpoints[2].line = 77;
  source->checkpoints[2].column = 1000000;
  ExpectLocation(At(source, 2048), GLECMA_Location{2048, 77, 1000000},
      "checkpoint 2 itself");
  ExpectLocation(At(source, 2100), GLECMA_Location{2100, 77, 1000052},
      "inside interval 2");
  ExpectLocation(At(source, 3000), GLECMA_Location{3000, 77, 1000952},
      "end, inside interval 2");
  ExpectLocation(At(source, 2047), GLECMA_Location{2047, 1, 2047},
      "interval 1, untouched");
  glecma_source_release(source);
}

// The UTF-16 straddle: a surrogate pair at units 1023 and 1024 puts
// checkpoint 1 at 1025, and the pair's low half is not a position.
TEST(SourceCheckpoints, SurrogatePairStraddlingABoundaryIsHandled) {
  std::vector<uint16_t> units(1023, 'a');
  units.push_back(0xD83D);  // unit 1023
  units.push_back(0xDE00);  // unit 1024
  units.push_back('b');     // unit 1025
  GLECMA_Source * source = MakeUtf16(units);
  ASSERT_EQ(source->checkpoint_count, 2u);
  EXPECT_EQ(source->checkpoints[1].offset, 1025u);
  GLECMA_Location location = {9, 9, 9};
  EXPECT_EQ(glecma_source_location(source, 1024, &location),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(location.offset, 9u);
  ExpectLocation(At(source, 1023), GLECMA_Location{1023, 1, 1023}, "pair");
  ExpectLocation(At(source, 1025), GLECMA_Location{1025, 1, 1025}, "b");
  glecma_source_release(source);
}

// Seeking and then reading gives the code point the walk read at that
// offset, from every checkpoint's neighborhood.
TEST(SourceCursor, SeekThenNextMatchesTheWalk) {
  std::vector<uint32_t> cps = Generated(42, 2500, true);
  std::vector<uint16_t> units;
  for (uint32_t c : cps) {
    std::vector<uint16_t> one = Utf16({c});
    units.insert(units.end(), one.begin(), one.end());
  }
  GLECMA_Source * source = MakeUtf16(units);
  Expected e = Reference(cps, true);
  for (size_t i = 0; i < cps.size(); i += 7) {
    GLECMA_SourceCursor cursor;
    ASSERT_EQ(glecma_source_cursor_seek(&cursor, source, e.offsets[i]),
        GLECMA_OK);
    uint32_t cp = 0;
    GLECMA_Location location;
    ASSERT_TRUE(glecma_source_cursor_next(&cursor, &cp, &location));
    EXPECT_EQ(cp, cps[i]) << i;
    ExpectLocation(location, e.locations[i], "seek " + std::to_string(i));
  }
  glecma_source_release(source);
}

// ---------------------------------------------------------------------------
// Threads. Meaningful under TSan, which make test-tsan runs; a plain run
// checks only that the answers agree.
// ---------------------------------------------------------------------------

// source.h: a source is immutable, so any number of threads may query it at
// once.
TEST(SourceThreads, ConcurrentQueriesOnOneSourceAgree) {
  std::vector<uint32_t> cps = Generated(7, 6000, false);
  GLECMA_Source * source = MakeUtf8(Utf8(cps));
  Expected e = Reference(cps, false);
  std::atomic<size_t> mismatches{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 6; ++t) {
    threads.emplace_back([&, t] {
      for (size_t i = static_cast<size_t>(t); i < cps.size(); i += 3) {
        GLECMA_Location location;
        if (glecma_source_location(source, e.offsets[i], &location) !=
                GLECMA_OK ||
            location.line != e.locations[i].line ||
            location.column != e.locations[i].column) {
          mismatches++;
        }
      }
    });
  }
  for (std::thread & thread : threads) {
    thread.join();
  }
  EXPECT_EQ(mismatches.load(), 0u);
  glecma_source_release(source);
}

// No process-wide state: creates and destroys on separate threads, each with
// its own sources, do not interfere.
TEST(SourceThreads, ConcurrentCreatesOnManySourcesAreIndependent) {
  std::atomic<size_t> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 6; ++t) {
    threads.emplace_back([&, t] {
      for (int round = 0; round < 40; ++round) {
        std::string text(200 + static_cast<size_t>(t * 37 + round), 'x');
        text += "\r\n\xE2\x80\xA8y";
        GLECMA_Source * source = nullptr;
        GLECMA_Failure failure = GLECMA_FAILURE_INIT;
        if (glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, &failure, &source) != GLECMA_OK) {
          failures++;
          continue;
        }
        GLECMA_Location location;
        if (glecma_source_location(source, text.size() - 1, &location) !=
                GLECMA_OK ||
            location.line != 3 || location.column != 0 ||
            glecma_source_lines(source) != 3) {
          failures++;
        }
        glecma_source_release(source);
      }
    });
  }
  for (std::thread & thread : threads) {
    thread.join();
  }
  EXPECT_EQ(failures.load(), 0u);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
