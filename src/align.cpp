#include "internal.hpp"
#include "msakit/core.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <string_view>
#include <utility>
#include <vector>

namespace msakit {
namespace {
using Score = std::int64_t;

bool is_residue(char value) {
    return value == 'A' || value == 'C' || value == 'G' || value == 'T' ||
           value == 'U' || value == 'N';
}

Score substitution(char center, char query) {
    if (center == 'N' || query == 'N') return 0;
    if (center == query || (center == 'T' && query == 'U') ||
        (center == 'U' && query == 'T')) return 1;
    return -1;
}

struct Projection {
    std::string at_center;
    std::vector<std::string> insertions;
};

Projection align_pair(std::string_view center, std::string_view query) {
    detail::check_score_bounds(center.size(), query.size());
    const auto height = detail::checked_add(center.size(), 1);
    const auto width = detail::checked_add(query.size(), 1);
    const auto cells = detail::checked_multiply(height, width);
    (void)detail::checked_multiply(cells, sizeof(Score));
    if (cells > std::vector<Score>().max_size()) {
        throw detail::Error(detail::kCoreError, "pairwise matrix exceeds container size");
    }

    // Deliberately use the full, easy-to-read DP table in this reference core.
    // It is local to this pair and is released before processing the next query.
    std::vector<Score> scores(cells);
    const auto cell = [&](std::size_t i, std::size_t j) -> Score& {
        return scores[i * width + j];
    };
    for (std::size_t i = 1; i < height; ++i) cell(i, 0) = -static_cast<Score>(i);
    for (std::size_t j = 1; j < width; ++j) cell(0, j) = -static_cast<Score>(j);
    for (std::size_t i = 1; i < height; ++i) {
        for (std::size_t j = 1; j < width; ++j) {
            cell(i, j) = std::max({
                cell(i - 1, j - 1) + substitution(center[i - 1], query[j - 1]),
                cell(i - 1, j) - 1,
                cell(i, j - 1) - 1});
        }
    }

    std::string aligned_center;
    std::string aligned_query;
    const auto longest_trace = detail::checked_add(center.size(), query.size());
    if (longest_trace > aligned_center.max_size()) {
        throw detail::Error(detail::kCoreError, "pairwise traceback exceeds string size");
    }
    aligned_center.reserve(longest_trace);
    aligned_query.reserve(longest_trace);
    std::size_t i = center.size();
    std::size_t j = query.size();
    while (i != 0 || j != 0) {
        // The order of these branches defines the documented tie rule:
        // consume both, consume center only, then consume query only.
        if (i != 0 && j != 0 && cell(i, j) ==
                cell(i - 1, j - 1) + substitution(center[i - 1], query[j - 1])) {
            aligned_center.push_back(center[--i]);
            aligned_query.push_back(query[--j]);
        } else if (i != 0 && cell(i, j) == cell(i - 1, j) - 1) {
            aligned_center.push_back(center[--i]);
            aligned_query.push_back('-');
        } else if (j != 0 && cell(i, j) == cell(i, j - 1) - 1) {
            aligned_center.push_back('-');
            aligned_query.push_back(query[--j]);
        } else {
            throw detail::Error(detail::kCoreError, "invalid pairwise traceback state");
        }
    }
    std::reverse(aligned_center.begin(), aligned_center.end());
    std::reverse(aligned_query.begin(), aligned_query.end());

    Projection projection;
    projection.at_center.assign(center.size(), '-');
    projection.insertions.resize(height);
    std::size_t consumed = 0;
    for (std::size_t column = 0; column < aligned_center.size(); ++column) {
        if (aligned_center[column] == '-') {
            // Slot s follows exactly s consumed center residues. This also
            // accounts for prefix (0) and suffix (center length) insertions.
            projection.insertions[consumed].push_back(aligned_query[column]);
        } else {
            projection.at_center[consumed++] = aligned_query[column];
        }
    }
    return projection;
}
}  // namespace

// Reference star alignment — replaceable core
// Replace this implementation to use another algorithm. Keep input unchanged,
// preserve row-to-input correspondence, and return only aligned strings.

// Experimental adaptive-star center selection.
// Candidate medoid minimizes average distance to other sequences, estimated by
// unique k-mer Jaccard distance plus a small length-difference penalty.
// Sampling prevents quadratic work for large numbers of input sequences.
// For tiny teaching examples keep original first-row behavior.
using KmerSet = std::unordered_set<std::uint64_t>;

KmerSet kmers(std::string_view seq, unsigned k) {
    KmerSet out;
    if (seq.size() < k) return out;
    std::uint64_t word = 0;
    unsigned valid = 0;
    const std::uint64_t mask = (std::uint64_t(1) << (2 * k)) - 1;
    for (char c : seq) {
        if (c == 'U') c = 'T';
        unsigned v = 0;
        if (c == 'A') v = 0;
        else if (c == 'C') v = 1;
        else if (c == 'G') v = 2;
        else if (c == 'T') v = 3;
        else { valid = 0; word = 0; continue; } // skip N-containing windows
        word = ((word << 2) | v) & mask;
        if (++valid >= k) out.insert(word);
    }
    return out;
}

double pair_distance(const KmerSet& a, const KmerSet& b,
                     std::size_t la, std::size_t lb) {
    // Jaccard similarity on sets (NOT alignment identity).
    const auto& small = a.size() <= b.size() ? a : b;
    const auto& large = a.size() <= b.size() ? b : a;
    std::size_t intersection = 0;
    for (auto k : small) if (large.count(k)) ++intersection;
    const std::size_t un = a.size() + b.size() - intersection;
    const double j = un ? double(intersection) / double(un) : 0.0;
    const double len = double(std::max(la, lb) - std::min(la, lb)) /
                       double(std::max(la, lb));
    return (1.0 - j) + 0.2 * len;
}

std::size_t adaptive_kmer_center(const std::vector<SequenceRecord>& input) {
    if (input.size() <= 2) return 0;
    std::size_t max_length = 0;
    for (const auto& rec : input) max_length = std::max(max_length, rec.sequence.size());
    if (max_length < 8) return 0; // preserve teaching-example layouts
    const unsigned k = max_length < 20 ? 3u : 5u;
    const std::size_t n = input.size();
    // Bounded deterministic subset: up to 48 candidates, 96 reference rows.
    // Sampling is evenly spaced in input order; no randomness or external deps.
    auto sample = [n](std::size_t limit) {
        std::vector<std::size_t> ids;
        const std::size_t m = std::min(n, limit);
        ids.reserve(m);
        for (std::size_t i = 0; i < m; ++i)
            ids.push_back(m == 1 ? 0 : i * (n - 1) / (m - 1));
        return ids;
    };
    const auto candidates = sample(48);
    const auto references = sample(96);
    // Cache only selected k-mer sets to bound the prepass memory.
    std::vector<std::size_t> indices = references;
    for (auto i : candidates)
        if (std::find(indices.begin(), indices.end(), i) == indices.end())
            indices.push_back(i);
    std::vector<KmerSet> sets;
    sets.reserve(indices.size());
    for (auto i : indices) sets.push_back(kmers(input[i].sequence, k));
    auto lookup = [&](std::size_t idx) -> const KmerSet& {
        auto it = std::find(indices.begin(), indices.end(), idx);
        return sets[static_cast<std::size_t>(it - indices.begin())];
    };
    std::size_t best = 0;
    double best_score = std::numeric_limits<double>::infinity();
    for (auto c : candidates) {
        double sum = 0.0;
        for (auto r : references) {
            if (c == r) continue;
            sum += pair_distance(lookup(c), lookup(r),
                                 input[c].sequence.size(), input[r].sequence.size());
        }
        if (sum < best_score) { best_score = sum; best = c; }
    }
    return best;
}

Alignment align(const std::vector<SequenceRecord>& input) {
    if (input.empty()) {
        throw detail::Error(detail::kCoreError, "the alignment core requires at least one record");
    }
    for (const auto& record : input) {
        if (record.sequence.empty() ||
            !std::all_of(record.sequence.begin(), record.sequence.end(), is_residue)) {
            throw detail::Error(detail::kCoreError,
                                "the alignment core requires nonempty cleaned ACGTUN sequences");
        }
    }
    if (input.size() == 1) return Alignment{{input.front().sequence}};

    const std::size_t center_index = adaptive_kmer_center(input);
    const auto& center = input[center_index].sequence;
    const auto slot_count = detail::checked_add(center.size(), 1);
    std::vector<std::size_t> slot_widths(slot_count, 0);
    // Index by ORIGINAL input row so output order never changes.
    std::vector<Projection> projections(input.size());
    for (std::size_t row = 0; row < input.size(); ++row) {
        if (row == center_index) continue;
        auto projection = align_pair(center, input[row].sequence);
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            slot_widths[slot] = std::max(slot_widths[slot], projection.insertions[slot].size());
        }
        projections[row] = std::move(projection);
    }

    auto output_width = center.size();
    for (const auto count : slot_widths) output_width = detail::checked_add(output_width, count);
    (void)detail::checked_multiply(input.size(), output_width);
    if (output_width > std::string().max_size()) {
        throw detail::Error(detail::kCoreError, "alignment row exceeds string size");
    }

    Alignment result;
    result.rows.resize(input.size());
    for (std::size_t row = 0; row < input.size(); ++row) {
        auto& output = result.rows[row];
        output.reserve(output_width);
        const Projection* projection = row == center_index ? nullptr : &projections[row];
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            std::size_t used = 0;
            if (projection != nullptr) {
                const auto& insertion = projection->insertions[slot];
                output.append(insertion);
                used = insertion.size();
            }
            // Each insertion string is left-aligned within its slot. A longer
            // query determines the width; shorter queries receive right gaps.
            output.append(slot_widths[slot] - used, '-');
            if (slot != center.size()) {
                output.push_back(projection == nullptr ? center[slot] : projection->at_center[slot]);
            }
        }
    }
    return result;
}
}  // namespace msakit
