// Sample code implied from the example:

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <thread>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

#include <examples/macros.hh>

namespace {

void process_result(bsoncxx::document::view result) {
  std::cout << bsoncxx::to_json(result) << std::endl;
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

// Start of example code:

// Step 1: Detect overload errors

constexpr auto k_retryable_error_label = "RetryableError";
constexpr auto k_system_overloaded_error_label = "SystemOverloadedError";

bool is_system_overloaded_error(mongocxx::operation_exception const& e) {
  return e.has_error_label(k_system_overloaded_error_label);
}

// Step 2: Implement operation "retry" logic using exponential backoff and jitter

using milliseconds_t = std::chrono::duration<double, std::milli>;

constexpr auto k_base_backoff = milliseconds_t(100.0);
constexpr auto k_max_backoff = milliseconds_t(10000.0);
constexpr auto k_max_attempts_default = 2;

double random_01() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(gen);
}

milliseconds_t calculate_exponential_backoff(int attempt, milliseconds_t base_backoff) {
  return random_01() * std::min(k_max_backoff, base_backoff * std::pow(2.0, attempt));
}

// get_base_backoff returns the base backoff to apply for an overload error. A server may attach a
// positive `baseBackoffMS` to the error to replace the default base backoff.
milliseconds_t get_base_backoff(mongocxx::operation_exception const& e) {
  if (auto const& error_reply = e.raw_server_error()) {
    auto const elem = error_reply->view()["baseBackoffMS"];

    if (elem && (elem.type() == bsoncxx::type::k_int32 || elem.type() == bsoncxx::type::k_int64)) {
      auto const base_backoff_ms = elem.type() == bsoncxx::type::k_int32
                                     ? static_cast<std::int64_t>(elem.get_int32().value)
                                     : elem.get_int64().value;

      if (base_backoff_ms > 0) {
        return milliseconds_t(static_cast<double>(base_backoff_ms));
      }
    }
  }

  return k_base_backoff;
}

using retryable_fn_t = std::function<void()>;

void execute_with_retries(retryable_fn_t fn, int max_attempts = k_max_attempts_default) {
  auto base_backoff_ms = k_base_backoff;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    auto const is_retry = attempt > 0;

    if (is_retry) {
      auto const delay = calculate_exponential_backoff(attempt, base_backoff_ms);
      std::this_thread::sleep_for(delay);
    }

    try {
      fn();
      return;

    } catch (mongocxx::operation_exception const& e) {
      auto const is_retryable_overload_error =
        is_system_overloaded_error(e) && e.has_error_label(k_retryable_error_label);
      auto const can_retry = is_retryable_overload_error && attempt + 1 < max_attempts;

      // Apply the server-requested base backoff, if any, to the next attempt's delay.
      base_backoff_ms = get_base_backoff(e);

      if (!can_retry) {
        throw;
      }
    }
  }
}

// Step 3: Use the retry helper for collection operations

int main() {
  using namespace bsoncxx::builder::basic;

  auto instance = mongocxx::instance();
  auto client = mongocxx::client(mongocxx::uri("mongodb://localhost:27017"));
  auto users_collection = client["db"]["users"];

  // Original:
  {
    auto cursor = users_collection.find(make_document());
    for (auto const& res : cursor) {
      process_result(res);
    }
  }

  // With retry:
  execute_with_retries([&]() {
    auto cursor = users_collection.find(make_document());
    for (auto const& res : cursor) {
      process_result(res);
    }
  });
}

// End of example code

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
