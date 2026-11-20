#include "integration.hpp"
#if defined(_MSC_VER) && _MSC_VER >= 1900
#include <time.h>
#define HAVE_STRUCT_TIMESPEC
#endif
#include <pthread.h>
#include <chrono>
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

    auto* result = new (std::nothrow) double;
    if (result) *result = partial_sum(args.first, args.last, args.a, args.h);
    return result;
}
double join_result(pthread_t thread) {
    void* raw = nullptr;
    const int code = pthread_join(thread, &raw);
    if (code != 0) {
        std::cerr << "Ошибка ожидания потока pthread_join (код " << code << ").\n";
        std::terminate();
    }
    std::unique_ptr<double> value(static_cast<double*>(raw));
    if (!value) throw std::bad_alloc();
    return *value;
}
}
double posix_integral(Index n, unsigned threads, double& elapsed_seconds) {
    validate(n, threads);
    const double h = (B - A) / static_cast<double>(n);
    std::vector<pthread_t> handles(threads);
    std::vector<Arguments> args(threads);
    std::vector<double> results(threads);
    for (unsigned t = 0; t < threads; ++t) {
        range(n, threads, t, args[t].first, args[t].last);
        args[t].a = A; args[t].h = h;
    }
    unsigned created = 0;
    int create_error = 0;
    const auto start = std::chrono::steady_clock::now();
    for (; created < threads; ++created) {
        create_error = pthread_create(&handles[created], nullptr, worker, &args[created]);
        if (create_error != 0) break;
    }
    bool allocation_failed = false;
    for (unsigned t = 0; t < created; ++t) {
        try {
            results[t] = join_result(handles[t]);
        } catch (const std::bad_alloc&) {
            allocation_failed = true;
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    elapsed_seconds = std::chrono::duration<double>(stop - start).count();
    if (create_error)
        throw std::runtime_error("Не удалось создать поток pthread_create (код " + std::to_string(create_error) + ")");
    if (allocation_failed) throw std::bad_alloc();
    double sum = 0.0, correction = 0.0;
    for (const auto& result : results) {
        const double y = result - correction;
        const double next = sum + y;
        correction = (next - sum) - y; sum = next;
    }
    return sum * h;
}
}
