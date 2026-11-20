#include "integration.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using lab::Index;
using Integrator = double (*)(Index, unsigned, double&);
constexpr unsigned REPEATS = 5;
constexpr double REQUIRED_ACCURACY = 1e-6;
struct Sample { double seconds, value; };
struct Row { unsigned threads; Sample sample; double speedup, efficiency; };

Index number(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Ожидается положительное целое число");
    Index value;
    try {
        value = std::stoull(text);
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("Введённое число слишком велико");
    }
    if (value == 0) throw std::invalid_argument("Ожидается положительное целое число");
    return value;
}

Index input(const char* prompt, Index maximum) {
    for (;;) {
        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line))
            throw std::runtime_error("Ввод завершён до получения числа");
        try {
            const Index value = number(line);
            if (value > maximum)
                throw std::invalid_argument("Допустимый диапазон: от 1 до " + std::to_string(maximum));
            return value;
        } catch (const std::invalid_argument& e) {
            std::cout << "  Ошибка: " << e.what() << ". Повторите ввод.\n";
        }
    }
}

Sample timed(Integrator integrate, Index n, unsigned p) {
    double seconds = 0.0;
    const double value = integrate(n, p, seconds);
    if (!std::isfinite(seconds) || seconds <= 0.0)
        throw std::runtime_error("Время вычисления неразличимо таймером. Увеличьте число разбиений");
    if (!std::isfinite(value))
        throw std::runtime_error("Получено неконечное значение интеграла");
    return {seconds, value};
}

double speedup(double baseline, double seconds) {
    if (!std::isfinite(baseline) || !std::isfinite(seconds) || baseline <= 0.0 || seconds <= 0.0)
        throw std::runtime_error("Невозможно вычислить ускорение: время должно быть положительным и конечным");
    const double value = baseline / seconds;
    if (!std::isfinite(value)) throw std::runtime_error("Получено неконечное значение ускорения");
    return value;
}

double efficiency(double acceleration, unsigned threads) {
    if (!std::isfinite(acceleration) || acceleration < 0.0 || threads == 0)
        throw std::runtime_error("Некорректные данные для расчёта эффективности");
    const double value = acceleration / threads * 100.0;
    if (!std::isfinite(value)) throw std::runtime_error("Получено неконечное значение эффективности");
    return value;
}

Sample median(std::vector<Sample> samples) {
    if (samples.empty()) throw std::invalid_argument("Нет измерений для вычисления медианы");
    std::sort(samples.begin(), samples.end(), [](Sample a, Sample b) { return a.seconds < b.seconds; });
    return samples[samples.size() / 2];
}

Sample measure(Integrator integrate, Index n, unsigned p) {
    std::vector<Sample> samples;
    samples.reserve(REPEATS);
    for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
        const auto sample = timed(integrate, n, p);
        if (!samples.empty() && std::abs(sample.value - samples.front().value) > 2e-14)
            throw std::runtime_error("Результат меняется между повторами одной конфигурации");
        samples.push_back(sample);
    }
    return median(std::move(samples));
}

void warmup(Integrator integrate, Index n, unsigned main_p, unsigned max_p) {
    (void)measure(integrate, n, 1);
    (void)measure(integrate, n, std::max<unsigned>(main_p, max_p));
}

Row row(unsigned p, Sample sample, double baseline) {
    const double acceleration = speedup(baseline, sample.seconds);
    return {p, sample, acceleration, efficiency(acceleration, p)};
}

std::vector<Row> benchmark(Integrator integrate, Index n, unsigned max_p) {
    std::vector<Row> rows;
    rows.reserve(max_p);
    double baseline = 0.0;
    for (unsigned p = 1; p <= max_p; ++p) {
        const auto sample = measure(integrate, n, p);
        if (p == 1) baseline = sample.seconds;
        if (!rows.empty() && std::abs(sample.value - rows.front().sample.value) > 2e-14)
            throw std::runtime_error("Результат зависит от числа потоков сверх допустимого округления");
        rows.push_back(row(p, sample, baseline));
    }
    return rows;
}

void table(const std::vector<Row>& rows) {
    std::cout << "Потоки  Время (с)      Результат          Ускорение     Эффективность (%)\n";
    for (const auto& r : rows)
        std::cout << std::left << std::setw(8) << r.threads
                  << std::fixed << std::setprecision(9) << std::setw(15) << r.sample.seconds
                  << std::setprecision(15) << std::setw(19) << r.sample.value
                  << std::setprecision(6) << std::setw(14) << r.speedup
                  << std::setprecision(4) << r.efficiency << '\n';
}

void accuracy(double result) {
    const double reference = lab::reference_integral();
    const double error = std::abs(result - reference);
    std::cout << "Значение по первообразной: " << std::fixed << std::setprecision(16) << reference << '\n'
              << "Абсолютная погрешность: " << std::scientific << std::setprecision(9) << error << '\n'
              << "Точность 1e-6: "
              << (error < REQUIRED_ACCURACY ? "достигнута" : "не достигнута") << '\n';
}
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        std::cout << "Лабораторная работа № 1. Параллельное интегрирование\n"
                     "Метод правых прямоугольников\n"
                     "f(x) = (1 + x²) / (x³ · √(2 + x)), отрезок [2; 5]\n\n";
        const Index n = input("Количество разбиений N (1–10^12): ", 1000000000000ULL);
        const unsigned main_p = static_cast<unsigned>(input("Потоки основного расчёта (1–256): ", 256));
        const unsigned max_p = static_cast<unsigned>(input("Максимум потоков для тестирования (1–256): ", 256));
        std::cout << "\nN = " << n << "; шаг h = "
                  << std::setprecision(9) << (lab::B - lab::A) / static_cast<double>(n) << '\n'
                  << "Ускорение = T(1)/T(p), эффективность = ускорение/p × 100 %.\n";
        Integrator functions[] = {lab::posix_integral, lab::std_integral};
        const char* names[] = {"POSIX", "std::thread"};
        for (int api = 0; api < 2; ++api) {
            std::cout << '\n' << names[api] << "\nПрогрев процессора...\n" << std::flush;
            warmup(functions[api], n, main_p, max_p);

            std::cout << "\nТестирование производительности (медиана 5 запусков)\n" << std::flush;
            table(benchmark(functions[api], n, max_p));

            std::cout << "\nОсновной расчёт (отдельные замеры на 1 и " << main_p << " потоках)\n" << std::flush;
            const auto baseline = measure(functions[api], n, 1);
            const auto main = measure(functions[api], n, main_p);
            if (std::abs(main.value - baseline.value) > 2e-14)
                throw std::runtime_error("Результаты основного и однопоточного расчёта не совпали");
            const auto main_row = row(main_p, main, baseline.seconds);
            std::cout << std::fixed << std::setprecision(9)
                      << "1 поток: время = " << baseline.seconds << " с, результат = "
                      << std::setprecision(15) << baseline.value << '\n'
                      << main_p << " поток(ов): время = " << std::setprecision(9) << main.seconds
                      << " с, результат = " << std::setprecision(15) << main.value << '\n'
                      << "Фактическое ускорение: " << std::setprecision(6) << main_row.speedup << '\n'
                      << "Идеальное ускорение: " << main_p << '\n'
                      << "Эффективность: " << std::setprecision(4) << main_row.efficiency << " %\n"
                      << "\nТочность\n";
            accuracy(main.value);
        }
        std::cout << "\nВычисления завершены.\n";
        return 0;
    } catch (const std::bad_alloc&) {
        std::cerr << "\nОшибка: недостаточно памяти для выполнения вычислений.\n";
    } catch (const std::system_error& e) {
        std::cerr << "\nОшибка системной операции (код " << e.code().value() << ").\n";
    } catch (const std::exception& e) {
        std::cerr << "\nОшибка: " << e.what() << '\n';
    }
    return 1;
}
