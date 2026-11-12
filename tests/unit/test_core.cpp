// SPDX-License-Identifier: LGPL-3.0-only
//
// The library-wide basics: the result vocabulary, the version, the edition,
// the allocator, and the conversion of runtime-core results.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <ghoti.io/cutil/allocator.h>
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "core/core_internal.h"

// The suite fixes the shared vocabulary and its order, so that a result
// means the same number in every library; an appended code goes before the
// count and never moves these.
TEST(Result, SharedVocabularyKeepsItsNumbers) {
  EXPECT_EQ(GLECMA_OK, 0);
  EXPECT_EQ(GLECMA_ERR_IO, 1);
  EXPECT_EQ(GLECMA_ERR_FORMAT, 2);
  EXPECT_EQ(GLECMA_ERR_UNSUPPORTED, 3);
  EXPECT_EQ(GLECMA_ERR_LIMIT, 4);
  EXPECT_EQ(GLECMA_ERR_CORRUPT, 5);
  EXPECT_EQ(GLECMA_ERR_OOM, 6);
  EXPECT_EQ(GLECMA_ERR_INVALID, 7);
  EXPECT_EQ(GLECMA_ERR_INTERNAL, 8);
}

// core.h: the syntax result is the first value appended after the
// shared nine, before the count, so it moves none of them.
TEST(Result, SyntaxIsAppendedAfterTheSharedNine) {
  EXPECT_EQ(GLECMA_ERR_SYNTAX, 9);
  EXPECT_EQ(GLECMA_RESULT_COUNT, 10);
}

TEST(Result, EveryResultHasItsOwnDescription) {
  std::set<std::string> seen;
  for (int i = 0; i < GLECMA_RESULT_COUNT; ++i) {
    const char * text = glecma_result_string(static_cast<GLECMA_Result>(i));
    ASSERT_NE(text, nullptr) << i;
    EXPECT_STRNE(text, "Unknown error") << i;
    EXPECT_TRUE(seen.insert(text).second) << "duplicate description: " << text;
  }
}

TEST(Result, ValueOutsideTheEnumIsUnknownNotNull) {
  EXPECT_STREQ(glecma_result_string(GLECMA_RESULT_COUNT), "Unknown error");
  EXPECT_STREQ(glecma_result_string(static_cast<GLECMA_Result>(-1)),
      "Unknown error");
  EXPECT_STREQ(glecma_result_string(static_cast<GLECMA_Result>(1000)),
      "Unknown error");
}

TEST(Version, LibraryReportsTheVersionOfItsHeaders) {
  EXPECT_STREQ(glecma_version_string(), GLECMA_VERSION_STRING);
  EXPECT_EQ(glecma_version_number(), GLECMA_VERSION_NUMBER);
}

TEST(Version, NumberPacksOneBytePerComponent) {
  EXPECT_EQ(GLECMA_MAKE_VERSION(1, 2, 3), 0x010203u);
  EXPECT_LT(GLECMA_MAKE_VERSION(1, 9, 9), GLECMA_MAKE_VERSION(2, 0, 0));
  EXPECT_EQ(GLECMA_VERSION_NUMBER,
      GLECMA_MAKE_VERSION(GLECMA_VERSION_MAJOR, GLECMA_VERSION_MINOR,
          GLECMA_VERSION_PATCH));
}

// The edition is a decision, recorded in core.h and documentation/design.md:
// ECMA-262 17th edition, ECMAScript 2026. Moving it is a deliberate change of
// this test, the header, the pinned conformance suite and the docs together.
TEST(Edition, TargetIsEcma262SeventeenthEditionEcmaScript2026) {
  EXPECT_EQ(GLECMA_ECMA262_EDITION, 17);
  EXPECT_EQ(GLECMA_ECMA262_YEAR, 2026);
  EXPECT_EQ(glecma_ecma262_edition(), static_cast<unsigned>(GLECMA_ECMA262_EDITION));
}

TEST(Allocator, DefaultIsCutilsDefault) {
  const GLECMA_Allocator * a = glecma_allocator_default();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a, gcu_allocator_default());
  void * p = a->malloc_fn(a->ctx, 16);
  ASSERT_NE(p, nullptr);
  a->free_fn(a->ctx, p);
}

// The shared nine mean the same thing in runtime-core's vocabulary and this
// one, so each converts to its namesake.
TEST(ResultFromGrcore, SharedVocabularyConvertsOneToOne) {
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_OK), GLECMA_OK);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_IO), GLECMA_ERR_IO);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_FORMAT), GLECMA_ERR_FORMAT);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_UNSUPPORTED),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_LIMIT), GLECMA_ERR_LIMIT);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_CORRUPT), GLECMA_ERR_CORRUPT);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_OOM), GLECMA_ERR_OOM);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_INVALID), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_INTERNAL),
      GLECMA_ERR_INTERNAL);
}

// A result this library has no counterpart for is a contract it did not
// expect a call to break: an internal error, never passed on as success or
// as a caller's mistake.
TEST(ResultFromGrcore, ResultWithNoCounterpartIsInternal) {
  EXPECT_EQ(glecma_result_from_grcore(GRCORE_ERR_GUEST), GLECMA_ERR_INTERNAL);
  EXPECT_EQ(glecma_result_from_grcore(static_cast<GRCORE_Result>(1000)),
      GLECMA_ERR_INTERNAL);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
