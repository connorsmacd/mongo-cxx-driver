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

// Start of example code:

// Step 1: Detect overload errors

char const* RETRYABLE_ERROR_LABEL = "RetryableError";
char const* SYSTEM_OVERLOADED_ERROR = "SystemOverloadedError";

bool is_system_overloaded_error(mongocxx::operation_exception const& e) {
  return e.has_error_label(SYSTEM_OVERLOADED_ERROR);
}

// Step 2: Implement operation "retry" logic using exponential backoff and jitter

double const BASE_BACKOFF_MS = 100.0;
double const MAX_BACKOFF_MS = 10000.0;
int const MAX_ATTEMPTS_DEFAULT = 2;

double random_01() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(gen);
}

double calculate_exponential_backoff(int attempt, double base_backoff_ms) {
  return random_01() * std::min(MAX_BACKOFF_MS, base_backoff_ms * std::pow(2.0, attempt));
}

// get_base_backoff returns the base backoff to apply for an overload error. A server may attach a
// positive `baseBackoffMS` to the error to replace the default base backoff.
double get_base_backoff(mongocxx::operation_exception const& e) {
  if (auto const& error_reply = e.raw_server_error()) {
    auto elem = error_reply->view()["baseBackoffMS"];
    if (elem && (elem.type() == bsoncxx::type::k_int32 || elem.type() == bsoncxx::type::k_int64)) {
      std::int64_t base_backoff_ms =
        elem.type() == bsoncxx::type::k_int32 ? elem.get_int32().value : elem.get_int64().value;
      if (base_backoff_ms > 0) {
        return static_cast<double>(base_backoff_ms);
      }
    }
  }
  return BASE_BACKOFF_MS;
}

using retryable_fn = std::function<void()>;

void execute_with_retries(retryable_fn fn, int max_attempts = MAX_ATTEMPTS_DEFAULT) {
  double base_backoff_ms = BASE_BACKOFF_MS;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    bool is_retry = attempt > 0;

    if (is_retry) {
      double delay = calculate_exponential_backoff(attempt, base_backoff_ms);
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay)));
    }
    try {
      fn();
      return;

    } catch (mongocxx::operation_exception const& e) {
      bool is_retryable_overload_error = is_system_overloaded_error(e) && e.has_error_label(RETRYABLE_ERROR_LABEL);
      bool can_retry = is_retryable_overload_error && attempt + 1 < max_attempts;

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
  auto db = client.database("db");
  auto users_collection = db.collection("users");

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
