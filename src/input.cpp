#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <kseq.h>
#include <vector>

namespace msakit::detail {
namespace {

[[noreturn]] void fail_zlib(const std::string& action, const std::string& path,
                           int result, const z_stream& stream) {
    if (result == Z_MEM_ERROR) {
        throw Error(kCoreError, "Memory allocation failed during " + action);
    }
    throw Error(kIoError, action + ": " + path +
                             (stream.msg ? ": " + std::string(stream.msg) : ""));
}

bool ascii_space(unsigned char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\v' || ch == '\f';
}

void increment(std::size_t& value) {
    if (value == std::numeric_limits<std::size_t>::max()) {
        throw Error(kCoreError, "Input statistics overflow");
    }
    ++value;
}

// Adapted from HAlign-5 include/utils.h at
// b0d5608811d44e30bc323da54f0583ceaaf684d2.
// Copyright (c) 2026 HAlign-5 contributors. MIT license; see LICENSE and
// the repository's third-party notices. Gaps are rejected before this table.
constexpr auto make_clean_table() {
    std::array<unsigned char, 256> table{};
    for (auto& value : table) value = 'N';
    constexpr std::string_view upper = "ACGTUN";
    constexpr std::string_view lower = "acgtun";
    for (std::size_t i = 0; i < upper.size(); ++i) {
        table[static_cast<unsigned char>(upper[i])] = upper[i];
        table[static_cast<unsigned char>(lower[i])] = upper[i];
    }
    return table;
}
constexpr auto kClean = make_clean_table();

class InputFd {
public:
    InputFd(const std::string& path, InputOperations& operations)
        : path_(path), owned_(path != "-"), operations_(operations) {
        fd_ = owned_ ? ::open(path.c_str(), O_RDONLY | O_CLOEXEC) : STDIN_FILENO;
        if (fd_ < 0) fail_io("Cannot open input", path_);
    }
    ~InputFd() { if (owned_ && fd_ >= 0) operations_.close_fd(fd_); }
    InputFd(const InputFd&) = delete;
    InputFd& operator=(const InputFd&) = delete;

    std::size_t read(unsigned char* data, std::size_t size) {
        std::ptrdiff_t count;
        do { count = operations_.read_fd(fd_, data, size); } while (count < 0 && errno == EINTR);
        if (count < 0) fail_io("Cannot read input", path_);
        return static_cast<std::size_t>(count);
    }
    std::optional<FileIdentity> identity() const {
        if (!owned_) return std::nullopt;
        struct stat st {};
        if (::fstat(fd_, &st) != 0) fail_io("Cannot inspect input", path_);
        return FileIdentity{static_cast<std::uintmax_t>(st.st_dev),
                            static_cast<std::uintmax_t>(st.st_ino)};
    }
    void finish() {
        if (owned_ && fd_ >= 0) {
            const int old = std::exchange(fd_, -1);
            // Linux releases the descriptor even when close reports an error.
            // Retrying could close an unrelated descriptor that reused its number.
            if (operations_.close_fd(old) != 0) fail_io("Cannot close input", path_);
        }
    }
private:
    std::string path_;
    bool owned_;
    InputOperations& operations_;
    int fd_ = -1;
};

class DecodedInput {
public:
    DecodedInput(const std::string& path, InputOperations& operations)
        : file_(path, operations), path_(path) {
        identity_ = file_.identity();
        // Pipes may return only one byte, so accumulate the complete magic prefix.
        while (prefix_size_ < 2) {
            const auto n = file_.read(encoded_.data() + prefix_size_, 2 - prefix_size_);
            if (n == 0) break;
            prefix_size_ += n;
        }
        gzip_ = prefix_size_ == 2 && encoded_[0] == 0x1f && encoded_[1] == 0x8b;
        if (gzip_) {
            const int result = ::inflateInit2(&stream_, 16 + MAX_WBITS);
            if (result != Z_OK) fail_zlib("Cannot initialize gzip input", path_, result, stream_);
            initialized_ = true;
            stream_.next_in = encoded_.data();
            stream_.avail_in = static_cast<uInt>(prefix_size_);
        }
    }
    ~DecodedInput() { if (initialized_) ::inflateEnd(&stream_); }
    DecodedInput(const DecodedInput&) = delete;
    DecodedInput& operator=(const DecodedInput&) = delete;

    const std::optional<FileIdentity>& identity() const { return identity_; }

    // kseq receives the original decoded bytes, with no line preprocessing.
    std::size_t read(void* destination, std::size_t capacity) {
        if (capacity == 0) return 0;
        if (decoded_pos_ == decoded_size_) {
            decoded_size_ = fill();
            decoded_pos_ = 0;
        }
        const auto count = std::min(capacity, decoded_size_ - decoded_pos_);
        std::memcpy(destination, decoded_.data() + decoded_pos_, count);
        decoded_pos_ += count;
        return count;
    }

    void finish() {
        if (initialized_) {
            const int result = ::inflateEnd(&stream_);
            initialized_ = false;
            if (result != Z_OK) fail_zlib("Cannot finalize gzip input", path_, result, stream_);
        }
        file_.finish();
    }
private:
    bool refill_compressed() {
        const auto size = file_.read(encoded_.data(), encoded_.size());
        stream_.next_in = encoded_.data();
        stream_.avail_in = static_cast<uInt>(size);
        return size != 0;
    }

    std::size_t fill() {
        if (!gzip_) {
            if (prefix_size_ != 0) {
                const auto n = std::exchange(prefix_size_, 0);
                std::copy_n(encoded_.begin(), n, decoded_.begin());
                return n;
            }
            return file_.read(decoded_.data(), decoded_.size());
        }
        if (finished_) return 0;
        stream_.next_out = decoded_.data();
        stream_.avail_out = static_cast<uInt>(decoded_.size());
        while (true) {
            if (member_done_) {
                if (stream_.avail_in == 0 && !refill_compressed()) {
                    finished_ = true;
                    return 0;
                }
                // gzip permits concatenated members. Reset without losing bytes
                // already read beyond the previous trailer; garbage fails parsing.
                auto* next = stream_.next_in;
                const auto available = stream_.avail_in;
                const int reset = ::inflateReset2(&stream_, 16 + MAX_WBITS);
                if (reset != Z_OK) fail_zlib("Cannot reset gzip input", path_, reset, stream_);
                stream_.next_in = next;
                stream_.avail_in = available;
                stream_.next_out = decoded_.data();
                stream_.avail_out = static_cast<uInt>(decoded_.size());
                member_done_ = false;
            }
            if (stream_.avail_in == 0 && !refill_compressed()) {
                throw Error(kIoError, "Truncated gzip input: " + path_);
            }
            const int result = ::inflate(&stream_, Z_NO_FLUSH);
            const auto produced = decoded_.size() - stream_.avail_out;
            if (result == Z_STREAM_END) member_done_ = true;
            else if (result != Z_OK && !(result == Z_BUF_ERROR && stream_.avail_in == 0)) {
                fail_zlib("Invalid gzip input", path_, result, stream_);
            }
            if (produced != 0) return produced;
        }
    }

    InputFd file_;
    std::string path_;
    std::optional<FileIdentity> identity_;
    std::array<unsigned char, kBufferSize> encoded_{};
    std::array<unsigned char, kBufferSize> decoded_{};
    std::size_t prefix_size_ = 0;
    std::size_t decoded_size_ = 0;
    std::size_t decoded_pos_ = 0;
    z_stream stream_{};
    bool gzip_ = false;
    bool initialized_ = false;
    bool member_done_ = false;
    bool finished_ = false;
};

// Never unwind through kseq's C allocation/initialization code. Retain the
// original typed exception so I/O, gzip and allocation failures keep their codes.
struct KseqSource {
    DecodedInput& input;
    std::exception_ptr failure;
};

int read_kseq_bytes(KseqSource* source, void* data, int size) noexcept {
    if (source->failure) return -1;
    try {
        return static_cast<int>(source->input.read(data, static_cast<std::size_t>(size)));
    } catch (...) {
        source->failure = std::current_exception();
        return -1;
    }
}

KSEQ_INIT(KseqSource*, read_kseq_bytes)

}  // namespace

std::ptrdiff_t InputOperations::read_fd(int fd, void* data, std::size_t size) noexcept {
    return ::read(fd, data, size);
}

int InputOperations::close_fd(int fd) noexcept { return ::close(fd); }

InputData read_input_with_operations(const std::string& path,
                                    InputOperations& operations) {
    DecodedInput reader(path, operations);
    KseqSource source{reader, {}};
    std::unique_ptr<kseq_t, decltype(&kseq_destroy)> parser(kseq_init(&source), kseq_destroy);
    if (!parser) throw Error(kCoreError, "Cannot allocate kseq reader: " + path);
    InputData result;
    result.identity = reader.identity();
    const auto fail = [&](const std::string& reason) {
        throw Error(kInputError, path + ": record " +
                    std::to_string(result.records.size() + 1) + ": " + reason);
    };
    while (true) {
        const int status = kseq_read(parser.get());
        if (source.failure) std::rethrow_exception(source.failure);
        if (status == -1) break;
        if (status == -4) throw Error(kCoreError, "kseq allocation or length limit failure: " + path);
        if (status == -3) throw Error(kIoError, "Cannot read sequence input: " + path);
        if (status < 0) fail("FASTQ quality length mismatch");

        SequenceRecord current;
        // Same name/comment reconstruction as HAlign-5's KseqReader.
        if (parser->name.l) current.header.assign(parser->name.s, parser->name.l);
        if (parser->comment.l) {
            if (!current.header.empty()) current.header.push_back(' ');
            current.header.append(parser->comment.s, parser->comment.l);
        }
        if (std::none_of(current.header.begin(), current.header.end(),
                         [](unsigned char ch) { return !ascii_space(ch); })) {
            fail("empty or whitespace-only header");
        }
        if (parser->seq.l == 0) fail("empty sequence");
        current.sequence.reserve(parser->seq.l);
        for (std::size_t i = 0; i < parser->seq.l; ++i) {
            const auto ch = static_cast<unsigned char>(parser->seq.s[i]);
            if (ch == '-' || ch == '.') fail("existing gap; supply unaligned sequences");
            if (std::string_view("acgtun").find(static_cast<char>(ch)) != std::string_view::npos) {
                increment(result.stats.uppercase_conversions);
            } else if (std::string_view("ACGTUN").find(static_cast<char>(ch)) == std::string_view::npos) {
                increment(result.stats.unknown_replacements);
            }
            current.sequence.push_back(static_cast<char>(kClean[ch]));
        }
        result.records.push_back(std::move(current));
    }
    if (result.records.empty()) throw Error(kInputError, path + ": no sequence records");
    reader.finish();
    return result;
}

InputData read_input(const std::string& path) {
    InputOperations operations;
    return read_input_with_operations(path, operations);
}

}  // namespace msakit::detail
