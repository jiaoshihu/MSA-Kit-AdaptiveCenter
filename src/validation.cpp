#include "internal.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace msakit::detail {

void validate_alignment(const std::vector<SequenceRecord>& input,
                        const Alignment& alignment) {
    if (input.empty() || alignment.rows.size() != input.size()) {
        throw Error(kCoreError, "alignment row count does not match nonempty input");
    }
    const auto width = alignment.rows.front().size();
    if (width == 0) throw Error(kCoreError, "alignment rows must not be empty");

    for (std::size_t row = 0; row < input.size(); ++row) {
        const auto& aligned = alignment.rows[row];
        if (aligned.size() != width) {
            throw Error(kCoreError, "alignment rows have different lengths at row " +
                        std::to_string(row + 1));
        }
        std::size_t consumed = 0;
        for (const char residue : aligned) {
            if (residue == '-') continue;
            if (residue != 'A' && residue != 'C' && residue != 'G' &&
                residue != 'T' && residue != 'U' && residue != 'N') {
                throw Error(kCoreError, "invalid alignment character at row " +
                            std::to_string(row + 1));
            }
            if (consumed == input[row].sequence.size() ||
                residue != input[row].sequence[consumed]) {
                throw Error(kCoreError, "alignment changes cleaned input residues at row " +
                            std::to_string(row + 1));
            }
            ++consumed;
        }
        if (consumed != input[row].sequence.size() || consumed == 0) {
            throw Error(kCoreError, "alignment loses cleaned input residues at row " +
                        std::to_string(row + 1));
        }
    }

    for (std::size_t column = 0; column < width; ++column) {
        bool contains_residue = false;
        for (const auto& row : alignment.rows) {
            if (row[column] != '-') {
                contains_residue = true;
                break;
            }
        }
        if (!contains_residue) {
            throw Error(kCoreError, "alignment contains an all-gap column at column " +
                        std::to_string(column + 1));
        }
    }
}

}  // namespace msakit::detail
