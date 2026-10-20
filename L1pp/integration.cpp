#include "integration.hpp"
#include <cmath>
#include <stdexcept>
namespace lab {
double function(double x) {
    return (1.0 + x * x) / (x * x * x * std::sqrt(2.0 + x));
}
double partial_sum(Index first, Index last, double a, double h) {
    // Kahan summation limits roundoff without changing the quadrature rule.
    double sum = 0.0, correction = 0.0;
    for (Index i = first; i < last; ++i) {
        const double value = function(a + static_cast<double>(i + 1) * h);
        const double y = value - correction;
        const double next = sum + y;
        correction = (next - sum) - y;
        sum = next;
    }
    return sum;
}
void validate(Index n, unsigned threads) {
    // Internal limit also permits the 2N accuracy check for user N <= 10^12.
    if (n == 0 || n > 2000000000000ULL)
        throw std::invalid_argument("Internal N must be in [1, 2*10^12]");
    if (threads == 0 || threads > 256 || threads > n)
        throw std::invalid_argument("Threads must be in [1, min(N,256)]");
}
void range(Index n, unsigned threads, unsigned id, Index& first, Index& last) {
    // Distribute the remainder: block sizes differ by at most one.
    const Index q = n / threads, r = n % threads;
    first = id * q + (id < r ? id : r);
    last = first + q + (id < r ? 1 : 0);
}
double sequential(Index n) {
    validate(n, 1);
    const double h = (B - A) / static_cast<double>(n);
    return h * partial_sum(0, n, A, h);
}
double reference_integral() {
    const auto primitive = [](double x) {
        const double t = std::sqrt(x + 2.0), r = std::sqrt(2.0);
        return 35.0 / (32.0 * r) * std::log((t - r) / (t + r))
             + t * (3.0 / (16.0 * x) - 1.0 / (4.0 * x * x));
    };
    return primitive(B) - primitive(A);
}
}
