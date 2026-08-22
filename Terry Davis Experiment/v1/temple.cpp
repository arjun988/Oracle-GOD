/*
    ============================================================
        TEMPLEOS / TERRY DAVIS TSC EXPERIMENT
    ============================================================

    Purpose:
        Explore the idea behind Terry Davis' use of CPU timing
        and randomness in TempleOS.

    Experiments:
        1. Raw TSC measurements
        2. TSC delta distribution
        3. Human timing experiment
        4. Machine timing experiment
        5. TempleOS-style RNG
        6. Human-TSC seeded RNG
        7. Oracle / God-Says-style output
        8. Large automated statistical experiment
        9. YES/NO/WAIT/UNKNOWN experiment
       10. Entropy analysis
       11. Chi-square analysis
       12. Collision analysis
       13. Blind prediction experiment
       14. Comparison against standard PRNG
       15. CSV export

    IMPORTANT:
        This is an experimental reconstruction.

        It is NOT the TempleOS kernel itself and is not claimed
        to reproduce every historical TempleOS release bit-for-bit.

        The goal is to experimentally investigate:
            - CPU timing
            - human timing
            - unpredictability
            - deterministic PRNG behavior
            - statistical anomalies

    Compile:
        Windows / MinGW:
            g++ -O2 -std=c++17 temple_experiment.cpp -o temple_experiment.exe

        Linux:
            g++ -O2 -std=c++17 temple_experiment.cpp -o temple_experiment

        MSVC:
            cl /O2 /std:c++17 temple_experiment.cpp

    ============================================================
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

static constexpr size_t DEFAULT_TRIALS = 10000;


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


// ------------------------------------------------------------
// More ordered TSC read.
// CPUID is expensive, but useful when we want stronger
// serialization for an experiment.
// ------------------------------------------------------------

static inline uint64_t read_tsc_serialized()
{
#ifdef _MSC_VER

    int cpuInfo[4];
    __cpuid(cpuInfo, 0);

    return __rdtsc();

#elif defined(__GNUC__) || defined(__clang__)

    unsigned int eax, ebx, ecx, edx;

    __asm__ volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(0)
        : "memory"
    );

    return ((uint64_t)edx << 32) | eax;

#endif
}


// ============================================================
// UTILITY
// ============================================================

static std::string csv_escape(const std::string& s)
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


static void pause_enter()
{
    std::cout << "\nPress ENTER to continue...";
    std::cin.get();
}


static void print_separator()
{
    std::cout
        << "\n============================================================\n";
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

    s.min = *std::min_element(values.begin(), values.end());
    s.max = *std::max_element(values.begin(), values.end());

    long double sum = 0.0L;

    for (uint64_t v : values)
        sum += (long double)v;

    s.mean =
        (double)(sum / values.size());

    long double variance = 0.0L;

    for (uint64_t v : values)
    {
        long double d =
            (long double)v - s.mean;

        variance += d * d;
    }

    if (values.size() > 1)
        variance /= (values.size() - 1);

    s.variance = (double)variance;
    s.stddev = std::sqrt(s.variance);

    return s;
}


// ============================================================
// SHANNON ENTROPY
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

    const double n =
        static_cast<double>(values.size());

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


// ============================================================
// MIN ENTROPY
// ============================================================

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
        static_cast<double>(max_count) /
        static_cast<double>(values.size());

    return -std::log2(probability);
}


// ============================================================
// CHI SQUARE
// ============================================================

static double chi_square_uniform(
    const std::vector<uint64_t>& values,
    uint64_t bucket_count)
{
    if (values.empty() || bucket_count == 0)
        return 0.0;

    std::vector<uint64_t> buckets(bucket_count, 0);

    for (uint64_t v : values)
        buckets[v % bucket_count]++;

    const double expected =
        static_cast<double>(values.size()) /
        bucket_count;

    double chi = 0.0;

    for (uint64_t observed : buckets)
    {
        double difference =
            observed - expected;

        chi +=
            (difference * difference) /
            expected;
    }

    return chi;
}


// ============================================================
// TEMPLEOS STYLE RNG
// ============================================================

class TempleRNG
{
private:

    uint64_t state;

public:

    explicit TempleRNG(uint64_t seed)
        : state(seed)
    {
    }

    uint64_t next()
    {
        /*
            TempleOS used an LCG-style state transition and
            mixed the TSC into the result.

            This implementation intentionally keeps the
            operations in uint64_t so arithmetic wraps modulo
            2^64.
        */

        uint64_t res = state;

        uint64_t high_mix =
            ((res & 0xFFFFFFFF0000ULL) >> 16);

        res =
            (TEMPLE_A * res)
            ^ high_mix;

        res += TEMPLE_C;

        res ^= read_tsc();

        state = res;

        return res;
    }

    uint64_t next_without_tsc()
    {
        /*
            Useful for comparison.

            Pure deterministic LCG.
        */

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

    uint64_t get_state() const
    {
        return state;
    }
};


// ============================================================
// WORD LIST
// ============================================================

static const std::vector<std::string> WORDS =
{
    "wisdom",
    "knowledge",
    "understanding",
    "truth",
    "faith",
    "hope",
    "love",
    "peace",
    "patience",
    "strength",
    "courage",
    "light",
    "darkness",
    "path",
    "journey",
    "beginning",
    "end",
    "wait",
    "build",
    "create",
    "learn",
    "study",
    "work",
    "rest",
    "listen",
    "speak",
    "remember",
    "forget",
    "change",
    "continue",
    "stop",
    "return",
    "search",
    "question",
    "answer",
    "truth",
    "reason",
    "choice",
    "freedom",
    "time",
    "future",
    "past",
    "present",
    "world",
    "life",
    "death",
    "fire",
    "water",
    "earth",
    "sky",
    "star",
    "sun",
    "moon",
    "computer",
    "machine",
    "code",
    "program",
    "system",
    "memory",
    "random",
    "signal",
    "noise",
    "chance",
    "order",
    "pattern",
    "number",
    "clock",
    "cycle",
    "begin",
    "finish",
    "focus",
    "move",
    "stand",
    "rise",
    "fall",
    "open",
    "close",
    "look",
    "see",
    "hear",
    "know",
    "do",
    "make",
    "give",
    "take",
    "trust",
    "doubt",
    "test",
    "measure",
    "observe",
    "discover",
    "unknown",
    "mystery",
    "signal",
    "answer",
    "question"
};


// ============================================================
// ORACLE RESULT
// ============================================================

static std::string oracle_word(uint64_t value)
{
    size_t index =
        static_cast<size_t>(
            value % WORDS.size()
        );

    return WORDS[index];
}


static std::string oracle_category(uint64_t value)
{
    /*
        Predefined mapping.

        IMPORTANT:
        These meanings are decided BEFORE the experiment.

        This prevents changing the interpretation after seeing
        the result.
    */

    uint64_t bucket =
        value % 100;

    if (bucket < 25)
        return "YES";

    if (bucket < 50)
        return "NO";

    if (bucket < 75)
        return "WAIT";

    return "UNKNOWN";
}


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
        << "Reading the CPU timestamp counter 50 times.\n\n";

    std::vector<uint64_t> values;

    for (int i = 0; i < 50; ++i)
    {
        uint64_t tsc =
            read_tsc();

        values.push_back(tsc);

        std::cout
            << std::setw(2)
            << i
            << " : "
            << tsc
            << "\n";
    }

    Statistics stats =
        calculate_stats(values);

    std::cout
        << "\nStatistics:\n"
        << "Min     : " << stats.min << "\n"
        << "Max     : " << stats.max << "\n"
        << "Mean    : " << stats.mean << "\n"
        << "Std Dev : " << stats.stddev << "\n";
}


// ============================================================
// EXPERIMENT 2
// TSC DELTAS
// ============================================================

void experiment_tsc_deltas()
{
    print_separator();

    std::cout
        << "EXPERIMENT 2: TSC DELTAS\n\n";

    std::cout
        << "Measuring the number of TSC ticks between reads.\n\n";

    std::vector<uint64_t> deltas;

    uint64_t previous =
        read_tsc_serialized();

    for (int i = 0; i < 10000; ++i)
    {
        uint64_t current =
            read_tsc_serialized();

        uint64_t delta =
            current - previous;

        deltas.push_back(delta);

        previous = current;
    }

    Statistics stats =
        calculate_stats(deltas);

    std::cout
        << "Samples : "
        << deltas.size()
        << "\n"
        << "Min     : "
        << stats.min
        << "\n"
        << "Max     : "
        << stats.max
        << "\n"
        << "Mean    : "
        << stats.mean
        << "\n"
        << "Std Dev : "
        << stats.stddev
        << "\n"
        << "Shannon entropy of exact deltas: "
        << shannon_entropy(deltas)
        << " bits/sample\n"
        << "Min entropy of exact deltas: "
        << min_entropy(deltas)
        << " bits/sample\n";

    std::ofstream file(
        "tsc_deltas.csv"
    );

    file
        << "index,delta\n";

    for (size_t i = 0; i < deltas.size(); ++i)
    {
        file
            << i
            << ","
            << deltas[i]
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: tsc_deltas.csv\n";
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

    std::cout
        << "You will press ENTER 100 times.\n"
        << "Do NOT deliberately synchronize your presses.\n"
        << "Take roughly 0.5-2 seconds between presses.\n\n";

    pause_enter();

    std::vector<uint64_t> timestamps;
    std::vector<uint64_t> deltas;

    for (int i = 0; i < 100; ++i)
    {
        std::cout
            << "\nPress ENTER ["
            << (i + 1)
            << "/100] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc_serialized();

        timestamps.push_back(tsc);

        uint64_t delta = 0;

        if (i > 0)
        {
            delta =
                tsc -
                timestamps[i - 1];

            deltas.push_back(delta);
        }

        std::cout
            << "TSC = "
            << tsc;

        if (i > 0)
        {
            std::cout
                << " | delta = "
                << delta;
        }

        std::cout << "\n";
    }

    std::ofstream file(
        "human_timing.csv"
    );

    file
        << "index,tsc,delta,low16,low8\n";

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
            << (timestamps[i] & 0xFFFF)
            << ","
            << (timestamps[i] & 0xFF)
            << "\n";
    }

    file.close();

    if (!deltas.empty())
    {
        Statistics stats =
            calculate_stats(deltas);

        std::cout
            << "\nHuman timing statistics:\n"
            << "Min delta     : "
            << stats.min
            << "\n"
            << "Max delta     : "
            << stats.max
            << "\n"
            << "Mean delta    : "
            << stats.mean
            << "\n"
            << "Std deviation : "
            << stats.stddev
            << "\n"
            << "Shannon entropy: "
            << shannon_entropy(deltas)
            << "\n"
            << "Min entropy   : "
            << min_entropy(deltas)
            << "\n";
    }

    std::cout
        << "\nSaved: human_timing.csv\n";
}


// ============================================================
// EXPERIMENT 4
// MACHINE TIMING
// ============================================================

void experiment_machine_timing()
{
    print_separator();

    std::cout
        << "EXPERIMENT 4: MACHINE TIMING\n\n";

    std::cout
        << "Generating 10,000 timing samples automatically.\n";

    std::vector<uint64_t> timestamps;
    std::vector<uint64_t> deltas;

    uint64_t previous =
        read_tsc_serialized();

    for (int i = 0; i < 10000; ++i)
    {
        /*
            Small variable workload.
        */

        volatile uint64_t x =
            static_cast<uint64_t>(i);

        for (int j = 0; j < (i % 31); ++j)
            x = x * 6364136223846793005ULL + 1;

        (void)x;

        uint64_t current =
            read_tsc_serialized();

        timestamps.push_back(current);

        deltas.push_back(
            current - previous
        );

        previous = current;
    }

    Statistics stats =
        calculate_stats(deltas);

    std::cout
        << "\nSamples       : "
        << deltas.size()
        << "\n"
        << "Min delta     : "
        << stats.min
        << "\n"
        << "Max delta     : "
        << stats.max
        << "\n"
        << "Mean delta    : "
        << stats.mean
        << "\n"
        << "Std deviation : "
        << stats.stddev
        << "\n"
        << "Shannon entropy: "
        << shannon_entropy(deltas)
        << "\n"
        << "Min entropy   : "
        << min_entropy(deltas)
        << "\n";

    std::ofstream file(
        "machine_timing.csv"
    );

    file
        << "index,tsc,delta,low16,low8\n";

    for (size_t i = 0;
         i < timestamps.size();
         ++i)
    {
        file
            << i
            << ","
            << timestamps[i]
            << ","
            << deltas[i]
            << ","
            << (timestamps[i] & 0xFFFF)
            << ","
            << (timestamps[i] & 0xFF)
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: machine_timing.csv\n";
}


// ============================================================
// EXPERIMENT 5
// TEMPLEOS RNG
// ============================================================

void experiment_temple_rng()
{
    print_separator();

    std::cout
        << "EXPERIMENT 5: TEMPLEOS-STYLE RNG\n\n";

    uint64_t seed =
        read_tsc();

    TempleRNG rng(seed);

    std::cout
        << "Seed = "
        << seed
        << "\n\n";

    std::vector<uint64_t> values;

    for (int i = 0; i < 100; ++i)
    {
        uint64_t value =
            rng.next();

        values.push_back(value);

        std::cout
            << std::setw(3)
            << i
            << " : "
            << value
            << "\n";
    }

    std::cout
        << "\nShannon entropy: "
        << shannon_entropy(values)
        << " bits/sample\n";

    std::cout
        << "Min entropy: "
        << min_entropy(values)
        << " bits/sample\n";
}


// ============================================================
// EXPERIMENT 6
// HUMAN TSC -> RNG
// ============================================================

void experiment_human_rng()
{
    print_separator();

    std::cout
        << "EXPERIMENT 6: HUMAN TSC -> TEMPLEOS RNG\n\n";

    std::cout
        << "This is the important experiment.\n\n";

    std::cout
        << "Each time you press ENTER:\n\n"
        << "    human action\n"
        << "         |\n"
        << "         v\n"
        << "       TSC\n"
        << "         |\n"
        << "         v\n"
        << "    RNG state\n"
        << "         |\n"
        << "         v\n"
        << "      result\n\n";

    std::cout
        << "Press ENTER 100 times.\n";

    pause_enter();

    std::ofstream file(
        "human_rng.csv"
    );

    file
        << "trial,tsc,delta,rng,word,category\n";

    uint64_t previous_tsc = 0;

    for (int i = 0; i < 100; ++i)
    {
        std::cout
            << "\nENTER ["
            << (i + 1)
            << "/100] ";

        std::cin.get();

        uint64_t tsc =
            read_tsc_serialized();

        uint64_t delta = 0;

        if (i > 0)
            delta =
                tsc - previous_tsc;

        /*
            Here the human timing itself becomes the seed.

            We deliberately make the seed depend on the
            observed physical timing event.
        */

        TempleRNG rng(tsc);

        uint64_t value =
            rng.next();

        std::string word =
            oracle_word(value);

        std::string category =
            oracle_category(value);

        std::cout
            << "TSC      = "
            << tsc
            << "\n"
            << "Delta    = "
            << delta
            << "\n"
            << "RNG      = "
            << value
            << "\n"
            << "WORD     = "
            << word
            << "\n"
            << "CATEGORY = "
            << category
            << "\n";

        file
            << i
            << ","
            << tsc
            << ","
            << delta
            << ","
            << value
            << ","
            << csv_escape(word)
            << ","
            << category
            << "\n";

        previous_tsc =
            tsc;
    }

    file.close();

    std::cout
        << "\nSaved: human_rng.csv\n";
}


// ============================================================
// EXPERIMENT 7
// ORACLE
// ============================================================

void experiment_oracle()
{
    print_separator();

    std::cout
        << "EXPERIMENT 7: ORACLE\n\n";

    std::cout
        << "This is deliberately simple.\n"
        << "It uses physical timing as an entropy input and\n"
        << "maps the result to a predefined word.\n\n";

    std::cout
        << "Ask yourself a question.\n"
        << "Do NOT change the interpretation after the output.\n\n";

    pause_enter();

    uint64_t tsc =
        read_tsc_serialized();

    TempleRNG rng(tsc);

    uint64_t value =
        rng.next();

    std::string word =
        oracle_word(value);

    std::string category =
        oracle_category(value);

    std::cout
        << "\n"
        << "=============================================\n"
        << "                ORACLE\n"
        << "=============================================\n\n";

    std::cout
        << "TSC       : "
        << tsc
        << "\n"
        << "RNG       : "
        << value
        << "\n"
        << "Category  : "
        << category
        << "\n"
        << "Word      : "
        << word
        << "\n\n";

    std::cout
        << "=============================================\n";

    std::cout
        << "\nRemember: a meaningful interpretation is not\n"
        << "itself evidence that the output was externally caused.\n";
}


// ============================================================
// EXPERIMENT 8
// LARGE AUTOMATED ORACLE
// ============================================================

void experiment_large_oracle()
{
    print_separator();

    std::cout
        << "EXPERIMENT 8: LARGE ORACLE EXPERIMENT\n\n";

    size_t trials;

    std::cout
        << "Number of trials [default "
        << DEFAULT_TRIALS
        << "]: ";

    std::string input;

    std::getline(std::cin, input);

    if (input.empty())
        trials = DEFAULT_TRIALS;
    else
        trials =
            std::stoull(input);

    std::cout
        << "\nRunning "
        << trials
        << " trials...\n\n";

    std::ofstream file(
        "oracle_experiment.csv"
    );

    file
        << "trial,tsc,rng,word,category\n";

    std::map<std::string, uint64_t>
        categories;

    std::map<std::string, uint64_t>
        words;

    std::vector<uint64_t>
        values;

    values.reserve(trials);

    for (size_t i = 0;
         i < trials;
         ++i)
    {
        uint64_t tsc =
            read_tsc();

        TempleRNG rng(tsc);

        uint64_t value =
            rng.next();

        std::string word =
            oracle_word(value);

        std::string category =
            oracle_category(value);

        values.push_back(value);

        categories[category]++;
        words[word]++;

        file
            << i
            << ","
            << tsc
            << ","
            << value
            << ","
            << csv_escape(word)
            << ","
            << category
            << "\n";

        /*
            Give the scheduler a tiny opportunity to behave
            naturally.

            This also means the timing source isn't simply a
            tight deterministic loop.
        */

        if ((i % 1000) == 0)
        {
            std::cout
                << "\rProgress: "
                << i
                << "/"
                << trials
                << std::flush;
        }
    }

    file.close();

    std::cout
        << "\rProgress: "
        << trials
        << "/"
        << trials
        << "\n\n";

    std::cout
        << "CATEGORY DISTRIBUTION\n"
        << "---------------------\n";

    for (const auto& p : categories)
    {
        double percentage =
            100.0 *
            p.second /
            static_cast<double>(trials);

        std::cout
            << std::setw(8)
            << p.first
            << " : "
            << p.second
            << " ("
            << percentage
            << "%)\n";
    }

    std::cout
        << "\nExpected approximately:\n"
        << "YES      25%\n"
        << "NO       25%\n"
        << "WAIT     25%\n"
        << "UNKNOWN  25%\n";

    std::cout
        << "\nWORD DISTRIBUTION\n"
        << "-----------------\n";

    std::vector<
        std::pair<std::string, uint64_t>
    > sorted_words(
        words.begin(),
        words.end()
    );

    std::sort(
        sorted_words.begin(),
        sorted_words.end(),
        [](const auto& a, const auto& b)
        {
            return a.second > b.second;
        }
    );

    size_t show =
        std::min<size_t>(
            20,
            sorted_words.size()
        );

    for (size_t i = 0;
         i < show;
         ++i)
    {
        std::cout
            << std::setw(20)
            << sorted_words[i].first
            << " : "
            << sorted_words[i].second
            << "\n";
    }

    std::cout
        << "\nSaved: oracle_experiment.csv\n";

    /*
        Analyze low-byte distribution.
    */

    std::cout
        << "\nLOW-BYTE CHI-SQUARE\n";

    std::vector<uint64_t>
        low_bytes;

    low_bytes.reserve(values.size());

    for (uint64_t v : values)
        low_bytes.push_back(v & 0xFF);

    double chi =
        chi_square_uniform(
            low_bytes,
            256
        );

    std::cout
        << "Chi-square statistic: "
        << chi
        << "\n";

    std::cout
        << "Shannon entropy of low byte: "
        << shannon_entropy(low_bytes)
        << " bits\n";

    std::cout
        << "Maximum possible: 8 bits\n";
}


// ============================================================
// EXPERIMENT 9
// COLLISION ANALYSIS
// ============================================================

void experiment_collisions()
{
    print_separator();

    std::cout
        << "EXPERIMENT 9: COLLISION ANALYSIS\n\n";

    const size_t N = 100000;

    std::cout
        << "Generating "
        << N
        << " TSC-derived outputs...\n";

    std::set<uint64_t> unique_values;

    uint64_t collisions = 0;

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc();

        TempleRNG rng(tsc);

        uint64_t value =
            rng.next();

        if (!unique_values.insert(value).second)
            collisions++;
    }

    std::cout
        << "\nTotal samples : "
        << N
        << "\n"
        << "Unique values : "
        << unique_values.size()
        << "\n"
        << "Collisions    : "
        << collisions
        << "\n";

    std::cout
        << "\nFor 64-bit outputs, collisions should generally be\n"
        << "extremely rare at this sample size if the values are\n"
        << "well distributed.\n";
}


// ============================================================
// EXPERIMENT 10
// PURE LCG VS TSC-MIXED
// ============================================================

void experiment_lcg_comparison()
{
    print_separator();

    std::cout
        << "EXPERIMENT 10: PURE LCG VS TSC-MIXED RNG\n\n";

    const size_t N = 100000;

    uint64_t seed =
        read_tsc();

    TempleRNG temple(seed);
    TempleRNG pure(seed);

    std::vector<uint64_t>
        temple_values;

    std::vector<uint64_t>
        pure_values;

    temple_values.reserve(N);
    pure_values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        temple_values.push_back(
            temple.next()
        );

        pure_values.push_back(
            pure.next_without_tsc()
        );
    }

    std::cout
        << "Pure deterministic LCG:\n"
        << "  Shannon entropy = "
        << shannon_entropy(pure_values)
        << "\n"
        << "  Min entropy     = "
        << min_entropy(pure_values)
        << "\n\n";

    std::cout
        << "TSC-mixed:\n"
        << "  Shannon entropy = "
        << shannon_entropy(temple_values)
        << "\n"
        << "  Min entropy     = "
        << min_entropy(temple_values)
        << "\n";

    std::ofstream file(
        "rng_comparison.csv"
    );

    file
        << "index,pure_lcg,tsc_mixed\n";

    for (size_t i = 0;
         i < N;
         ++i)
    {
        file
            << i
            << ","
            << pure_values[i]
            << ","
            << temple_values[i]
            << "\n";
    }

    file.close();

    std::cout
        << "\nSaved: rng_comparison.csv\n";
}


// ============================================================
// EXPERIMENT 11
// STANDARD PRNG COMPARISON
// ============================================================

void experiment_standard_comparison()
{
    print_separator();

    std::cout
        << "EXPERIMENT 11: STANDARD PRNG COMPARISON\n\n";

    const size_t N = 100000;

    uint64_t seed =
        read_tsc();

    TempleRNG temple(seed);

    std::mt19937_64 mt(seed);

    std::vector<uint64_t>
        temple_values;

    std::vector<uint64_t>
        mt_values;

    temple_values.reserve(N);
    mt_values.reserve(N);

    for (size_t i = 0;
         i < N;
         ++i)
    {
        temple_values.push_back(
            temple.next()
        );

        mt_values.push_back(
            mt()
        );
    }

    std::cout
        << "Temple-style:\n"
        << "  Shannon entropy = "
        << shannon_entropy(temple_values)
        << "\n"
        << "  Min entropy     = "
        << min_entropy(temple_values)
        << "\n\n";

    std::cout
        << "Mersenne Twister:\n"
        << "  Shannon entropy = "
        << shannon_entropy(mt_values)
        << "\n"
        << "  Min entropy     = "
        << min_entropy(mt_values)
        << "\n";

    std::cout
        << "\nThis is NOT a cryptographic-security test.\n"
        << "Entropy estimated from finite samples is only an\n"
        << "empirical statistic.\n";
}


// ============================================================
// EXPERIMENT 12
// BLIND PREDICTION
// ============================================================

void experiment_prediction()
{
    print_separator();

    std::cout
        << "EXPERIMENT 12: BLIND PREDICTION\n\n";

    std::cout
        << "We will NOT ask you to predict the exact TSC.\n"
        << "Instead, predict its lowest byte.\n\n";

    std::cout
        << "Before each measurement, enter your prediction\n"
        << "from 0 to 255.\n\n";

    const int N = 100;

    int correct = 0;

    std::ofstream file(
        "prediction_experiment.csv"
    );

    file
        << "trial,prediction,actual,correct\n";

    for (int i = 0;
         i < N;
         ++i)
    {
        int prediction;

        while (true)
        {
            std::cout
                << "Trial "
                << (i + 1)
                << "/"
                << N
                << " prediction [0-255]: ";

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

        uint64_t tsc =
            read_tsc_serialized();

        int actual =
            static_cast<int>(
                tsc & 0xFF
            );

        bool is_correct =
            prediction == actual;

        if (is_correct)
            correct++;

        std::cout
            << "Actual = "
            << actual
            << (is_correct
                ? "  CORRECT"
                : "")
            << "\n";

        file
            << i
            << ","
            << prediction
            << ","
            << actual
            << ","
            << (is_correct ? 1 : 0)
            << "\n";
    }

    file.close();

    double accuracy =
        100.0 *
        correct /
        static_cast<double>(N);

    std::cout
        << "\nCorrect: "
        << correct
        << "/"
        << N
        << "\n"
        << "Accuracy: "
        << accuracy
        << "%\n"
        << "Random 1-byte guessing expectation: "
        << (100.0 / 256.0)
        << "%\n";

    std::cout
        << "\nSaved: prediction_experiment.csv\n";
}


// ============================================================
// EXPERIMENT 13
// LOW-BIT DISTRIBUTION
// ============================================================

void experiment_low_bits()
{
    print_separator();

    std::cout
        << "EXPERIMENT 13: LOW-BIT DISTRIBUTION\n\n";

    const size_t N = 1000000;

    std::array<uint64_t, 256> counts{};

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t tsc =
            read_tsc();

        TempleRNG rng(tsc);

        uint64_t value =
            rng.next();

        counts[
            value & 0xFF
        ]++;
    }

    double expected =
        static_cast<double>(N) /
        256.0;

    double chi = 0.0;

    for (uint64_t observed : counts)
    {
        double d =
            observed - expected;

        chi +=
            d * d /
            expected;
    }

    std::cout
        << "Samples: "
        << N
        << "\n"
        << "Buckets: 256\n"
        << "Expected count/bucket: "
        << expected
        << "\n"
        << "Chi-square: "
        << chi
        << "\n\n";

    std::cout
        << "First 32 buckets:\n\n";

    for (int i = 0; i < 32; ++i)
    {
        std::cout
            << std::setw(3)
            << i
            << " : "
            << counts[i]
            << "\n";
    }

    std::ofstream file(
        "low_bits.csv"
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
        << "\nSaved: low_bits.csv\n";
}


// ============================================================
// EXPERIMENT 14
// REPRODUCIBILITY
// ============================================================

void experiment_reproducibility()
{
    print_separator();

    std::cout
        << "EXPERIMENT 14: REPRODUCIBILITY\n\n";

    uint64_t seed;

    std::cout
        << "Enter a seed: ";

    std::cin
        >> seed;

    std::cin.ignore(
        std::numeric_limits<
            std::streamsize
        >::max(),
        '\n'
    );

    TempleRNG rng(seed);

    std::cout
        << "\nFirst 20 deterministic outputs:\n\n";

    for (int i = 0;
         i < 20;
         ++i)
    {
        std::cout
            << i
            << " : "
            << rng.next_without_tsc()
            << "\n";
    }

    std::cout
        << "\nRun this again with the same seed.\n"
        << "The pure LCG sequence will reproduce exactly.\n\n";

    std::cout
        << "This demonstrates the distinction between:\n"
        << "\n"
        << "    entropy source\n"
        << "          vs\n"
        << "    deterministic PRNG\n";
}


// ============================================================
// EXPERIMENT 15
// AUTOMATIC SUMMARY
// ============================================================

void experiment_summary()
{
    print_separator();

    std::cout
        << "AUTOMATIC SUMMARY\n\n";

    std::cout
        << "This runs several quick tests automatically.\n";

    const size_t N = 100000;

    std::vector<uint64_t>
        tsc_deltas;

    std::vector<uint64_t>
        rng_values;

    tsc_deltas.reserve(N);
    rng_values.reserve(N);

    uint64_t previous =
        read_tsc_serialized();

    for (size_t i = 0;
         i < N;
         ++i)
    {
        uint64_t current =
            read_tsc_serialized();

        tsc_deltas.push_back(
            current - previous
        );

        previous = current;

        TempleRNG rng(current);

        rng_values.push_back(
            rng.next()
        );
    }

    Statistics tsc_stats =
        calculate_stats(tsc_deltas);

    std::cout
        << "\nTSC delta statistics\n"
        << "--------------------\n"
        << "Min       : "
        << tsc_stats.min
        << "\n"
        << "Max       : "
        << tsc_stats.max
        << "\n"
        << "Mean      : "
        << tsc_stats.mean
        << "\n"
        << "Std Dev   : "
        << tsc_stats.stddev
        << "\n"
        << "Shannon   : "
        << shannon_entropy(tsc_deltas)
        << "\n"
        << "Min entropy: "
        << min_entropy(tsc_deltas)
        << "\n";

    std::cout
        << "\nRNG output statistics\n"
        << "---------------------\n"
        << "Shannon   : "
        << shannon_entropy(rng_values)
        << "\n"
        << "Min entropy: "
        << min_entropy(rng_values)
        << "\n";

    std::vector<uint64_t>
        low_bytes;

    low_bytes.reserve(N);

    for (uint64_t v : rng_values)
        low_bytes.push_back(
            v & 0xFF
        );

    std::cout
        << "Low-byte Shannon entropy: "
        << shannon_entropy(low_bytes)
        << " bits\n";

    std::cout
        << "\nSummary complete.\n";
}


// ============================================================
// MENU
// ============================================================

void print_menu()
{
    print_separator();

    std::cout
        << "        TEMPLEOS / TSC EXPERIMENT LAB\n"
        << "\n"

        << " 1. Raw TSC measurements\n"
        << " 2. TSC delta analysis\n"
        << " 3. Human timing experiment\n"
        << " 4. Machine timing experiment\n"
        << " 5. TempleOS-style RNG\n"
        << " 6. Human TSC -> RNG\n"
        << " 7. Oracle / God Says-style output\n"
        << " 8. Large oracle experiment\n"
        << " 9. Collision analysis\n"
        << "10. Pure LCG vs TSC-mixed\n"
        << "11. Temple RNG vs Mersenne Twister\n"
        << "12. Blind prediction experiment\n"
        << "13. Low-bit distribution\n"
        << "14. Reproducibility experiment\n"
        << "15. Automatic summary\n"
        << "\n"
        << " 0. Exit\n";

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
        << "       TEMPLEOS / TERRY DAVIS EXPERIMENT LAB\n"
        << "============================================================\n\n";

    std::cout
        << "This program investigates CPU timing and randomness.\n\n";

    std::cout
        << "It does NOT assume beforehand that unusual results\n"
        << "are supernatural. It records measurable data so that\n"
        << "the hypothesis can be evaluated statistically.\n\n";

    std::cout
        << "Architecture detected: ";

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_AMD64)
    std::cout << "x86-64\n";
#elif defined(_M_IX86) || defined(__i386__)
    std::cout << "x86-32\n";
#else
    std::cout << "unknown\n";
#endif

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
                    experiment_tsc_deltas();
                    break;

                case 3:
                    experiment_human_timing();
                    break;

                case 4:
                    experiment_machine_timing();
                    break;

                case 5:
                    experiment_temple_rng();
                    break;

                case 6:
                    experiment_human_rng();
                    break;

                case 7:
                    experiment_oracle();
                    break;

                case 8:
                    experiment_large_oracle();
                    break;

                case 9:
                    experiment_collisions();
                    break;

                case 10:
                    experiment_lcg_comparison();
                    break;

                case 11:
                    experiment_standard_comparison();
                    break;

                case 12:
                    experiment_prediction();
                    break;

                case 13:
                    experiment_low_bits();
                    break;

                case 14:
                    experiment_reproducibility();
                    break;

                case 15:
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