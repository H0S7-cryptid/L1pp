#include "integration.hpp"
#include <chrono>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
namespace lab {
namespace {
void worker(Index first, Index last, double a, double h, double& result) {
    result = partial_sum(first, last, a, h);
}
}
double std_integral(Index n, unsigned threads, double& elapsed_seconds) {
    validate(n, threads);
    const double h = (B - A) / static_cast<double>(n);
    std::vector<double> results(threads);
    std::vector<std::pair<Index, Index>> ranges(threads);
    std::vector<std::thread> handles;
    handles.reserve(threads);
    for (unsigned t = 0; t < threads; ++t)
        range(n, threads, t, ranges[t].first, ranges[t].second);
    const auto start = std::chrono::steady_clock::now();
    try {
        for (unsigned t = 0; t < threads; ++t) {
            handles.emplace_back(worker, ranges[t].first, ranges[t].second, A, h,
                                 std::ref(results[t]));
        }
    } catch (...) {
        for (auto& thread : handles) thread.join();
        throw;
    }
    for (auto& thread : handles) thread.join();
    const auto stop = std::chrono::steady_clock::now();
    elapsed_seconds = std::chrono::duration<double>(stop - start).count();
    double sum = 0.0, correction = 0.0;
    for (const auto& value : results) {
        const double y = value - correction;
        const double next = sum + y;
        correction = (next - sum) - y; sum = next;
    }
    return h * sum;
}
}
