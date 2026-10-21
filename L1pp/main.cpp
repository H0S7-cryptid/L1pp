#include "integration.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <random>
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
using Integrator = double (*)(Index, unsigned);
struct Sample { double seconds, value; };
struct Row { int api; unsigned threads; double seconds, value, speedup, efficiency; };

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
Sample timed(Integrator integrate, Index n, unsigned p) {
    const auto start = Clock::now();
    const double value = integrate(n, p);
    const auto stop = Clock::now();
    return {std::chrono::duration<double>(stop - start).count(), value};
}
double serial_adapter(Index n, unsigned) { return lab::sequential(n); }
Sample median(std::vector<Sample> values) {
    std::sort(values.begin(), values.end(), [](Sample a, Sample b) {
        return a.seconds < b.seconds;
    });
    return values[values.size() / 2];
}
void line() {
    std::cout << "+---------+--------------+--------------------+------------+------------+\n";
}
void table(const std::string& title, const std::vector<Row>& rows, int api) {
    std::cout << '\n' << title << '\n';
    line();
    std::cout << "| Потоки  | Время, с     | Интеграл           | Ускорение  | Эффект., % |\n";
    line();
    for (const auto& r : rows) if (r.api == api) {
        std::cout << "| " << std::setw(7) << r.threads << " | "
                  << std::fixed << std::setprecision(9) << std::setw(12) << r.seconds << " | "
                  << std::setprecision(15) << std::setw(18) << r.value << " | "
                  << std::setprecision(4) << std::setw(10) << r.speedup << " | "
                  << std::setprecision(2) << std::setw(10) << r.efficiency << " |\n";
    }
    line();
}
void self_test() {
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
            for (auto fn : {lab::posix_integral, lab::std_integral})
                if (std::abs(fn(n, p) - serial) > 2e-14)
                    throw std::runtime_error("Параллельная сумма не совпадает с последовательной");
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
                 "граница погрешности, сходимость, отклонение некорректного ввода.\n";
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
                         "L1pp [N потоки_расчёта максимум_потоков [нечётное_число_повторов [папка_результатов]]]\n"
                         "Без аргументов: интерактивный ввод.\n"
                         "--self-test: проверка корректности. --help: справка.\n";
            return 0;
        }
        if (argc != 1 && (argc < 4 || argc > 6)) throw std::invalid_argument("Для просмотра справки используйте --help");
        std::cout << "ЛАБОРАТОРНАЯ РАБОТА 1. ПРАВЫЕ ПРЯМОУГОЛЬНИКИ\n"
                     "Функция: f(x) = (1+x²)/(x³·√(2+x)), отрезок [2;5]\n";
        const Index n = argc > 1 ? number(argv[1]) : input("Количество разбиений N: ");
        if (n > 1000000000000ULL) throw std::invalid_argument("Число разбиений N должно быть от 1 до 10^12");
        const Index main_input = argc > 1 ? number(argv[2]) : input("Потоки основного вычисления: ");
        const Index max_input = argc > 1 ? number(argv[3]) : input("Максимум потоков для тестирования: ");
        const Index repeats_input = argc > 4 ? number(argv[4]) : 5;
        if (main_input > 256 || max_input > 256)
            throw std::invalid_argument("Максимально допустимое число потоков: 256");
        if (repeats_input > 99 || repeats_input % 2 == 0)
            throw std::invalid_argument("Число повторов должно быть нечётным и находиться в диапазоне от 1 до 99");
        const unsigned main_p = static_cast<unsigned>(main_input);
        const unsigned max_p = static_cast<unsigned>(max_input);
        const unsigned repeats = static_cast<unsigned>(repeats_input);
        lab::validate(n, main_p); lab::validate(n, max_p);
        const std::filesystem::path output = argc > 5 ? argv[5] : "results";
        std::filesystem::create_directories(output);
        std::ofstream raw(output / "raw.csv"), summary(output / "summary.csv"),
                      main_csv(output / "main.csv"), accuracy(output / "accuracy.csv"),
                      metadata(output / "metadata.txt");
        if (!raw || !summary || !main_csv || !accuracy || !metadata)
            throw std::runtime_error("Не удалось создать файлы результатов");
        raw << "technology,threads,repeat,seconds,result\n" << std::setprecision(17);
        summary << "technology,threads,seconds,result,speedup,efficiency_percent\n"
                << std::setprecision(17);
        main_csv << "technology,threads,seconds,result,speedup_vs_direct\n"
                 << std::setprecision(17);
        accuracy << "n,h,result,absolute_error,relative_error,runge_estimate,bound,observed_order\n"
                 << std::setprecision(17);
        const double h = 3.0 / static_cast<double>(n);
        const double reference = lab::reference_integral();
        std::cout << "\nЧисло разбиений N=" << n << "; шаг h=" << std::scientific << h
                  << "; потоки расчёта=" << main_p << "; максимум потоков=" << max_p
                  << "; повторы=" << repeats << "; логические потоки процессора="
                  << std::thread::hardware_concurrency() << "\n";
        metadata << "N=" << n << "\nmain_threads=" << main_p << "\nmax_threads=" << max_p
                 << "\nrepeats=" << repeats << "\nhardware_threads="
                 << std::thread::hardware_concurrency()
                 << "\nclock=steady_clock\nstatistic=median\nwarmup=one_per_configuration\n"
                 << "shuffle_seed=412\ntiming=allocation+creation+computation+join+reduction\n";
        Integrator functions[] = {lab::posix_integral, lab::std_integral};
        const char* names[] = {"POSIX", "std::thread"};
        for (auto fn : functions)
            for (unsigned p = 1; p <= max_p; ++p) (void)fn(n, p);
        (void)lab::sequential(n);
        std::vector<Sample> serial_samples;
        std::vector<std::vector<std::vector<Sample>>> samples(2,
            std::vector<std::vector<Sample>>(max_p + 1));
        std::vector<std::pair<int, unsigned>> schedule;
        for (int api = 0; api < 2; ++api)
            for (unsigned p = 1; p <= max_p; ++p) schedule.emplace_back(api, p);
        std::mt19937 generator(412);
        for (unsigned repeat = 1; repeat <= repeats; ++repeat) {
            std::shuffle(schedule.begin(), schedule.end(), generator);
            const auto direct = timed(serial_adapter, n, 1);
            serial_samples.push_back(direct);
            raw << "direct,1," << repeat << ',' << direct.seconds << ',' << direct.value << '\n';
            for (auto config : schedule) {
                const auto s = timed(functions[config.first], n, config.second);
                samples[config.first][config.second].push_back(s);
                raw << names[config.first] << ',' << config.second << ',' << repeat
                    << ',' << s.seconds << ',' << s.value << '\n';
            }
            std::cout << "Повтор " << repeat << '/' << repeats << " завершён.\n" << std::flush;
        }
        const auto direct = median(serial_samples);
        std::vector<Row> rows;
        for (int api = 0; api < 2; ++api) {
            const double baseline = median(samples[api][1]).seconds;
            for (unsigned p = 1; p <= max_p; ++p) {
                const auto s = median(samples[api][p]);
                const double speed = baseline / s.seconds, eff = 100.0 * speed / p;
                rows.push_back({api, p, s.seconds, s.value, speed, eff});
                summary << names[api] << ',' << p << ',' << s.seconds << ',' << s.value
                        << ',' << speed << ',' << eff << '\n';
            }
            table(std::string("ТЕСТИРОВАНИЕ ") + names[api] + " (медиана; ускорение T1/Tp)", rows, api);
        }
        std::cout << "\nОСНОВНОЕ ВЫЧИСЛЕНИЕ (ускорение относительно прямого последовательного цикла)\n"
                     "+--------------+---------+--------------+--------------------+------------+\n"
                     "| Реализация   | Потоки  | Время, с     | Интеграл           | Ускорение  |\n"
                     "+--------------+---------+--------------+--------------------+------------+\n";
        const auto main_row = [&](const char* name, unsigned p, Sample s) {
            const double speed = direct.seconds / s.seconds;
            std::cout << "| ";
            if (std::string(name) == "direct") std::cout << "Последоват. ";
            else std::cout << std::setw(12) << name;
            std::cout << " | " << std::setw(7) << p << " | "
                      << std::fixed << std::setprecision(9) << std::setw(12) << s.seconds << " | "
                      << std::setprecision(15) << std::setw(18) << s.value << " | "
                      << std::setprecision(4) << std::setw(10) << speed << " |\n";
            main_csv << name << ',' << p << ',' << s.seconds << ',' << s.value << ',' << speed << '\n';
        };
        main_row("direct", 1, direct);
        for (int api = 0; api < 2; ++api) {
            Sample s{};
            if (main_p <= max_p) s = median(samples[api][main_p]);
            else {
                (void)functions[api](n, main_p);
                std::vector<Sample> extra;
                for (unsigned r = 1; r <= repeats; ++r) {
                    const auto run = timed(functions[api], n, main_p);
                    extra.push_back(run);
                    raw << names[api] << ',' << main_p << ',' << r << ','
                        << run.seconds << ',' << run.value << '\n';
                }
                s = median(extra);
            }
            main_row(names[api], main_p, s);
        }
        std::cout << "+--------------+---------+--------------+--------------------+------------+\n";
        std::cout << "\nТОЧНОСТЬ И ШАГ (метод первого порядка, O(h))\n"
                     "+--------------+--------------+--------------------+--------------+--------------+\n"
                     "| Разбиения N  | Шаг h        | Интеграл           | Абс. ошибка  | Оценка Рунге |\n"
                     "+--------------+--------------+--------------------+--------------+--------------+\n";
        std::vector<Index> sizes = {10, 100, 1000, 10000, 100000, 1000000, n};
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
        for (Index size : sizes) {
            const double coarse = lab::sequential(size), fine = lab::sequential(2 * size);
            const double err = std::abs(reference - coarse);
            const double fine_err = std::abs(reference - fine);
            const double runge = std::abs(fine - coarse);
            accuracy << size << ',' << 3.0 / static_cast<double>(size) << ',' << coarse << ',' << err << ','
                     << err / std::abs(reference) << ',' << runge << ','
                     << 1.16015625 / static_cast<double>(size) << ',' << std::log2(err / fine_err) << '\n';
            std::cout << "| " << std::setw(12) << size << " | " << std::scientific << std::setprecision(5)
                      << std::setw(12) << 3.0 / static_cast<double>(size) << " | " << std::fixed
                      << std::setprecision(15) << std::setw(18) << coarse << " | " << std::scientific
                      << std::setprecision(5) << std::setw(12) << err << " | "
                      << std::setw(12) << runge << " |\n";
        }
        std::cout << "+--------------+--------------+--------------------+--------------+--------------+\n"
                  << "Оценка Рунге относится к ошибке уточнённого результата I(2N).\nЭталон: " << std::fixed
                  << std::setprecision(16) << reference << "\nГраница ошибки: " << std::scientific
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
