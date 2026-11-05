#pragma once

#include "msakit/core.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Keep the replaceable core independent of zlib and other third-party headers.
struct z_stream_s;

namespace msakit::detail {

// Shared error categories and byte-stream helpers.
inline constexpr int kCliError = 2;
inline constexpr int kInputError = 3;
inline constexpr int kIoError = 4;
inline constexpr int kCoreError = 5;

class Error : public std::runtime_error {
public:
    Error(int code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    int exit_code() const noexcept { return code_; }
private:
    int code_;
};

[[noreturn]] inline void fail_io(const std::string& action, const std::string& path,
                          int error = errno) {
    throw Error(kIoError, action + ": " + path + ": " + std::strerror(error));
}

inline constexpr std::size_t kBufferSize = 64 * 1024;

// Checked arithmetic for the replaceable core.
inline std::size_t checked_add(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw Error(kCoreError, "alignment size addition overflow");
    }
    return left + right;
}

inline std::size_t checked_multiply(std::size_t left, std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw Error(kCoreError, "alignment size multiplication overflow");
    }
    return left * right;
}

// Every global-alignment score lies between -(center + query) and
// min(center, query). Check before converting lengths to signed scores.
inline void check_score_bounds(std::size_t center, std::size_t query) {
    constexpr auto limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if (center > limit || query > limit ||
        static_cast<std::uint64_t>(center) >
            limit - static_cast<std::uint64_t>(query)) {
        throw Error(kCoreError, "sequence lengths exceed signed score range");
    }
}

// Command line and application flow.
struct Options {
    std::string input;
    std::string output;
    std::size_t line_width = 80;
    bool force = false;
    bool quiet = false;
};

using CoreFunction = Alignment (*)(const std::vector<SequenceRecord>&);

// The function parameter permits separate test executables to exercise errors
// from an alternate core without adding production CLI switches or backdoors.
int run_application(int argc, char** argv, CoreFunction core);

// File identity shared by input and output protection.
struct FileIdentity {
    std::uintmax_t device;
    std::uintmax_t inode;
};

// Input records, cleaning statistics and read-operation test seam.
struct CleaningStats {
    std::size_t uppercase_conversions = 0;
    std::size_t unknown_replacements = 0;
};

struct InputData {
    std::vector<SequenceRecord> records;
    CleaningStats stats;
    std::optional<FileIdentity> identity;
};

InputData read_input(const std::string& path);

class InputOperations {
public:
    virtual ~InputOperations() = default;
    virtual std::ptrdiff_t read_fd(int fd, void* data, std::size_t size) noexcept;
    // Linux releases the descriptor even when close reports an error. Test
    // substitutes must preserve that ownership rule rather than leak it.
    virtual int close_fd(int fd) noexcept;
};

InputData read_input_with_operations(const std::string& path,
                                    InputOperations& operations);

// Output operations and publication test seam.
void check_output_path(const Options& options,
                       const std::optional<FileIdentity>& input_identity);
void write_output(const Options& options,
                  const std::vector<SequenceRecord>& input,
                  const Alignment& alignment,
                  const std::optional<FileIdentity>& input_identity);

class FileOperations {
public:
    virtual ~FileOperations() = default;
    virtual std::ptrdiff_t write_fd(int fd, const void* data, std::size_t size) noexcept;
    virtual int close_fd(int fd) noexcept;
    virtual int deflate_step(z_stream_s* stream, int flush) noexcept;
    // Success transfers ownership of the completed output. The caller must
    // relinquish the temporary pathname, even if it has since been reused.
    // The link-based fallback performs its own best-effort temporary cleanup.
    virtual int publish(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        bool replace) noexcept;
};

void write_output_with_operations(
    const Options& options, const std::vector<SequenceRecord>& input,
    const Alignment& alignment,
    const std::optional<FileIdentity>& input_identity,
    FileOperations& operations);

// Algorithm-independent result validation.
void validate_alignment(const std::vector<SequenceRecord>& input,
                        const Alignment& alignment);

}  // namespace msakit::detail
