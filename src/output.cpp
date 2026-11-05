#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib.h>
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

class OutputTransaction {
public:
    OutputTransaction(const std::string& destination, bool force, FileOperations& operations)
        : destination_(destination), force_(force), operations_(operations) {
        auto parent = destination_.parent_path();
        if (parent.empty()) parent = ".";
        temporary_ = (parent / ".msakit-XXXXXX").string();
        fd_ = ::mkstemp(temporary_.data());
        if (fd_ < 0) fail_io("Cannot create temporary output", destination);
        if (::fcntl(fd_, F_SETFD, FD_CLOEXEC) == -1) {
            const int saved = errno;
            ::close(std::exchange(fd_, -1));
            ::unlink(temporary_.c_str());
            fail_io("Cannot configure temporary output", destination, saved);
        }
    }
    ~OutputTransaction() {
        if (fd_ >= 0) ::close(fd_);
        // Only an unpublished transaction still owns this name. Publication
        // releases it; another file may immediately reuse the old temporary name.
        if (!temporary_.empty()) ::unlink(temporary_.c_str());
    }
    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    int fd() const noexcept { return fd_; }
    void commit() {
        const int old = std::exchange(fd_, -1);
        if (operations_.close_fd(old) != 0) fail_io("Cannot close output", destination_.string());
        if (operations_.publish(temporary_, destination_, force_) != 0) {
            fail_io("Cannot publish output", destination_.string());
        }
        temporary_.clear();
    }
private:
    std::filesystem::path destination_;
    std::string temporary_;
    bool force_;
    FileOperations& operations_;
    int fd_ = -1;
};

class OutputStream {
public:
    OutputStream(int fd, bool gzip, const std::string& path, FileOperations& operations)
        : fd_(fd), gzip_(gzip), path_(path), operations_(operations) {
        if (gzip_) {
            const int result = ::deflateInit2(&stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                                               16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            if (result != Z_OK) fail_zlib("Cannot initialize gzip output", path_, result, stream_);
            initialized_ = true;
        }
    }
    ~OutputStream() { if (initialized_) ::deflateEnd(&stream_); }
    OutputStream(const OutputStream&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;

    void append(std::string_view bytes) {
        while (!bytes.empty()) {
            const auto size = std::min(bytes.size(), buffer_.size() - used_);
            std::memcpy(buffer_.data() + used_, bytes.data(), size);
            used_ += size;
            bytes.remove_prefix(size);
            if (used_ == buffer_.size()) flush();
        }
    }

    void finish() {
        flush();
        if (gzip_) {
            int result;
            do {
                stream_.next_out = compressed_.data();
                stream_.avail_out = static_cast<uInt>(compressed_.size());
                result = operations_.deflate_step(&stream_, Z_FINISH);
                if (result != Z_OK && result != Z_STREAM_END) {
                    fail_zlib("Cannot finalize gzip output", path_, result, stream_);
                }
                write_all(compressed_.data(), compressed_.size() - stream_.avail_out);
            } while (result != Z_STREAM_END);
            const int ended = ::deflateEnd(&stream_);
            initialized_ = false;
            if (ended != Z_OK) fail_zlib("Cannot close gzip output", path_, ended, stream_);
        }
    }
private:
    void write_all(const unsigned char* data, std::size_t size) {
        while (size != 0) {
            const auto written = operations_.write_fd(fd_, data, size);
            if (written < 0) {
                if (errno == EINTR) continue;
                fail_io("Cannot write output", path_);
            }
            if (written == 0) throw Error(kIoError, "Output write made no progress: " + path_);
            data += written;
            size -= static_cast<std::size_t>(written);
        }
    }
    void flush() {
        if (used_ == 0) return;
        if (!gzip_) {
            write_all(buffer_.data(), used_);
        } else {
            stream_.next_in = buffer_.data();
            stream_.avail_in = static_cast<uInt>(used_);
            while (stream_.avail_in != 0) {
                stream_.next_out = compressed_.data();
                stream_.avail_out = static_cast<uInt>(compressed_.size());
                const int result = operations_.deflate_step(&stream_, Z_NO_FLUSH);
                if (result != Z_OK) fail_zlib("Cannot compress output", path_, result, stream_);
                write_all(compressed_.data(), compressed_.size() - stream_.avail_out);
            }
        }
        used_ = 0;
    }
    int fd_;
    bool gzip_;
    std::string path_;
    FileOperations& operations_;
    std::array<unsigned char, kBufferSize> buffer_{};
    std::array<unsigned char, kBufferSize> compressed_{};
    std::size_t used_ = 0;
    z_stream stream_{};
    bool initialized_ = false;
};

void serialize(OutputStream& stream, const std::vector<SequenceRecord>& input,
               const Alignment& alignment, std::size_t width) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        stream.append(">");
        stream.append(input[i].header);
        stream.append("\n");
        const std::string_view row = alignment.rows[i];
        if (width == 0) {
            stream.append(row);
            stream.append("\n");
        } else {
            for (std::size_t offset = 0; offset < row.size();) {
                const auto count = std::min(width, row.size() - offset);
                stream.append(row.substr(offset, count));
                stream.append("\n");
                offset += count;
            }
        }
    }
    stream.finish();
}
}  // namespace

void check_output_path(const Options& options,
                       const std::optional<FileIdentity>& input_identity) {
    if (options.output == "-") return;
    // Compare actual file identities. Lexical normalization is incorrect for
    // paths containing a symlink followed by '..': the kernel follows the
    // symlink before resolving '..', which can name a different directory.
    struct stat target {};
    if (::stat(options.output.c_str(), &target) == 0) {
        if (input_identity && input_identity->device == static_cast<std::uintmax_t>(target.st_dev) &&
            input_identity->inode == static_cast<std::uintmax_t>(target.st_ino)) {
            throw Error(kCliError, "Input and output refer to the same file (including link aliases)");
        }
        if (!S_ISREG(target.st_mode)) {
            throw Error(kIoError, "Output must be a regular file: " + options.output);
        }
    } else if (errno != ENOENT) {
        fail_io("Cannot inspect output", options.output);
    }
    struct stat link {};
    if (::lstat(options.output.c_str(), &link) == 0) {
        if (!options.force) throw Error(kIoError, "Output already exists; use --force: " + options.output);
    } else if (errno != ENOENT) {
        fail_io("Cannot inspect output", options.output);
    }
    auto parent = std::filesystem::path(options.output).parent_path();
    if (parent.empty()) parent = ".";
    std::error_code ec;
    if (!std::filesystem::is_directory(parent, ec)) {
        throw Error(kIoError, "Output parent directory does not exist or is inaccessible: " + parent.string());
    }
}

std::ptrdiff_t FileOperations::write_fd(int fd, const void* data, std::size_t size) noexcept {
    return ::write(fd, data, size);
}
int FileOperations::close_fd(int fd) noexcept { return ::close(fd); }
int FileOperations::deflate_step(z_stream* stream, int flush) noexcept { return ::deflate(stream, flush); }

int FileOperations::publish(const std::filesystem::path& temporary,
                            const std::filesystem::path& destination, bool replace) noexcept {
    if (replace) return ::rename(temporary.c_str(), destination.c_str());
#ifdef SYS_renameat2
    constexpr unsigned int no_replace = 1;  // RENAME_NOREPLACE from linux/fs.h.
    if (::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
                  destination.c_str(), no_replace) == 0) return 0;
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) return -1;
#endif
    // link is also atomic and refuses to replace an existing destination. The
    // same-directory temporary file guarantees both names are on one filesystem.
    struct stat owned {};
    if (::lstat(temporary.c_str(), &owned) != 0) return -1;
    if (::link(temporary.c_str(), destination.c_str()) != 0) return -1;
    if (::unlink(temporary.c_str()) != 0) {
        // The output is already published. Do not report a failed transaction
        // or let its destructor blindly remove a name we no longer own. A
        // best-effort retry is limited to the original temporary inode.
        struct stat remaining {};
        if (::lstat(temporary.c_str(), &remaining) == 0 &&
            remaining.st_dev == owned.st_dev && remaining.st_ino == owned.st_ino) {
            ::unlink(temporary.c_str());
        }
    }
    return 0;
}

void write_output_with_operations(const Options& options,
                                  const std::vector<SequenceRecord>& input,
                                  const Alignment& alignment,
                                  const std::optional<FileIdentity>& input_identity,
                                  FileOperations& operations) {
    if (input.empty() || alignment.rows.size() != input.size()) {
        throw Error(kCoreError, "Cannot serialize an alignment with an invalid row count");
    }
    check_output_path(options, input_identity);
    if (options.output == "-") {
        OutputStream stream(STDOUT_FILENO, false, "standard output", operations);
        serialize(stream, input, alignment, options.line_width);
    } else {
        OutputTransaction transaction(options.output, options.force, operations);
        const bool gzip = options.output.size() >= 3 &&
                          options.output.compare(options.output.size() - 3, 3, ".gz") == 0;
        {
            OutputStream stream(transaction.fd(), gzip, options.output, operations);
            serialize(stream, input, alignment, options.line_width);
        }
        transaction.commit();
    }
}

void write_output(const Options& options, const std::vector<SequenceRecord>& input,
                  const Alignment& alignment,
                  const std::optional<FileIdentity>& input_identity) {
    FileOperations operations;
    write_output_with_operations(options, input, alignment, input_identity, operations);
}
}  // namespace msakit::detail
