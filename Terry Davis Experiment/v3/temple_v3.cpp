/*
====================================================================
    TSC / HUMAN TIMING EXPERIMENT LAB v2

    Purpose
    -------
    Investigate CPU timestamp-counter behavior and whether human
    timing contributes measurable variability to the sampled value.

    IMPORTANT
    ---------
    This is NOT TempleOS itself.

    This program does NOT assume:
        - randomness is supernatural
        - unusual results imply God
        - human timing is inherently unpredictable

    Instead it tests measurable hypotheses.

    Core questions
    -------------
    1. How variable is the TSC?
    2. How variable is human timing?
    3. Are low TSC bits approximately uniform?
    4. Are successive TSC samples correlated?
    5. Can previous TSC observations predict the next byte?
    6. Does human timing differ from machine timing?
    7. How does a TSC-derived RNG compare with a deterministic PRNG?
    8. Are apparent effects reproducible?

    Compile
    -------

    Windows / MinGW:
        g++ -O2 -std=c++17 tsc_lab_v2.cpp -o tsc_lab_v2.exe

    Linux:
        g++ -O2 -std=c++17 tsc_lab_v2.cpp -o tsc_lab_v2

====================================================================
*/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <array>
#include <string>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <chrono>
#include <thread>
#include <sstream>

#ifdef _MSC_VER
    #include <intrin.h>
#else
    #include <x86intrin.h>
#endif


// ================================================================
// CONFIGURATION
// ================================================================

static constexpr uint64_t LCG_A =
    6364136223846793005ULL;

static constexpr uint64_t LCG_C =
    1442695040888963407ULL;

static constexpr size_t DEFAULT_SAMPLES = 100000;


// ================================================================
// TSC READING
// ================================================================
//
// We use LFENCE + RDTSC.
//
// CPUID is deliberately NOT used here because CPUID is extremely
// expensive and introduces a large artificial timing disturbance.
//
// RDTSCP is available on modern x86/x64 processors and can provide
// stronger ordering semantics. We use it where available.
//
// ================================================================

static inline uint64_t read_tsc()
{
#if defined(_MSC_VER)

    _ReadWriteBarrier();
    _mm_lfence();

    uint64_t value = __rdtsc();

    _mm_lfence();
    _ReadWriteBarrier();

    return value;

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

    return ((uint64_t)hi << 32) | lo;

#else

    #error "Unsupported architecture/compiler"

#endif
}


// ================================================================
// RDTSCP
// ================================================================

static inline uint64_t read_tscp(unsigned int* aux = nullptr)
{
#if defined(_MSC_VER)

    unsigned int c = 0;

    uint64_t value = __rdtscp(&c);

    if (aux)
        *aux = c;

    _mm_lfence();

    return value;

#elif defined(__GNUC__) || defined(__clang__)

    unsigned int lo;
    unsigned int hi;
    unsigned int c;

    asm volatile(
        "rdtscp\n\t"
        "lfence\n\t"
        : "=a"(lo), "=d"(hi), "=c"(c)
        :
        : "memory"
    );

    if (aux)
        *aux = c;

    return ((uint64_t)hi << 32) | lo;

#else

    #error "Unsupported architecture/compiler"

#endif
}


// ================================================================
// STATISTICS
// ================================================================

struct Stats
{
    double mean = 0.0;
    double variance = 0.0;
    double stddev = 0.0;

    uint64_t min = 0;
    uint64_t max = 0;
};


static Stats calculate_stats(
    const std::vector<uint64_t>& values)
{
    Stats s;

    if (values.empty())
        return s;

    s.min =
        *std::min_element(values.begin(), values.end());

    s.max =
        *std::max_element(values.begin(), values.end());

    long double sum = 0.0L;

    for (uint64_t v : values)
        sum += static_cast<long double>(v);

    s.mean =
        static_cast<double>(
            sum / values.size()
        );

    long double variance = 0.0L;

    for (uint64_t v : values)
    {
        long double d =
            static_cast<long double>(v) -
            s.mean;

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


// ================================================================
// SHANNON ENTROPY
// ================================================================

template <typename T>
static double shannon_entropy(
    const std::vector<T>& values)
{
    if (values.empty())
        return 0.0;

    std::map<T, uint64_t> counts;

    for (const auto& v : values)
        counts[v]++;

    double n =
        static_cast<double>(values.size());

    double entropy = 0.0;

    for (const auto& p : counts)
    {
        double probability =
            p.second / n;

        entropy -=
            probability *
            std::log2(probability);
    }

    return entropy;
}


// ================================================================
// MIN ENTROPY
// ================================================================

template <typename T>
static double min_entropy(
    const std::vector<T>& values)
{
    if (values.empty())
        return 0.0;

    std::map<T, uint64_t> counts;

    for (const auto& v : values)
        counts[v]++;

    uint64_t maximum = 0;

    for (const auto& p : counts)
        maximum =
            std::max(maximum, p.second);

    double probability =
        static_cast<double>(maximum) /
        static_cast<double>(values.size());

    return -std::log2(probability);
}


// ================================================================
// PEARSON CORRELATION
// ================================================================

static double correlation(
    const std::vector<double>& x,
    const std::vector<double>& y)
{
    if (x.size() != y.size() ||
        x.size() < 2)
        return 0.0;

    double mx =
        std::accumulate(
            x.begin(),
            x.end(),
            0.0
        ) / x.size();

    double my =
        std::accumulate(
            y.begin(),
            y.end(),
            0.0
        ) / y.size();

    double numerator = 0.0;
    double dx2 = 0.0;
    double dy2 = 0.0;

    for (size_t i = 0; i < x.size(); ++i)
    {
        double dx = x[i] - mx;
        double dy = y[i] - my;

        numerator += dx * dy;
        dx2 += dx * dx;
        dy2 += dy * dy;
    }

    if (dx2 == 0.0 || dy2 == 0.0)
        return 0.0;

    return numerator /
           std::sqrt(dx2 * dy2);
}


// ================================================================
// CHI-SQUARE FOR 256 BUCKETS
// ================================================================

static double chi_square_256(
    const std::array<uint64_t, 256>& counts,
    uint64_t total)
{
    double expected =
        static_cast<double>(total) /
        256.0;

    double chi = 0.0;

    for (uint64_t observed : counts)
    {
        double d =
            static_cast<double>(observed) -
            expected;

        chi +=
            d * d / expected;
    }

    return chi;
}


// ================================================================
// BINOMIAL APPROXIMATION
// ================================================================
//
// Used for prediction accuracy.
//
// Under random guessing of one byte:
//
//     p = 1/256
//
// We calculate a z-score and two-sided normal approximation.
// This is only an approximation; exact binomial should be used
// for final publication-quality analysis.
//
// ================================================================

static double prediction_z(
    int correct,
    int total)
{
    if (total <= 0)
        return 0.0;

    double p = 1.0 / 256.0;

    double expected =
        total * p;

    double variance =
        total * p * (1.0 - p);

    if (variance <= 0.0)
        return 0.0;

    return
        (correct - expected) /
        std::sqrt(variance);
}


static double normal_two_sided_p(double z)
{
    double az = std::fabs(z);

    return std::erfc(
        az / std::sqrt(2.0)
    );
}


// ================================================================
// LCG
// ================================================================

class LCG
{
private:

    uint64_t state;

public:

    explicit LCG(uint64_t seed)
        : state(seed)
    {
    }

    uint64_t next()
    {
        state =
            LCG_A * state +
            LCG_C;

        return state;
    }
};


// ================================================================
// TSC-MIXED RNG
// ================================================================

class TSC_RNG
{
private:

    uint64_t state;

public:

    explicit TSC_RNG(uint64_t seed)
        : state(seed)
    {
    }

    uint64_t next()
    {
        uint64_t tsc =
            read_tsc();

        state =
            LCG_A * state +
            LCG_C;

        state ^=
            tsc;

        return state;
    }
};


// ================================================================
// CSV ESCAPE
// ================================================================

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


// ================================================================
// SEPARATOR
// ================================================================

static void separator()
{
    std::cout
        << "\n"
        << "============================================================\n";
}


// ================================================================
// EXPERIMENT 1
// RAW TSC
// ================================================================

static void experiment_raw_tsc()
{
    separator();

    std::cout
        << "EXPERIMENT 1: RAW TSC\n\n";

    const int N = 100;

    std::vector<uint64_t> values;

    values.reserve(N);

    for (int i = 0; i < N; ++i)
    {
        uint64_t tsc =
            read_tsc();

        values.push_back(tsc);

        std::cout
            << std::setw(3)
            << i
            << " : "
            << tsc
            << "\n";
    }

    Stats s =
        calculate_stats(values);

    std::cout
        << "\nStatistics\n"
        << "----------\n"
        << "Min     : " << s.min << "\n"
        << "Max     : " << s.max << "\n"
        << "Mean    : " << s.mean << "\n"
        << "Std Dev : " << s.stddev << "\n";
}


// ================================================================
// EXPERIMENT 2
// MACHINE TSC DELTAS
// ================================================================

static void experiment_machine_timing()
{
    separator();

    std::cout
        << "EXPERIMENT 2: MACHINE TSC TIMING\n\n";

    size_t N;

    std::cout
        << "Samples [default "
        << DEFAULT_SAMPLES
        << "]: ";

    std::string input;

    std::getline(
        std::cin,
        input
    );

    if (input.empty())
        N = DEFAULT_SAMPLES;
    else
        N = std::stoull(input);

    std::vector<uint64_t> deltas;

    deltas.reserve(N);

    uint64_t previous =
        read_tsc();

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t current =
            read_tsc();

        deltas.push_back(
            current - previous
        );

        previous = current;
    }

    Stats s =
        calculate_stats(deltas);

    std::cout
        << "\nSamples : " << N << "\n"
        << "Min     : " << s.min << "\n"
        << "Max     : " << s.max << "\n"
        << "Mean    : " << s.mean << "\n"
        << "Std Dev : " << s.stddev << "\n";

    std::cout
        << "Exact-delta Shannon entropy: "
        << shannon_entropy(deltas)
        << "\n";

    std::cout
        << "Exact-delta min entropy: "
        << min_entropy(deltas)
        << "\n";

    std::ofstream file(
        "machine_timing_v2.csv"
    );

    file
        << "index,delta\n";

    for (size_t i = 0; i < N; ++i)
    {
        file
            << i
            << ","
            << deltas[i]
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: machine_timing_v2.csv\n";
}


// ================================================================
// EXPERIMENT 3
// HUMAN TIMING
// ================================================================

static void experiment_human_timing()
{
    separator();

    std::cout
        << "EXPERIMENT 3: HUMAN TIMING\n\n";

    const int N = 1000;

    std::cout
        << "You will press ENTER "
        << N
        << " times.\n\n"

        << "Do not deliberately synchronize the presses.\n"

        << "Do not try to produce a particular timing.\n\n"

        << "This experiment measures human timing variability,\n"
        << "not supernatural effects.\n\n";

    std::cout
        << "Press ENTER to start...";

    std::cin.get();

    std::vector<uint64_t> timestamps;
    std::vector<uint64_t> deltas;
    std::vector<uint64_t> low8;

    timestamps.reserve(N);
    deltas.reserve(N);
    low8.reserve(N);

    uint64_t previous = 0;

    for (int i = 0; i < N; ++i)
    {
        std::cout
            << "\nENTER ["
            << (i + 1)
            << "/"
            << N
            << "] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc();

        timestamps.push_back(tsc);

        uint64_t delta = 0;

        if (i > 0)
        {
            delta =
                tsc - previous;

            deltas.push_back(delta);
        }

        low8.push_back(
            tsc & 0xFF
        );

        previous = tsc;

        std::cout
            << "TSC = "
            << tsc
            << "\n";

        if (i > 0)
        {
            std::cout
                << "Delta = "
                << delta
                << "\n";
        }
    }

    Stats s =
        calculate_stats(deltas);

    std::cout
        << "\n\nHUMAN TIMING STATISTICS\n"
        << "-----------------------\n"
        << "Samples : "
        << deltas.size()
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
        << shannon_entropy(deltas)
        << "\n";

    std::cout
        << "Exact-delta min entropy: "
        << min_entropy(deltas)
        << "\n";

    std::array<uint64_t, 256> counts{};

    for (uint64_t v : low8)
        counts[v]++;

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
        << chi_square_256(
               counts,
               low8.size()
           )
        << "\n";

    std::vector<double> x;
    std::vector<double> y;

    if (low8.size() >= 2)
    {
        for (size_t i = 1;
             i < low8.size();
             ++i)
        {
            x.push_back(
                static_cast<double>(
                    low8[i - 1]
                )
            );

            y.push_back(
                static_cast<double>(
                    low8[i]
                )
            );
        }
    }

    std::cout
        << "Lag-1 low-byte correlation: "
        << correlation(x, y)
        << "\n";

    std::ofstream file(
        "human_timing_v2.csv"
    );

    file
        << "trial,tsc,delta,low8\n";

    for (size_t i = 0;
         i < timestamps.size();
         ++i)
    {
        uint64_t delta = 0;

        if (i > 0)
            delta =
                timestamps[i] -
                timestamps[i - 1];

        file
            << i
            << ","
            << timestamps[i]
            << ","
            << delta
            << ","
            << (timestamps[i] & 0xFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: human_timing_v2.csv\n";
}


// ================================================================
// EXPERIMENT 4
// HUMAN TIMING -> RNG
// ================================================================

static void experiment_human_rng()
{
    separator();

    std::cout
        << "EXPERIMENT 4: HUMAN TIMING -> TSC RNG\n\n";

    const int N = 100;

    std::cout
        << "Press ENTER "
        << N
        << " times.\n\n";

    std::cout
        << "Each TSC sample becomes an independent seed.\n";

    std::cout
        << "\nPress ENTER to begin...";

    std::cin.get();

    std::ofstream file(
        "human_rng_v2.csv"
    );

    file
        << "trial,tsc,delta,rng_low8\n";

    uint64_t previous = 0;

    for (int i = 0; i < N; ++i)
    {
        std::cout
            << "\nENTER ["
            << (i + 1)
            << "/"
            << N
            << "] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc();

        uint64_t delta = 0;

        if (i > 0)
            delta =
                tsc - previous;

        /*
            IMPORTANT:

            The TSC is the entropy/input source.

            The LCG transformation does not create entropy.
            It merely transforms the input.
        */

        LCG rng(tsc);

        uint64_t value =
            rng.next();

        std::cout
            << "TSC       = "
            << tsc
            << "\n"

            << "Delta     = "
            << delta
            << "\n"

            << "RNG       = "
            << value
            << "\n"

            << "RNG low8  = "
            << (value & 0xFF)
            << "\n";

        file
            << i
            << ","
            << tsc
            << ","
            << delta
            << ","
            << (value & 0xFF)
            << "\n";

        previous = tsc;
    }

    file.close();

    std::cout
        << "\nSaved: human_rng_v2.csv\n";
}


// ================================================================
// EXPERIMENT 5
// MACHINE PREDICTION
// ================================================================
//
// This is one of the most important experiments.
//
// We NEVER ask the human to type anything.
//
// Procedure:
//
//     previous TSC
//          ↓
//     predictor
//          ↓
//     small controlled computation
//          ↓
//     next TSC
//          ↓
//     compare
//
// The predictor is trained ONLINE but evaluated against the next
// unseen observation.
//
// Baselines:
//     1. uniform random
//     2. previous byte
//     3. running majority
//
// ================================================================

static void experiment_machine_prediction()
{
    separator();

    std::cout
        << "EXPERIMENT 5: MACHINE PREDICTION CONTROL\n\n";

    std::cout
        << "This tests whether previous TSC observations provide\n"
        << "useful predictive information about the next low byte.\n\n";

    size_t N;

    std::cout
        << "Number of trials [default "
        << DEFAULT_SAMPLES
        << "]: ";

    std::string input;

    std::getline(
        std::cin,
        input
    );

    if (input.empty())
        N = DEFAULT_SAMPLES;
    else
        N = std::stoull(input);

    std::mt19937_64 random_engine(
        static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now()
                .time_since_epoch()
                .count()
        )
    );

    std::uniform_int_distribution<int>
        random_byte(
            0,
            255
        );

    uint64_t previous =
        read_tsc();

    int previous_prediction_correct = 0;
    int majority_prediction_correct = 0;
    int random_prediction_correct = 0;

    std::array<uint64_t, 256> counts{};

    std::ofstream file(
        "machine_prediction_v2.csv"
    );

    file
        << "trial,previous_low8,"
        << "previous_prediction,"
        << "majority_prediction,"
        << "random_prediction,"
        << "actual,"
        << "previous_correct,"
        << "majority_correct,"
        << "random_correct\n";

    for (size_t i = 0; i < N; ++i)
    {
        int previous_byte =
            static_cast<int>(
                previous & 0xFF
            );

        /*
            Predictor 1:
            simply predict the previous byte.
        */

        int prediction_previous =
            previous_byte;

        /*
            Predictor 2:
            predict the byte that has appeared most often so far.
        */

        int prediction_majority = 0;

        uint64_t best_count = 0;

        for (int b = 0; b < 256; ++b)
        {
            if (counts[b] > best_count)
            {
                best_count =
                    counts[b];

                prediction_majority =
                    b;
            }
        }

        /*
            Predictor 3:
            true random baseline.
        */

        int prediction_random =
            random_byte(
                random_engine
            );

        /*
            Small fixed workload.

            This is NOT intended to create entropy.

            It simply separates consecutive measurements enough
            that we are not measuring two adjacent instructions.
        */

        volatile uint64_t x =
            static_cast<uint64_t>(i);

        for (int j = 0; j < 17; ++j)
        {
            x =
                x *
                6364136223846793005ULL
                + 1;
        }

        (void)x;

        uint64_t current =
            read_tsc();

        int actual =
            static_cast<int>(
                current & 0xFF
            );

        bool c1 =
            prediction_previous ==
            actual;

        bool c2 =
            prediction_majority ==
            actual;

        bool c3 =
            prediction_random ==
            actual;

        if (c1)
            previous_prediction_correct++;

        if (c2)
            majority_prediction_correct++;

        if (c3)
            random_prediction_correct++;

        counts[actual]++;

        file
            << i
            << ","
            << previous_byte
            << ","
            << prediction_previous
            << ","
            << prediction_majority
            << ","
            << prediction_random
            << ","
            << actual
            << ","
            << (c1 ? 1 : 0)
            << ","
            << (c2 ? 1 : 0)
            << ","
            << (c3 ? 1 : 0)
            << "\n";

        previous =
            current;
    }

    file.close();

    double baseline =
        100.0 / 256.0;

    std::cout
        << "\nTrials: "
        << N
        << "\n\n";

    auto print_result =
        [&](const std::string& name,
            int correct)
        {
            double accuracy =
                100.0 *
                correct /
                static_cast<double>(N);

            double z =
                prediction_z(
                    correct,
                    static_cast<int>(N)
                );

            double p =
                normal_two_sided_p(z);

            std::cout
                << name
                << ":\n"

                << "  Correct: "
                << correct
                << "\n"

                << "  Accuracy: "
                << accuracy
                << "%\n"

                << "  z-score: "
                << z
                << "\n"

                << "  approx p: "
                << p
                << "\n\n";
        };

    print_result(
        "Previous-byte predictor",
        previous_prediction_correct
    );

    print_result(
        "Online-majority predictor",
        majority_prediction_correct
    );

    print_result(
        "Random baseline",
        random_prediction_correct
    );

    std::cout
        << "Theoretical random baseline: "
        << baseline
        << "%\n";

    std::cout
        << "\nSaved: machine_prediction_v2.csv\n";
}


// ================================================================
// EXPERIMENT 6
// TSC LOW-BIT ANALYSIS
// ================================================================

static void experiment_low_bits()
{
    separator();

    std::cout
        << "EXPERIMENT 6: TSC LOW-BIT ANALYSIS\n\n";

    size_t N;

    std::cout
        << "Number of TSC samples [default 1000000]: ";

    std::string input;

    std::getline(
        std::cin,
        input
    );

    if (input.empty())
        N = 1000000;
    else
        N = std::stoull(input);

    std::array<uint64_t, 256> counts{};

    std::vector<uint64_t> values;

    values.reserve(N);

    uint64_t previous =
        read_tsc();

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t current =
            read_tsc();

        uint64_t low =
            current & 0xFF;

        values.push_back(low);

        counts[low]++;

        previous =
            current;
    }

    double chi =
        chi_square_256(
            counts,
            N
        );

    std::cout
        << "Samples: "
        << N
        << "\n"

        << "Buckets: 256\n"

        << "Expected bucket count: "
        << static_cast<double>(N) / 256.0
        << "\n"

        << "Chi-square: "
        << chi
        << "\n"

        << "Shannon entropy: "
        << shannon_entropy(values)
        << " bits\n"

        << "Min entropy: "
        << min_entropy(values)
        << " bits\n";

    /*
        Lag-1 correlation.
    */

    std::vector<double> x;
    std::vector<double> y;

    x.reserve(N - 1);
    y.reserve(N - 1);

    for (size_t i = 1; i < N; ++i)
    {
        x.push_back(
            static_cast<double>(
                values[i - 1]
            )
        );

        y.push_back(
            static_cast<double>(
                values[i]
            )
        );
    }

    double r =
        correlation(x, y);

    std::cout
        << "\nLag-1 correlation: "
        << r
        << "\n";

    /*
        BIT BALANCE
    */

    std::cout
        << "\nBIT BALANCE\n"
        << "-----------\n";

    for (int bit = 0; bit < 8; ++bit)
    {
        uint64_t ones = 0;

        for (uint64_t v : values)
        {
            if ((v >> bit) & 1)
                ones++;
        }

        uint64_t zeros =
            N - ones;

        double percentage =
            100.0 *
            ones /
            static_cast<double>(N);

        std::cout
            << "bit "
            << bit
            << " : ones="
            << std::setw(8)
            << ones

            << " zeros="
            << std::setw(8)
            << zeros

            << " ones%="
            << std::fixed
            << std::setprecision(4)
            << percentage
            << "\n";
    }

    std::ofstream file(
        "tsc_low_bits_v2.csv"
    );

    file
        << "bucket,count\n";

    for (int i = 0; i < 256; ++i)
    {
        file
            << i
            << ","
            << counts[i]
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: tsc_low_bits_v2.csv\n";
}


// ================================================================
// EXPERIMENT 7
// HUMAN VS MACHINE TIMING
// ================================================================
//
// Important:
// Human timing and machine timing are fundamentally different.
//
// This experiment compares distributions of DELTAS.
//
// It does NOT claim that entropy estimates alone prove
// unpredictability.
//
// ================================================================

static void experiment_compare_timing()
{
    separator();

    std::cout
        << "EXPERIMENT 7: HUMAN VS MACHINE TIMING\n\n";

    std::cout
        << "This experiment compares previously collected data.\n";

    std::cout
        << "\nRequired files:\n"
        << "  human_timing_v2.csv\n"
        << "  machine_timing_v2.csv\n";

    std::cout
        << "\nThis analysis is intentionally left to Python/R/\n"
        << "statistical software because distribution comparisons\n"
        << "are much more useful there.\n";
}


// ================================================================
// EXPERIMENT 8
// REPRODUCIBILITY
// ================================================================

static void experiment_reproducibility()
{
    separator();

    std::cout
        << "EXPERIMENT 8: REPRODUCIBILITY\n\n";

    uint64_t seed;

    std::cout
        << "Enter seed: ";

    std::cin
        >> seed;

    std::cin.ignore(
        std::numeric_limits<
            std::streamsize
        >::max(),
        '\n'
    );

    LCG rng(seed);

    std::cout
        << "\nPure deterministic sequence:\n\n";

    for (int i = 0; i < 20; ++i)
    {
        std::cout
            << std::setw(3)
            << i
            << " : "
            << rng.next()
            << "\n";
    }

    std::cout
        << "\nRun again using the same seed.\n"
        << "The sequence will reproduce exactly.\n";

    std::cout
        << "\nThis demonstrates:\n\n"

        << "entropy source\n"
        << "      ↓\n"
        << "seed\n"
        << "      ↓\n"
        << "deterministic PRNG\n"
        << "      ↓\n"
        << "output\n";
}


// ================================================================
// EXPERIMENT 9
// CONTROL: STANDARD PRNG
// ================================================================

static void experiment_standard_prng()
{
    separator();

    std::cout
        << "EXPERIMENT 9: TSC VS STANDARD PRNG\n\n";

    const size_t N = 100000;

    uint64_t seed =
        read_tsc();

    LCG lcg(seed);

    std::mt19937_64 mt(seed);

    std::vector<uint64_t>
        lcg_values;

    std::vector<uint64_t>
        mt_values;

    std::vector<uint64_t>
        tsc_values;

    lcg_values.reserve(N);
    mt_values.reserve(N);
    tsc_values.reserve(N);

    for (size_t i = 0; i < N; ++i)
    {
        lcg_values.push_back(
            lcg.next()
        );

        mt_values.push_back(
            mt()
        );

        tsc_values.push_back(
            read_tsc()
        );
    }

    std::cout
        << "LCG Shannon entropy: "
        << shannon_entropy(lcg_values)
        << "\n";

    std::cout
        << "MT19937_64 Shannon entropy: "
        << shannon_entropy(mt_values)
        << "\n";

    std::cout
        << "Raw TSC Shannon entropy: "
        << shannon_entropy(tsc_values)
        << "\n";

    std::cout
        << "\nIMPORTANT:\n"
        << "Finite-sample Shannon entropy is NOT a cryptographic\n"
        << "security measurement.\n";
}


// ================================================================
// EXPERIMENT 10
// CONTROLLED HUMAN FUTURE-BYTE PREDICTION
// ================================================================
//
// This is deliberately different from the old experiment.
//
// OLD:
//
//     human types prediction
//             ↓
//     immediate TSC measurement
//
// Problem:
//
//     typing/input itself changes timing.
//
//
//
// NEW:
//
//     experiment announces prediction window
//             ↓
//     human chooses a byte mentally
//             ↓
//     human presses ENTER to START countdown
//             ↓
//     fixed machine delay
//             ↓
//     TSC measurement
//
// The prediction is made BEFORE the target measurement.
//
// The target measurement is therefore not directly triggered by
// entering the prediction.
//
// This tests HUMAN PREDICTION under a standardized protocol.
//
// It does NOT establish supernatural causation.
// ================================================================

static void experiment_controlled_human_prediction()
{
    separator();

    std::cout
        << "EXPERIMENT 10: CONTROLLED HUMAN PREDICTION\n\n";

    const int N = 100;

    std::cout
        << "This is a cleaner human prediction test.\n\n"

        << "For every trial:\n\n"

        << "1. Choose a number from 0-255 mentally.\n"
        << "2. Type that number and press ENTER.\n"
        << "3. The program waits a fixed amount of time.\n"
        << "4. The TSC is sampled.\n"
        << "5. Your prediction is compared with the actual byte.\n\n"

        << "IMPORTANT:\n"
        << "The measurement is NOT taken immediately after your\n"
        << "prediction input.\n\n";

    std::cout
        << "Trials: "
        << N
        << "\n\n";

    std::ofstream file(
        "controlled_human_prediction.csv"
    );

    file
        << "trial,prediction,actual,correct\n";

    int correct = 0;

    /*
        Fixed delay.

        We deliberately use a fixed delay rather than a random
        delay because the goal here is to standardize the
        protocol.

        This experiment is not trying to maximize entropy.
        It is testing prediction.
    */

    const int delay_ms = 50;

    for (int i = 0; i < N; ++i)
    {
        int prediction;

        while (true)
        {
            std::cout
                << "\nTrial "
                << (i + 1)
                << "/"
                << N
                << "\nPrediction [0-255]: ";

            std::string input;

            std::getline(
                std::cin,
                input
            );

            try
            {
                prediction =
                    std::stoi(input);

                if (prediction >= 0 &&
                    prediction <= 255)
                    break;
            }
            catch (...)
            {
            }

            std::cout
                << "Invalid prediction.\n";
        }

        std::cout
            << "Press ENTER to start measurement...";

        std::cin.get();

        /*
            The target measurement is separated from the input
            by a fixed delay.
        */

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                delay_ms
            )
        );

        uint64_t tsc =
            read_tsc();

        int actual =
            static_cast<int>(
                tsc & 0xFF
            );

        bool hit =
            prediction == actual;

        if (hit)
            correct++;

        std::cout
            << "Actual: "
            << actual
            << "   "
            << (hit
                ? "CORRECT"
                : "MISS")
            << "\n";

        file
            << i
            << ","
            << prediction
            << ","
            << actual
            << ","
            << (hit ? 1 : 0)
            << "\n";
    }

    file.close();

    double accuracy =
        100.0 *
        correct /
        N;

    double baseline =
        100.0 /
        256.0;

    double z =
        prediction_z(
            correct,
            N
        );

    double p =
        normal_two_sided_p(z);

    std::cout
        << "\nRESULT\n"
        << "------\n"

        << "Correct: "
        << correct
        << "/"
        << N
        << "\n"

        << "Accuracy: "
        << accuracy
        << "%\n"

        << "Chance: "
        << baseline
        << "%\n"

        << "z-score: "
        << z
        << "\n"

        << "Approx p: "
        << p
        << "\n";

    std::cout
        << "\nSaved: controlled_human_prediction.csv\n";
}


// ================================================================
// EXPERIMENT 11
// AUTOMATED SUMMARY
// ================================================================

static void experiment_summary()
{
    separator();

    std::cout
        << "EXPERIMENT 11: AUTOMATED TSC SUMMARY\n\n";

    const size_t N = 1000000;

    std::vector<uint64_t> low;

    low.reserve(N);

    uint64_t previous =
        read_tsc();

    std::vector<double> x;
    std::vector<double> y;

    x.reserve(N - 1);
    y.reserve(N - 1);

    for (size_t i = 0; i < N; ++i)
    {
        uint64_t current =
            read_tsc();

        uint64_t value =
            current & 0xFF;

        low.push_back(value);

        if (i > 0)
        {
            x.push_back(
                static_cast<double>(
                    low[i - 1]
                )
            );

            y.push_back(
                static_cast<double>(
                    low[i]
                )
            );
        }

        previous =
            current;
    }

    std::array<uint64_t, 256> counts{};

    for (uint64_t v : low)
        counts[v]++;

    std::cout
        << "Samples: "
        << N
        << "\n\n"

        << "Low-byte Shannon entropy: "
        << shannon_entropy(low)
        << " / 8 bits\n"

        << "Low-byte min entropy: "
        << min_entropy(low)
        << " bits\n"

        << "Chi-square: "
        << chi_square_256(
               counts,
               N
           )
        << "\n"

        << "Lag-1 correlation: "
        << correlation(x, y)
        << "\n";

    std::cout
        << "\nSummary complete.\n";
}


// ================================================================
// MENU
// ================================================================

static void print_menu()
{
    separator();

    std::cout
        << "       TSC / HUMAN TIMING EXPERIMENT LAB v2\n\n"

        << " 1. Raw TSC measurements\n"
        << " 2. Machine TSC timing\n"
        << " 3. Human timing experiment\n"
        << " 4. Human timing -> TSC RNG\n"
        << " 5. Machine prediction control\n"
        << " 6. TSC low-bit analysis\n"
        << " 7. Human vs machine timing notes\n"
        << " 8. Reproducibility experiment\n"
        << " 9. TSC vs standard PRNG\n"
        << "10. Controlled human prediction\n"
        << "11. Automated TSC summary\n\n"

        << " 0. Exit\n";

    separator();

    std::cout
        << "Select experiment: ";
}


// ================================================================
// MAIN
// ================================================================

int main()
{
    std::cout
        << "\n"
        << "============================================================\n"
        << "       TSC / HUMAN TIMING EXPERIMENT LAB v2\n"
        << "============================================================\n\n";

#if defined(_M_X64) || \
    defined(__x86_64__) || \
    defined(_M_AMD64)

    std::cout
        << "Architecture: x86-64\n";

#elif defined(_M_IX86) || \
      defined(__i386__)

    std::cout
        << "Architecture: x86-32\n";

#else

    std::cout
        << "Architecture: unknown\n";

#endif

    std::cout
        << "\nThis experiment investigates:\n"

        << "  CPU timing\n"
        << "  human timing\n"
        << "  TSC low bits\n"
        << "  prediction\n"
        << "  statistical structure\n"
        << "  reproducibility\n\n"

        << "It does NOT assume a supernatural explanation.\n";

    while (true)
    {
        print_menu();

        std::string choice;

        std::getline(
            std::cin,
            choice
        );

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
                    experiment_human_rng();
                    break;

                case 5:
                    experiment_machine_prediction();
                    break;

                case 6:
                    experiment_low_bits();
                    break;

                case 7:
                    experiment_compare_timing();
                    break;

                case 8:
                    experiment_reproducibility();
                    break;

                case 9:
                    experiment_standard_prng();
                    break;

                case 10:
                    experiment_controlled_human_prediction();
                    break;

                case 11:
                    experiment_summary();
                    break;

                default:
                    std::cout
                        << "\nInvalid option.\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cout
                << "\nError: "
                << e.what()
                << "\n";
        }

        std::cout
            << "\n";
    }

    return 0;
}