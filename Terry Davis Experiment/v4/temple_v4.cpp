/*
====================================================================
        TSC / HUMAN ENTROPY LABORATORY v3
====================================================================

Purpose
-------

Measure and analyze:

    - CPU timestamp-counter behavior
    - human timing variability
    - machine timing variability
    - TSC low-bit structure
    - autocorrelation
    - mutual information
    - conditional predictability
    - permutation significance
    - block stability
    - deterministic PRNG controls
    - operating-system randomness controls
    - historical TempleOS-style RNG reconstruction

IMPORTANT
---------

This program does NOT claim:

    high entropy == random
    high entropy == independent
    high entropy == unpredictable
    unpredictable == cryptographically secure

The laboratory explicitly distinguishes these properties.

Scientific question
-------------------

Does human interaction inject measurable information into a
CPU timing process that survives statistical controls?

Controls
--------

    MACHINE
    HUMAN
    FIXED DELAY
    JITTERED DELAY
    MT19937-64
    OS CSPRNG

Architecture
------------

                    TSC SOURCE
                        |
          +-------------+-------------+
          |             |             |
          v             v             v
       machine        human        controls
       timing        timing
          |             |
          +-------------+-------------+
                        |
                 raw observations
                        |
          +-------------+-------------+
          |             |             |
          v             v             v
     distribution   correlation   prediction
          |             |             |
          +-------------+-------------+
                        |
                 mutual information
                        |
                 statistical tests
                        |
                  entropy estimates
                        |
                     result

Compile
-------

Windows / MinGW:

    g++ -O2 -std=c++17 tsc_entropy_lab_v3.cpp -o tsc_entropy_lab_v3.exe

Linux:

    g++ -O2 -std=c++17 tsc_entropy_lab_v3.cpp -o tsc_entropy_lab_v3

Linux OS randomness uses /dev/urandom.

Windows OS randomness uses BCryptGenRandom when available.

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
#include <unordered_map>
#include <chrono>
#include <thread>
#include <sstream>
#include <deque>
#include <filesystem>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#endif

#ifdef _MSC_VER
    #include <intrin.h>
#else
    #include <x86intrin.h>
#endif


// ================================================================
// CONFIGURATION
// ================================================================

static constexpr size_t DEFAULT_SAMPLES = 100000;

static constexpr uint64_t LCG_A =
    6364136223846793005ULL;

static constexpr uint64_t LCG_C =
    1442695040888963407ULL;


// ================================================================
// TSC READING
// ================================================================

enum class TSCMode
{
    RDTSC_LFENCE,
    RDTSCP,
    CPUID_RDTSC
};


// ---------------------------------------------------------------
// LFENCE + RDTSC
// ---------------------------------------------------------------

static inline uint64_t read_tsc_lfence()
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

    return (static_cast<uint64_t>(hi) << 32) | lo;

#else

    #error "Unsupported compiler"

#endif
}


// ---------------------------------------------------------------
// RDTSCP
// ---------------------------------------------------------------

static inline uint64_t read_tscp(unsigned int* aux = nullptr)
{
#if defined(_MSC_VER)

    unsigned int c = 0;

    uint64_t value = __rdtscp(&c);

    _mm_lfence();

    if (aux)
        *aux = c;

    return value;

#elif defined(__GNUC__) || defined(__clang__)

    unsigned int lo;
    unsigned int hi;
    unsigned int c;

    asm volatile(
        "rdtscp\n\t"
        "lfence\n\t"
        : "=a"(lo),
          "=d"(hi),
          "=c"(c)
        :
        : "memory"
    );

    if (aux)
        *aux = c;

    return (static_cast<uint64_t>(hi) << 32) | lo;

#else

    #error "Unsupported compiler"

#endif
}


// ---------------------------------------------------------------
// CPUID + RDTSC
// ---------------------------------------------------------------

static inline uint64_t read_tsc_cpuid()
{
#if defined(_MSC_VER)

    int cpu_info[4];

    __cpuid(cpu_info, 0);

    return __rdtsc();

#elif defined(__GNUC__) || defined(__clang__)

    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    asm volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(0)
        : "memory"
    );

    return (static_cast<uint64_t>(edx) << 32) | eax;

#else

    #error "Unsupported compiler"

#endif
}


// ---------------------------------------------------------------
// Unified reader
// ---------------------------------------------------------------

static inline uint64_t read_tsc(TSCMode mode)
{
    switch (mode)
    {
        case TSCMode::RDTSC_LFENCE:
            return read_tsc_lfence();

        case TSCMode::RDTSCP:
            return read_tscp();

        case TSCMode::CPUID_RDTSC:
            return read_tsc_cpuid();
    }

    return read_tsc_lfence();
}


static const char* tsc_mode_name(TSCMode mode)
{
    switch (mode)
    {
        case TSCMode::RDTSC_LFENCE:
            return "LFENCE_RDTSC";

        case TSCMode::RDTSCP:
            return "RDTSCP";

        case TSCMode::CPUID_RDTSC:
            return "CPUID_RDTSC";
    }

    return "UNKNOWN";
}


// ================================================================
// BASIC STATISTICS
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
        *std::min_element(
            values.begin(),
            values.end()
        );

    s.max =
        *std::max_element(
            values.begin(),
            values.end()
        );

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
        variance /= values.size() - 1;

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
            static_cast<double>(p.second) / n;

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
// CORRELATION
// ================================================================

static double correlation(
    const std::vector<double>& x,
    const std::vector<double>& y)
{
    if (x.size() != y.size() ||
        x.size() < 2)
        return 0.0;

    long double mx = 0.0L;
    long double my = 0.0L;

    for (size_t i = 0; i < x.size(); ++i)
    {
        mx += x[i];
        my += y[i];
    }

    mx /= x.size();
    my /= y.size();

    long double numerator = 0.0L;
    long double dx2 = 0.0L;
    long double dy2 = 0.0L;

    for (size_t i = 0; i < x.size(); ++i)
    {
        long double dx =
            static_cast<long double>(x[i]) - mx;

        long double dy =
            static_cast<long double>(y[i]) - my;

        numerator += dx * dy;
        dx2 += dx * dx;
        dy2 += dy * dy;
    }

    if (dx2 == 0.0L ||
        dy2 == 0.0L)
        return 0.0;

    return static_cast<double>(
        numerator /
        std::sqrt(dx2 * dy2)
    );
}


// ================================================================
// MULTI-LAG AUTOCORRELATION
// ================================================================

static double lag_correlation(
    const std::vector<double>& values,
    size_t lag)
{
    if (values.size() <= lag)
        return 0.0;

    std::vector<double> x;
    std::vector<double> y;

    x.reserve(values.size() - lag);
    y.reserve(values.size() - lag);

    for (size_t i = lag;
         i < values.size();
         ++i)
    {
        x.push_back(values[i - lag]);
        y.push_back(values[i]);
    }

    return correlation(x, y);
}


// ================================================================
// CHI SQUARE 256
// ================================================================

static double chi_square_256(
    const std::array<uint64_t, 256>& counts,
    uint64_t total)
{
    if (total == 0)
        return 0.0;

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
// APPROXIMATE CHI-SQUARE P VALUE
// ================================================================
//
// For df=255 we use a Wilson-Hilferty approximation.
//
// This is intended as a diagnostic, not a publication-grade
// replacement for an exact statistical library.
//
// ================================================================

static double chi_square_p_value(
    double chi,
    double df)
{
    if (chi <= 0.0 || df <= 0.0)
        return 1.0;

    double z =
        (
            std::pow(chi / df, 1.0 / 3.0)
            -
            (
                1.0 -
                2.0 / (9.0 * df)
            )
        )
        /
        std::sqrt(
            2.0 / (9.0 * df)
        );

    return 0.5 *
           std::erfc(
               z /
               std::sqrt(2.0)
           );
}


// ================================================================
// BIT BALANCE
// ================================================================

static void print_bit_balance(
    const std::vector<uint64_t>& values,
    int bits)
{
    if (values.empty())
        return;

    std::cout
        << "\nBIT BALANCE\n"
        << "-----------\n";

    for (int bit = 0;
         bit < bits;
         ++bit)
    {
        uint64_t ones = 0;

        for (uint64_t v : values)
        {
            if ((v >> bit) & 1ULL)
                ones++;
        }

        uint64_t zeros =
            values.size() - ones;

        double percentage =
            100.0 *
            static_cast<double>(ones) /
            values.size();

        std::cout
            << "bit "
            << std::setw(2)
            << bit
            << " : ones="
            << std::setw(10)
            << ones
            << " zeros="
            << std::setw(10)
            << zeros
            << " ones%="
            << std::fixed
            << std::setprecision(4)
            << percentage
            << "\n";
    }
}


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
        << "================================================================\n";
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
// OS RANDOMNESS CONTROL
// ================================================================

static bool os_random_bytes(
    void* buffer,
    size_t size)
{
#ifdef _WIN32

    return BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(buffer),
        static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;

#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)

    std::ifstream file(
        "/dev/urandom",
        std::ios::binary
    );

    if (!file)
        return false;

    file.read(
        static_cast<char*>(buffer),
        static_cast<std::streamsize>(size)
    );

    return file.good();

#else

    return false;

#endif
}


static uint64_t os_random_u64()
{
    uint64_t value = 0;

    if (os_random_bytes(
            &value,
            sizeof(value)))
    {
        return value;
    }

    // Fallback is explicitly NOT a CSPRNG.
    std::random_device rd;

    return
        (static_cast<uint64_t>(rd()) << 32) ^
        static_cast<uint64_t>(rd());
}


// ================================================================
// MUTUAL INFORMATION
// ================================================================
//
// For byte-valued observations.
//
// I(X;Y) = sum p(x,y) log2(p(x,y)/(p(x)p(y)))
//
// ================================================================

static double mutual_information_bytes(
    const std::vector<uint8_t>& x,
    const std::vector<uint8_t>& y)
{
    if (x.size() != y.size() ||
        x.empty())
        return 0.0;

    std::array<uint64_t, 256> cx{};
    std::array<uint64_t, 256> cy{};

    std::array<
        std::array<uint64_t, 256>,
        256
    > joint{};

    const double n =
        static_cast<double>(x.size());

    for (size_t i = 0;
         i < x.size();
         ++i)
    {
        cx[x[i]]++;
        cy[y[i]]++;
        joint[x[i]][y[i]]++;
    }

    double mi = 0.0;

    for (int a = 0;
         a < 256;
         ++a)
    {
        if (cx[a] == 0)
            continue;

        for (int b = 0;
             b < 256;
             ++b)
        {
            if (joint[a][b] == 0)
                continue;

            double pxy =
                joint[a][b] / n;

            double px =
                cx[a] / n;

            double py =
                cy[b] / n;

            mi +=
                pxy *
                std::log2(
                    pxy /
                    (px * py)
                );
        }
    }

    return mi;
}


// ================================================================
// MULTI-LAG MUTUAL INFORMATION
// ================================================================

static double lag_mutual_information(
    const std::vector<uint8_t>& values,
    size_t lag)
{
    if (values.size() <= lag)
        return 0.0;

    std::vector<uint8_t> x;
    std::vector<uint8_t> y;

    x.reserve(values.size() - lag);
    y.reserve(values.size() - lag);

    for (size_t i = lag;
         i < values.size();
         ++i)
    {
        x.push_back(values[i - lag]);
        y.push_back(values[i]);
    }

    return mutual_information_bytes(x, y);
}


// ================================================================
// CONDITIONAL PREDICTION
// ================================================================
//
// Predictor:
//
//     P(next | previous)
//
// Uses the most frequent next byte for each observed previous byte.
//
// This is substantially more informative than simply asking whether
// "previous byte == next byte".
//
// ================================================================

struct ConditionalPredictionResult
{
    uint64_t correct = 0;
    uint64_t total = 0;
    double accuracy = 0.0;
};


static ConditionalPredictionResult
conditional_prediction(
    const std::vector<uint8_t>& values)
{
    ConditionalPredictionResult result;

    if (values.size() < 2)
        return result;

    std::array<
        std::array<uint64_t, 256>,
        256
    > transition{};

    for (size_t i = 1;
         i < values.size();
         ++i)
    {
        transition
            [values[i - 1]]
            [values[i]]++;
    }

    std::array<uint8_t, 256> predictor{};

    for (int previous = 0;
         previous < 256;
         ++previous)
    {
        uint64_t best = 0;
        uint8_t best_value = 0;

        for (int next = 0;
             next < 256;
             ++next)
        {
            if (transition[previous][next] > best)
            {
                best =
                    transition[previous][next];

                best_value =
                    static_cast<uint8_t>(next);
            }
        }

        predictor[previous] =
            best_value;
    }

    for (size_t i = 1;
         i < values.size();
         ++i)
    {
        uint8_t prediction =
            predictor[values[i - 1]];

        if (prediction == values[i])
            result.correct++;

        result.total++;
    }

    if (result.total)
    {
        result.accuracy =
            100.0 *
            static_cast<double>(result.correct) /
            result.total;
    }

    return result;
}


// ================================================================
// BASELINE PREDICTION
// ================================================================

static double random_byte_baseline()
{
    return 100.0 / 256.0;
}


// ================================================================
// PERMUTATION TEST
// ================================================================
//
// Tests lag-1 correlation against shuffled data.
//
// ================================================================

static double permutation_correlation_p(
    const std::vector<uint8_t>& values,
    size_t permutations,
    uint64_t seed)
{
    if (values.size() < 3)
        return 1.0;

    std::vector<double> original;

    original.reserve(values.size());

    for (uint8_t v : values)
        original.push_back(
            static_cast<double>(v)
        );

    double observed =
        lag_correlation(
            original,
            1
        );

    std::vector<uint8_t> shuffled =
        values;

    std::mt19937_64 rng(seed);

    uint64_t extreme = 0;

    for (size_t p = 0;
         p < permutations;
         ++p)
    {
        std::shuffle(
            shuffled.begin(),
            shuffled.end(),
            rng
        );

        std::vector<double> temp;

        temp.reserve(shuffled.size());

        for (uint8_t v : shuffled)
            temp.push_back(
                static_cast<double>(v)
            );

        double r =
            lag_correlation(
                temp,
                1
            );

        if (std::fabs(r) >=
            std::fabs(observed))
        {
            extreme++;
        }
    }

    return
        static_cast<double>(extreme + 1) /
        static_cast<double>(permutations + 1);
}


// ================================================================
// COMPRESSION ESTIMATE
// ================================================================
//
// This is NOT a formal entropy estimator.
//
// It simply reports:
//
//     compressed_size / original_size
//
// using a basic run-length encoding.
//
// It is useful as a structural control.
//
// ================================================================

static double simple_rle_ratio(
    const std::vector<uint8_t>& values)
{
    if (values.empty())
        return 1.0;

    size_t encoded = 0;

    size_t i = 0;

    while (i < values.size())
    {
        uint8_t value =
            values[i];

        size_t run = 1;

        while (
            i + run < values.size() &&
            values[i + run] == value
        )
        {
            run++;
        }

        encoded += 2;
        i += run;

        (void)value;
    }

    return
        static_cast<double>(encoded) /
        static_cast<double>(values.size());
}


// ================================================================
// BLOCK ANALYSIS
// ================================================================

static void block_analysis(
    const std::vector<uint8_t>& values,
    size_t block_size,
    const std::string& filename)
{
    std::ofstream file(filename);

    if (!file)
    {
        std::cerr
            << "Could not open "
            << filename
            << "\n";

        return;
    }

    file
        << "block,start,end,entropy,min_entropy,"
        << "chi_square,chi_p,lag1_mi\n";

    size_t block = 0;

    for (size_t start = 0;
         start < values.size();
         start += block_size)
    {
        size_t end =
            std::min(
                start + block_size,
                values.size()
            );

        std::vector<uint8_t> part(
            values.begin() + start,
            values.begin() + end
        );

        std::array<uint64_t, 256> counts{};

        for (uint8_t v : part)
            counts[v]++;

        double chi =
            chi_square_256(
                counts,
                part.size()
            );

        double p =
            chi_square_p_value(
                chi,
                255.0
            );

        double mi =
            lag_mutual_information(
                part,
                1
            );

        file
            << block
            << ","
            << start
            << ","
            << end
            << ","
            << shannon_entropy(part)
            << ","
            << min_entropy(part)
            << ","
            << chi
            << ","
            << p
            << ","
            << mi
            << "\n";

        block++;
    }

    file.close();
}


// ================================================================
// GENERIC BYTE ANALYSIS
// ================================================================

static void analyze_bytes(
    const std::vector<uint8_t>& values,
    const std::string& name)
{
    separator();

    std::cout
        << "ANALYSIS: "
        << name
        << "\n\n";

    if (values.empty())
    {
        std::cout
            << "No samples.\n";

        return;
    }

    std::array<uint64_t, 256> counts{};

    for (uint8_t v : values)
        counts[v]++;

    double chi =
        chi_square_256(
            counts,
            values.size()
        );

    double chi_p =
        chi_square_p_value(
            chi,
            255.0
        );

    std::cout
        << "Samples                 : "
        << values.size()
        << "\n";

    std::cout
        << "Shannon entropy         : "
        << shannon_entropy(values)
        << " bits\n";

    std::cout
        << "Maximum possible        : 8 bits\n";

    std::cout
        << "Min entropy             : "
        << min_entropy(values)
        << " bits\n";

    std::cout
        << "Chi-square              : "
        << chi
        << "\n";

    std::cout
        << "Chi-square approx p     : "
        << chi_p
        << "\n";

    std::cout
        << "\nLag analysis\n"
        << "------------\n";

    std::vector<double> numeric;

    numeric.reserve(values.size());

    for (uint8_t v : values)
        numeric.push_back(
            static_cast<double>(v)
        );

    const size_t max_lag =
        std::min<size_t>(
            20,
            values.size() - 1
        );

    for (size_t lag = 1;
         lag <= max_lag;
         ++lag)
    {
        double r =
            lag_correlation(
                numeric,
                lag
            );

        double mi =
            lag_mutual_information(
                values,
                lag
            );

        std::cout
            << "lag "
            << std::setw(2)
            << lag
            << " : correlation="
            << std::setw(12)
            << r
            << " MI="
            << mi
            << " bits\n";
    }

    ConditionalPredictionResult prediction =
        conditional_prediction(values);

    std::cout
        << "\nConditional prediction\n"
        << "----------------------\n"
        << "Correct                 : "
        << prediction.correct
        << "\n";

    std::cout
        << "Trials                  : "
        << prediction.total
        << "\n";

    std::cout
        << "Accuracy                : "
        << prediction.accuracy
        << "%\n";

    std::cout
        << "Uniform byte baseline   : "
        << random_byte_baseline()
        << "%\n";

    std::cout
        << "\nRLE structural ratio    : "
        << simple_rle_ratio(values)
        << "\n";

    std::cout
        << "\nPermutation test\n"
        << "----------------\n";

    double permutation_p =
        permutation_correlation_p(
            values,
            1000,
            0x123456789ABCDEF0ULL
        );

    std::cout
        << "Lag-1 permutation p     : "
        << permutation_p
        << "\n";

    print_bit_balance(
        std::vector<uint64_t>(
            values.begin(),
            values.end()
        ),
        8
    );
}


// ================================================================
// EXPERIMENT 1
// TSC MODE COMPARISON
// ================================================================

static void experiment_tsc_modes()
{
    separator();

    std::cout
        << "EXPERIMENT 1: TSC SAMPLING MODE COMPARISON\n\n";

    const size_t N = 10000;

    const std::array<TSCMode, 3> modes =
    {
        TSCMode::RDTSC_LFENCE,
        TSCMode::RDTSCP,
        TSCMode::CPUID_RDTSC
    };

    for (TSCMode mode : modes)
    {
        std::vector<uint64_t> deltas;

        deltas.reserve(N);

        uint64_t previous =
            read_tsc(mode);

        for (size_t i = 0;
             i < N;
             ++i)
        {
            uint64_t current =
                read_tsc(mode);

            deltas.push_back(
                current - previous
            );

            previous = current;
        }

        Stats s =
            calculate_stats(deltas);

        std::cout
            << "\nMode: "
            << tsc_mode_name(mode)
            << "\n";

        std::cout
            << "Min     : "
            << s.min
            << "\n";

        std::cout
            << "Max     : "
            << s.max
            << "\n";

        std::cout
            << "Mean    : "
            << s.mean
            << "\n";

        std::cout
            << "Std dev : "
            << s.stddev
            << "\n";
    }

    std::cout
        << "\nImportant: these are different measurement protocols.\n"
        << "They should not be treated as interchangeable TSC values.\n";
}


// ================================================================
// EXPERIMENT 2
// MACHINE TIMING
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

    std::vector<uint64_t> tsc;
    std::vector<uint64_t> deltas;

    tsc.reserve(N);
    deltas.reserve(N);

    uint64_t previous =
        read_tsc_lfence();

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t current =
            read_tsc_lfence();

        tsc.push_back(current);

        deltas.push_back(
            current - previous
        );

        previous = current;
    }

    Stats s =
        calculate_stats(deltas);

    std::cout
        << "\nSamples : "
        << N
        << "\n";

    std::cout
        << "Min delta : "
        << s.min
        << "\n";

    std::cout
        << "Max delta : "
        << s.max
        << "\n";

    std::cout
        << "Mean delta: "
        << s.mean
        << "\n";

    std::cout
        << "Std dev   : "
        << s.stddev
        << "\n";

    std::ofstream file(
        "machine_timing.csv"
    );

    file
        << "trial,tsc,delta,low8,low16\n";

    for (size_t i = 0;
         i < N;
         ++i)
    {
        file
            << i
            << ","
            << tsc[i]
            << ","
            << deltas[i]
            << ","
            << (tsc[i] & 0xFF)
            << ","
            << (tsc[i] & 0xFFFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: machine_timing.csv\n";
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

    const size_t N = 1000;

    std::cout
        << "Press ENTER "
        << N
        << " times.\n\n"

        << "Do not deliberately synchronize your presses.\n"
        << "Do not attempt to target a particular TSC value.\n\n"

        << "This experiment measures human timing variability.\n";

    std::cout
        << "\nPress ENTER to start...";

    std::cin.get();

    std::vector<uint64_t> timestamps;
    std::vector<uint64_t> deltas;
    std::vector<uint8_t> low8;

    timestamps.reserve(N);
    deltas.reserve(N);
    low8.reserve(N);

    uint64_t previous = 0;

    for (size_t i = 0;
         i < N;
         ++i)
    {
        std::cout
            << "\nENTER ["
            << i + 1
            << "/"
            << N
            << "] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc_lfence();

        uint64_t delta = 0;

        if (i > 0)
        {
            delta =
                tsc - previous;

            deltas.push_back(delta);
        }

        timestamps.push_back(tsc);

        low8.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );

        previous = tsc;

        std::cout
            << "TSC   = "
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
        << "\nHUMAN TIMING STATISTICS\n"
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

        << "Std dev : "
        << s.stddev
        << "\n";

    analyze_bytes(
        low8,
        "HUMAN TSC LOW8"
    );

    std::ofstream file(
        "human_timing.csv"
    );

    file
        << "trial,tsc,delta,low8,low16\n";

    for (size_t i = 0;
         i < timestamps.size();
         ++i)
    {
        uint64_t delta = 0;

        if (i > 0)
        {
            delta =
                timestamps[i] -
                timestamps[i - 1];
        }

        file
            << i
            << ","
            << timestamps[i]
            << ","
            << delta
            << ","
            << (timestamps[i] & 0xFF)
            << ","
            << (timestamps[i] & 0xFFFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: human_timing.csv\n";
}


// ================================================================
// EXPERIMENT 4
// MACHINE LOW-BIT ANALYSIS
// ================================================================

static void experiment_machine_low_bits()
{
    separator();

    std::cout
        << "EXPERIMENT 4: MACHINE LOW-BIT ANALYSIS\n\n";

    size_t N;

    std::cout
        << "Samples [default 1000000]: ";

    std::string input;

    std::getline(
        std::cin,
        input
    );

    if (input.empty())
        N = 1000000;
    else
        N = std::stoull(input);

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "MACHINE TSC LOW8"
    );

    block_analysis(
        values,
        10000,
        "machine_blocks.csv"
    );

    std::cout
        << "\nSaved: machine_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 5
// HUMAN LOW-BIT ANALYSIS
// ================================================================

static void experiment_human_low_bits()
{
    separator();

    std::cout
        << "EXPERIMENT 5: HUMAN LOW-BIT ANALYSIS\n\n";

    const size_t N = 1000;

    std::cout
        << "Press ENTER "
        << N
        << " times.\n";

    std::cout
        << "\nPress ENTER to begin...";

    std::cin.get();

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        std::cout
            << "\nENTER ["
            << i + 1
            << "/"
            << N
            << "] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "HUMAN TSC LOW8"
    );

    block_analysis(
        values,
        100,
        "human_blocks.csv"
    );

    std::cout
        << "\nSaved: human_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 6
// CONTROLLED MACHINE DELAY
// ================================================================
//
// fixed delay between TSC samples.
//
// ================================================================

static void experiment_fixed_delay()
{
    separator();

    std::cout
        << "EXPERIMENT 6: FIXED-DELAY CONTROL\n\n";

    const size_t N = 10000;

    const int delay_us = 100;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        std::this_thread::sleep_for(
            std::chrono::microseconds(
                delay_us
            )
        );

        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "FIXED DELAY"
    );

    block_analysis(
        values,
        1000,
        "fixed_delay_blocks.csv"
    );

    std::cout
        << "\nSaved: fixed_delay_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 7
// JITTERED MACHINE DELAY
// ================================================================

static void experiment_jittered_delay()
{
    separator();

    std::cout
        << "EXPERIMENT 7: JITTERED MACHINE DELAY CONTROL\n\n";

    const size_t N = 10000;

    std::mt19937_64 rng(
        read_tsc_lfence()
    );

    std::uniform_int_distribution<int>
        delay_dist(50, 150);

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        int delay =
            delay_dist(rng);

        std::this_thread::sleep_for(
            std::chrono::microseconds(
                delay
            )
        );

        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "JITTERED DELAY"
    );

    block_analysis(
        values,
        1000,
        "jittered_delay_blocks.csv"
    );

    std::cout
        << "\nSaved: jittered_delay_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 8
// MT19937 CONTROL
// ================================================================

static void experiment_mt_control()
{
    separator();

    std::cout
        << "EXPERIMENT 8: MT19937-64 CONTROL\n\n";

    const size_t N = 100000;

    std::mt19937_64 rng(
        read_tsc_lfence()
    );

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        values.push_back(
            static_cast<uint8_t>(
                rng() & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "MT19937-64 LOW8"
    );

    block_analysis(
        values,
        10000,
        "mt_blocks.csv"
    );

    std::cout
        << "\nSaved: mt_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 9
// OS CSPRNG CONTROL
// ================================================================

static void experiment_os_random()
{
    separator();

    std::cout
        << "EXPERIMENT 9: OS RANDOMNESS CONTROL\n\n";

    const size_t N = 100000;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t value =
            os_random_u64();

        values.push_back(
            static_cast<uint8_t>(
                value & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "OS RANDOM LOW8"
    );

    block_analysis(
        values,
        10000,
        "os_random_blocks.csv"
    );

    std::cout
        << "\nSaved: os_random_blocks.csv\n";
}


// ================================================================
// EXPERIMENT 10
// MULTI-LAG MACHINE ANALYSIS
// ================================================================

static void experiment_multi_lag()
{
    separator();

    std::cout
        << "EXPERIMENT 10: MULTI-LAG STRUCTURE\n\n";

    const size_t N = 100000;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    std::vector<double> numeric;

    numeric.reserve(N);

    for (uint8_t v : values)
        numeric.push_back(
            static_cast<double>(v)
        );

    std::ofstream file(
        "multi_lag_analysis.csv"
    );

    file
        << "lag,correlation,mutual_information\n";

    const size_t max_lag = 100;

    for (size_t lag = 1;
         lag <= max_lag;
         ++lag)
    {
        double r =
            lag_correlation(
                numeric,
                lag
            );

        double mi =
            lag_mutual_information(
                values,
                lag
            );

        file
            << lag
            << ","
            << r
            << ","
            << mi
            << "\n";

        std::cout
            << "lag "
            << std::setw(3)
            << lag
            << " : r="
            << std::setw(12)
            << r
            << " MI="
            << mi
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: multi_lag_analysis.csv\n";
}


// ================================================================
// EXPERIMENT 11
// PERMUTATION TEST
// ================================================================

static void experiment_permutation()
{
    separator();

    std::cout
        << "EXPERIMENT 11: PERMUTATION TEST\n\n";

    const size_t N = 50000;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    std::vector<double> numeric;

    numeric.reserve(N);

    for (uint8_t v : values)
        numeric.push_back(
            static_cast<double>(v)
        );

    double observed =
        lag_correlation(
            numeric,
            1
        );

    double p =
        permutation_correlation_p(
            values,
            5000,
            read_tsc_lfence()
        );

    std::cout
        << "Observed lag-1 correlation : "
        << observed
        << "\n";

    std::cout
        << "Permutation p-value         : "
        << p
        << "\n";

    std::cout
        << "\nInterpretation:\n"
        << "The permutation test asks whether the observed\n"
        << "association is unusual relative to shuffled sequences.\n";

    std::ofstream file(
        "permutation_test.csv"
    );

    file
        << "observed_correlation,p_value\n";

    file
        << observed
        << ","
        << p
        << "\n";

    file.close();

    std::cout
        << "\nSaved: permutation_test.csv\n";
}


// ================================================================
// EXPERIMENT 12
// BLOCK STABILITY
// ================================================================

static void experiment_block_stability()
{
    separator();

    std::cout
        << "EXPERIMENT 12: BLOCK STABILITY\n\n";

    const size_t N = 1000000;

    const size_t block_size = 10000;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    block_analysis(
        values,
        block_size,
        "block_stability.csv"
    );

    std::cout
        << "Samples     : "
        << N
        << "\n";

    std::cout
        << "Block size  : "
        << block_size
        << "\n";

    std::cout
        << "\nSaved: block_stability.csv\n";
}


// ================================================================
// EXPERIMENT 13
// HISTORICAL TEMPLEOS-STYLE CONTROL
// ================================================================
//
// IMPORTANT:
//
// This is intentionally separated from the scientific core.
//
// It is a reconstruction/control based on the historical form:
//
//     res = Fs->rand_seed;
//     res = LIN_CONGRUE_A * res ^
//           (res & 0xFFFFFFFF0000) >> 16
//           + LIN_CONGRUE_C;
//
//     if (!Bt(&Fs->task_flags, TASKf_NONTIMER_RAND))
//         res ^= GetTSC;
//
//     Fs->rand_seed = res;
//
// DO NOT call this "exact TempleOS RNG" until the original source
// expression and HolyC operator precedence have been verified.
//
// Parentheses below make the assumed interpretation explicit.
//
// ================================================================

class TempleOSStyleReconstruction
{
private:

    uint64_t state;

public:

    explicit TempleOSStyleReconstruction(
        uint64_t seed)
        : state(seed)
    {
    }

    uint64_t next(
        uint64_t tsc,
        bool use_tsc)
    {
        uint64_t res =
            state;

        //
        // Explicitly parenthesized reconstruction.
        //
        uint64_t transformed =
            (
                LCG_A *
                res
            )
            ^
            (
                (
                    res &
                    0xFFFFFFFF0000ULL
                )
                >> 16
            );

        transformed += LCG_C;

        if (use_tsc)
            transformed ^= tsc;

        state =
            transformed;

        return state;
    }
};


static void experiment_templeos_reconstruction()
{
    separator();

    std::cout
        << "EXPERIMENT 13: TEMPLEOS-STYLE HISTORICAL RECONSTRUCTION\n\n";

    std::cout
        << "WARNING:\n"
        << "This is a reconstruction/control, not a claim of\n"
        << "byte-for-byte TempleOS compatibility.\n\n";

    const size_t N = 100000;

    uint64_t seed =
        read_tsc_lfence();

    TempleOSStyleReconstruction rng(seed);

    std::vector<uint8_t> values;

    values.reserve(N);

    std::ofstream file(
        "templeos_reconstruction.csv"
    );

    file
        << "trial,tsc,rng,low8\n";

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        uint64_t value =
            rng.next(
                tsc,
                true
            );

        uint8_t low =
            static_cast<uint8_t>(
                value & 0xFF
            );

        values.push_back(low);

        file
            << i
            << ","
            << tsc
            << ","
            << value
            << ","
            << static_cast<int>(low)
            << "\n";
    }

    file.close();

    analyze_bytes(
        values,
        "TEMPLEOS-STYLE RECONSTRUCTION LOW8"
    );

    std::cout
        << "\nSaved: templeos_reconstruction.csv\n";
}


// ================================================================
// EXPERIMENT 14
// HUMAN VS MACHINE DATASET COMPARISON
// ================================================================

static bool read_column_uint64(
    const std::string& filename,
    size_t column,
    std::vector<uint64_t>& output)
{
    std::ifstream file(filename);

    if (!file)
        return false;

    std::string line;

    std::getline(file, line);

    while (std::getline(file, line))
    {
        std::stringstream ss(line);

        std::string cell;

        size_t current = 0;

        while (
            std::getline(
                ss,
                cell,
                ','
            )
        )
        {
            if (current == column)
            {
                try
                {
                    output.push_back(
                        std::stoull(cell)
                    );
                }
                catch (...)
                {
                }

                break;
            }

            current++;
        }
    }

    return !output.empty();
}


static void experiment_compare_existing()
{
    separator();

    std::cout
        << "EXPERIMENT 14: HUMAN VS MACHINE COMPARISON\n\n";

    std::vector<uint64_t> human;
    std::vector<uint64_t> machine;

    bool human_ok =
        read_column_uint64(
            "human_timing.csv",
            2,
            human
        );

    bool machine_ok =
        read_column_uint64(
            "machine_timing.csv",
            2,
            machine
        );

    if (!human_ok ||
        !machine_ok)
    {
        std::cout
            << "Run experiments 2 and 3 first.\n";

        return;
    }

    Stats hs =
        calculate_stats(human);

    Stats ms =
        calculate_stats(machine);

    std::cout
        << "HUMAN\n"
        << "------\n"
        << "n       : "
        << human.size()
        << "\n"
        << "mean    : "
        << hs.mean
        << "\n"
        << "stddev  : "
        << hs.stddev
        << "\n"
        << "min     : "
        << hs.min
        << "\n"
        << "max     : "
        << hs.max
        << "\n";

    std::cout
        << "\nMACHINE\n"
        << "-------\n"
        << "n       : "
        << machine.size()
        << "\n"
        << "mean    : "
        << ms.mean
        << "\n"
        << "stddev  : "
        << ms.stddev
        << "\n"
        << "min     : "
        << ms.min
        << "\n"
        << "max     : "
        << ms.max
        << "\n";

    std::cout
        << "\nThis comparison is descriptive only.\n"
        << "Use external statistical software for formal\n"
        << "distribution tests such as KS / Mann-Whitney.\n";
}


// ================================================================
// EXPERIMENT 15
// AUTOMATED SUMMARY
// ================================================================

static void experiment_summary()
{
    separator();

    std::cout
        << "EXPERIMENT 15: AUTOMATED TSC SUMMARY\n\n";

    const size_t N = 1000000;

    std::vector<uint8_t> values;

    values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc_lfence();

        values.push_back(
            static_cast<uint8_t>(
                tsc & 0xFF
            )
        );
    }

    analyze_bytes(
        values,
        "AUTOMATED TSC SUMMARY"
    );

    block_analysis(
        values,
        10000,
        "summary_blocks.csv"
    );

    std::cout
        << "\nSaved: summary_blocks.csv\n";
}


// ================================================================
// MENU
// ================================================================

static void print_menu()
{
    separator();

    std::cout
        << "        TSC / HUMAN ENTROPY LABORATORY v3\n\n"

        << " 1. Compare TSC sampling modes\n"
        << " 2. Machine TSC timing\n"
        << " 3. Human timing\n"
        << " 4. Machine low-bit analysis\n"
        << " 5. Human low-bit analysis\n"
        << " 6. Fixed-delay machine control\n"
        << " 7. Jittered-delay machine control\n"
        << " 8. MT19937 control\n"
        << " 9. OS randomness control\n"
        << "10. Multi-lag correlation + mutual information\n"
        << "11. Permutation test\n"
        << "12. Block stability\n"
        << "13. TempleOS-style reconstruction control\n"
        << "14. Human vs machine timing comparison\n"
        << "15. Automated TSC summary\n\n"

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
        << "================================================================\n"
        << "        TSC / HUMAN ENTROPY LABORATORY v3\n"
        << "================================================================\n\n";

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
        << "Architecture: unsupported/non-x86\n";

#endif

    std::cout
        << "\nScientific principles:\n\n"

        << "High entropy\n"
        << "    != independence\n"
        << "    != unpredictability\n"
        << "    != cryptographic security\n\n"

        << "The program measures these properties separately.\n\n"

        << "No oracle interpretation is part of the scientific core.\n";

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
                    experiment_tsc_modes();
                    break;

                case 2:
                    experiment_machine_timing();
                    break;

                case 3:
                    experiment_human_timing();
                    break;

                case 4:
                    experiment_machine_low_bits();
                    break;

                case 5:
                    experiment_human_low_bits();
                    break;

                case 6:
                    experiment_fixed_delay();
                    break;

                case 7:
                    experiment_jittered_delay();
                    break;

                case 8:
                    experiment_mt_control();
                    break;

                case 9:
                    experiment_os_random();
                    break;

                case 10:
                    experiment_multi_lag();
                    break;

                case 11:
                    experiment_permutation();
                    break;

                case 12:
                    experiment_block_stability();
                    break;

                case 13:
                    experiment_templeos_reconstruction();
                    break;

                case 14:
                    experiment_compare_existing();
                    break;

                case 15:
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

        std::cout
            << "\n";
    }

    return 0;
}