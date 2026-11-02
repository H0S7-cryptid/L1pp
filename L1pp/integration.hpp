#pragma once
#include <cstdint>
#include <vector>
namespace lab {
using Index = std::uint64_t;
constexpr double A = 2.0, B = 5.0;
constexpr double derivative_bound = 33.0 / 128.0;
double function(double x);
double partial_sum(Index first, Index last, double a, double h);
double sequential(Index n);
struct ThreadResult {
    Index first = 0, last = 0;
    double sum = 0.0, seconds = 0.0;
};
double posix_integral(Index n, unsigned threads, std::vector<ThreadResult>* details = nullptr);
double std_integral(Index n, unsigned threads, std::vector<ThreadResult>* details = nullptr);
double reference_integral();
void validate(Index n, unsigned threads);
void range(Index n, unsigned threads, unsigned id, Index& first, Index& last);
}
