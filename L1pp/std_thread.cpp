#include "integration.hpp"
#include <chrono>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
namespace lab {
namespace {
void worker(Index first, Index last, double a, double h, double& result, double& seconds) {
    const auto start = std::chrono::steady_clock::now();
    result = partial_sum(first, last, a, h);
    const auto stop = std::chrono::steady_clock::now();
    seconds = std::chrono::duration<double>(stop - start).count();
}
}
double std_integral(Index n, unsigned threads, std::vector<ThreadResult>* details) {
    validate(n, threads);
    const double h = (B - A) / static_cast<double>(n);
    std::vector<ThreadResult> results(threads);
    std::vector<std::thread> handles;
    handles.reserve(threads);
    try {
        for (unsigned t = 0; t < threads; ++t) {
            Index first, last;
            range(n, threads, t, first, last);
            results[t].first = first;
            results[t].last = last;
            handles.emplace_back(worker, first, last, A, h,
                                 std::ref(results[t].sum), std::ref(results[t].seconds));
        }
    } catch (...) {
        for (auto& thread : handles) thread.join();
        throw;
    }
    for (auto& thread : handles) thread.join();
    double sum = 0.0, correction = 0.0;
    for (const auto& value : results) {
        const double y = value.sum - correction;
        const double next = sum + y;
        correction = (next - sum) - y; sum = next;
    }
    if (details) *details = std::move(results);
    return h * sum;
}
}
