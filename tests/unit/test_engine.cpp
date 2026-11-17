// SPDX-License-Identifier: LGPL-3.0-only
//
// The engine as runtime-core sees it: its descriptor, and registration with a
// context. engine.h is the contract each test cites.

#include <ghoti.io/lang-ecmascript/lang-ecmascript.h>

#include <ghoti.io/runtime-core/b/group.h>
#include <ghoti.io/runtime-core/b/options.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace {

// A sentinel an output must still hold after a refusal, because outputs are
// written only on success.
constexpr GRCORE_EngineId kUntouched = UINT32_C(0xA5A5A5A5);

class Engine : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(grcore_group_create(nullptr, nullptr, &group_), GRCORE_OK);
    ASSERT_EQ(grcore_context_create(group_, nullptr, &context_), GRCORE_OK);
  }

  void TearDown() override {
    if (context_ != nullptr) {
      EXPECT_EQ(grcore_context_destroy(context_), GRCORE_OK);
    }
    if (group_ != nullptr) {
      EXPECT_EQ(grcore_group_destroy(group_), GRCORE_OK);
    }
  }

  GRCORE_Group * group_ = nullptr;
  GRCORE_Context * context_ = nullptr;
};

// Another language's engine, so a context holds more than this one.
const GRCORE_EngineDescriptor other_engine = GRCORE_ENGINE_DESCRIPTOR_INIT(
    "other", nullptr, nullptr, nullptr, {nullptr, nullptr, nullptr}, {0, 0, 0},
    nullptr, nullptr, nullptr, nullptr);

}  // namespace

TEST(Descriptor, NamesTheEngineAndIsValidToRegister) {
  const GRCORE_EngineDescriptor * d = glecma_engine_descriptor();
  ASSERT_NE(d, nullptr);
  ASSERT_NE(d->name, nullptr);
  EXPECT_STREQ(d->name, GLECMA_ENGINE_NAME);
  EXPECT_STREQ(d->name, GLECMA_ENGINE_NAME);
  EXPECT_EQ(d->size, sizeof(GRCORE_EngineDescriptor));
  EXPECT_TRUE(grcore_engine_descriptor_valid(d));
}

// The address identifies the engine in every context, so it must not change.
TEST(Descriptor, IsOneObjectForTheLifeOfTheProcess) {
  EXPECT_EQ(glecma_engine_descriptor(), glecma_engine_descriptor());
}

TEST_F(Engine, RegisterGivesANonZeroIdNamingTheDescriptor) {
  GRCORE_EngineId id = 0;
  ASSERT_EQ(glecma_engine_register(context_, &id), GLECMA_OK);
  EXPECT_NE(id, 0u);
  EXPECT_EQ(grcore_engine_descriptor(context_, id), glecma_engine_descriptor());
  EXPECT_EQ(grcore_engine_count(context_), 1u);
}

// engine.h: registration is idempotent, unlike grcore_engine_register.
TEST_F(Engine, RegisterTwiceReturnsTheFirstIdAndAddsNothing) {
  GRCORE_EngineId first = 0;
  GRCORE_EngineId second = 0;
  ASSERT_EQ(glecma_engine_register(context_, &first), GLECMA_OK);
  ASSERT_EQ(glecma_engine_register(context_, &second), GLECMA_OK);
  EXPECT_EQ(first, second);
  EXPECT_EQ(grcore_engine_count(context_), 1u);
}

// The id is the one this descriptor was given, not the first in the table.
TEST_F(Engine, RegisterBesideAnotherEngineFindsItsOwnId) {
  GRCORE_EngineId other = 0;
  ASSERT_EQ(grcore_engine_register(context_, &other_engine, &other), GRCORE_OK);
  GRCORE_EngineId id = 0;
  ASSERT_EQ(glecma_engine_register(context_, &id), GLECMA_OK);
  EXPECT_NE(id, other);
  GRCORE_EngineId again = 0;
  ASSERT_EQ(glecma_engine_register(context_, &again), GLECMA_OK);
  EXPECT_EQ(again, id);
  GRCORE_EngineId found = 0;
  ASSERT_EQ(glecma_engine_find(context_, &found), GLECMA_OK);
  EXPECT_EQ(found, id);
  EXPECT_EQ(grcore_engine_count(context_), 2u);
}

TEST_F(Engine, FindRefusesAContextThatDoesNotHoldTheEngine) {
  GRCORE_EngineId id = kUntouched;
  EXPECT_EQ(glecma_engine_find(context_, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
  GRCORE_EngineId other = 0;
  ASSERT_EQ(grcore_engine_register(context_, &other_engine, &other), GRCORE_OK);
  EXPECT_EQ(glecma_engine_find(context_, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
}

TEST_F(Engine, NullArgumentsAreRefusedAndWriteNothing) {
  GRCORE_EngineId id = kUntouched;
  EXPECT_EQ(glecma_engine_register(nullptr, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
  EXPECT_EQ(glecma_engine_register(context_, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_engine_find(nullptr, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
  EXPECT_EQ(glecma_engine_find(context_, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(grcore_engine_count(context_), 0u);
}

// engine.h: the caller must own the context. The idempotent path must refuse
// it too, rather than answer from a table the caller had no right to read.
TEST_F(Engine, RegisterRefusesAContextTheCallerDoesNotOwn) {
  GRCORE_EngineId first = 0;
  ASSERT_EQ(glecma_engine_register(context_, &first), GLECMA_OK);
  ASSERT_EQ(grcore_context_release(context_), GRCORE_OK);
  ASSERT_FALSE(grcore_context_is_owner(context_));
  GRCORE_EngineId id = kUntouched;
  EXPECT_EQ(glecma_engine_register(context_, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
  ASSERT_EQ(grcore_context_acquire(context_), GRCORE_OK);
  EXPECT_EQ(grcore_engine_count(context_), 1u);
}

TEST_F(Engine, RegisterRefusesAContextThatHasNotRegisteredAndIsNotOwned) {
  ASSERT_EQ(grcore_context_release(context_), GRCORE_OK);
  GRCORE_EngineId id = kUntouched;
  EXPECT_EQ(glecma_engine_register(context_, &id), GLECMA_ERR_INVALID);
  EXPECT_EQ(id, kUntouched);
  ASSERT_EQ(grcore_context_acquire(context_), GRCORE_OK);
  EXPECT_EQ(grcore_engine_count(context_), 0u);
}

// engine.h: the context's memory budget refusing the engine table is
// ERR_LIMIT, and the refusal leaves the context's engines as they were.
TEST(EngineBudget, MemoryBudgetRefusalIsALimitAndChangesNothing) {
  GRCORE_Group * group = nullptr;
  GRCORE_Options * options = nullptr;
  GRCORE_Context * context = nullptr;
  ASSERT_EQ(grcore_group_create(nullptr, nullptr, &group), GRCORE_OK);
  ASSERT_EQ(grcore_options_create(nullptr, &options), GRCORE_OK);
  ASSERT_EQ(grcore_options_set_memory_bytes(options, 1), GRCORE_OK);
  ASSERT_EQ(grcore_options_set_memory_reserve(options, 0), GRCORE_OK);
  ASSERT_EQ(grcore_context_create(group, options, &context), GRCORE_OK);
  grcore_options_destroy(options);

  GRCORE_EngineId id = kUntouched;
  EXPECT_EQ(glecma_engine_register(context, &id), GLECMA_ERR_LIMIT);
  EXPECT_EQ(id, kUntouched);
  EXPECT_EQ(grcore_engine_count(context), 0u);
  EXPECT_EQ(glecma_engine_find(context, &id), GLECMA_ERR_INVALID);

  EXPECT_EQ(grcore_context_destroy(context), GRCORE_OK);
  EXPECT_EQ(grcore_group_destroy(group), GRCORE_OK);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
