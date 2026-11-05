#include "internal.hpp"

#include <charconv>
#include <csignal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <CLI/CLI.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_sinks.h>

#ifndef MSAKIT_VERSION
#define MSAKIT_VERSION "0.3.0"
#endif

namespace msakit::detail {
namespace {

// Reporting an allocation failure must not itself require an allocation.
void report_error(const char* message) noexcept {
    std::fprintf(stderr, "msakit: %s\n", message);
    std::fflush(stderr);
}

std::size_t parse_width(std::string_view text) {
    if (text.empty() || text.find_first_not_of("0123456789") != text.npos) {
        throw Error(kCliError, "--line-width must be a nonnegative decimal integer");
    }
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw Error(kCliError, "--line-width is outside the supported integer range");
    }
    return result;
}

}  // namespace

int run_application(int argc, char** argv, CoreFunction core) {
    // Convert broken-pipe writes to an ordinary I/O error (exit 4).
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        report_error("failed to configure broken-pipe handling");
        return kCoreError;
    }

    try {
        Options options;
        std::string width_text = "80";
        CLI::App app{"DNA/RNA multiple sequence alignment with a replaceable reference core", "msakit"};
        app.set_version_flag("-v,--version", "msakit " MSAKIT_VERSION);
        app.add_option("-i,--input", options.input, "Input FASTA/FASTQ, optionally gzip; '-' reads stdin")
            ->required();
        app.add_option("-o,--output", options.output, "Output FASTA; '.gz' compresses; '-' writes plain stdout")
            ->required();
        app.add_option("--line-width", width_text, "Sequence line width; 0 disables wrapping")
            ->default_val("80");
        app.add_flag("--force", options.force, "Replace an existing output file after a complete write");
        app.add_flag("--quiet", options.quiet, "Suppress ordinary logs; errors remain visible");

        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError& error) {
            // CLI11 handles help/version formatting, but its numeric parse-error
            // codes are not MSA-Kit's public exit-code contract.
            const int parser_status = app.exit(error, std::cout, std::cerr);
            return parser_status == 0 ? 0 : kCliError;
        }

        options.line_width = parse_width(width_text);
        if (options.input.empty() || options.output.empty()) {
            throw Error(kCliError, "--input and --output must not be empty");
        }

        auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        spdlog::logger logger("msakit", std::move(sink));
        logger.set_pattern("[msakit] %v");
        logger.set_level(options.quiet ? spdlog::level::off : spdlog::level::info);

        InputData input = read_input(options.input);
        check_output_path(options, input.identity);
        logger.info("Read {} sequences; lowercase conversions: {}; replaced with N: {}",
                    input.records.size(), input.stats.uppercase_conversions,
                    input.stats.unknown_replacements);

        Alignment alignment;
        try {
            if (core == nullptr) {
                throw std::runtime_error("no alignment core was supplied");
            }
            alignment = core(input.records);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& error) {
            throw Error(kCoreError, std::string("alignment core failed: ") + error.what());
        } catch (...) {
            throw Error(kCoreError, "alignment core failed with an unknown exception");
        }

        validate_alignment(input.records, alignment);
        logger.info("Validated {} alignment rows; writing output", alignment.rows.size());
        write_output(options, input.records, alignment, input.identity);
        // Publishing the completed file is the last fallible operation. Logging
        // afterward must not turn a successful replacement into a reported error.
        return 0;
    } catch (const Error& error) {
        report_error(error.what());
        return error.exit_code();
    } catch (const std::bad_alloc&) {
        report_error("memory allocation failed");
        return kCoreError;
    } catch (const std::exception& error) {
        report_error(error.what());
        return kCoreError;
    } catch (...) {
        report_error("unexpected internal failure");
        return kCoreError;
    }
}

}  // namespace msakit::detail
