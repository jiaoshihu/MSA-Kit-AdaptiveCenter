#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace {
// This translation unit supplies allocator hooks only for its own kseq
// instantiation. The production parser always uses the C allocator defaults.
struct Allocations {
    std::array<void*, 64> live{};
    std::size_t calls = 0;
    std::size_t fail_on = 0;
    bool invalid_free = false;

    bool fail() { return ++calls == fail_on; }
    std::size_t index(void* pointer) const {
        for (std::size_t i = 0; i < live.size(); ++i)
            if (live[i] == pointer) return i;
        return live.size();
    }
    std::size_t count() const {
        return static_cast<std::size_t>(std::count_if(live.begin(), live.end(),
                                                    [](void* p) { return p != nullptr; }));
    }
    void remember(void* pointer) {
        if (!pointer) return;
        const auto slot = index(nullptr);
        if (slot == live.size()) std::abort();
        live[slot] = pointer;
    }
} allocations;

void* checked_malloc(std::size_t bytes) {
    if (allocations.fail()) return nullptr;
    void* result = std::malloc(bytes);
    allocations.remember(result);
    return result;
}
void* checked_calloc(std::size_t count, std::size_t bytes) {
    if (allocations.fail()) return nullptr;
    void* result = std::calloc(count, bytes);
    allocations.remember(result);
    return result;
}
void* checked_realloc(void* pointer, std::size_t bytes) {
    if (allocations.fail()) return nullptr;
    const auto slot = pointer ? allocations.index(pointer) : allocations.index(nullptr);
    if (slot == allocations.live.size()) std::abort();
    void* result = std::realloc(pointer, bytes);
    if (result) allocations.live[slot] = result;
    return result;
}
void checked_free(void* pointer) {
    if (!pointer) return;
    const auto slot = allocations.index(pointer);
    if (slot == allocations.live.size()) {
        allocations.invalid_free = true;
        return;
    }
    allocations.live[slot] = nullptr;
    std::free(pointer);
}
} // namespace

#define KSEQ_MALLOC checked_malloc
#define KSEQ_CALLOC checked_calloc
#define KSEQ_REALLOC checked_realloc
#define KSEQ_FREE checked_free
#include <kseq.h>

namespace {
struct Bytes {
    std::string_view data;
    std::size_t offset = 0;
    std::size_t chunk = std::numeric_limits<std::size_t>::max();
    std::size_t fail_at = std::numeric_limits<std::size_t>::max();
    int error = -1;
};
int read_bytes(Bytes* source, unsigned char* destination, int capacity) {
    if (source->offset >= source->fail_at) return source->error;
    const auto size = std::min({static_cast<std::size_t>(capacity), source->chunk,
                               source->data.size() - source->offset,
                               source->fail_at - source->offset});
    if (size) std::memcpy(destination, source->data.data() + source->offset, size);
    source->offset += size;
    return static_cast<int>(size);
}

KSEQ_INIT(Bytes*, read_bytes)

void reset_allocations(std::size_t fail_on = 0) {
    REQUIRE(allocations.count() == 0);
    REQUIRE_FALSE(allocations.invalid_free);
    allocations.calls = 0;
    allocations.fail_on = fail_on;
}
void check_released() {
    CHECK(allocations.count() == 0);
    CHECK_FALSE(allocations.invalid_free);
}

TEST_CASE("KSEQ-RESOURCE-001 initialization failure releases partial objects") {
    for (std::size_t point = 1; point <= 3; ++point) {
        CAPTURE(point);
        reset_allocations(point);
        Bytes input{">one\nAC\n"};
        kseq_t* reader = kseq_init(&input);
        CHECK(reader == nullptr);
        kseq_destroy(reader); // null destruction is explicitly supported
        CHECK(allocations.calls == point);
        check_released();
    }
}

TEST_CASE("KSEQ-RESOURCE-002 every parsing allocation failure returns resource error") {
    const std::string sequence(50000, 'A');
    const std::string raw = "@" + std::string(200, 'n') + " " + std::string(250, 'c') +
                            "\n" + sequence + "\n+\n" + std::string(50000, '!') + "\n";
    reset_allocations();
    Bytes baseline{raw, 0, 17};
    auto* reader = kseq_init(&baseline);
    REQUIRE(reader != nullptr);
    CHECK(kseq_read(reader) == 50000);
    CHECK(std::string_view(reader->seq.s, reader->seq.l) == sequence);
    CHECK(kseq_read(reader) == -1);
    const auto allocation_count = allocations.calls;
    CHECK(allocation_count > 10); // name, comment, sequence and quality all grow
    kseq_destroy(reader);
    check_released();

    for (std::size_t point = 4; point <= allocation_count; ++point) {
        CAPTURE(point);
        reset_allocations(point);
        Bytes input{raw, 0, 17};
        reader = kseq_init(&input);
        REQUIRE(reader != nullptr);
        CHECK(kseq_read(reader) == -4);
        CHECK(allocations.calls == point);
        kseq_destroy(reader);
        check_released();
    }
}

TEST_CASE("KSEQ-RESOURCE-003 failed expansion preserves owner and existing bytes") {
    reset_allocations();
    kstring_t value{};
    REQUIRE(kseq_reserve(&value, 2) == 0);
    std::memcpy(value.s, "AC", 3);
    value.l = 2;
    char* original = value.s;
    const auto capacity = value.m;
    allocations.fail_on = allocations.calls + 1;
    CHECK(kseq_reserve(&value, 100) == -4);
    CHECK(value.s == original);
    CHECK(value.m == capacity);
    CHECK(value.l == 2);
    CHECK(std::string_view(value.s, value.l) == "AC");
    checked_free(value.s);
    check_released();
}

TEST_CASE("KSEQ-LIMIT-001 lengths are checked before allocation addition or conversion") {
    reset_allocations();
    kstring_t value{};
    CHECK(kseq_reserve(&value, std::numeric_limits<std::size_t>::max()) == -4);
    CHECK(kseq_reserve(&value, static_cast<std::size_t>(INT_MAX) + 1) == -4);
    value.l = static_cast<std::size_t>(INT_MAX);
    CHECK(kseq_reserve(&value, 1) == -4);
    value.l += 1;
    CHECK(kseq_reserve(&value, 0) == -4);
    CHECK(allocations.calls == 0);

    // INT_MAX itself is legal, but the test stops its allocation in the hook
    // before requesting gigabytes from the allocator.
    value.l = static_cast<std::size_t>(INT_MAX);
    allocations.fail_on = 1;
    CHECK(kseq_reserve(&value, 0) == -4);
    CHECK(allocations.calls == 1);
    CHECK(value.s == nullptr);
    value.l = 0;
    check_released();

    reset_allocations();
    Bytes input{"A\n"};
    auto* stream = ks_init(&input);
    REQUIRE(stream != nullptr);
    value.l = static_cast<std::size_t>(INT_MAX);
    const auto before = allocations.calls;
    CHECK(ks_getuntil2(stream, KS_SEP_LINE, &value, nullptr, 1) == -4);
    CHECK(allocations.calls == before);
    ks_destroy(stream);
    check_released();
}

TEST_CASE("KSEQ-READ-001 errors in every record region propagate instead of EOF or success") {
    const std::string raw = "@name descriptor\nACGT\n+description\n!!!!\n";
    for (int error : {-1, -7}) {
        for (std::size_t position = 0; position <= raw.size(); ++position) {
            CAPTURE(error);
            CAPTURE(position);
            reset_allocations();
            Bytes input{raw, 0, 3, position, error};
            auto* reader = kseq_init(&input);
            REQUIRE(reader != nullptr);
            int status = kseq_read(reader);
            if (status >= 0) status = kseq_read(reader);
            CHECK(status == -3);
            CHECK(kseq_read(reader) == -3); // failure remains sticky
            kseq_destroy(reader);
            check_released();
        }
    }
}

TEST_CASE("KSEQ-READ-002 short reads preserve native name comment sequence and quality") {
    const std::string raw = "preamble\n>first\t full description\r\nAC\r\nGT\n"
                            "@second name\nTU\nNN\n+ignored\n!#\n$%\n";
    for (std::size_t chunk : {1, 2, 3, 7, 16384}) {
        CAPTURE(chunk);
        reset_allocations();
        Bytes input{raw, 0, chunk};
        auto* reader = kseq_init(&input);
        REQUIRE(reader != nullptr);
        CHECK(kseq_read(reader) == 4);
        CHECK(std::string_view(reader->name.s, reader->name.l) == "first");
        CHECK(std::string_view(reader->comment.s, reader->comment.l) == " full description");
        CHECK(std::string_view(reader->seq.s, reader->seq.l) == "ACGT");
        CHECK(reader->qual.l == 0);
        CHECK(kseq_read(reader) == 4);
        CHECK(std::string_view(reader->name.s, reader->name.l) == "second");
        CHECK(std::string_view(reader->comment.s, reader->comment.l) == "name");
        CHECK(std::string_view(reader->seq.s, reader->seq.l) == "TUNN");
        CHECK(std::string_view(reader->qual.s, reader->qual.l) == "!#$%");
        CHECK(kseq_read(reader) == -1);
        kseq_destroy(reader);
        check_released();
    }
}
} // namespace
