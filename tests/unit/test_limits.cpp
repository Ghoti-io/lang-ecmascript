// SPDX-License-Identifier: LGPL-3.0-only
//
// GLECMA_Limits: the defaults, and each field driven one past its cap.
// limits.h is the contract each test cites: a cap is checked before the
// allocation it bounds, exceeding it is ERR_LIMIT naming the field, and zero
// is a cap of zero.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "../test_helpers.h"

namespace {

GLECMA_Source * const kUntouched =
    reinterpret_cast<GLECMA_Source *>(static_cast<uintptr_t>(0xA5A5A5A5u));

GLECMA_Limits Cap(uint64_t source_units) {
  GLECMA_Limits limits;
  EXPECT_EQ(glecma_limits_default(&limits), GLECMA_OK);
  limits.source_units = source_units;
  return limits;
}

}  // namespace

// The default is a decision recorded in limits.h and documentation/design.md:
// 64 Mi units.
TEST(LimitsDefault, SourceUnitsIsSixtyFourMebiUnits) {
  GLECMA_Limits limits;
  std::memset(&limits, 0, sizeof(limits));
  ASSERT_EQ(glecma_limits_default(&limits), GLECMA_OK);
  EXPECT_EQ(limits.source_units, UINT64_C(64) * 1024 * 1024);
  EXPECT_EQ(limits.source_units, GLECMA_LIMIT_SOURCE_UNITS_DEFAULT);
}

TEST(LimitsDefault, NullIsInvalid) {
  EXPECT_EQ(glecma_limits_default(nullptr), GLECMA_ERR_INVALID);
}

// limits.h: 11 units against a cap of 10 is refused, names the field,
// allocates nothing and writes no source.
TEST(SourceUnits, OnePastTheCapIsALimitAndAllocatesNothing) {
  const char text[] = "abcdefghijk";
  const uint16_t wide[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
      'k'};
  GLECMA_Limits limits = Cap(10);
  for (int encoding = 0; encoding < 2; ++encoding) {
    glecma_test::CountingAllocator counting;
    GLECMA_Failure failure;
    std::memset(&failure, 0xFF, sizeof(failure));
    GLECMA_Source * source = kUntouched;
    GLECMA_Result result = encoding == 0
        ? glecma_source_create_utf8(text, 11, &limits, counting.get(),
              &failure, &source)
        : glecma_source_create_utf16(wide, 11, &limits, counting.get(),
              &failure, &source);
    EXPECT_EQ(result, GLECMA_ERR_LIMIT) << encoding;
    EXPECT_EQ(source, kUntouched) << encoding;
    EXPECT_EQ(counting.calls(), 0u) << encoding;
    EXPECT_EQ(failure.result, GLECMA_ERR_LIMIT) << encoding;
    ASSERT_NE(failure.limit, nullptr) << encoding;
    EXPECT_STREQ(failure.limit, "source_units") << encoding;
    EXPECT_NE(failure.message, nullptr) << encoding;
    EXPECT_EQ(failure.location.offset, 0u) << encoding;
    EXPECT_EQ(failure.location.line, 0u) << encoding;
    EXPECT_EQ(failure.location.column, 0u) << encoding;
  }
}

// The cap is inclusive: a source exactly as long as it is accepted.
TEST(SourceUnits, ExactlyTheCapIsAccepted) {
  GLECMA_Limits limits = Cap(10);
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("abcdefghij", 10, &limits, nullptr,
                nullptr, &source),
      GLECMA_OK);
  EXPECT_EQ(glecma_source_units(source), 10u);
  glecma_source_destroy(source);
  const uint16_t wide[10] = {};
  ASSERT_EQ(glecma_source_create_utf16(wide, 10, &limits, nullptr, nullptr,
                &source),
      GLECMA_OK);
  EXPECT_EQ(glecma_source_units(source), 10u);
  glecma_source_destroy(source);
}

// The cap counts input units, not code points: four bytes of one emoji are
// four UTF-8 units and two UTF-16 units.
TEST(SourceUnits, CapCountsInputUnitsNotCodePoints) {
  GLECMA_Limits limits = Cap(3);
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure;
  EXPECT_EQ(glecma_source_create_utf8("\xF0\x9F\x98\x80", 4, &limits,
                nullptr, &failure, &source),
      GLECMA_ERR_LIMIT);
  EXPECT_EQ(source, kUntouched);
  const uint16_t pair[] = {0xD83D, 0xDE00};
  ASSERT_EQ(glecma_source_create_utf16(pair, 2, &limits, nullptr, &failure,
                &source),
      GLECMA_OK);
  EXPECT_EQ(glecma_source_code_points(source), 1u);
  glecma_source_destroy(source);
}

// limits.h: zero is a cap of zero, not a request for the default.
TEST(SourceUnits, ZeroIsACapOfZero) {
  GLECMA_Limits limits = Cap(0);
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("", 0, &limits, nullptr, nullptr,
                &source),
      GLECMA_OK);
  glecma_source_destroy(source);
  source = kUntouched;
  GLECMA_Failure failure;
  EXPECT_EQ(glecma_source_create_utf8("a", 1, &limits, nullptr, &failure,
                &source),
      GLECMA_ERR_LIMIT);
  EXPECT_EQ(source, kUntouched);
  EXPECT_STREQ(failure.limit, "source_units");
}

// The cap is checked before the encoding: an over-long ill-formed source is
// a limit, so a hostile input's cost is bounded before it is read.
TEST(SourceUnits, CapIsCheckedBeforeTheEncoding) {
  GLECMA_Limits limits = Cap(2);
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure;
  EXPECT_EQ(glecma_source_create_utf8("\xFF\xFF\xFF", 3, &limits, nullptr,
                &failure, &source),
      GLECMA_ERR_LIMIT);
  EXPECT_EQ(source, kUntouched);
}

// A NULL limits pointer means the defaults: one unit past 64 Mi is refused
// without being read or copied.
TEST(SourceUnits, NullLimitsMeanTheDefaultCap) {
  const size_t length = static_cast<size_t>(GLECMA_LIMIT_SOURCE_UNITS_DEFAULT) + 1;
  std::vector<char> text(length, 'a');
  glecma_test::CountingAllocator counting;
  GLECMA_Source * source = kUntouched;
  GLECMA_Failure failure;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), length, nullptr,
                counting.get(), &failure, &source),
      GLECMA_ERR_LIMIT);
  EXPECT_EQ(source, kUntouched);
  EXPECT_EQ(counting.calls(), 0u);
  EXPECT_STREQ(failure.limit, "source_units");
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
