#include "internal.hpp"
// Separate test executable: none of these fault modes exist in production CLI.

#include <algorithm>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string mode;

msakit::Alignment fake_core(const std::vector<msakit::SequenceRecord>& input) {
    if (mode == "throw") throw std::runtime_error("test core exception");
    if (mode == "badalloc") throw std::bad_alloc{};
    if (mode == "typed-error") throw msakit::detail::Error(4, "test core I/O-shaped exception");
    if (mode == "unknown-exception") throw 42;

    msakit::Alignment result;
    std::size_t width = 0;
    for (const auto& record : input) width = std::max(width, record.sequence.size());
    for (const auto& record : input) {
        result.rows.push_back(record.sequence + std::string(width - record.sequence.size(), '-'));
    }
    if (mode == "missing") result.rows.pop_back();
    else if (mode == "extra") result.rows.push_back(result.rows.front());
    else if (mode == "reorder") std::reverse(result.rows.begin(), result.rows.end());
    else if (mode == "unequal") result.rows.front().push_back('-');
    else if (mode == "changed") result.rows.front()[0] = result.rows.front()[0] == 'A' ? 'C' : 'A';
    else if (mode == "invalid") result.rows.front()[0] = '?';
    else if (mode == "all-gap") {
        for (auto& row : result.rows) row.insert(row.begin(), '-');
    } else if (mode == "empty-width") {
        for (auto& row : result.rows) row.clear();
    } else if (mode != "valid") {
        throw std::runtime_error("unknown test core mode");
    }
    return result;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: msakit_test_driver MODE [msakit arguments]\n", stderr);
        return 2;
    }
    mode = argv[1];
    return msakit::detail::run_application(argc - 1, argv + 1, fake_core);
}
