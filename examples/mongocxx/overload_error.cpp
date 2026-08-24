// Sample code implied from the example:

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <thread>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/json.hpp>

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

double const BASE_BACKOFF_MS = 100;
double const MAX_BACKOFF_MS = 10000;

double random_01() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(gen);
}

double calculate_exponential_backoff(int attempt) {
  return random_01() * std::min(MAX_BACKOFF_MS, BASE_BACKOFF_MS * std::pow(2.0, attempt - 1));
}

using retryable_fn = std::function<void()>;

void execute_with_retries(retryable_fn fn, int max_attempts = 2) {
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    bool is_retry = attempt > 0;

    if (is_retry) {
      double delay = calculate_exponential_backoff(attempt);
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay)));
    }
    try {
      fn();
      return;

    } catch (mongocxx::operation_exception const& e) {
      bool is_retryable_overload_error = is_system_overloaded_error(e) && e.has_error_label(RETRYABLE_ERROR_LABEL);
      bool can_retry = is_retryable_overload_error && attempt + 1 < max_attempts;

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
