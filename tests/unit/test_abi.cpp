// SPDX-License-Identifier: LGPL-3.0-only
//
// The size-first structs and the counted source: what story 1c changed. A
// caller compiled against a shorter struct keeps working when a member is
// added (AD-14), and a source is read, retained and released from any thread.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// What a caller built before any member followed the header would declare.
struct HeaderOnlyLimits {
  uint32_t size;
  uint32_t reserved;
};
struct HeaderOnlyFailure {
  uint32_t size;
  uint32_t result;
};

constexpr unsigned char kCanary = 0xA5;

// A header-only struct followed by canaries, so a write past the declared
// size is seen.
template <typename Header>
struct Guarded {
  Header header;
  unsigned char canary[256];
  Guarded() {
    std::memset(this, 0, sizeof(*this));
    std::memset(canary, kCanary, sizeof(canary));
  }
  bool intact() const {
    for (unsigned char c : canary) {
      if (c != kCanary) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace

TEST(AbiLimits, DefaultWritesOnlyWhatTheCallersSizeDeclares) {
  Guarded<HeaderOnlyLimits> g;
  g.header.size = sizeof(HeaderOnlyLimits);
  ASSERT_EQ(glecma_limits_default(reinterpret_cast<GLECMA_Limits *>(&g)),
      GLECMA_OK);
  EXPECT_TRUE(g.intact()) << "a member beyond the declared size was written";
}

TEST(AbiLimits, AMemberBeyondTheCallersSizeTakesItsDefault) {
  // A caller that predates source_units asks for a 20-unit text; the cap is
  // the default, not whatever follows its struct.
  Guarded<HeaderOnlyLimits> g;
  g.header.size = sizeof(HeaderOnlyLimits);
  std::memset(g.canary, 0, sizeof(g.canary));  // a zero cap if it were read
  GLECMA_Source * source = nullptr;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_source_create_utf8("abcdefghijklmnopqrst", 20,
                reinterpret_cast<GLECMA_Limits *>(&g), nullptr, &failure,
                &source),
      GLECMA_OK);
  glecma_source_release(source);
}

TEST(AbiLimits, ASizeBelowTheHeaderIsInvalid) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.size = 4;
  EXPECT_EQ(glecma_limits_default(&limits), GLECMA_ERR_INVALID);
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8("a", 1, &limits, nullptr, nullptr,
                &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(source, nullptr);
}

TEST(AbiLimits, TheInitializerNamesItsSizeAndTheDefaults) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  EXPECT_EQ(limits.size, sizeof(GLECMA_Limits));
  EXPECT_EQ(limits.reserved, 0u);
  EXPECT_EQ(limits.source_units, GLECMA_LIMIT_SOURCE_UNITS_DEFAULT);
}

TEST(AbiFailure, IsWrittenOnlyWithinTheCallersSize) {
  Guarded<HeaderOnlyFailure> g;
  g.header.size = sizeof(HeaderOnlyFailure);
  GLECMA_Source * source = nullptr;
  // Ill-formed UTF-8 would write a message and a location in a full record.
  ASSERT_EQ(glecma_source_create_utf8("a\xC0", 2, nullptr, nullptr,
                reinterpret_cast<GLECMA_Failure *>(&g), &source),
      GLECMA_ERR_CORRUPT);
  EXPECT_EQ(g.header.result, static_cast<uint32_t>(GLECMA_ERR_CORRUPT));
  EXPECT_TRUE(g.intact()) << "a member beyond the declared size was written";
}

TEST(AbiFailure, ASizeBelowTheHeaderIsInvalidAndWritesNothing) {
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  failure.size = 4;
  failure.result = 77;
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8("a", 1, nullptr, nullptr, &failure,
                &source),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(failure.result, 77u);
  EXPECT_EQ(source, nullptr);
}

TEST(AbiFailure, TheInitializerNamesItsSizeAndIsZeroElsewhere) {
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(failure.size, sizeof(GLECMA_Failure));
  EXPECT_EQ(failure.result, 0u);
  EXPECT_EQ(failure.message, nullptr);
  EXPECT_EQ(failure.limit, nullptr);
  EXPECT_EQ(failure.location.offset, 0u);
}

// The layout rules (contract 22): the members sit where a caller built
// earlier expects them.
TEST(AbiLayout, SizeFirstAndFixedWidth) {
  EXPECT_EQ(offsetof(GLECMA_Limits, size), 0u);
  EXPECT_EQ(offsetof(GLECMA_Limits, source_units), 8u);
  EXPECT_EQ(offsetof(GLECMA_Failure, size), 0u);
  EXPECT_EQ(offsetof(GLECMA_Failure, result), 4u);
  EXPECT_EQ(sizeof(GLECMA_Location), 3 * sizeof(size_t));
}

TEST(SourceCount, RetainThenReleaseKeepsTheSourceUntilTheLastHolder) {
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8("ab\ncd", 5, nullptr, nullptr, nullptr,
                &source),
      GLECMA_OK);
  EXPECT_EQ(glecma_source_retain(source), source);
  glecma_source_release(source);  // the creator's hold
  EXPECT_EQ(glecma_source_code_points(source), 5u);  // still held by the retain
  glecma_source_release(source);
}

TEST(SourceCount, NullIsAccepted) {
  EXPECT_EQ(glecma_source_retain(nullptr), nullptr);
  glecma_source_release(nullptr);
}

// Retained, read and released from many threads at once; run under TSan by
// `make test-tsan`, and the last release is by the creating thread.
TEST(SourceCount, ManyThreadsRetainReadAndRelease) {
  std::string text;
  for (int i = 0; i < 4000; ++i) {
    text += "line \xC3\xA9 \xF0\x9F\x98\x80\n";
  }
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  const size_t points = glecma_source_code_points(source);
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    GLECMA_Source * mine = glecma_source_retain(source);
    threads.emplace_back([mine, points, &bad] {
      for (int i = 0; i < 200; ++i) {
        GLECMA_Source * again = glecma_source_retain(mine);
        GLECMA_Location location;
        if (glecma_source_code_points(again) != points ||
            glecma_source_location(again, 13 * 7, &location) != GLECMA_OK) {
          bad++;
        }
        glecma_source_release(again);
      }
      glecma_source_release(mine);
    });
  }
  glecma_source_release(source);  // the creator lets go while they run
  for (std::thread & thread : threads) {
    thread.join();
  }
  EXPECT_EQ(bad.load(), 0);
}

TEST(EngineName, CarriesTheBuildsBranchToken) {
  const GRCORE_EngineDescriptor * d = glecma_engine_descriptor();
  ASSERT_NE(d, nullptr);
  const std::string name = d->name;
  EXPECT_EQ(name, std::string("ecmascript") + GHOTIIO_LANG_ECMASCRIPT_BRANCH);
  EXPECT_GT(name.size(), std::string("ecmascript").size());
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
