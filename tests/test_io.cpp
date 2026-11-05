#include <doctest/doctest.h>

#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

namespace {
using namespace msakit;
using namespace msakit::detail;
namespace fs = std::filesystem;

class TempDirectory {
public:
    TempDirectory() {
        std::string pattern = (fs::temp_directory_path() / "msakit-io-test-XXXXXX").string();
        const char* created = ::mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

void put(const fs::path& file, const std::string& content) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close();
    REQUIRE(stream.good());
}

std::string get(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string gzip(const std::string& content) {
    z_stream stream{};
    REQUIRE(::deflateInit2(&stream, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(content.data()));
    stream.avail_in = static_cast<uInt>(content.size());
    std::array<unsigned char, 4096> buffer{};
    std::string result;
    int status;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = ::deflate(&stream, Z_FINISH);
        REQUIRE((status == Z_OK || status == Z_STREAM_END));
        result.append(reinterpret_cast<const char*>(buffer.data()), buffer.size() - stream.avail_out);
    } while (status != Z_STREAM_END);
    REQUIRE(::deflateEnd(&stream) == Z_OK);
    return result;
}

void error_code(const std::function<void()>& action, int expected) {
    try {
        action();
        FAIL("Expected a typed MSA-Kit error");
    } catch (const Error& error) {
        INFO(error.what());
        CHECK(error.exit_code() == expected);
    }
}

void no_temporary_files(const TempDirectory& temporary) {
    for (const auto& file : fs::directory_iterator(temporary.path)) {
        CHECK_FALSE(file.path().filename().string().compare(0, 8, ".msakit-") == 0);
    }
}

enum class Fault { Write, Close, Finalize, CompressionMemory, Publish, Collision, ShortWrites, ReusedTemporaryName };

class InjectedOperations : public FileOperations {
public:
    explicit InjectedOperations(Fault fault) : fault_(fault) {}
    int close_calls = 0;
    int publish_calls = 0;
    int write_calls = 0;
    bool marker_created = false;

    std::ptrdiff_t write_fd(int fd, const void* data, std::size_t size) noexcept override {
        ++write_calls;
        if (fault_ == Fault::Write) { errno = ENOSPC; return -1; }
        if (fault_ == Fault::ShortWrites) {
            if (write_calls == 1) { errno = EINTR; return -1; }
            return FileOperations::write_fd(fd, data, std::min<std::size_t>(3, size));
        }
        return FileOperations::write_fd(fd, data, size);
    }
    int close_fd(int fd) noexcept override {
        ++close_calls;
        const int result = FileOperations::close_fd(fd);
        // The underlying descriptor is already released on this simulated error.
        if (fault_ == Fault::Close) { errno = EIO; return -1; }
        return result;
    }
    int deflate_step(z_stream* stream, int flush) noexcept override {
        if (flush == Z_FINISH && fault_ == Fault::Finalize) return Z_STREAM_ERROR;
        if (flush == Z_FINISH && fault_ == Fault::CompressionMemory) return Z_MEM_ERROR;
        return FileOperations::deflate_step(stream, flush);
    }
    int publish(const fs::path& temporary, const fs::path& destination, bool replace) noexcept override {
        ++publish_calls;
        if (fault_ == Fault::Publish) { errno = EIO; return -1; }
        if (fault_ == Fault::ReusedTemporaryName) {
            const int result = FileOperations::publish(temporary, destination, replace);
            if (result == 0) {
                const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (fd >= 0) {
                    marker_created = (::write(fd, "unrelated", 9) == 9);
                    ::close(fd);
                }
            }
            return result;
        }
        if (fault_ == Fault::Collision) {
            const int fd = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0) return -1;
            const auto count = ::write(fd, "winner", 6);
            ::close(fd);
            if (count != 6) { errno = EIO; return -1; }
        }
        return FileOperations::publish(temporary, destination, replace);
    }
private:
    Fault fault_;
};

// Limits each real read without sleeps or scheduling assumptions. Faults occur
// at byte/call positions chosen by the test, including during magic detection.
class ScriptedInputOperations : public InputOperations {
public:
    explicit ScriptedInputOperations(std::vector<std::size_t> chunks)
        : chunks_(std::move(chunks)) {}

    std::vector<std::size_t> interrupted_calls;
    std::size_t fail_after = std::numeric_limits<std::size_t>::max();
    bool fail_close = false;
    std::size_t read_calls = 0;
    std::size_t interrupts = 0;
    std::size_t bytes_read = 0;
    int close_calls = 0;
    int last_fd = -1;

    std::ptrdiff_t read_fd(int fd, void* data, std::size_t size) noexcept override {
        last_fd = fd;
        ++read_calls;
        if (std::find(interrupted_calls.begin(), interrupted_calls.end(), read_calls) !=
            interrupted_calls.end()) {
            ++interrupts;
            errno = EINTR;
            return -1;
        }
        if (bytes_read >= fail_after) { errno = EIO; return -1; }
        const auto chunk = chunks_[(read_calls - 1) % chunks_.size()];
        const auto limit = std::min({size, chunk, fail_after - bytes_read});
        const auto count = InputOperations::read_fd(fd, data, limit);
        if (count > 0) bytes_read += static_cast<std::size_t>(count);
        return count;
    }

    int close_fd(int fd) noexcept override {
        ++close_calls;
        const int result = InputOperations::close_fd(fd);
        if (fail_close) { errno = EIO; return -1; }
        return result;
    }

private:
    std::vector<std::size_t> chunks_;
};

void require_input_closed_once(const ScriptedInputOperations& operations) {
    CHECK(operations.close_calls == 1);
    REQUIRE(operations.last_fd >= 0);
    errno = 0;
    CHECK(::fcntl(operations.last_fd, F_GETFD) == -1);
    CHECK(errno == EBADF);
}
}  // namespace

TEST_CASE("Kseq rebuilds duplicate headers from name and comment and preserves record positions") {
    TempDirectory temporary;
    const auto input = temporary.path / "records.fa";
    put(input, "\n> same\t description  \r\nac\r\nguR\n\n> same\t description  \nTu");
    const auto data = read_input(input.string());
    REQUIRE(data.records.size() == 2);
    CHECK(data.records[0].header == "same\t description  ");
    CHECK(data.records[1].header == data.records[0].header);
    CHECK(data.records[0].sequence == "ACGUN");
    CHECK(data.records[1].sequence == "TU");
    CHECK(data.stats.uppercase_conversions == 5);
    CHECK(data.stats.unknown_replacements == 1);
    CHECK(data.identity.has_value());
}

TEST_CASE("Sequence cleaning acts on bytes returned by kseq including its final CR handling") {
    TempDirectory temporary;
    const auto input = temporary.path / "bytes.fa";
    std::string bytes = ">s\nnN \t";
    bytes.push_back(static_cast<char>(0xff));
    bytes.push_back('\0');
    bytes += "\r";
    put(input, bytes);
    const auto data = read_input(input.string());
    REQUIRE(data.records.size() == 1);
    CHECK(data.records[0].sequence == std::string(6, 'N'));
    CHECK(data.stats.uppercase_conversions == 1);
    CHECK(data.stats.unknown_replacements == 4);
}

TEST_CASE("Invalid returned records quality strings and pre-existing gaps are content errors") {
    TempDirectory temporary;
    const auto input = temporary.path / "invalid.fa";
    for (const std::string text : {"", "\n\n", "AC\n", ">\nAC", "> \t\nAC", ">s\n", ">a\n>b\nAC", ">a\nAC\n>b\n", ">s\nA-C", ">s\nA.C", "@s\nAC\n+\n!", "@s\nAC\n+\n!!!"}) {
        INFO(text);
        put(input, text);
        error_code([&] { (void)read_input(input.string()); }, kInputError);
    }
    error_code([&] { (void)read_input((temporary.path / "missing.fa").string()); }, kIoError);
    put(input, ">s\n \t\n");
    CHECK(read_input(input.string()).records[0].sequence == "NN");
}

TEST_CASE("INPUT-KSEQ-001 FASTQ qualities and their wrapping do not change prepared sequences") {
    TempDirectory temporary;
    const auto input = temporary.path / "records.data";
    for (const std::string qualities : {"!!!!\n", "\"#\n$%\n", "A@\n+.\n"}) {
        for (const bool compressed : {false, true}) {
            INFO("quality bytes = ", qualities, ", gzip = ", compressed);
            const std::string raw = "@same\tdescription\r\nac\r\ngu\n+ignored title\n" +
                qualities + ">same description\nTUN\n@third\nR\n+\n!\n";
            put(input, compressed ? gzip(raw) : raw);
            const auto data = read_input(input.string());
            REQUIRE(data.records.size() == 3);
            CHECK(data.records[0].header == "same description");
            CHECK(data.records[1].header == "same description");
            CHECK(data.records[2].header == "third");
            CHECK(data.records[0].sequence == "ACGU");
            CHECK(data.records[1].sequence == "TUN");
            CHECK(data.records[2].sequence == "N");
            CHECK(data.stats.uppercase_conversions == 4);
            CHECK(data.stats.unknown_replacements == 1);
        }
    }
}

TEST_CASE("INPUT-KSEQ-002 content diagnostics identify the source and returned record") {
    TempDirectory temporary;
    const auto input = temporary.path / "invalid.data";
    for (const std::string bad_record : {">\nAC\n", ">bad\n", ">bad\nA-C\n",
                                         "@bad\nAC\n+\n!\n"}) {
        put(input, ">good\nAC\n" + bad_record);
        try {
            (void)read_input(input.string());
            FAIL("The second record is invalid");
        } catch (const Error& error) {
            CHECK(error.exit_code() == kInputError);
            const std::string message = error.what();
            CHECK(message.find(input.string()) != std::string::npos);
            CHECK(message.find("record 2:") != std::string::npos);
        }
    }
}

TEST_CASE("Gzip detection is independent of extension and concatenated members are decoded") {
    TempDirectory temporary;
    const auto compressed = temporary.path / "compressed.txt";
    put(compressed, gzip(">a\nAC") + gzip("GT\n>b\nU\n"));
    const auto data = read_input(compressed.string());
    REQUIRE(data.records.size() == 2);
    CHECK(data.records[0].sequence == "ACGT");
    CHECK(data.records[1].sequence == "U");
    const auto plain = temporary.path / "actually-plain.gz";
    put(plain, ">a\nACGT\n>b\nU\n");
    CHECK(read_input(plain.string()).records[0].sequence == "ACGT");
    // Large decoded content crosses the fixed-sized reader buffers.
    put(compressed, gzip(">long\n" + std::string(140000, 'A') + "\n"));
    CHECK(read_input(compressed.string()).records[0].sequence.size() == 140000);
}

TEST_CASE("Truncated corrupt and trailing-data gzip streams fail explicitly") {
    TempDirectory temporary;
    const auto input = temporary.path / "bad.gz";
    const std::string original = gzip(">s\nACGT\n");
    for (const auto size : {std::size_t(2), original.size() / 2, original.size() - 1}) {
        put(input, original.substr(0, size));
        error_code([&] { (void)read_input(input.string()); }, kIoError);
    }
    auto crc_corruption = original;
    crc_corruption[crc_corruption.size() - 8] ^= 1;
    put(input, crc_corruption);
    error_code([&] { (void)read_input(input.string()); }, kIoError);
    put(input, original + "trailing garbage");
    error_code([&] { (void)read_input(input.string()); }, kIoError);
    put(input, original + gzip(">b\nTT\n").substr(0, 9));
    error_code([&] { (void)read_input(input.string()); }, kIoError);
    put(input, gzip(""));
    error_code([&] { (void)read_input(input.string()); }, kInputError);
}

TEST_CASE("FASTA serialization has exact headers width LF and final newline") {
    TempDirectory temporary;
    const std::vector<SequenceRecord> records{{"same description  ", "ACGTU"}, {"same description  ", "ACGTU"}};
    const Alignment alignment{{"ACGTU", "ACGTU"}};
    Options options;
    options.output = (temporary.path / "result.fa").string();
    options.line_width = 2;
    write_output(options, records, alignment, std::nullopt);
    CHECK(get(options.output) == ">same description  \nAC\nGT\nU\n>same description  \nAC\nGT\nU\n");
    error_code([&] { write_output(options, records, alignment, std::nullopt); }, kIoError);
    options.force = true;
    options.line_width = 0;
    write_output(options, records, alignment, std::nullopt);
    CHECK(get(options.output) == ">same description  \nACGTU\n>same description  \nACGTU\n");
    options.output = (temporary.path / "result.fa.gz").string();
    write_output(options, records, alignment, std::nullopt);
    const auto bytes = get(options.output);
    REQUIRE(bytes.size() >= 2);
    CHECK(static_cast<unsigned char>(bytes[0]) == 0x1f);
    CHECK(static_cast<unsigned char>(bytes[1]) == 0x8b);
    const auto decoded = read_input(options.output);
    REQUIRE(decoded.records.size() == 2);
    CHECK(decoded.records[1].header == records[1].header);
    CHECK(decoded.records[1].sequence == "ACGTU");
    no_temporary_files(temporary);
}

TEST_CASE("File identities reject identical paths and symbolic or hard link aliases") {
    TempDirectory temporary;
    const auto source = temporary.path / "input.fa";
    put(source, ">s\nAC\n");
    const auto data = read_input(source.string());
    Options options;
    options.input = source.string();
    options.force = true;
    for (const auto& target : {source, temporary.path / "." / "input.fa"}) {
        options.output = target.string();
        error_code([&] { check_output_path(options, data.identity); }, kCliError);
    }
    const auto symlink = temporary.path / "symbolic.fa";
    fs::create_symlink(source, symlink);
    options.output = symlink.string();
    error_code([&] { check_output_path(options, data.identity); }, kCliError);
    const auto hardlink = temporary.path / "hard.fa";
    fs::create_hard_link(source, hardlink);
    options.output = hardlink.string();
    error_code([&] { check_output_path(options, data.identity); }, kCliError);
    CHECK(get(source) == ">s\nAC\n");
    options.output = (temporary.path / "missing" / "output.fa").string();
    error_code([&] { check_output_path(options, data.identity); }, kIoError);
    options.output = temporary.path.string();
    error_code([&] { check_output_path(options, data.identity); }, kIoError);
}

TEST_CASE("Symlink-parent dot-dot paths use filesystem identity rather than lexical identity") {
    TempDirectory temporary;
    const auto source_directory = temporary.path / "source";
    const auto other_directory = temporary.path / "other";
    fs::create_directories(source_directory);
    fs::create_directories(other_directory / "child");
    const auto source = source_directory / "input.fa";
    const auto actual_output = other_directory / "input.fa";
    put(source, ">s\nAC\n");
    put(actual_output, "old output");
    fs::create_directory_symlink(other_directory / "child", source_directory / "link");
    const auto output_alias = source_directory / "link" / ".." / "input.fa";
    REQUIRE(source.lexically_normal() == output_alias.lexically_normal());
    REQUIRE_FALSE(fs::equivalent(source, output_alias));

    const auto input = read_input(source.string());
    Options options;
    options.input = source.string();
    options.output = output_alias.string();
    options.force = true;
    CHECK_NOTHROW(check_output_path(options, input.identity));
    write_output(options, input.records, Alignment{{"AC"}}, input.identity);
    CHECK(get(actual_output) == ">s\nAC\n");
    CHECK(get(source) == ">s\nAC\n");
}

TEST_CASE("A reused temporary pathname is not deleted after successful publication") {
    TempDirectory temporary;
    Options options;
    options.output = (temporary.path / "result.fa").string();
    const std::vector<SequenceRecord> records{{"s", "ACGT"}};
    const Alignment alignment{{"ACGT"}};
    InjectedOperations operations(Fault::ReusedTemporaryName);
    write_output_with_operations(options, records, alignment, std::nullopt, operations);
    CHECK(operations.marker_created);
    CHECK(get(options.output) == ">s\nACGT\n");
    std::size_t preserved = 0;
    for (const auto& file : fs::directory_iterator(temporary.path)) {
        if (file.path().filename().string().compare(0, 8, ".msakit-") == 0) {
            ++preserved;
            CHECK(get(file.path()) == "unrelated");
        }
    }
    CHECK(preserved == 1);
}

TEST_CASE("Failed writes finalization close or publish preserve the old destination") {
    const std::vector<SequenceRecord> records{{"s", "ACGT"}};
    const Alignment alignment{{"ACGT"}};
    for (const auto fault : {Fault::Write, Fault::Close, Fault::Finalize, Fault::CompressionMemory, Fault::Publish}) {
        TempDirectory temporary;
        Options options;
        options.output = (temporary.path / "existing.gz").string();
        options.force = true;
        put(options.output, "old bytes must survive");
        InjectedOperations operations(fault);
        const int expected = fault == Fault::CompressionMemory ? kCoreError : kIoError;
        error_code([&] { write_output_with_operations(options, records, alignment, std::nullopt, operations); }, expected);
        CHECK(get(options.output) == "old bytes must survive");
        no_temporary_files(temporary);
        if (fault == Fault::Close) {
            CHECK(operations.close_calls == 1);
            CHECK(operations.publish_calls == 0);
        }
    }
}

TEST_CASE("A destination appearing at publication is never overwritten without force") {
    TempDirectory temporary;
    Options options;
    options.output = (temporary.path / "race.fa").string();
    InjectedOperations operations(Fault::Collision);
    const std::vector<SequenceRecord> records{{"s", "ACGT"}};
    const Alignment alignment{{"ACGT"}};
    error_code([&] { write_output_with_operations(options, records, alignment, std::nullopt, operations); }, kIoError);
    CHECK(get(options.output) == "winner");
    CHECK(operations.publish_calls == 1);
    no_temporary_files(temporary);
}

TEST_CASE("Interrupted and short writes are retried without duplicating output bytes") {
    TempDirectory temporary;
    Options options;
    options.output = (temporary.path / "short.fa").string();
    InjectedOperations operations(Fault::ShortWrites);
    const std::vector<SequenceRecord> records{{"s", "ACGT"}};
    const Alignment alignment{{"ACGT"}};
    write_output_with_operations(options, records, alignment, std::nullopt, operations);
    CHECK(get(options.output) == ">s\nACGT\n");
    CHECK(operations.write_calls > 2);
    CHECK(operations.close_calls == 1);
    no_temporary_files(temporary);
}

TEST_CASE("INPUT-BYTES-001 all 256 input bytes follow the independent cleaning contract") {
    TempDirectory temporary;
    const auto input = temporary.path / "byte.fa";
    for (unsigned value = 0; value < 256; ++value) {
        INFO("byte = ", value);
        std::string raw = ">byte\nA";
        raw.push_back(static_cast<char>(value));
        raw += "C\n";
        put(input, raw);
        if (value == '-' || value == '.') {
            error_code([&] { (void)read_input(input.string()); }, kInputError);
            continue;
        }

        // A declarative oracle independent of the production lookup table.
        // The byte is surrounded by residues, so CR, '>', '@' and '+' cannot
        // acquire their line-ending/record-marker meaning. LF separates lines.
        std::string expected = "ANC";
        std::size_t lower = 0;
        std::size_t unknown = 0;
        switch (value) {
        case '\n': expected = "AC"; break;
        case 'A': case 'C': case 'G': case 'T': case 'U': case 'N':
            expected[1] = static_cast<char>(value); break;
        case 'a': expected = "AAC"; lower = 1; break;
        case 'c': expected = "ACC"; lower = 1; break;
        case 'g': expected = "AGC"; lower = 1; break;
        case 't': expected = "ATC"; lower = 1; break;
        case 'u': expected = "AUC"; lower = 1; break;
        case 'n': expected = "ANC"; lower = 1; break;
        default: unknown = 1; break;
        }
        const auto data = read_input(input.string());
        REQUIRE(data.records.size() == 1);
        CHECK(data.records[0].header == "byte");
        CHECK(data.records[0].sequence == expected);
        CHECK(data.stats.uppercase_conversions == lower);
        CHECK(data.stats.unknown_replacements == unknown);
    }
}

TEST_CASE("INPUT-STRUCTURE-001 LF CRLF and header markers have context-dependent meaning") {
    TempDirectory temporary;
    const auto input = temporary.path / "structure.fa";
    struct Case {
        const char* id;
        std::string raw;
        std::vector<SequenceRecord> expected;
        std::size_t unknown;
    };
    const std::vector<Case> cases{
        {"line-endings", ">one\r\nA\r\nC\n", {{"one", "AC"}}, 0},
        {"embedded-cr", ">one\nA\rC\n", {{"one", "ANC"}}, 1},
        {"final-cr", ">one\nAC\r", {{"one", "AC"}}, 0},
        {"double-cr-before-lf", ">one\nA\r\r\nC\n", {{"one", "ANC"}}, 1},
        {"header-markers", ">one>description\nA>C\n>two\nT\n",
            {{"one>description", "ANC"}, {"two", "T"}}, 1},
        {"header-internal-cr", ">one\rdescription\nAC\n", {{"one description", "AC"}}, 0},
        {"comment-internal-cr", ">one description\rmore\nAC\n", {{"one description\rmore", "AC"}}, 0},
        {"header-tab", ">one\tdescription\nAC\n", {{"one description", "AC"}}, 0},
        {"header-leading-space", ">  one description\nAC\n", {{" one description", "AC"}}, 0},
        {"header-trailing-separator", ">one\t\nAC\n", {{"one", "AC"}}, 0},
        {"preamble", "junk\n \t>one\nAC\n", {{"one", "AC"}}, 0},
        {"embedded-at-plus", ">one\nA@+C\n", {{"one", "ANNC"}}, 2},
        {"at-record", ">one\nAC\n@two\nTU\n+ignored\n!!\n", {{"one", "AC"}, {"two", "TU"}}, 0},
        {"plus-quality", ">one\nAC\n+ignored\n!!\n", {{"one", "AC"}}, 0},
        {"empty-crlf", ">one\n\r\nAC\n", {{"one", "NAC"}}, 1},
    };
    for (const auto& item : cases) {
        INFO("case = ", item.id);
        put(input, item.raw);
        const auto data = read_input(input.string());
        REQUIRE(data.records.size() == item.expected.size());
        for (std::size_t i = 0; i < item.expected.size(); ++i) {
            CHECK(data.records[i].header == item.expected[i].header);
            CHECK(data.records[i].sequence == item.expected[i].sequence);
        }
        CHECK(data.stats.uppercase_conversions == 0);
        CHECK(data.stats.unknown_replacements == item.unknown);
    }
}

TEST_CASE("INPUT-BOUNDARY-001 structural bytes survive both decoded and prefix-offset buffer boundaries") {
    TempDirectory temporary;
    const auto input = temporary.path / "boundary.data";
    // Test both kseq's 16 KiB buffer and the byte decoder's 64 KiB buffer.
    // gzip decoded chunks start at offset 0. Plain input first returns its
    // two-byte magic probe, then full chunks, so its first boundary is 65,538.
    // All offsets below are zero-based positions in the uncompressed stream.
    for (const auto buffer : {std::size_t(16'384), std::size_t(65'536)}) {
        for (const auto offset : {buffer - 1, buffer, buffer + 1, buffer + 2, buffer + 3}) {
            for (const auto layout : {0, 1, 2, 3}) {
                std::string raw;
                std::string header;
                std::string sequence;
                std::size_t lower = 0;
                if (layout == 0) {
                    header = std::string(offset - 1, 'h');
                    raw = ">" + header + "\r\nac\n";  // CR at offset.
                    sequence = "AC";
                    lower = 2;
                } else if (layout == 1) {
                    header = "s";
                    sequence = std::string(offset - 3, 'A') + "C";
                    raw = ">s\n" + std::string(offset - 3, 'A') + "\r\nc";
                    lower = 1;
                } else if (layout == 2) {
                    header = "s";
                    sequence = "AC";
                    raw = std::string(offset, '\n') + ">s\nac\n";  // '>' at offset.
                    lower = 2;
                } else {
                    header = std::string(offset - 2, 'h');
                    sequence = "AC";
                    raw = ">" + header + "\nac\n";  // First residue at offset.
                    lower = 2;
                }
                for (const bool compressed : {false, true}) {
                    INFO("offset = ", offset, ", layout = ", layout, ", gzip = ", compressed);
                    put(input, compressed ? gzip(raw) : raw);
                    const auto data = read_input(input.string());
                    REQUIRE(data.records.size() == 1);
                    CHECK(data.records[0].header == header);
                    CHECK(data.records[0].sequence == sequence);
                    CHECK(data.stats.uppercase_conversions == lower);
                    CHECK(data.stats.unknown_replacements == 0);
                }
            }
        }
    }
}

TEST_CASE("INPUT-READ-001 deterministic short reads and EINTR preserve plain and gzip input") {
    TempDirectory temporary;
    const auto input = temporary.path / "short.data";
    const std::vector<std::vector<std::size_t>> schedules{{1}, {2, 1, 7, 3}, {65'536}};
    for (const std::string raw : {">one description\r\nacgR\t\r\n>one description\nTuN",
                                 "@one description\r\nacgR\t\r\n+\n!!\n!!!\n@one description\nTuN\n+\n!!!"}) {
        for (const bool compressed : {false, true}) {
            const auto bytes = compressed ? gzip(raw) : raw;
            put(input, bytes);
            for (std::size_t schedule = 0; schedule < schedules.size(); ++schedule) {
                for (const bool interrupted : {false, true}) {
                    INFO("gzip = ", compressed, ", schedule = ", schedule, ", EINTR = ", interrupted);
                    ScriptedInputOperations operations(schedules[schedule]);
                    if (interrupted) operations.interrupted_calls = {1, 3};
                    const auto data = read_input_with_operations(input.string(), operations);
                    REQUIRE(data.records.size() == 2);
                    CHECK(data.records[0].header == "one description");
                    CHECK(data.records[1].header == "one description");
                    CHECK(data.records[0].sequence == "ACGNN");
                    CHECK(data.records[1].sequence == "TUN");
                    CHECK(data.stats.uppercase_conversions == 4);
                    CHECK(data.stats.unknown_replacements == 2);
                    CHECK(operations.bytes_read == bytes.size());
                    CHECK(operations.interrupts == (interrupted ? 2 : 0));
                    require_input_closed_once(operations);
                    CHECK(get(input) == bytes);
                }
            }
        }
    }
}

TEST_CASE("INPUT-READ-002 read errors before during and after data never return partial records") {
    TempDirectory temporary;
    const auto input = temporary.path / "read-failure.data";
    for (const std::string raw : {">one\nACGT\n>two\nTUN\n",
                                 "@one\nACGT\n+ignored\n!!\n!!\n@two\nTUN\n+\n!!!\n"}) {
        for (const bool compressed : {false, true}) {
            const auto bytes = compressed ? gzip(raw) : raw;
            put(input, bytes);
            for (const auto fail_after : {std::size_t(0), std::size_t(1), bytes.size() / 2, bytes.size()}) {
                INFO("gzip = ", compressed, ", fail after bytes = ", fail_after);
                ScriptedInputOperations operations({7});
                operations.fail_after = fail_after;
                error_code([&] { (void)read_input_with_operations(input.string(), operations); }, kIoError);
                CHECK(operations.bytes_read == fail_after);
                require_input_closed_once(operations);
                CHECK(get(input) == bytes);
                no_temporary_files(temporary);
            }
        }
    }
}

TEST_CASE("INPUT-READ-003 checked close failures release the descriptor once and report IO error") {
    TempDirectory temporary;
    const auto input = temporary.path / "close-failure.data";
    for (const bool compressed : {false, true}) {
        INFO("gzip = ", compressed);
        const std::string raw = ">one\nACGT\n";
        const auto bytes = compressed ? gzip(raw) : raw;
        put(input, bytes);
        ScriptedInputOperations operations({3});
        operations.fail_close = true;
        try {
            (void)read_input_with_operations(input.string(), operations);
            FAIL("Closing the input must complete before returning its records");
        } catch (const Error& error) {
            CHECK(error.exit_code() == kIoError);
            CHECK(std::string(error.what()).find("Cannot close input") != std::string::npos);
        }
        require_input_closed_once(operations);
        CHECK(get(input) == bytes);
    }
}

TEST_CASE("INPUT-READ-004 malformed input releases its descriptor during unwinding") {
    TempDirectory temporary;
    const auto input = temporary.path / "invalid.data";
    for (const bool compressed : {false, true}) {
        INFO("gzip = ", compressed);
        const std::string raw = ">one\nAC-\n";
        put(input, compressed ? gzip(raw) : raw);
        ScriptedInputOperations operations({1});
        error_code([&] { (void)read_input_with_operations(input.string(), operations); }, kInputError);
        require_input_closed_once(operations);
    }
}
