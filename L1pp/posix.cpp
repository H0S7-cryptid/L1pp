#include "integration.hpp"
#if defined(_MSC_VER) && _MSC_VER >= 1900
#include <time.h>
#define HAVE_STRUCT_TIMESPEC
#endif
#include <pthread.h>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
namespace lab {
namespace {
struct Arguments { Index first, last; double a, h; };
void* worker(void* raw) {
    const auto& args = *static_cast<Arguments*>(raw);
    // Heap storage stays valid after this worker returns.
    auto* result = new (std::nothrow) double;
    if (result) *result = partial_sum(args.first, args.last, args.a, args.h);
    return result; // Received by the caller ONLY through pthread_join.
}
double join_result(pthread_t thread) {
    void* raw = nullptr;
    const int code = pthread_join(thread, &raw);
    if (code != 0) {
        // Do not unwind storage possibly still used by a live worker.
        std::cerr << "pthread_join failed: " << code << '\n';
        std::terminate();
    }
    std::unique_ptr<double> value(static_cast<double*>(raw));
    if (!value) throw std::bad_alloc();
    return *value;
}
}
double posix_integral(Index n, unsigned threads) {
    validate(n, threads);
    const double h = (B - A) / static_cast<double>(n);
    std::vector<pthread_t> handles(threads);
    std::vector<Arguments> args(threads);
    unsigned created = 0;
    int create_error = 0;
    for (; created < threads; ++created) {
        range(n, threads, created, args[created].first, args[created].last);
        args[created].a = A; args[created].h = h;
        create_error = pthread_create(&handles[created], nullptr, worker, &args[created]);
        if (create_error != 0) break;
    }
    double sum = 0.0, correction = 0.0;
    bool allocation_failed = false;
    for (unsigned t = 0; t < created; ++t) {
        try {
            const double y = join_result(handles[t]) - correction;
            const double next = sum + y;
            correction = (next - sum) - y; sum = next;
        } catch (const std::bad_alloc&) {
            allocation_failed = true;
        }
    }
    if (create_error)
        throw std::runtime_error("pthread_create failed: " + std::to_string(create_error));
    if (allocation_failed) throw std::bad_alloc();
    return sum * h;
}
}
