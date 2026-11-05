#include "internal.hpp"
#include <doctest/doctest.h>

#include "msakit/core.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {
using msakit::Alignment;
using msakit::SequenceRecord;

std::vector<SequenceRecord> records(std::initializer_list<std::string> sequences) {
    std::vector<SequenceRecord> result;
    for (const auto& sequence : sequences) {
        result.push_back({"record " + std::to_string(result.size() + 1), sequence});
    }
    return result;
}

int residue_score(char a, char b) {
    if (a == 'N' || b == 'N') return 0;
    if (a == 'U') a = 'T';
    if (b == 'U') b = 'T';
    return a == b ? 1 : -1;
}

int score_rows(const std::string& a, const std::string& b) {
    REQUIRE(a.size() == b.size());
    int score = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE_FALSE((a[i] == '-' && b[i] == '-'));
        score += (a[i] == '-' || b[i] == '-') ? -1 : residue_score(a[i], b[i]);
    }
    return score;
}

// Exhaustively visit complete monotone alignment paths. This independent
// oracle has no DP table or memoization and is intentionally limited to tiny
// sequences; it checks optimal scores without requiring one gap layout.
void enumerate_scores(std::string_view a, std::string_view b,
                      std::size_t i, std::size_t j, int score, int& best) {
    if (i == a.size() && j == b.size()) {
        best = std::max(best, score);
        return;
    }
    if (i != a.size()) enumerate_scores(a, b, i + 1, j, score - 1, best);
    if (j != b.size()) enumerate_scores(a, b, i, j + 1, score - 1, best);
    if (i != a.size() && j != b.size()) {
        enumerate_scores(a, b, i + 1, j + 1, score + residue_score(a[i], b[j]), best);
    }
}

std::vector<std::string> short_words(std::string_view alphabet, std::size_t max_length) {
    std::vector<std::string> result;
    std::vector<std::string> layer{std::string{}};
    for (std::size_t length = 1; length <= max_length; ++length) {
        std::vector<std::string> next;
        for (const auto& prefix : layer) {
            for (const char letter : alphabet) next.push_back(prefix + letter);
        }
        result.insert(result.end(), next.begin(), next.end());
        layer = std::move(next);
    }
    return result;
}

void require_core_error(const std::vector<SequenceRecord>& input,
                        const Alignment& output) {
    try {
        msakit::detail::validate_alignment(input, output);
        FAIL("invalid alignment was accepted");
    } catch (const msakit::detail::Error& error) {
        CHECK(error.exit_code() == msakit::detail::kCoreError);
    }
}
}  // namespace

TEST_CASE("reference core preserves one row and repeated input records" * doctest::test_suite("reference")) {
    auto input = records({"ACGTUN"});
    const auto single = msakit::align(input);
    CHECK(single.rows == std::vector<std::string>{"ACGTUN"});
    msakit::detail::validate_alignment(input, single);

    input = {{"duplicate full header", "ACG"}, {"duplicate full header", "ACG"}};
    const auto output = msakit::align(input);
    CHECK(output.rows == std::vector<std::string>{"ACG", "ACG"});
    CHECK(input[0].header == "duplicate full header");
    CHECK(input[1].header == "duplicate full header");
    msakit::detail::validate_alignment(input, output);
}

TEST_CASE("reference star merges a shared slot with right padding" * doctest::test_suite("reference")) {
    const auto input = records({"AC", "ATC", "AGGC"});
    const auto output = msakit::align(input);
    CHECK(output.rows == std::vector<std::string>{"A--C", "AT-C", "AGGC"});
    msakit::detail::validate_alignment(input, output);
    CHECK(msakit::align(input).rows == output.rows);
}

TEST_CASE("prefix suffix insertion and center deletion preserve every residue" * doctest::test_suite("reference")) {
    for (const auto& query : {"TACG", "ACGT", "ATCG", "AG", "CG", "AC"}) {
        const auto input = records({"ACG", query});
        INFO("query = ", query);
        msakit::detail::validate_alignment(input, msakit::align(input));
    }
    CHECK(msakit::align(records({"ACG", "TACG"})).rows ==
          std::vector<std::string>{"-ACG", "TACG"});
    CHECK(msakit::align(records({"ACG", "ACGT"})).rows ==
          std::vector<std::string>{"ACG-", "ACGT"});
    CHECK(msakit::align(records({"ACG", "AG"})).rows ==
          std::vector<std::string>{"ACG", "A-G"});
}

TEST_CASE("N scores zero and T U compare equal without output conversion" * doctest::test_suite("reference")) {
    const auto tu = msakit::align(records({"T", "U"}));
    CHECK(tu.rows == std::vector<std::string>{"T", "U"});
    CHECK(score_rows(tu.rows[0], tu.rows[1]) == 1);
    for (const std::string base : {"A", "C", "G", "T", "U", "N"}) {
        const auto out = msakit::align(records({"N", base}));
        CHECK(out.rows == std::vector<std::string>{"N", base});
        CHECK(score_rows(out.rows[0], out.rows[1]) == 0);
    }
}

TEST_CASE("diagonal first traceback gives the specified tied alignment" * doctest::test_suite("reference")) {
    CHECK(msakit::align(records({"A", "AA"})).rows ==
          std::vector<std::string>{"-A", "AA"});
    CHECK(msakit::align(records({"AA", "A"})).rows ==
          std::vector<std::string>{"AA", "-A"});
}

TEST_CASE("short pairwise reference scores match exhaustive path enumeration" * doctest::test_suite("reference")) {
    auto words = short_words("AC", 3);
    const auto special = short_words("ATUN", 2);
    words.insert(words.end(), special.begin(), special.end());
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    for (const auto& a : words) {
        for (const auto& b : words) {
            INFO("center = ", a, ", query = ", b);
            int best = std::numeric_limits<int>::min();
            enumerate_scores(a, b, 0, 0, 0, best);
            const auto input = records({a, b});
            const auto output = msakit::align(input);
            msakit::detail::validate_alignment(input, output);
            CHECK(score_rows(output.rows[0], output.rows[1]) == best);
        }
    }
}

TEST_CASE("small generated multiple alignments preserve input and repeat exactly" * doctest::test_suite("reference")) {
    std::mt19937 generator(0x4d5341U);
    constexpr std::string_view alphabet = "ACGTUN";
    for (unsigned run = 0; run < 40; ++run) {
        std::vector<SequenceRecord> input;
        const auto count = 2U + generator() % 5U;
        for (unsigned row = 0; row < count; ++row) {
            std::string sequence;
            const auto length = 1U + generator() % 12U;
            for (unsigned j = 0; j < length; ++j) {
                sequence.push_back(alphabet[generator() % alphabet.size()]);
            }
            input.push_back({"same header", std::move(sequence)});
        }
        const auto before = input;
        const auto output = msakit::align(input);
        msakit::detail::validate_alignment(input, output);
        CHECK(msakit::align(input).rows == output.rows);
        for (std::size_t i = 0; i < input.size(); ++i) {
            CHECK(input[i].header == before[i].header);
            CHECK(input[i].sequence == before[i].sequence);
        }
    }
}

TEST_CASE("direct core calls reject empty or unprepared input" * doctest::test_suite("reference")) {
    CHECK_THROWS_AS(msakit::align(std::vector<SequenceRecord>{}), msakit::detail::Error);
    for (const std::string sequence : {"", "a", "A-", "A.", "R", "A "}) {
        CHECK_THROWS_AS(msakit::align(records({sequence})), msakit::detail::Error);
    }
}

TEST_CASE("framework validation rejects malformed output independent of the core") {
    const auto input = records({"AC", "AG"});
    require_core_error(input, Alignment{{"AC"}});
    require_core_error(input, Alignment{{"AC", "AG", "AC"}});
    require_core_error(input, Alignment{{"AC", "A-G"}});
    require_core_error(input, Alignment{{"AG", "AC"}});
    require_core_error(input, Alignment{{"AT", "AG"}});
    require_core_error(input, Alignment{{"AC", "A?"}});
    require_core_error(input, Alignment{{"A-", "AG"}});
    require_core_error(input, Alignment{{"ACC", "AG-"}});
    require_core_error(input, Alignment{{"-AC", "-AG"}});
    require_core_error(input, Alignment{{"", ""}});
    require_core_error({}, Alignment{});
    // Different valid gap placement remains legal for replacement algorithms.
    CHECK_NOTHROW(msakit::detail::validate_alignment(input, Alignment{{"A-C", "AG-"}}));
}

TEST_CASE("checked size and score arithmetic reject overflow without large allocation") {
    using namespace msakit::detail;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    CHECK(checked_add(10, 20) == 30);
    CHECK(checked_add(maximum, 0) == maximum);
    CHECK_THROWS_AS(checked_add(maximum, 1), Error);
    CHECK(checked_multiply(0, maximum) == 0);
    CHECK(checked_multiply(6, 7) == 42);
    CHECK_THROWS_AS(checked_multiply(maximum, 2), Error);
    CHECK_NOTHROW(check_score_bounds(12, 20));
    if constexpr (sizeof(std::size_t) >= sizeof(std::int64_t)) {
        const auto score_max = static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
        CHECK_NOTHROW(check_score_bounds(score_max, 0));
        CHECK_THROWS_AS(check_score_bounds(score_max, 1), Error);
        CHECK_THROWS_AS(check_score_bounds(maximum, 0), Error);
    }
}
