#include "integration.hpp"
#include <functional>
#include <thread>
#include <vector>
namespace lab {
namespace {
void worker(Index first, Index last, double a, double h, double& result) {
    result = partial_sum(first, last, a, h);
}
}
double std_integral(Index n, unsigned threads) {
    validate(n, threads);
    const double h = (B - A) / static_cast<double>(n);
    std::vector<double> results(threads);
    std::vector<std::thread> handles;
    handles.reserve(threads);
    try {
        for (unsigned t = 0; t < threads; ++t) {
            Index first, last;
            range(n, threads, t, first, last);
            handles.emplace_back(worker, first, last, A, h, std::ref(results[t]));
        }
    } catch (...) {
        for (auto& thread : handles) thread.join();
        throw;
    }
    for (auto& thread : handles) thread.join();
    double sum = 0.0, correction = 0.0;
    for (double value : results) {
        const double y = value - correction;
        const double next = sum + y;
        correction = (next - sum) - y; sum = next;
    }
    return h * sum;
}
}
