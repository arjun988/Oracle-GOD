/*
====================================================================
    TSC / HUMAN TIMING EXPERIMENT LAB v2
====================================================================

PURPOSE
-------

Investigate a specific, testable intuition:

    Human action
         |
         v
    Timing variation
         |
         v
       TSC
         |
         v
   measurable output
         |
         v
  Is it predictable?

This program does NOT assume that unusual results are supernatural.

It attempts to separate:

    1. Human timing variability
    2. Machine timing variability
    3. TSC statistical behavior
    4. Predictability
    5. TSC-derived PRNG behavior
    6. Deterministic PRNG control behavior

IMPORTANT
---------

This is NOT the TempleOS kernel.

The RNG included here is an experimental / TempleOS-inspired
TSC-mixed generator and should not be described as a bit-for-bit
reconstruction of Terry Davis' implementation.

BUILD
-----

Windows / MinGW:

    g++ -O2 -std=c++17 tsc_experiment_v2.cpp -o tsc_experiment_v2.exe

Linux:

    g++ -O2 -std=c++17 tsc_experiment_v2.cpp -o tsc_experiment_v2

MSVC:

    cl /O2 /std:c++17 tsc_experiment_v2.cpp

RECOMMENDED ORDER
-----------------

    1   Raw TSC
    2   Machine timing
    3   Human timing
    4   Human prediction
    5   Machine prediction control
    6   TSC low-bit analysis
    7   RNG comparison
    8   Reproducibility

====================================================================
*/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <random>
#include <chrono>
#include <thread>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <functional>
#include <array>
#include <deque>
#include <cstdlib>

#ifdef _MSC_VER
    #include <intrin.h>
    #include <windows.h>
#else
    #include <x86intrin.h>
    #include <unistd.h>
#endif


// ============================================================
// CONFIGURATION
// ============================================================

static constexpr uint64_t TEMPLE_A =
    6364136223846793005ULL;

static constexpr uint64_t TEMPLE_C =
    1442695040888963407ULL;

static constexpr size_t DEFAULT_HUMAN_TRIALS = 1000;
static constexpr size_t DEFAULT_MACHINE_TRIALS = 100000;


// ============================================================
// TSC
// ============================================================

static inline uint64_t read_tsc()
{
#ifdef _MSC_VER
    return __rdtsc();
#elif defined(__GNUC__) || defined(__clang__)
    return __rdtsc();
#else
#error "Unsupported compiler / architecture"
#endif
}


/*
    LFENCE + RDTSC.

    This is lighter than CPUID serialization and is generally useful
    for timing measurements on modern x86 CPUs.

    The exact microarchitectural behavior depends on the CPU.
*/

static inline uint64_t read_tsc_serialized()
{
#if defined(_MSC_VER)

    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    return t;

#elif defined(__GNUC__) || defined(__clang__)

    unsigned int lo;
    unsigned int hi;

    asm volatile(
        "lfence\n\t"
        "rdtsc\n\t"
        "lfence\n\t"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );

    return (static_cast<uint64_t>(hi) << 32) | lo;

#else
#error "Unsupported compiler / architecture"
#endif
}


// ============================================================
// UTILITY
// ============================================================

static void print_separator()
{
    std::cout
        << "\n============================================================\n";
}


static void wait_for_enter()
{
    std::cout << "\nPress ENTER to continue...";
    std::string line;
    std::getline(std::cin, line);
}


static bool read_size_t(
    const std::string& prompt,
    size_t& result,
    size_t default_value)
{
    std::cout << prompt
              << " [default "
              << default_value
              << "]: ";

    std::string input;
    std::getline(std::cin, input);

    if (input.empty())
    {
        result = default_value;
        return true;
    }

    try
    {
        unsigned long long x =
            std::stoull(input);

        if (x == 0)
            return false;

        result = static_cast<size_t>(x);
        return true;
    }
    catch (...)
    {
        return false;
    }
}


static std::string csv_escape(
    const std::string& s)
{
    std::string out = "\"";

    for (char c : s)
    {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }

    out += "\"";

    return out;
}


// ============================================================
// STATISTICS
// ============================================================

struct Statistics
{
    double mean = 0.0;
    double variance = 0.0;
    double stddev = 0.0;

    uint64_t min = 0;
    uint64_t max = 0;
};


static Statistics calculate_stats(
    const std::vector<uint64_t>& values)
{
    Statistics s;

    if (values.empty())
        return s;

    s.min =
        *std::min_element(
            values.begin(),
            values.end());

    s.max =
        *std::max_element(
            values.begin(),
            values.end());

    long double sum = 0.0L;

    for (uint64_t v : values)
        sum += static_cast<long double>(v);

    s.mean =
        static_cast<double>(
            sum / values.size());

    long double variance = 0.0L;

    for (uint64_t v : values)
    {
        long double d =
            static_cast<long double>(v)
            - s.mean;

        variance += d * d;
    }

    if (values.size() > 1)
        variance /= (values.size() - 1);

    s.variance =
        static_cast<double>(variance);

    s.stddev =
        std::sqrt(s.variance);

    return s;
}


// ============================================================
// ENTROPY
// ============================================================

static double shannon_entropy(
    const std::vector<uint64_t>& values)
{
    if (values.empty())
        return 0.0;

    std::unordered_map<uint64_t, uint64_t> counts;

    for (uint64_t v : values)
        counts[v]++;

    double entropy = 0.0;

    double n =
        static_cast<double>(values.size());

    for (const auto& p : counts)
    {
        double probability =
            static_cast<double>(p.second) / n;

        entropy -=
            probability *
            std::log2(probability);
    }

    return entropy;
}


static double min_entropy(
    const std::vector<uint64_t>& values)
{
    if (values.empty())
        return 0.0;

    std::unordered_map<uint64_t, uint64_t> counts;

    for (uint64_t v : values)
        counts[v]++;

    uint64_t max_count = 0;

    for (const auto& p : counts)
        max_count =
            std::max(max_count, p.second);

    double probability =
        static_cast<double>(max_count)
        / static_cast<double>(values.size());

    return -std::log2(probability);
}


// ============================================================
// CHI-SQUARE
// ============================================================

static double chi_square(
    const std::vector<uint64_t>& values,
    size_t buckets)
{
    if (values.empty() || buckets == 0)
        return 0.0;

    std::vector<uint64_t> counts(buckets, 0);

    for (uint64_t v : values)
        counts[v % buckets]++;

    double expected =
        static_cast<double>(values.size())
        / static_cast<double>(buckets);

    double chi = 0.0;

    for (uint64_t observed : counts)
    {
        double d =
            static_cast<double>(observed)
            - expected;

        chi +=
            (d * d) / expected;
    }

    return chi;
}


// ============================================================
// NORMAL APPROXIMATION
// ============================================================

/*
    Two-sided normal approximation for a binomial proportion.

    This is useful for prediction experiments.

    It is an approximation, not a replacement for an exact
    binomial test.
*/

static double normal_cdf(double x)
{
    return 0.5 *
        std::erfc(
            -x / std::sqrt(2.0));
}


static double binomial_normal_pvalue(
    size_t successes,
    size_t n,
    double p0)
{
    if (n == 0)
        return 1.0;

    double expected =
        n * p0;

    double variance =
        n * p0 * (1.0 - p0);

    if (variance <= 0.0)
        return 1.0;

    double z =
        (static_cast<double>(successes)
         - expected)
        / std::sqrt(variance);

    double p =
        2.0 *
        (1.0 - normal_cdf(std::fabs(z)));

    return std::clamp(p, 0.0, 1.0);
}


// ============================================================
// CORRELATION
// ============================================================

static double pearson_correlation(
    const std::vector<uint64_t>& a,
    const std::vector<uint64_t>& b)
{
    if (a.size() != b.size() ||
        a.size() < 2)
        return 0.0;

    long double mean_a = 0.0L;
    long double mean_b = 0.0L;

    for (size_t i = 0; i < a.size(); ++i)
    {
        mean_a +=
            static_cast<long double>(a[i]);

        mean_b +=
            static_cast<long double>(b[i]);
    }

    mean_a /= a.size();
    mean_b /= b.size();

    long double numerator = 0.0L;
    long double denominator_a = 0.0L;
    long double denominator_b = 0.0L;

    for (size_t i = 0; i < a.size(); ++i)
    {
        long double da =
            static_cast<long double>(a[i])
            - mean_a;

        long double db =
            static_cast<long double>(b[i])
            - mean_b;

        numerator += da * db;
        denominator_a += da * da;
        denominator_b += db * db;
    }

    if (denominator_a == 0.0L ||
        denominator_b == 0.0L)
        return 0.0;

    return static_cast<double>(
        numerator /
        std::sqrt(
            denominator_a *
            denominator_b));
}


// ============================================================
// BIT BALANCE
// ============================================================

struct BitStatistics
{
    uint64_t ones = 0;
    uint64_t zeros = 0;
};


static BitStatistics bit_statistics(
    const std::vector<uint64_t>& values,
    int bit)
{
    BitStatistics s;

    for (uint64_t v : values)
    {
        if ((v >> bit) & 1ULL)
            s.ones++;
        else
            s.zeros++;
    }

    return s;
}


static void print_bit_statistics(
    const std::vector<uint64_t>& values,
    int max_bit)
{
    std::cout
        << "\nBIT BALANCE\n"
        << "-----------\n";

    for (int bit = 0; bit <= max_bit; ++bit)
    {
        BitStatistics s =
            bit_statistics(values, bit);

        uint64_t total =
            s.ones + s.zeros;

        double percentage =
            total
                ? 100.0 *
                  static_cast<double>(s.ones)
                  / total
                : 0.0;

        std::cout
            << "bit "
            << std::setw(2)
            << bit
            << " : ones="
            << std::setw(8)
            << s.ones
            << " zeros="
            << std::setw(8)
            << s.zeros
            << " ones%="
            << std::fixed
            << std::setprecision(3)
            << percentage
            << "\n";
    }

    std::cout << std::defaultfloat;
}


// ============================================================
// TEMPLEOS-INSPIRED EXPERIMENTAL RNG
// ============================================================

class TempleInspiredRNG
{
private:

    uint64_t state;

public:

    explicit TempleInspiredRNG(
        uint64_t seed)
        : state(seed)
    {
    }


    uint64_t next_without_tsc()
    {
        uint64_t res = state;

        uint64_t high_mix =
            ((res & 0xFFFFFFFF0000ULL) >> 16);

        res =
            (TEMPLE_A * res)
            ^ high_mix;

        res += TEMPLE_C;

        state = res;

        return res;
    }


    uint64_t next()
    {
        uint64_t res =
            next_without_tsc();

        /*
            Inject a fresh TSC sample.

            This is deliberately an experimental construction.
        */

        res ^= read_tsc_serialized();

        state = res;

        return res;
    }


    uint64_t get_state() const
    {
        return state;
    }
};


// ============================================================
// EXPERIMENT 1
// RAW TSC
// ============================================================

void experiment_raw_tsc()
{
    print_separator();

    std::cout
        << "EXPERIMENT 1: RAW TSC\n\n";

    std::cout
        << "Reading the TSC repeatedly.\n";

    const size_t N = 100;

    std::vector<uint64_t> values;
    values.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t t =
            read_tsc_serialized();

        values.push_back(t);

        std::cout
            << std::setw(3)
            << i
            << " : "
            << t
            << "\n";
    }

    Statistics s =
        calculate_stats(values);

    std::cout
        << "\nStatistics\n"
        << "----------\n"
        << "Min     : " << s.min << "\n"
        << "Max     : " << s.max << "\n"
        << "Mean    : " << s.mean << "\n"
        << "Std Dev : " << s.stddev << "\n";
}


// ============================================================
// EXPERIMENT 2
// MACHINE TIMING
// ============================================================

void experiment_machine_timing()
{
    print_separator();

    std::cout
        << "EXPERIMENT 2: MACHINE TIMING\n\n";

    size_t N;

    if (!read_size_t(
            "Number of samples",
            N,
            DEFAULT_MACHINE_TRIALS))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    std::vector<uint64_t> tscs;
    std::vector<uint64_t> deltas;
    std::vector<uint64_t> low8;

    tscs.reserve(N);
    deltas.reserve(N);
    low8.reserve(N);

    uint64_t previous =
        read_tsc_serialized();

    for (size_t i = 0; i < N; ++i)
    {
        /*
            Small deterministic workload.

            This is intentionally NOT intended to create
            randomness. It is a control condition.
        */

        volatile uint64_t x =
            static_cast<uint64_t>(i);

        for (int j = 0; j < static_cast<int>(i % 31); ++j)
        {
            x =
                x *
                6364136223846793005ULL
                + 1;
        }

        (void)x;

        uint64_t current =
            read_tsc_serialized();

        uint64_t delta =
            current - previous;

        tscs.push_back(current);
        deltas.push_back(delta);
        low8.push_back(current & 0xFF);

        previous = current;
    }

    Statistics s =
        calculate_stats(deltas);

    std::cout
        << "\nTSC delta statistics\n"
        << "--------------------\n"
        << "Samples : " << N << "\n"
        << "Min     : " << s.min << "\n"
        << "Max     : " << s.max << "\n"
        << "Mean    : " << s.mean << "\n"
        << "Std Dev : " << s.stddev << "\n";

    std::cout
        << "\nLow-byte Shannon entropy: "
        << shannon_entropy(low8)
        << " bits\n";

    std::cout
        << "Low-byte min entropy: "
        << min_entropy(low8)
        << " bits\n";

    std::cout
        << "Low-byte chi-square: "
        << chi_square(low8, 256)
        << "\n";

    if (low8.size() > 1)
    {
        std::vector<uint64_t> a(
            low8.begin(),
            low8.end() - 1);

        std::vector<uint64_t> b(
            low8.begin() + 1,
            low8.end());

        std::cout
            << "Lag-1 low-byte correlation: "
            << pearson_correlation(a, b)
            << "\n";
    }

    std::ofstream file(
        "machine_timing_v2.csv");

    file
        << "trial,tsc,delta,low16,low8\n";

    for (size_t i = 0; i < N; ++i)
    {
        file
            << i << ","
            << tscs[i] << ","
            << deltas[i] << ","
            << (tscs[i] & 0xFFFF) << ","
            << (tscs[i] & 0xFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: machine_timing_v2.csv\n";
}


// ============================================================
// EXPERIMENT 3
// HUMAN TIMING
// ============================================================

void experiment_human_timing()
{
    print_separator();

    std::cout
        << "EXPERIMENT 3: HUMAN TIMING\n\n";

    size_t N;

    if (!read_size_t(
            "Number of ENTER trials",
            N,
            DEFAULT_HUMAN_TRIALS))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    std::cout
        << "\nInstructions:\n"
        << "\n"
        << "Press ENTER at a natural pace.\n"
        << "Do NOT deliberately alternate fast/slow.\n"
        << "Do NOT attempt to create specific numbers.\n"
        << "Just interact naturally.\n\n";

    wait_for_enter();

    std::vector<uint64_t> tscs;
    std::vector<uint64_t> deltas;
    std::vector<uint64_t> low8;

    tscs.reserve(N);
    deltas.reserve(N);
    low8.reserve(N);

    uint64_t previous = 0;

    for (size_t i = 0; i < N; ++i)
    {
        std::cout
            << "\nENTER ["
            << (i + 1)
            << "/"
            << N
            << "] ";

        std::string line;
        std::getline(std::cin, line);

        uint64_t tsc =
            read_tsc_serialized();

        uint64_t delta = 0;

        if (i > 0)
            delta = tsc - previous;

        tscs.push_back(tsc);
        deltas.push_back(delta);
        low8.push_back(tsc & 0xFF);

        previous = tsc;

        std::cout
            << "TSC   = "
            << tsc
            << "\n"
            << "Delta = "
            << delta
            << "\n"
            << "Low8  = "
            << (tsc & 0xFF)
            << "\n";
    }

    std::ofstream file(
        "human_timing_v2.csv");

    file
        << "trial,tsc,delta,low16,low8\n";

    for (size_t i = 0; i < N; ++i)
    {
        file
            << i << ","
            << tscs[i] << ","
            << deltas[i] << ","
            << (tscs[i] & 0xFFFF) << ","
            << (tscs[i] & 0xFF)
            << "\n";
    }

    file.close();

    if (N > 1)
    {
        std::vector<uint64_t> real_deltas(
            deltas.begin() + 1,
            deltas.end());

        Statistics s =
            calculate_stats(real_deltas);

        std::cout
            << "\nHUMAN TIMING STATISTICS\n"
            << "-----------------------\n"
            << "Samples : "
            << real_deltas.size()
            << "\n"
            << "Min     : "
            << s.min
            << "\n"
            << "Max     : "
            << s.max
            << "\n"
            << "Mean    : "
            << s.mean
            << "\n"
            << "Std Dev : "
            << s.stddev
            << "\n";

        std::cout
            << "\nExact-delta Shannon entropy: "
            << shannon_entropy(real_deltas)
            << "\n";

        std::cout
            << "Exact-delta min entropy: "
            << min_entropy(real_deltas)
            << "\n";
    }

    std::cout
        << "\nLOW BYTE ANALYSIS\n"
        << "-----------------\n"
        << "Shannon entropy: "
        << shannon_entropy(low8)
        << " bits\n"
        << "Min entropy: "
        << min_entropy(low8)
        << " bits\n"
        << "Chi-square: "
        << chi_square(low8, 256)
        << "\n";

    if (low8.size() > 1)
    {
        std::vector<uint64_t> a(
            low8.begin(),
            low8.end() - 1);

        std::vector<uint64_t> b(
            low8.begin() + 1,
            low8.end());

        std::cout
            << "Lag-1 low-byte correlation: "
            << pearson_correlation(a, b)
            << "\n";
    }

    std::cout
        << "\nSaved: human_timing_v2.csv\n";
}


// ============================================================
// EXPERIMENT 4
// HUMAN BLIND PREDICTION
// ============================================================

void experiment_human_prediction()
{
    print_separator();

    std::cout
        << "EXPERIMENT 4: HUMAN BLIND TSC PREDICTION\n\n";

    std::cout
        << "This is the central experiment.\n\n";

    std::cout
        << "For each trial:\n\n"
        << "1. You choose a number from 0-255.\n"
        << "2. Your prediction is committed.\n"
        << "3. The program asks you to press ENTER.\n"
        << "4. The TSC is sampled immediately after ENTER.\n"
        << "5. The prediction is compared with TSC & 0xFF.\n\n";

    std::cout
        << "Do NOT change your prediction after entering it.\n";

    std::cout
        << "\nChance baseline = "
        << (100.0 / 256.0)
        << "% per trial.\n";

    size_t N;

    if (!read_size_t(
            "Number of prediction trials",
            N,
            1000))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    wait_for_enter();

    int correct_count = 0;

    std::ofstream file(
        "human_prediction_v2.csv");

    file
        << "trial,prediction,tsc,actual_low8,correct\n";

    for (size_t i = 0; i < N; ++i)
    {
        int prediction = -1;

        while (prediction < 0 ||
               prediction > 255)
        {
            std::cout
                << "\nTrial "
                << (i + 1)
                << "/"
                << N
                << "\n";

            std::cout
                << "Enter prediction [0-255]: ";

            std::string input;
            std::getline(
                std::cin,
                input);

            try
            {
                prediction =
                    std::stoi(input);
            }
            catch (...)
            {
                prediction = -1;
            }

            if (prediction < 0 ||
                prediction > 255)
            {
                std::cout
                    << "Invalid prediction.\n";
            }
        }

        std::cout
            << "Prediction committed: "
            << prediction
            << "\n";

        std::cout
            << "Press ENTER when ready... ";

        std::string line;
        std::getline(
            std::cin,
            line);

        uint64_t tsc =
            read_tsc_serialized();

        int actual =
            static_cast<int>(
                tsc & 0xFF);

        bool is_correct =
            prediction == actual;

        if (is_correct)
            correct_count++;

        std::cout
            << "Actual TSC low8 = "
            << actual
            << "\n";

        if (is_correct)
        {
            std::cout
                << ">>> CORRECT <<<\n";
        }

        file
            << i << ","
            << prediction << ","
            << tsc << ","
            << actual << ","
            << (is_correct ? 1 : 0)
            << "\n";
    }

    file.close();

    double accuracy =
        100.0 *
        static_cast<double>(correct_count)
        / static_cast<double>(N);

    double baseline =
        1.0 / 256.0;

    double p =
        binomial_normal_pvalue(
            static_cast<size_t>(correct_count),
            N,
            baseline);

    std::cout
        << "\nRESULT\n"
        << "------\n"
        << "Correct      : "
        << correct_count
        << "/"
        << N
        << "\n"
        << "Accuracy     : "
        << accuracy
        << "%\n"
        << "Chance       : "
        << (100.0 / 256.0)
        << "%\n"
        << "Approx p     : "
        << p
        << "\n";

    std::cout
        << "\nIMPORTANT:\n"
        << "This p-value is only an approximation.\n"
        << "It is not evidence of a supernatural cause.\n";

    std::cout
        << "\nSaved: human_prediction_v2.csv\n";
}


// ============================================================
// EXPERIMENT 5
// MACHINE PREDICTION CONTROL
// ============================================================

void experiment_machine_prediction()
{
    print_separator();

    std::cout
        << "EXPERIMENT 5: MACHINE PREDICTION CONTROL\n\n";

    std::cout
        << "This tests whether previous TSC observations provide\n"
        << "useful predictive information about the next low byte.\n\n";

    size_t N;

    if (!read_size_t(
            "Number of trials",
            N,
            100000))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    /*
        Two extremely simple predictors:

        A. Always predict the previous low byte.
        B. Always predict the most frequent observed byte.

        Neither should systematically predict a fresh TSC
        low byte if the source is effectively unpredictable.
    */

    uint64_t previous =
        read_tsc_serialized();

    int previous_low =
        static_cast<int>(
            previous & 0xFF);

    std::array<uint64_t, 256> counts{};

    size_t lag_correct = 0;
    size_t majority_correct = 0;

    std::ofstream file(
        "machine_prediction_v2.csv");

    file
        << "trial,previous_low8,predicted_majority,"
        << "actual_low8,lag_correct,majority_correct\n";

    for (size_t i = 0; i < N; ++i)
    {
        /*
            Small workload so that this is not merely a
            zero-instruction tight loop.
        */

        volatile uint64_t x =
            static_cast<uint64_t>(i);

        x ^= 0x9E3779B97F4A7C15ULL;

        uint64_t current =
            read_tsc_serialized();

        int actual =
            static_cast<int>(
                current & 0xFF);

        /*
            Determine current empirical majority from
            previous observations only.
        */

        int majority_prediction = 0;

        if (i > 0)
        {
            uint64_t best = counts[0];

            for (int k = 1; k < 256; ++k)
            {
                if (counts[k] > best)
                {
                    best = counts[k];
                    majority_prediction = k;
                }
            }
        }

        bool lag_ok =
            previous_low == actual;

        bool majority_ok =
            majority_prediction == actual;

        if (lag_ok)
            lag_correct++;

        if (majority_ok)
            majority_correct++;

        file
            << i << ","
            << previous_low << ","
            << majority_prediction << ","
            << actual << ","
            << (lag_ok ? 1 : 0) << ","
            << (majority_ok ? 1 : 0)
            << "\n";

        counts[actual]++;

        previous_low = actual;
    }

    file.close();

    double lag_accuracy =
        100.0 *
        static_cast<double>(lag_correct)
        / static_cast<double>(N);

    double majority_accuracy =
        100.0 *
        static_cast<double>(majority_correct)
        / static_cast<double>(N);

    std::cout
        << "Trials: "
        << N
        << "\n\n";

    std::cout
        << "Previous-byte predictor:\n"
        << "  Correct: "
        << lag_correct
        << "\n"
        << "  Accuracy: "
        << lag_accuracy
        << "%\n\n";

    std::cout
        << "Online-majority predictor:\n"
        << "  Correct: "
        << majority_correct
        << "\n"
        << "  Accuracy: "
        << majority_accuracy
        << "%\n\n";

    std::cout
        << "Random baseline:\n"
        << "  Accuracy: "
        << (100.0 / 256.0)
        << "%\n";

    std::cout
        << "\nSaved: machine_prediction_v2.csv\n";
}


// ============================================================
// EXPERIMENT 6
// LOW-BIT ANALYSIS
// ============================================================

void experiment_low_bits()
{
    print_separator();

    std::cout
        << "EXPERIMENT 6: TSC LOW-BIT ANALYSIS\n\n";

    size_t N;

    if (!read_size_t(
            "Number of TSC samples",
            N,
            1000000))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    std::array<uint64_t, 256> counts{};

    std::vector<uint64_t> low8;
    low8.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t t =
            read_tsc_serialized();

        uint64_t x =
            t & 0xFF;

        counts[x]++;
        low8.push_back(x);
    }

    std::vector<uint64_t> low8_vector =
        low8;

    double chi =
        chi_square(
            low8_vector,
            256);

    std::cout
        << "Samples: "
        << N
        << "\n"
        << "Buckets: 256\n"
        << "Expected bucket count: "
        << (static_cast<double>(N) / 256.0)
        << "\n"
        << "Chi-square: "
        << chi
        << "\n"
        << "Shannon entropy: "
        << shannon_entropy(low8_vector)
        << " bits\n"
        << "Min entropy: "
        << min_entropy(low8_vector)
        << " bits\n";

    if (low8.size() > 1)
    {
        std::vector<uint64_t> a(
            low8.begin(),
            low8.end() - 1);

        std::vector<uint64_t> b(
            low8.begin() + 1,
            low8.end());

        std::cout
            << "Lag-1 correlation: "
            << pearson_correlation(a, b)
            << "\n";
    }

    print_bit_statistics(
        low8_vector,
        7);

    std::ofstream file(
        "tsc_low8_v2.csv");

    file
        << "bucket,count\n";

    for (int i = 0; i < 256; ++i)
    {
        file
            << i << ","
            << counts[i]
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: tsc_low8_v2.csv\n";
}


// ============================================================
// EXPERIMENT 7
// TSC-DERIVED RNG
// ============================================================

void experiment_tsc_rng()
{
    print_separator();

    std::cout
        << "EXPERIMENT 7: TSC-DERIVED RNG\n\n";

    size_t N;

    if (!read_size_t(
            "Number of outputs",
            N,
            100000))
    {
        std::cout << "Invalid number.\n";
        return;
    }

    std::vector<uint64_t> values;
    values.reserve(N);

    std::vector<uint64_t> low8;
    low8.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t seed =
            read_tsc_serialized();

        TempleInspiredRNG rng(seed);

        uint64_t value =
            rng.next();

        values.push_back(value);
        low8.push_back(value & 0xFF);
    }

    std::cout
        << "Outputs: "
        << N
        << "\n\n";

    std::cout
        << "64-bit output Shannon entropy: "
        << shannon_entropy(values)
        << "\n";

    std::cout
        << "64-bit output min entropy: "
        << min_entropy(values)
        << "\n";

    std::cout
        << "\nLOW BYTE\n"
        << "--------\n"
        << "Shannon entropy: "
        << shannon_entropy(low8)
        << " bits\n"
        << "Min entropy: "
        << min_entropy(low8)
        << " bits\n"
        << "Chi-square: "
        << chi_square(low8, 256)
        << "\n";

    print_bit_statistics(
        values,
        7);

    std::ofstream file(
        "tsc_rng_v2.csv");

    file
        << "trial,value,low16,low8\n";

    for (size_t i = 0; i < N; ++i)
    {
        file
            << i << ","
            << values[i] << ","
            << (values[i] & 0xFFFF)
            << ","
            << (values[i] & 0xFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: tsc_rng_v2.csv\n";
}


// ============================================================
// EXPERIMENT 8
// PURE PRNG CONTROL
// ============================================================

void experiment_prng_control()
{
    print_separator();

    std::cout
        << "EXPERIMENT 8: DETERMINISTIC PRNG CONTROL\n\n";

    const size_t N = 100000;

    uint64_t seed =
        0x123456789ABCDEF0ULL;

    TempleInspiredRNG rng(seed);

    std::vector<uint64_t> values;
    values.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        values.push_back(
            rng.next_without_tsc());
    }

    std::vector<uint64_t> low8;
    low8.reserve(N);

    for (uint64_t v : values)
        low8.push_back(v & 0xFF);

    std::cout
        << "Samples: "
        << N
        << "\n\n";

    std::cout
        << "Full-output Shannon entropy: "
        << shannon_entropy(values)
        << "\n";

    std::cout
        << "Low-byte Shannon entropy: "
        << shannon_entropy(low8)
        << "\n";

    std::cout
        << "Low-byte chi-square: "
        << chi_square(low8, 256)
        << "\n";

    std::cout
        << "\nThis demonstrates that a deterministic generator\n"
        << "can produce outputs that look statistically random\n"
        << "while remaining completely reproducible.\n";

    std::ofstream file(
        "prng_control_v2.csv");

    file
        << "trial,value,low8\n";

    for (size_t i = 0; i < N; ++i)
    {
        file
            << i << ","
            << values[i] << ","
            << (values[i] & 0xFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: prng_control_v2.csv\n";
}


// ============================================================
// EXPERIMENT 9
// RNG COLLISION ANALYSIS
// ============================================================

void experiment_collisions()
{
    print_separator();

    std::cout
        << "EXPERIMENT 9: COLLISION ANALYSIS\n\n";

    const size_t N = 100000;

    std::set<uint64_t> unique_values;

    uint64_t collisions = 0;

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t seed =
            read_tsc_serialized();

        TempleInspiredRNG rng(seed);

        uint64_t value =
            rng.next();

        if (!unique_values.insert(value).second)
            collisions++;
    }

    std::cout
        << "Samples    : "
        << N
        << "\n"
        << "Unique     : "
        << unique_values.size()
        << "\n"
        << "Collisions : "
        << collisions
        << "\n";

    std::cout
        << "\nFor genuinely random 64-bit outputs,\n"
        << "collisions should be extremely uncommon at\n"
        << "100,000 samples.\n";
}


// ============================================================
// EXPERIMENT 10
// REPRODUCIBILITY
// ============================================================

void experiment_reproducibility()
{
    print_separator();

    std::cout
        << "EXPERIMENT 10: REPRODUCIBILITY\n\n";

    uint64_t seed;

    std::cout
        << "Enter a 64-bit seed: ";

    std::string input;
    std::getline(
        std::cin,
        input);

    try
    {
        seed =
            std::stoull(
                input,
                nullptr,
                0);
    }
    catch (...)
    {
        std::cout
            << "Invalid seed.\n";
        return;
    }

    TempleInspiredRNG a(seed);
    TempleInspiredRNG b(seed);

    std::cout
        << "\nSequence A\n"
        << "----------\n";

    for (int i = 0; i < 20; ++i)
    {
        std::cout
            << i
            << " : "
            << a.next_without_tsc()
            << "\n";
    }

    std::cout
        << "\nSequence B\n"
        << "----------\n";

    for (int i = 0; i < 20; ++i)
    {
        std::cout
            << i
            << " : "
            << b.next_without_tsc()
            << "\n";
    }

    std::cout
        << "\nBoth sequences are identical.\n"
        << "This separates deterministic transformation\n"
        << "from the entropy source used to seed it.\n";
}


// ============================================================
// EXPERIMENT 11
// HUMAN VS MACHINE SUMMARY
// ============================================================

void experiment_summary()
{
    print_separator();

    std::cout
        << "EXPERIMENT 11: QUICK AUTOMATED SUMMARY\n\n";

    const size_t N = 100000;

    std::vector<uint64_t> machine_low8;
    machine_low8.reserve(N);

    std::vector<uint64_t> machine_delta;
    machine_delta.reserve(N);

    uint64_t previous =
        read_tsc_serialized();

    for (size_t i = 0; i < N; ++i)
    {
        volatile uint64_t x =
            i * 0x9E3779B97F4A7C15ULL;

        (void)x;

        uint64_t current =
            read_tsc_serialized();

        machine_delta.push_back(
            current - previous);

        machine_low8.push_back(
            current & 0xFF);

        previous = current;
    }

    Statistics s =
        calculate_stats(machine_delta);

    std::cout
        << "Machine timing\n"
        << "--------------\n"
        << "Samples : "
        << N
        << "\n"
        << "Delta min: "
        << s.min
        << "\n"
        << "Delta max: "
        << s.max
        << "\n"
        << "Delta mean: "
        << s.mean
        << "\n"
        << "Delta stddev: "
        << s.stddev
        << "\n";

    std::cout
        << "\nLow8 Shannon entropy: "
        << shannon_entropy(machine_low8)
        << "\n";

    std::cout
        << "Low8 min entropy: "
        << min_entropy(machine_low8)
        << "\n";

    std::cout
        << "Low8 chi-square: "
        << chi_square(machine_low8, 256)
        << "\n";

    std::cout
        << "\nThis is only an automated control.\n"
        << "It does NOT replace the human experiment.\n";
}


// ============================================================
// MENU
// ============================================================

static void print_menu()
{
    print_separator();

    std::cout
        << "       TSC / HUMAN TIMING EXPERIMENT LAB v2\n\n"

        << " 1. Raw TSC measurements\n"
        << " 2. Machine timing control\n"
        << " 3. Human timing experiment\n"
        << " 4. Human blind prediction\n"
        << " 5. Machine prediction control\n"
        << " 6. TSC low-bit analysis\n"
        << " 7. TSC-derived RNG\n"
        << " 8. Deterministic PRNG control\n"
        << " 9. Collision analysis\n"
        << "10. Reproducibility\n"
        << "11. Automated summary\n"

        << "\n 0. Exit\n";

    print_separator();

    std::cout
        << "Select experiment: ";
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    std::cout
        << "\n"
        << "============================================================\n"
        << "       TSC / HUMAN TIMING EXPERIMENT LAB v2\n"
        << "============================================================\n\n";

    std::cout
        << "This program investigates CPU timing,\n"
        << "human timing and predictability.\n\n";

    std::cout
        << "It does NOT assume beforehand that unusual\n"
        << "results have a supernatural explanation.\n\n";

    std::cout
        << "The experiment attempts to distinguish:\n\n"

        << "  timing variation\n"
        << "        from\n"
        << "  statistical randomness\n"
        << "        from\n"
        << "  unpredictability\n\n";

    std::cout
        << "Architecture: ";

#if defined(_M_X64) || \
    defined(__x86_64__) || \
    defined(_M_AMD64)

    std::cout
        << "x86-64\n";

#elif defined(_M_IX86) || \
      defined(__i386__)

    std::cout
        << "x86-32\n";

#else

    std::cout
        << "unknown / unsupported\n";

#endif

    while (true)
    {
        print_menu();

        std::string choice;

        std::getline(
            std::cin,
            choice);

        if (choice == "0")
        {
            std::cout
                << "\nGoodbye.\n";

            break;
        }

        try
        {
            int option =
                std::stoi(choice);

            switch (option)
            {
                case 1:
                    experiment_raw_tsc();
                    break;

                case 2:
                    experiment_machine_timing();
                    break;

                case 3:
                    experiment_human_timing();
                    break;

                case 4:
                    experiment_human_prediction();
                    break;

                case 5:
                    experiment_machine_prediction();
                    break;

                case 6:
                    experiment_low_bits();
                    break;

                case 7:
                    experiment_tsc_rng();
                    break;

                case 8:
                    experiment_prng_control();
                    break;

                case 9:
                    experiment_collisions();
                    break;

                case 10:
                    experiment_reproducibility();
                    break;

                case 11:
                    experiment_summary();
                    break;

                default:
                    std::cout
                        << "\nInvalid option.\n";
                    break;
            }
        }
        catch (const std::exception& e)
        {
            std::cout
                << "\nError: "
                << e.what()
                << "\n";
        }

        std::cout << "\n";
    }

    return 0;
}