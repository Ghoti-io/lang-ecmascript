// SPDX-License-Identifier: LGPL-3.0-only
//
// What surrounds the tokenizer: the parse options and the feature set, the
// token and literal-length limits, the rule codes, the internal built-in
// mode, the Unicode version query and the failure record's new members.

#include "../lex_test_support.h"

#include "../test_helpers.h"

#include <ghoti.io/unicode/char.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace glecma_test;

namespace {

uint32_t R(GLECMA_Rule rule) { return static_cast<uint32_t>(rule); }

constexpr unsigned char kCanary = 0xA5;

// A struct a caller built before a member was added, followed by canaries,
// so that a read or write beyond what it declared is seen.
template <typename Header>
struct Guarded {
  Header header;
  unsigned char canary[512];
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

struct HeaderOnlyOptions {
  uint32_t size;
  uint32_t reserved;
};

// Every member of a failure record up to and including `limit`: what a
// caller of an earlier version of the library declared.
struct FailureUpToLimit {
  uint32_t size;
  uint32_t result;
  const char * message;
  GLECMA_Location location;
  const char * limit;
};

GLECMA_Source * Make(const std::string & text) {
  GLECMA_Source * source = nullptr;
  EXPECT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                nullptr, nullptr, &source),
      GLECMA_OK);
  return source;
}

}  // namespace

// The codes of the enumeration are an append-only contract: a host stores and
// compares the numbers, so each is pinned here by value. A renumbering fails
// this test.
TEST(Rules, EveryCodeHasItsPinnedValue) {
  EXPECT_EQ(GLECMA_RULE_NONE, 0);
  EXPECT_EQ(GLECMA_RULE_UNEXPECTED_CHARACTER, 1);
  EXPECT_EQ(GLECMA_RULE_UNTERMINATED_COMMENT, 2);
  EXPECT_EQ(GLECMA_RULE_UNTERMINATED_STRING, 3);
  EXPECT_EQ(GLECMA_RULE_UNTERMINATED_TEMPLATE, 4);
  EXPECT_EQ(GLECMA_RULE_UNTERMINATED_REGEXP, 5);
  EXPECT_EQ(GLECMA_RULE_REGEXP_FLAGS, 6);
  EXPECT_EQ(GLECMA_RULE_IDENTIFIER_ESCAPE, 7);
  EXPECT_EQ(GLECMA_RULE_PRIVATE_NAME, 8);
  EXPECT_EQ(GLECMA_RULE_ESCAPE_HEX, 9);
  EXPECT_EQ(GLECMA_RULE_ESCAPE_UNICODE, 10);
  EXPECT_EQ(GLECMA_RULE_ESCAPE_CODE_POINT, 11);
  EXPECT_EQ(GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, 12);
  EXPECT_EQ(GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT, 13);
  EXPECT_EQ(GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL, 14);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, 15);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_SEPARATOR, 16);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, 17);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_BIGINT, 18);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_TRAILING, 19);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, 20);
  EXPECT_EQ(GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT, 21);
  EXPECT_EQ(GLECMA_RULE_ESCAPED_RESERVED_WORD, 22);
  EXPECT_EQ(GLECMA_RULE_RESERVED_WORD, 23);
  EXPECT_EQ(GLECMA_RULE_RESERVED_WORD_STRICT, 24);
  // The rules of the parser, appended after the lexical ones.
  EXPECT_EQ(GLECMA_RULE_UNEXPECTED_TOKEN, 25);
  EXPECT_EQ(GLECMA_RULE_UNEXPECTED_END, 26);
  EXPECT_EQ(GLECMA_RULE_MISSING_SEMICOLON, 27);
  EXPECT_EQ(GLECMA_RULE_INVALID_ASSIGNMENT_TARGET, 28);
  EXPECT_EQ(GLECMA_RULE_INVALID_UPDATE_TARGET, 29);
  EXPECT_EQ(GLECMA_RULE_INVALID_FOR_TARGET, 30);
  EXPECT_EQ(GLECMA_RULE_COVER_INITIALIZED_NAME, 31);
  EXPECT_EQ(GLECMA_RULE_REST_ELEMENT_POSITION, 32);
  EXPECT_EQ(GLECMA_RULE_EXPONENT_UNARY_OPERAND, 33);
  EXPECT_EQ(GLECMA_RULE_NULLISH_MIXED, 34);
  EXPECT_EQ(GLECMA_RULE_OPTIONAL_CHAIN_TEMPLATE, 35);
  EXPECT_EQ(GLECMA_RULE_OPTIONAL_CHAIN_NEW, 36);
  EXPECT_EQ(GLECMA_RULE_INVALID_SUPER, 37);
  EXPECT_EQ(GLECMA_RULE_IMPORT_META_OUTSIDE_MODULE, 38);
  EXPECT_EQ(GLECMA_RULE_IMPORT_CALL_ARGUMENTS, 39);
  EXPECT_EQ(GLECMA_RULE_ARROW_LINE_TERMINATOR, 40);
  EXPECT_EQ(GLECMA_RULE_INVALID_ARROW_PARAMETERS, 41);
  EXPECT_EQ(GLECMA_RULE_DECLARATION_POSITION, 42);
  EXPECT_EQ(GLECMA_RULE_FOR_HEAD_RESTRICTED, 43);
  EXPECT_EQ(GLECMA_RULE_FOR_AWAIT_POSITION, 44);
  EXPECT_EQ(GLECMA_RULE_INVALID_CLASS_ELEMENT, 45);
  EXPECT_EQ(GLECMA_RULE_THROW_LINE_TERMINATOR, 46);
  // Story 5's early errors (the redeclaration rules and the regular
  // expression rules are appended after these).
  EXPECT_EQ(GLECMA_RULE_BINDING_EVAL_ARGUMENTS, 47);
  EXPECT_EQ(GLECMA_RULE_RETURN_POSITION, 48);
  EXPECT_EQ(GLECMA_RULE_SUPER_PROPERTY_POSITION, 49);
  EXPECT_EQ(GLECMA_RULE_SUPER_CALL_POSITION, 50);
  EXPECT_EQ(GLECMA_RULE_NEW_TARGET_POSITION, 51);
  EXPECT_EQ(GLECMA_RULE_ARGUMENTS_IN_INITIALIZER, 52);
  EXPECT_EQ(GLECMA_RULE_YIELD_IN_PARAMETERS, 53);
  EXPECT_EQ(GLECMA_RULE_AWAIT_IN_PARAMETERS, 54);
  EXPECT_EQ(GLECMA_RULE_DELETE_IDENTIFIER_STRICT, 55);
  EXPECT_EQ(GLECMA_RULE_DELETE_PRIVATE_REFERENCE, 56);
  EXPECT_EQ(GLECMA_RULE_WITH_STRICT, 57);
  EXPECT_EQ(GLECMA_RULE_DUPLICATE_PROTO, 58);
  EXPECT_EQ(GLECMA_RULE_CONST_WITHOUT_INITIALIZER, 59);
  EXPECT_EQ(GLECMA_RULE_LET_BOUND_NAME, 60);
  EXPECT_EQ(GLECMA_RULE_USE_STRICT_NON_SIMPLE_PARAMETERS, 61);
  EXPECT_EQ(GLECMA_RULE_LABEL_REDECLARED, 62);
  EXPECT_EQ(GLECMA_RULE_BREAK_TARGET, 63);
  EXPECT_EQ(GLECMA_RULE_CONTINUE_TARGET, 64);
  EXPECT_EQ(GLECMA_RULE_LABELLED_FUNCTION_BODY, 65);
  EXPECT_EQ(GLECMA_RULE_CLASS_CONSTRUCTOR_DUPLICATE, 66);
  EXPECT_EQ(GLECMA_RULE_CLASS_CONSTRUCTOR_KIND, 67);
  EXPECT_EQ(GLECMA_RULE_CLASS_STATIC_PROTOTYPE, 68);
  EXPECT_EQ(GLECMA_RULE_PRIVATE_NAME_CONSTRUCTOR, 69);
  EXPECT_EQ(GLECMA_RULE_PRIVATE_NAME_DUPLICATE, 70);
  EXPECT_EQ(GLECMA_RULE_PRIVATE_NAME_UNDECLARED, 71);
  EXPECT_EQ(GLECMA_RULE_BLOCK_REDECLARATION, 72);
  EXPECT_EQ(GLECMA_RULE_SWITCH_REDECLARATION, 73);
  EXPECT_EQ(GLECMA_RULE_SCRIPT_REDECLARATION, 74);
  EXPECT_EQ(GLECMA_RULE_FUNCTION_REDECLARATION, 75);
  EXPECT_EQ(GLECMA_RULE_CATCH_REDECLARATION, 76);
  EXPECT_EQ(GLECMA_RULE_FOR_REDECLARATION, 77);
  EXPECT_EQ(GLECMA_RULE_STATIC_BLOCK_REDECLARATION, 78);
  EXPECT_EQ(GLECMA_RULE_DUPLICATE_PARAMETER, 79);
  EXPECT_EQ(GLECMA_RULE_REGEXP_PATTERN, 80);
  EXPECT_EQ(GLECMA_RULE_REGEXP_FLAG_SET, 81);
  EXPECT_EQ(GLECMA_RULE_MODULE_REDECLARATION, 82);
  EXPECT_EQ(GLECMA_RULE_MODULE_DUPLICATE_EXPORT, 83);
  EXPECT_EQ(GLECMA_RULE_MODULE_EXPORT_UNDECLARED, 84);
  EXPECT_EQ(GLECMA_RULE_MODULE_EXPORT_STRING_LOCAL, 85);
  EXPECT_EQ(GLECMA_RULE_MODULE_NAME_ILL_FORMED, 86);
  EXPECT_EQ(GLECMA_RULE_IMPORT_ATTRIBUTE_DUPLICATE, 87);
  // The count closes the enum: a rule appended later moves it, and every
  // value above must still be what it is.
  EXPECT_EQ(GLECMA_RULE_COUNT, 88);
}

TEST(Rules, EveryCodeHasANameAClauseAndAMessage) {
  std::set<std::string> names;
  for (uint32_t rule = 0; rule < GLECMA_RULE_COUNT; ++rule) {
    std::string name = glecma_rule_name(rule);
    EXPECT_NE(name, "unknown") << rule;
    EXPECT_NE(name, "") << rule;
    EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
    if (rule != GLECMA_RULE_NONE) {
      EXPECT_NE(std::string(glecma_rule_clause(rule)), "") << rule;
    }
  }
  EXPECT_STREQ(glecma_rule_name(GLECMA_RULE_NONE), "none");
  EXPECT_STREQ(glecma_rule_name(GLECMA_RULE_UNTERMINATED_STRING),
      "unterminated-string");
  EXPECT_STREQ(glecma_rule_clause(GLECMA_RULE_UNTERMINATED_STRING), "12.9.4");
  EXPECT_STREQ(glecma_rule_name(GLECMA_RULE_COUNT), "unknown");
  EXPECT_STREQ(glecma_rule_name(0xFFFFFFFFu), "unknown");
  EXPECT_STREQ(glecma_rule_clause(0xFFFFFFFFu), "");
}

// Every lexical rule the tokenizer itself reports, with an input that
// breaks it: the failure record carries the code, a message and a location
// inside the input.
TEST(Rules, EachLexicalRuleIsReportedByTheFailureRecord) {
  struct Case {
    GLECMA_Rule rule;
    const char * text;
    uint32_t mode;
  };
  const Case cases[] = {
      {GLECMA_RULE_UNEXPECTED_CHARACTER, "@", 0},
      {GLECMA_RULE_UNTERMINATED_COMMENT, "/*", 0},
      {GLECMA_RULE_UNTERMINATED_STRING, "'", 0},
      {GLECMA_RULE_UNTERMINATED_TEMPLATE, "`", 0},
      {GLECMA_RULE_UNTERMINATED_REGEXP, "/", 0},
      {GLECMA_RULE_REGEXP_FLAGS, "/a/\\u0067", 0},
      {GLECMA_RULE_IDENTIFIER_ESCAPE, "\\u0020", 0},
      {GLECMA_RULE_PRIVATE_NAME, "#", 0},
      {GLECMA_RULE_ESCAPE_HEX, "'\\x'", 0},
      {GLECMA_RULE_ESCAPE_UNICODE, "'\\u'", 0},
      {GLECMA_RULE_ESCAPE_CODE_POINT, "'\\u{}'", 0},
      {GLECMA_RULE_ESCAPE_LEGACY_OCTAL_STRICT, "'\\1'", GLECMA_LEXF_STRICT},
      {GLECMA_RULE_ESCAPE_NON_OCTAL_DECIMAL_STRICT, "'\\8'",
          GLECMA_LEXF_STRICT},
      {GLECMA_RULE_NUMERIC_EXPECTED_DIGIT, "0x", 0},
      {GLECMA_RULE_NUMERIC_SEPARATOR, "1_", 0},
      {GLECMA_RULE_NUMERIC_SEPARATOR_LEGACY, "01_1", 0},
      {GLECMA_RULE_NUMERIC_BIGINT, "1.5n", 0},
      {GLECMA_RULE_NUMERIC_TRAILING, "3in", 0},
      {GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT, "017", GLECMA_LEXF_STRICT},
      {GLECMA_RULE_NUMERIC_NON_OCTAL_DECIMAL_STRICT, "08",
          GLECMA_LEXF_STRICT},
  };
  for (const Case & c : cases) {
    Lexed lexed = Lex(c.text, c.mode);
    EXPECT_EQ(lexed.result, GLECMA_ERR_SYNTAX) << c.text;
    EXPECT_EQ(lexed.failure.result, static_cast<uint32_t>(GLECMA_ERR_SYNTAX));
    EXPECT_EQ(lexed.failure.rule, R(c.rule)) << c.text;
    ASSERT_NE(lexed.failure.message, nullptr) << c.text;
    EXPECT_STRNE(lexed.failure.message, "") << c.text;
    EXPECT_EQ(lexed.failure.limit, nullptr) << c.text;
    EXPECT_EQ(lexed.failure.feature[0], '\0') << c.text;
    EXPECT_GE(lexed.failure.location.line, 1u) << c.text;
    EXPECT_LE(lexed.failure.location.offset, std::strlen(c.text)) << c.text;
    // The same input reports the same error every time.
    Lexed again = Lex(c.text, c.mode);
    EXPECT_EQ(again.failure.rule, lexed.failure.rule) << c.text;
    EXPECT_EQ(again.failure.location.offset, lexed.failure.location.offset)
        << c.text;
    EXPECT_STREQ(again.failure.message, lexed.failure.message) << c.text;
  }
  // Rules the parser reports through ::glecma_token_identifier_check and a
  // template's escape_rule are tested with those; the rest of the lexical
  // rules are all above.
  std::set<uint32_t> tested;
  for (const Case & c : cases) {
    tested.insert(R(c.rule));
  }
  tested.insert(R(GLECMA_RULE_TEMPLATE_ESCAPE_OCTAL));   // a template's escape
  tested.insert(R(GLECMA_RULE_ESCAPED_RESERVED_WORD));   // identifier check
  tested.insert(R(GLECMA_RULE_RESERVED_WORD));
  tested.insert(R(GLECMA_RULE_RESERVED_WORD_STRICT));
  // The parser's rules (after the last lexical one) are tested with the parser.
  for (uint32_t rule = 1; rule <= GLECMA_RULE_RESERVED_WORD_STRICT; ++rule) {
    EXPECT_EQ(tested.count(rule), 1u) << "no test reports rule " << rule;
  }
}

// A lexical failure that is not an error of the source says so apart from
// ::GLECMA_ERR_SYNTAX: the rule is zero.
TEST(Rules, OnlyASyntaxErrorCarriesARule) {
  Lexed ok = Lex("a b");
  EXPECT_EQ(ok.failure.result, static_cast<uint32_t>(GLECMA_OK));
  EXPECT_EQ(ok.failure.rule, 0u);
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.tokens = 1;
  Lexed over = Lex("a b", 0, &limits);
  EXPECT_EQ(over.result, GLECMA_ERR_LIMIT);
  EXPECT_EQ(over.failure.rule, 0u);
}

// The Unicode version rule: one version for the whole engine, pinned, and
// asked of the library in use.
TEST(UnicodeVersion, IsThePinnedVersionAndUnicodes) {
  EXPECT_STREQ(glecma_unicode_version(), guni_ucd_version());
  EXPECT_STREQ(glecma_unicode_version(), GLECMA_UNICODE_VERSION);
  EXPECT_STREQ(glecma_unicode_version(), "17.0.0");
}

// The parse options: size-first, with a default call.
TEST(ParseOptions, TheInitializerAndTheDefaultCall) {
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  EXPECT_EQ(options.size, sizeof(GLECMA_ParseOptions));
  EXPECT_EQ(options.reserved, 0u);
  EXPECT_EQ(options.limits, nullptr);
  EXPECT_EQ(options.features, nullptr);
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  GLECMA_FeatureSet features = GLECMA_FEATURE_SET_INIT;
  options.limits = &limits;
  options.features = &features;
  ASSERT_EQ(glecma_parse_options_default(&options), GLECMA_OK);
  EXPECT_EQ(options.limits, nullptr);
  EXPECT_EQ(options.features, nullptr);
  EXPECT_EQ(glecma_parse_options_default(nullptr), GLECMA_ERR_INVALID);
  options.size = 4;
  EXPECT_EQ(glecma_parse_options_default(&options), GLECMA_ERR_INVALID);
}

// A caller built before any member followed the header gets the defaults for
// every member: nothing past its declared size is read or written.
TEST(ParseOptions, AShorterStructTakesTheDefaults) {
  Guarded<HeaderOnlyOptions> g;
  g.header.size = sizeof(HeaderOnlyOptions);
  // The bytes after the header, read as the limits pointer, would crash.
  std::memset(g.canary, 0xFF, sizeof(g.canary));
  auto * options = reinterpret_cast<GLECMA_ParseOptions *>(&g);
  EXPECT_EQ(glecma_parse_options_default(options), GLECMA_OK);
  for (size_t i = 0; i < sizeof(g.canary); ++i) {
    ASSERT_EQ(g.canary[i], 0xFF) << "written past the declared size";
  }
  GLECMA_Source * source = Make("a b c");
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, options, 0, nullptr), GLECMA_OK);
  EXPECT_EQ(lexer.max_tokens, GLECMA_LIMIT_TOKENS_DEFAULT);
  EXPECT_EQ(lexer.max_literal, GLECMA_LIMIT_LITERAL_LENGTH_DEFAULT);
  EXPECT_EQ(glecma_lexer_run(&lexer, nullptr, nullptr), GLECMA_OK);
  // A size below the header is refused.
  g.header.size = 4;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, options, 0, nullptr),
      GLECMA_ERR_INVALID);
  glecma_source_release(source);
}

// A struct of the limits that predates the token and literal caps is read
// as far as it goes, and the rest are the defaults.
TEST(ParseOptions, AShorterLimitsStructTakesTheDefaultsForTheNewCaps) {
  struct SourceUnitsOnly {
    uint32_t size;
    uint32_t reserved;
    uint64_t source_units;
  };
  Guarded<SourceUnitsOnly> g;
  g.header.size = sizeof(SourceUnitsOnly);
  g.header.source_units = 1000;
  std::memset(g.canary, 0, sizeof(g.canary));  // a zero cap if it were read
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = reinterpret_cast<GLECMA_Limits *>(&g);
  GLECMA_Source * source = Make("a b c d e");
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0, nullptr),
      GLECMA_OK);
  EXPECT_EQ(lexer.max_tokens, GLECMA_LIMIT_TOKENS_DEFAULT);
  EXPECT_EQ(lexer.max_literal, GLECMA_LIMIT_LITERAL_LENGTH_DEFAULT);
  EXPECT_EQ(glecma_lexer_run(&lexer, nullptr, nullptr), GLECMA_OK);
  // And the default call writes only what the struct declares.
  Guarded<SourceUnitsOnly> d;
  d.header.size = sizeof(SourceUnitsOnly);
  ASSERT_EQ(glecma_limits_default(reinterpret_cast<GLECMA_Limits *>(&d)),
      GLECMA_OK);
  EXPECT_EQ(d.header.source_units, GLECMA_LIMIT_SOURCE_UNITS_DEFAULT);
  EXPECT_TRUE(d.intact());
  glecma_source_release(source);
}

// A limits struct below the header is invalid, and the failure says so.
TEST(ParseOptions, LimitsBelowTheHeaderAreInvalid) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.size = 4;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  GLECMA_Source * source = Make("a");
  GLECMA_Lexer lexer;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, &options, 0, &failure),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(failure.result, static_cast<uint32_t>(GLECMA_ERR_INVALID));
  EXPECT_NE(failure.message, nullptr);
  glecma_source_release(source);
}

TEST(Limits, TheDefaultsAreTheDocumentedOnes) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  std::memset(&limits.tokens, 0, 2 * sizeof(uint64_t));
  ASSERT_EQ(glecma_limits_default(&limits), GLECMA_OK);
  EXPECT_EQ(limits.tokens, UINT64_C(48) * 1024 * 1024);
  EXPECT_EQ(limits.literal_length, UINT64_C(16) * 1024 * 1024);
  EXPECT_EQ(limits.tokens, GLECMA_LIMIT_TOKENS_DEFAULT);
  EXPECT_EQ(limits.literal_length, GLECMA_LIMIT_LITERAL_LENGTH_DEFAULT);
  EXPECT_EQ(offsetof(GLECMA_Limits, tokens), 16u);
  EXPECT_EQ(offsetof(GLECMA_Limits, literal_length), 24u);
}

// A feature set: add, has, and the bounds.
TEST(FeatureSet, AddAndHas) {
  GLECMA_FeatureSet set = GLECMA_FEATURE_SET_INIT;
  EXPECT_EQ(set.size, sizeof(GLECMA_FeatureSet));
  EXPECT_EQ(set.count, 0u);
  EXPECT_FALSE(glecma_feature_set_has(&set, "decorators"));
  EXPECT_FALSE(glecma_feature_set_has(nullptr, "decorators"));
  EXPECT_EQ(glecma_feature_set_add(&set, "decorators"), GLECMA_OK);
  EXPECT_EQ(glecma_feature_set_add(&set, "source-phase-imports"), GLECMA_OK);
  EXPECT_TRUE(glecma_feature_set_has(&set, "decorators"));
  EXPECT_TRUE(glecma_feature_set_has(&set, "source-phase-imports"));
  EXPECT_FALSE(glecma_feature_set_has(&set, "decorator"));
  EXPECT_FALSE(glecma_feature_set_has(&set, "decorators2"));
  EXPECT_FALSE(glecma_feature_set_has(&set, nullptr));
  EXPECT_EQ(set.count, 2u);
  // Adding a name again changes nothing.
  EXPECT_EQ(glecma_feature_set_add(&set, "decorators"), GLECMA_OK);
  EXPECT_EQ(set.count, 2u);
}

TEST(FeatureSet, NamesAreChecked) {
  GLECMA_FeatureSet set = GLECMA_FEATURE_SET_INIT;
  for (const char * bad : {"", "A", "Decorators", "1a", "-a", "a b", "a_b",
           "a.b", "\xC3\xA9"}) {
    EXPECT_EQ(glecma_feature_set_add(&set, bad), GLECMA_ERR_INVALID) << bad;
  }
  EXPECT_EQ(glecma_feature_set_add(&set, nullptr), GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_feature_set_add(nullptr, "a"), GLECMA_ERR_INVALID);
  // The longest name fits; one more does not.
  std::string longest(GLECMA_FEATURE_NAME_MAX - 1, 'a');
  EXPECT_EQ(glecma_feature_set_add(&set, longest.c_str()), GLECMA_OK);
  EXPECT_TRUE(glecma_feature_set_has(&set, longest.c_str()));
  EXPECT_EQ(glecma_feature_set_add(&set, (longest + "a").c_str()),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(set.count, 1u);
}

TEST(FeatureSet, TheSetHasRoomForSixteenNames) {
  GLECMA_FeatureSet set = GLECMA_FEATURE_SET_INIT;
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(glecma_feature_set_add(&set, ("f" + std::to_string(i)).c_str()),
        GLECMA_OK)
        << i;
  }
  EXPECT_EQ(glecma_feature_set_add(&set, "f16"), GLECMA_ERR_LIMIT);
  EXPECT_EQ(set.count, 16u);
  EXPECT_FALSE(glecma_feature_set_has(&set, "f16"));
  EXPECT_TRUE(glecma_feature_set_has(&set, "f15"));
}

// Size-first: a set declared with room for fewer names holds fewer, and the
// library never touches the bytes beyond the size it is told.
TEST(FeatureSet, ASetOfASmallerSizeHoldsFewerNames) {
  struct TwoNames {
    uint32_t size;
    uint32_t count;
    char names[2][GLECMA_FEATURE_NAME_MAX];
  };
  Guarded<TwoNames> g;
  g.header.size = sizeof(TwoNames);
  auto * set = reinterpret_cast<GLECMA_FeatureSet *>(&g);
  EXPECT_EQ(glecma_feature_set_add(set, "one"), GLECMA_OK);
  EXPECT_EQ(glecma_feature_set_add(set, "two"), GLECMA_OK);
  EXPECT_EQ(glecma_feature_set_add(set, "three"), GLECMA_ERR_LIMIT);
  EXPECT_TRUE(glecma_feature_set_has(set, "two"));
  EXPECT_TRUE(g.intact());
  // Only the header: no room for any name.
  Guarded<HeaderOnlyOptions> h;
  h.header.size = sizeof(HeaderOnlyOptions);
  auto * none = reinterpret_cast<GLECMA_FeatureSet *>(&h);
  EXPECT_EQ(glecma_feature_set_add(none, "one"), GLECMA_ERR_LIMIT);
  EXPECT_TRUE(h.intact());
  h.header.size = 4;
  EXPECT_EQ(glecma_feature_set_add(none, "one"), GLECMA_ERR_INVALID);
  EXPECT_FALSE(glecma_feature_set_has(none, "one"));
}

TEST(FeatureSet, ALayoutAHostCanRelyOn) {
  EXPECT_EQ(offsetof(GLECMA_FeatureSet, size), 0u);
  EXPECT_EQ(offsetof(GLECMA_FeatureSet, count), 4u);
  EXPECT_EQ(offsetof(GLECMA_FeatureSet, names), 8u);
  EXPECT_EQ(sizeof(GLECMA_FeatureSet), 8u + 16u * GLECMA_FEATURE_NAME_MAX);
  EXPECT_EQ(offsetof(GLECMA_ParseOptions, size), 0u);
  EXPECT_EQ(offsetof(GLECMA_ParseOptions, limits), 8u);
}

// 5.4: a set naming a feature the build lacks, which is every non-empty set
// in M1, is refused before any token is read, and the feature is named.
TEST(FeatureSet, AnUnbuiltFeatureIsRefusedBeforeAnyTokenIsRead) {
  GLECMA_Source * source = Make("a b c");
  GLECMA_FeatureSet set = GLECMA_FEATURE_SET_INIT;
  ASSERT_EQ(glecma_feature_set_add(&set, "decorators"), GLECMA_OK);
  ASSERT_EQ(glecma_feature_set_add(&set, "using"), GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.features = &set;

  GLECMA_Lexer lexer;
  std::memset(&lexer, 0x5A, sizeof(lexer));
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, &options, 0, &failure),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(failure.result, static_cast<uint32_t>(GLECMA_ERR_UNSUPPORTED));
  EXPECT_STREQ(failure.feature, "decorators");  // the first, in order
  EXPECT_NE(failure.message, nullptr);
  EXPECT_EQ(failure.rule, 0u);
  EXPECT_EQ(failure.limit, nullptr);
  // The lexer was not written, so no token can be read from it.
  EXPECT_EQ(reinterpret_cast<unsigned char *>(&lexer)[0], 0x5A);
  // The internal entry refuses it the same way.
  std::memset(&failure, 0, sizeof(failure));
  failure.size = sizeof(failure);
  EXPECT_EQ(glecma_lexer_open_builtin(&lexer, source, &options, &failure),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_STREQ(failure.feature, "decorators");
  // An empty set, and no set, are accepted.
  GLECMA_FeatureSet empty = GLECMA_FEATURE_SET_INIT;
  options.features = &empty;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, &options, 0, &failure),
      GLECMA_OK);
  EXPECT_EQ(failure.feature[0], '\0');
  options.features = nullptr;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, &options, 0, nullptr),
      GLECMA_OK);
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  glecma_source_release(source);
}

// The failure record carries a feature name as a copy, and a caller whose
// record predates the member is never written past.
TEST(FeatureSet, TheFailureRecordHoldsACopyOfTheName) {
  GLECMA_Source * source = Make("a");
  GLECMA_FeatureSet set = GLECMA_FEATURE_SET_INIT;
  ASSERT_EQ(glecma_feature_set_add(&set, "stage-four"), GLECMA_OK);
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.features = &set;
  GLECMA_Lexer lexer;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0, &failure),
      GLECMA_ERR_UNSUPPORTED);
  std::memset(&set, 0, sizeof(set));  // the caller's set is gone
  EXPECT_STREQ(failure.feature, "stage-four");
  // A longest name is copied whole and NUL-terminated.
  std::string longest(GLECMA_FEATURE_NAME_MAX - 1, 'z');
  GLECMA_FeatureSet big = GLECMA_FEATURE_SET_INIT;
  ASSERT_EQ(glecma_feature_set_add(&big, longest.c_str()), GLECMA_OK);
  options.features = &big;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0, &failure),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(std::string(failure.feature), longest);
  // A record that stops at `limit` takes no rule and no feature.
  Guarded<FailureUpToLimit> g;
  g.header.size = sizeof(FailureUpToLimit);
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0,
                reinterpret_cast<GLECMA_Failure *>(&g)),
      GLECMA_ERR_UNSUPPORTED);
  EXPECT_EQ(g.header.result, static_cast<uint32_t>(GLECMA_ERR_UNSUPPORTED));
  EXPECT_TRUE(g.intact());
  glecma_source_release(source);
}

// The tokens cap: N tokens pass, the N+1st is a limit naming the field, with
// the failure at that token.
TEST(Limits, TokensOnePastTheCap) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.tokens = 4;
  Lexed ok = Lex("a b c d", 0, &limits);
  EXPECT_EQ(ok.result, GLECMA_OK);
  EXPECT_EQ(ok.tokens.size(), 5u);  // four and the end of input
  Lexed over = Lex("a b c d e", 0, &limits);
  EXPECT_EQ(over.result, GLECMA_ERR_LIMIT);
  EXPECT_EQ(over.failure.result, static_cast<uint32_t>(GLECMA_ERR_LIMIT));
  ASSERT_NE(over.failure.limit, nullptr);
  EXPECT_STREQ(over.failure.limit, "tokens");
  EXPECT_NE(over.failure.message, nullptr);
  EXPECT_EQ(over.failure.location.offset, 8u);
  EXPECT_EQ(over.failure.location.column, 8u);
  EXPECT_EQ(over.failure.rule, 0u);
  // Tokens read before the cap were delivered.
  EXPECT_EQ(over.tokens.size(), 4u);
  // The end of input is not a token, and a comment is not either.
  EXPECT_EQ(Lex("a b c d /* e */", 0, &limits).result, GLECMA_OK);
  EXPECT_EQ(Lex("", 0, &limits).result, GLECMA_OK);
}

TEST(Limits, ACapOfZeroTokensIsACapOfZero) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.tokens = 0;
  EXPECT_EQ(Lex("", 0, &limits).result, GLECMA_OK);
  EXPECT_EQ(Lex(" /* c */ ", 0, &limits).result, GLECMA_OK);
  Lexed over = Lex("a", 0, &limits);
  EXPECT_EQ(over.result, GLECMA_ERR_LIMIT);
  EXPECT_STREQ(over.failure.limit, "tokens");
}

// A lexer that has failed fails the same way on every later call, and a
// restored copy goes on from before it.
TEST(Limits, AFailureIsSticky) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.tokens = 1;
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.limits = &limits;
  GLECMA_Source * source = Make("a b");
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0, nullptr),
      GLECMA_OK);
  GLECMA_Lexer start = lexer;
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_ERR_LIMIT);
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_ERR_LIMIT);
  lexer = start;
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, &t), GLECMA_OK);
  glecma_source_release(source);
}

// The literal length is in code points of the source text, delimiters
// included: a literal of exactly the cap passes and one more does not.
TEST(Limits, LiteralLengthOnePastTheCap) {
  struct Case {
    const char * text;     // A literal of `length` code points.
    uint64_t length;
    const char * context;  // Text after it that is not part of it.
  };
  const Case cases[] = {
      {"abcde", 5, ""},                            // an identifier
      {"a\\u0062c", 8, ""},                        // escapes count as written
      {"#abcd", 5, ""},                            // a private name, `#` too
      {"'abc'", 5, ""},                            // a string, quotes too
      {"\"a\\nb\"", 6, ""},                        // an escape in a string
      {"'\xF0\x9F\x98\x80\xF0\x9F\x98\x80'", 4, ""},  // astral: one each
      {"`abc`", 5, ""},                            // a template
      {"`a${", 4, "b}c`"},                         // a template head
      {"/ab/g", 5, ""},                            // a regular expression
      {"/[/]/gi", 7, ""},
      {"\xCE\xB1\xCE\xB2\xCE\xB3", 3, ""},          // a Unicode identifier
  };
  for (const Case & c : cases) {
    const std::string text = std::string(c.text) + c.context;
    GLECMA_Limits limits = GLECMA_LIMITS_INIT;
    limits.literal_length = c.length;
    Lexed ok = Lex(text, 0, &limits);
    EXPECT_NE(ok.result, GLECMA_ERR_LIMIT) << text;
    limits.literal_length = c.length - 1;
    Lexed over = Lex(text, 0, &limits);
    EXPECT_EQ(over.result, GLECMA_ERR_LIMIT) << text;
    ASSERT_NE(over.failure.limit, nullptr) << text;
    EXPECT_STREQ(over.failure.limit, "literal_length") << text;
    EXPECT_EQ(over.failure.location.offset, 0u) << text;
    EXPECT_EQ(over.failure.rule, 0u) << text;
  }
}

TEST(Limits, LiteralLengthCoversEveryPartOfATemplate) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.literal_length = 4;
  // "`a${" is 4, "}b${" is 4, "}c`" is 3: all within the cap.
  EXPECT_EQ(Lex("`a${x}b${y}c`", 0, &limits).result, GLECMA_OK);
  limits.literal_length = 3;
  EXPECT_EQ(Lex("`a${x}b${y}c`", 0, &limits).result, GLECMA_ERR_LIMIT);
  // A middle part alone over the cap.
  limits.literal_length = 4;
  Lexed over = Lex("`a${x}bbbb${y}c`", 0, &limits);
  EXPECT_EQ(over.result, GLECMA_ERR_LIMIT);
  EXPECT_EQ(over.failure.location.offset, 5u);
}

TEST(Limits, NumbersCommentsAndPunctuatorsHaveNoLiteralLength) {
  GLECMA_Limits limits = GLECMA_LIMITS_INIT;
  limits.literal_length = 1;
  EXPECT_EQ(Lex("123456789 0x123456789 1_000_000 // a long comment\n"
                "/* another long comment */ >>>= ...",
                0, &limits)
                .result,
      GLECMA_OK);
  limits.literal_length = 0;
  EXPECT_EQ(Lex("1 + 2", 0, &limits).result, GLECMA_OK);
  EXPECT_EQ(Lex("a", 0, &limits).result, GLECMA_ERR_LIMIT);
  EXPECT_EQ(Lex("''", 0, &limits).result, GLECMA_ERR_LIMIT);
}

// The tokenizer allocates nothing: a source made with a counting allocator
// sees no further request while it is read to the end, and none is left.
TEST(Limits, TheTokenizerAllocatesNothing) {
  CountingAllocator counting;
  const std::string text = "var a = `x${1 + 'two'}` / /re/g; // end\n";
  GLECMA_Source * source = nullptr;
  ASSERT_EQ(glecma_source_create_utf8(text.data(), text.size(), nullptr,
                counting.get(), nullptr, &source),
      GLECMA_OK);
  size_t calls = counting.calls();
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  EXPECT_EQ(glecma_lexer_run(&lexer, nullptr, nullptr), GLECMA_OK);
  EXPECT_EQ(counting.calls(), calls);
  glecma_source_release(source);
  EXPECT_EQ(counting.live(), 0u);
}

// The internal built-in mode: `%Name` is one token where a
// primary expression is expected, and only through the internal entry.
TEST(Builtin, IntrinsicNameThroughTheInternalEntry) {
  Lexed lexed = Lex("%ArrayPrototype(x)", 0, nullptr, kBuiltin);
  ASSERT_EQ(lexed.result, GLECMA_OK);
  EXPECT_EQ(Sig(lexed), "INTRINSIC_NAME ( NAME )");
  EXPECT_EQ(lexed.tokens[0].text, "%ArrayPrototype");
  EXPECT_EQ(Sig(Lex("x = %A + %B", 0, nullptr, kBuiltin)),
      "NAME = INTRINSIC_NAME + INTRINSIC_NAME");
  EXPECT_EQ(Sig(Lex("f(%A, %B)", 0, nullptr, kBuiltin)),
      "NAME ( INTRINSIC_NAME , INTRINSIC_NAME )");
  EXPECT_EQ(Sig(Lex("return %A", 0, nullptr, kBuiltin)),
      "NAME INTRINSIC_NAME");
  EXPECT_EQ(Sig(Lex("[%A]", 0, nullptr, kBuiltin)), "[ INTRINSIC_NAME ]");
  EXPECT_EQ(Sig(Lex("`${%A}`", 0, nullptr, kBuiltin)),
      "TEMPLATE INTRINSIC_NAME TEMPLATE");
  // Non-ASCII names are IdentifierNames too.
  EXPECT_EQ(Sig(Lex("%\xCE\xB1", 0, nullptr, kBuiltin)), "INTRINSIC_NAME");
  // The name is the value, without the `%`.
  GLECMA_Source * source = Make("%Abc");
  GLECMA_Lexer lexer;
  ASSERT_EQ(glecma_lexer_open_builtin(&lexer, source, nullptr, nullptr),
      GLECMA_OK);
  GLECMA_Token token;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP, &token), GLECMA_OK);
  uint16_t * units = nullptr;
  size_t length = 0;
  ASSERT_EQ(glecma_token_value(source, &token, GLECMA_VALUE_COOKED, nullptr,
                &units, &length),
      GLECMA_OK);
  EXPECT_EQ(length, 3u);
  EXPECT_EQ(units[0], 'A');
  glecma_token_value_free(nullptr, units);
  glecma_source_release(source);
}

TEST(Builtin, OnlyWherePrimaryExpressionIsExpected) {
  // After an operand `%` is the remainder operator, even in the mode.
  EXPECT_EQ(Sig(Lex("a %B", 0, nullptr, kBuiltin)), "NAME % NAME");
  EXPECT_EQ(Sig(Lex("a % b", 0, nullptr, kBuiltin)), "NAME % NAME");
  EXPECT_EQ(Sig(Lex("(a) %B", 0, nullptr, kBuiltin)), "( NAME ) % NAME");
  EXPECT_EQ(Sig(Lex("1 %B", 0, nullptr, kBuiltin)), "NUMBER % NAME");
  // Where an expression begins but no name follows, it is the operator.
  EXPECT_EQ(Sig(Lex("%", 0, nullptr, kBuiltin)), "%");
  EXPECT_EQ(Sig(Lex("% A", 0, nullptr, kBuiltin)), "% NAME");
  EXPECT_EQ(Sig(Lex("%1", 0, nullptr, kBuiltin)), "% NUMBER");
  EXPECT_EQ(Sig(Lex("%=", 0, nullptr, kBuiltin)), "%=");
  EXPECT_EQ(Sig(Lex("%%A", 0, nullptr, kBuiltin)), "% INTRINSIC_NAME");
  // No escape in an intrinsic's name.
  EXPECT_EQ(Sig(Lex("%\\u0041", 0, nullptr, kBuiltin)), "% NAME");
}

// In every public goal `%` is the remainder operator: `%Name(x)` is the
// tokens it always was, and the internal entry alone reads it as an
// intrinsic. (The parser's refusal of these tokens in each goal is
// tested with the parser; this is the tokenizer's half.)
TEST(Builtin, EveryPublicGoalReadsThePercentAsAnOperator) {
  const char * golden = "% NAME ( NAME )";
  for (uint32_t mode : {0u, GLECMA_LEXF_STRICT, GLECMA_LEXF_MODULE,
           GLECMA_LEXF_STRICT | GLECMA_LEXF_MODULE}) {
    Lexed lexed = Lex("%Name(x)", mode);
    EXPECT_EQ(Sig(lexed), golden) << mode;
    EXPECT_EQ(lexed.tokens[0].kind, GLECMA_TOK_PERCENT) << mode;
    EXPECT_EQ(lexed.tokens[1].start.offset, 1u) << mode;
  }
  EXPECT_EQ(Sig(Lex("x = %Name(y)")), "NAME = % NAME ( NAME )");
  EXPECT_EQ(Sig(Lex("%Name(x)", 0, nullptr, kBuiltin)),
      "INTRINSIC_NAME ( NAME )");
  // The same source, the same tokens, every time.
  EXPECT_EQ(Sig(Lex("%Name(x)")), Sig(Lex("%Name(x)")));
}

TEST(Builtin, NoPublicOptionSelectsTheMode) {
  GLECMA_Source * source = Make("%A");
  GLECMA_Lexer lexer;
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr, GLECMA_LEXF_BUILTIN,
                &failure),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr,
                GLECMA_LEXF_BUILTIN | GLECMA_LEXF_STRICT, nullptr),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0x80u, nullptr),
      GLECMA_ERR_INVALID);
  // The parse options have no member that could carry it.
  GLECMA_ParseOptions options = GLECMA_PARSE_OPTIONS_INIT;
  options.reserved = 4;  // nothing reads it as a mode
  ASSERT_EQ(glecma_lexer_open(&lexer, source, &options, 0, nullptr),
      GLECMA_OK);
  EXPECT_EQ(lexer.mode & GLECMA_LEXF_BUILTIN, 0u);
  GLECMA_Token t;
  ASSERT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_REGEXP, &t), GLECMA_OK);
  EXPECT_EQ(t.kind, GLECMA_TOK_PERCENT);
  // The internal entry sets it, and the mode is strict.
  ASSERT_EQ(glecma_lexer_open_builtin(&lexer, source, nullptr, nullptr),
      GLECMA_OK);
  EXPECT_NE(lexer.mode & GLECMA_LEXF_BUILTIN, 0u);
  EXPECT_NE(lexer.mode & GLECMA_LEXF_STRICT, 0u);
  EXPECT_EQ(Lex("017", 0, nullptr, kBuiltin).failure.rule,
      R(GLECMA_RULE_NUMERIC_LEGACY_OCTAL_STRICT));
  glecma_source_release(source);
}

// Arguments.
TEST(Open, BadArgumentsAreInvalid) {
  GLECMA_Source * source = Make("a");
  GLECMA_Lexer lexer;
  EXPECT_EQ(glecma_lexer_open(nullptr, source, nullptr, 0, nullptr),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(glecma_lexer_open(&lexer, nullptr, nullptr, 0, nullptr),
      GLECMA_ERR_INVALID);
  GLECMA_Failure failure = GLECMA_FAILURE_INIT;
  EXPECT_EQ(glecma_lexer_open(&lexer, nullptr, nullptr, 0, &failure),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(failure.result, static_cast<uint32_t>(GLECMA_ERR_INVALID));
  // A failure record below the header is refused and written not at all.
  failure.size = 4;
  failure.result = 77;
  EXPECT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, &failure),
      GLECMA_ERR_INVALID);
  EXPECT_EQ(failure.result, 77u);
  GLECMA_Token t;
  EXPECT_EQ(glecma_lexer_next(nullptr, GLECMA_GOAL_DIV, &t),
      GLECMA_ERR_INVALID);
  ASSERT_EQ(glecma_lexer_open(&lexer, source, nullptr, 0, nullptr), GLECMA_OK);
  EXPECT_EQ(glecma_lexer_next(&lexer, GLECMA_GOAL_DIV, nullptr),
      GLECMA_ERR_INVALID);
  glecma_source_release(source);
}

int main(int argc, char * * argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
