#include <chrono>

#include <bsoncxx/builder/basic/document.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

#include <bsoncxx/test/catch.hh>

#include <catch2/generators/catch_generators_range.hpp>

template <typename Rep, typename Period>
double to_raw_us(std::chrono::duration<Rep, Period> const& duration) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(duration).count();
}

namespace {

TEST_CASE("block_time_ms_bug_repro") {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto const uri = mongocxx::uri("mongodb://localhost:27017/");

    auto client = mongocxx::client(uri);

    auto db = client["admin"];

    auto const iteration = GENERATE(range(0, 100));

    CAPTURE(iteration);

    static constexpr auto block_time = std::chrono::milliseconds(50);

    db.run_command(make_document(
        kvp("configureFailPoint", "failCommand"),
        kvp("mode", make_document(kvp("times", 1))),
        kvp("data",
            make_document(
                kvp("failCommands", bsoncxx::builder::basic::make_array("ping")),
                kvp("blockTimeMS", std::chrono::milliseconds(block_time).count()),
                kvp("blockConnection", true)))));

    auto const start = std::chrono::steady_clock::now();

    db.run_command(make_document(kvp("ping", 1)));

    auto const end = std::chrono::steady_clock::now();

    auto const elapsed = end - start;

    REQUIRE(to_raw_us(elapsed) >= to_raw_us(block_time));
}

} // namespace
