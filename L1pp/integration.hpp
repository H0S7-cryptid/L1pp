#pragma once
#include <cstdint>
namespace lab {
using Index = std::uint64_t;
constexpr double A = 2.0, B = 5.0;
constexpr double derivative_bound = 33.0 / 128.0;
double function(double x);
double partial_sum(Index first, Index last, double a, double h);
double sequential(Index n);
double posix_integral(Index n, unsigned threads);
double std_integral(Index n, unsigned threads);
double reference_integral();
void validate(Index n, unsigned threads);
void range(Index n, unsigned threads, unsigned id, Index& first, Index& last);
}
