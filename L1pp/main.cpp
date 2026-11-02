#include "integration.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using lab::Index;
using Clock = std::chrono::steady_clock;
using Integrator = double (*)(Index, unsigned, std::vector<lab::ThreadResult>*);
struct Sample { double seconds, value; };

Index number(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Ожидается положительное целое число: " + text);
    Index value;
    try {
        value = std::stoull(text);
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("Введённое число слишком велико: " + text);
    }
    if (value == 0) throw std::invalid_argument("Ожидается положительное целое число");
    return value;
}
Index input(const char* prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) throw std::runtime_error("Ввод завершён до получения числа");
    return number(line);
}
Sample timed(Integrator integrate, Index n, unsigned p,
             std::vector<lab::ThreadResult>* details = nullptr) {
    const auto start = Clock::now();
    const double value = integrate(n, p, details);
    const auto stop = Clock::now();
    const double seconds = std::chrono::duration<double>(stop - start).count();
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
double efficiency(double useful_seconds, double total_seconds) {
    if (!std::isfinite(useful_seconds) || useful_seconds < 0.0
        || !std::isfinite(total_seconds) || total_seconds <= 0.0)
        throw std::runtime_error("Некорректное время для вычисления эффективности потока");
    return std::clamp(useful_seconds / total_seconds, 0.0, 1.0) * 100.0;
}
std::string formatted(double value, int width, int precision, bool scientific_notation = false) {
    if (!std::isfinite(value))
        throw std::runtime_error("Невозможно вывести неконечное числовое значение");
    if (value != 0.0 && std::abs(value) < 0.5 * std::pow(10.0, -precision))
        scientific_notation = true;
    for (int digits = precision; digits >= 0; --digits) {
        std::ostringstream out;
        out << (scientific_notation ? std::scientific : std::fixed)
            << std::setprecision(digits) << value;
        const std::string text = out.str();
        if (text.size() <= static_cast<std::size_t>(width))
            return std::string(static_cast<std::size_t>(width) - text.size(), ' ') + text;
        scientific_notation = true;
    }
    throw std::runtime_error("Числовое значение не помещается в столбец таблицы");
}
double serial_adapter(Index n, unsigned, std::vector<lab::ThreadResult>*) {
    return lab::sequential(n);
}
void line() {
    std::cout << "+---------+---------------+--------------+--------------------+------------+\n";
}
void table(const char* technology, const std::vector<lab::ThreadResult>& details,
           Sample total, double h, std::ofstream& summary) {
    std::cout << "\nДАННЫЕ КАЖДОГО ПОТОКА: " << technology << '\n'
              << "Полное время параллельного расчёта: " << formatted(total.seconds, 12, 9)
              << " с; общий интеграл: " << formatted(total.value, 18, 15) << '\n';
    line();
    std::cout << "| Поток   | Прямоугольн.  | Вычисл., с   | Вклад в интеграл   | Эффект., % |\n";
    line();
    for (std::size_t t = 0; t < details.size(); ++t) {
        const auto& r = details[t];
        const double contribution = h * r.sum;
        const double eff = efficiency(r.seconds, total.seconds);
        std::cout << "| " << std::setw(7) << t + 1 << " | "
                  << std::setw(13) << r.last - r.first << " | "
                  << formatted(r.seconds, 12, 9) << " | "
                  << formatted(contribution, 18, 15) << " | "
                  << formatted(eff, 10, 2) << " |\n";
        summary << technology << ',' << t + 1 << ',' << r.first + 1 << ',' << r.last << ','
                << r.last - r.first << ',' << r.seconds << ',' << contribution << ','
                << total.seconds << ',' << eff << '\n';
    }
    line();
}
void self_test() {
    if (efficiency(4.0, 4.0) != 100.0 || efficiency(0.0, 4.0) != 0.0
        || efficiency(2.0, 4.0) != 50.0 || speedup(4.0, 2.0) != 2.0)
        throw std::runtime_error("Проверка ускорения и границ эффективности не пройдена");
    bool zero_time_rejected = false;
    try { (void)speedup(1.0, 0.0); }
    catch (const std::runtime_error&) { zero_time_rejected = true; }
    if (!zero_time_rejected)
        throw std::runtime_error("Проверка защиты от нулевого времени не пройдена");
    for (double value : {0.0, 1e-12, 1e12, 1e100}) {
        const auto text = formatted(value, 10, 4);
        if (text.size() != 10 || (value != 0.0 && std::stod(text) == 0.0))
            throw std::runtime_error("Проверка ширины и точности вывода не пройдена");
    }
    const double ref = lab::reference_integral();
    for (Index n : {Index(1), Index(2), Index(7), Index(31), Index(1000), Index(10000)}) {
        const double serial = lab::sequential(n);
        if (!(serial < ref && ref - serial <= 1.16015625 / static_cast<double>(n)))
            throw std::runtime_error("Нарушена теоретическая граница погрешности");
        for (unsigned p = 1; p <= std::min<Index>(n, 8); ++p) {
            Index previous = 0;
            for (unsigned t = 0; t < p; ++t) {
                Index first, last;
                lab::range(n, p, t, first, last);
                if (first != previous || last <= first)
                    throw std::runtime_error("Ошибка распределения индексов между потоками");
                previous = last;
            }
            if (previous != n) throw std::runtime_error("Обработаны не все индексы разбиения");
            for (auto fn : {lab::posix_integral, lab::std_integral}) {
                std::vector<lab::ThreadResult> details;
                const auto total = timed(fn, n, p, &details);
                if (std::abs(total.value - serial) > 2e-14)
                    throw std::runtime_error("Параллельная сумма не совпадает с последовательной");
                if (details.size() != p)
                    throw std::runtime_error("Получены данные не всех рабочих потоков");
                Index covered = 0;
                double contributions = 0.0;
                for (const auto& detail : details) {
                    if (detail.first != covered || detail.last <= detail.first
                        || !std::isfinite(detail.sum) || !std::isfinite(detail.seconds)
                        || detail.seconds < 0.0 || detail.seconds > total.seconds)
                        throw std::runtime_error("Некорректные данные рабочего потока");
                    covered = detail.last;
                    contributions += detail.sum * (lab::B - lab::A) / static_cast<double>(n);
                }
                if (covered != n || std::abs(contributions - total.value) > 2e-14)
                    throw std::runtime_error("Сумма вкладов потоков не совпадает с интегралом");
            }
        }
    }
    const double e1 = ref - lab::sequential(1000), e2 = ref - lab::sequential(2000);
    if (e1 / e2 < 1.99 || e1 / e2 > 2.01) throw std::runtime_error("Проверка порядка сходимости не пройдена");
    for (auto pair : {std::pair<Index,unsigned>{0,1}, {1,0}, {2,3}, {1000,257}}) {
        bool rejected = false;
        try { lab::validate(pair.first, pair.second); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Проверка отклонения некорректных параметров не пройдена");
    }
    std::cout << "ПРОВЕРКИ ПРОЙДЕНЫ: распределение индексов, POSIX, std::thread, "
                 "граница погрешности, сходимость, отклонение некорректного ввода, "
                 "данные каждого потока, показатели производительности, форматирование таблиц.\n";
}
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") { self_test(); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Использование:\n"
                         "L1pp [N потоки_основного_расчёта потоки_анализа [папка_результатов]]\n"
                         "Без аргументов: интерактивный ввод.\n"
                         "--self-test: проверка корректности. --help: справка.\n";
            return 0;
        }
        if (argc != 1 && argc != 4 && argc != 5)
            throw std::invalid_argument("Для просмотра справки используйте --help");
        std::cout << "ЛАБОРАТОРНАЯ РАБОТА 1. ПРАВЫЕ ПРЯМОУГОЛЬНИКИ\n"
                     "Функция: f(x) = (1+x²)/(x³·√(2+x)), отрезок [2;5]\n";
        const Index n = argc > 1 ? number(argv[1]) : input("Количество разбиений N: ");
        if (n > 1000000000000ULL)
            throw std::invalid_argument("Число разбиений N должно быть от 1 до 10^12");
        const Index main_input = argc > 1 ? number(argv[2]) : input("Потоки основного вычисления: ");
        const Index analysis_input = argc > 1 ? number(argv[3]) : input("Количество потоков для анализа работы каждого потока: ");
        if (main_input > 256 || analysis_input > 256)
            throw std::invalid_argument("Максимально допустимое число потоков: 256");
        const unsigned main_p = static_cast<unsigned>(main_input);
        const unsigned analysis_p = static_cast<unsigned>(analysis_input);
        lab::validate(n, main_p);
        lab::validate(n, analysis_p);
        const std::filesystem::path output = argc > 4 ? argv[4] : "results";
        std::filesystem::create_directories(output);
        std::ofstream summary(output / "summary.csv"), main_csv(output / "main.csv"),
                      accuracy(output / "accuracy.csv"), metadata(output / "metadata.txt");
        if (!summary || !main_csv || !accuracy || !metadata)
            throw std::runtime_error("Не удалось создать файлы результатов");
        summary << "technology,thread_id,first_node,last_node,subdivisions,compute_seconds,"
                   "integral_contribution,total_seconds,efficiency_percent\n" << std::setprecision(17);
        main_csv << "technology,threads,seconds,result,speedup_vs_direct\n" << std::setprecision(17);
        accuracy << "n,h,result,absolute_error,relative_error,runge_estimate,bound,observed_order\n"
                 << std::setprecision(17);
        const double h = (lab::B - lab::A) / static_cast<double>(n);
        const double reference = lab::reference_integral();
        std::cout << "\nЧисло разбиений N=" << n << "; шаг h=" << std::scientific << h
                  << "; потоки основного расчёта=" << main_p << "; потоки анализа=" << analysis_p
                  << "; логические потоки процессора=" << std::thread::hardware_concurrency() << '\n';
        metadata << "N=" << n << "\nmain_threads=" << main_p << "\nanalysis_threads=" << analysis_p
                 << "\nhardware_threads=" << std::thread::hardware_concurrency()
                 << "\nclock=steady_clock\nmeasurement=single_run\nwarmup=none\n"
                 << "timing=allocation+creation+computation+join+reduction\n"
                 << "worker_timing=partial_sum_elapsed_time\n"
                 << "efficiency_percent=clamp(compute_seconds/total_seconds,0,1)*100\n";
        Integrator functions[] = {lab::posix_integral, lab::std_integral};
        const char* names[] = {"POSIX", "std::thread"};
        std::cout << "\nАНАЛИЗ КАЖДОГО РАБОЧЕГО ПОТОКА\n"
                     "Эффективность = время вычислений потока / полное время расчёта его технологии × 100 %.\n"
                     "Вычисления измеряются внутри потока; полное время включает запуск, ожидание и объединение.\n"
                     "Вклад потока — часть интеграла на его участке; сумма вкладов даёт общий интеграл.\n";
        for (int api = 0; api < 2; ++api) {
            std::vector<lab::ThreadResult> details;
            const auto total = timed(functions[api], n, analysis_p, &details);
            table(names[api], details, total, h, summary);
        }
        const auto direct = timed(serial_adapter, n, 1);
        const Sample parallel_main[] = {
            timed(functions[0], n, main_p),
            timed(functions[1], n, main_p)
        };
        std::cout << "\nОСНОВНОЕ ВЫЧИСЛЕНИЕ (ускорение относительно прямого последовательного цикла)\n"
                     "+--------------+---------+--------------+--------------------+------------+\n"
                     "| Реализация   | Потоки  | Время, с     | Интеграл           | Ускорение  |\n"
                     "+--------------+---------+--------------+--------------------+------------+\n";
        const auto main_row = [&](const char* name, unsigned p, Sample s) {
            const double speed = speedup(direct.seconds, s.seconds);
            std::cout << "| ";
            if (std::string(name) == "direct") std::cout << "Последоват. ";
            else std::cout << std::setw(12) << name;
            std::cout << " | " << std::setw(7) << p << " | "
                      << formatted(s.seconds, 12, 9) << " | "
                      << formatted(s.value, 18, 15) << " | "
                      << formatted(speed, 10, 4) << " |\n";
            main_csv << name << ',' << p << ',' << s.seconds << ',' << s.value << ',' << speed << '\n';
        };
        main_row("direct", 1, direct);
        for (int api = 0; api < 2; ++api)
            main_row(names[api], main_p, parallel_main[api]);
        std::cout << "+--------------+---------+--------------+--------------------+------------+\n";
        std::cout << "\nТОЧНОСТЬ И ШАГ (метод первого порядка, O(h))\n"
                     "+---------------+--------------+--------------------+--------------+--------------+\n"
                     "| Разбиения N   | Шаг h        | Интеграл           | Абс. ошибка  | Оценка Рунге |\n"
                     "+---------------+--------------+--------------------+--------------+--------------+\n";
        std::vector<Index> sizes = {10, 100, 1000, 10000, 100000, 1000000, n};
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        for (Index size : sizes) {
            const double coarse = size == n ? direct.value : lab::sequential(size);
            const double fine = lab::sequential(2 * size);
            const double err = std::abs(reference - coarse);
            const double fine_err = std::abs(reference - fine);
            const double runge = std::abs(fine - coarse);
            accuracy << size << ',' << 3.0 / static_cast<double>(size) << ',' << coarse << ',' << err << ','
                     << err / std::abs(reference) << ',' << runge << ','
                     << 1.16015625 / static_cast<double>(size) << ',' << std::log2(err / fine_err) << '\n';
            std::cout << "| " << std::setw(13) << size << " | "
                      << formatted(3.0 / static_cast<double>(size), 12, 5, true) << " | "
                      << formatted(coarse, 18, 15) << " | "
                      << formatted(err, 12, 5, true) << " | "
                      << formatted(runge, 12, 5, true) << " |\n";
        }
        std::cout << "+---------------+--------------+--------------------+--------------+--------------+\n"
                  << "Оценка Рунге относится к ошибке уточнённого результата I(2N).\nЭталон: " << std::fixed
                  << std::setprecision(16) << reference << "\nГраница ошибки: " << std::scientific << std::setprecision(6)
                  << 1.16015625 / static_cast<double>(n)
                  << "\nДля точности ε: N ≥ ⌈1,16015625/ε⌉, шаг h=3/N.\nФайлы результатов сохранены в папке: "
                  << output.string() << '\n';
        return 0;
    } catch (const std::bad_alloc&) {
        std::cerr << "Ошибка: недостаточно памяти для выполнения вычислений.\n";
        return 1;
    } catch (const std::system_error& e) {
        std::cerr << "Ошибка системной операции (код " << e.code().value() << ").\n";
        return 1;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    } catch (const std::exception&) {
        std::cerr << "Ошибка: не удалось выполнить программу.\n";
        return 1;
    }
}
