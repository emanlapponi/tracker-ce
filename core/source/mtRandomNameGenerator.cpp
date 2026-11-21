#include "mtRandomNameGenerator.h"

#include "Platform.h"
#include "SdTextFile.h"
#include "util_math.h"

#include <cstdio>
#include <cstring>

namespace
{
constexpr const char* kAdjectivesPath = "/Words/adjectives.txt";
constexpr const char* kNounsPath = "/Words/nouns.txt";

constexpr const char* kFallbackWords1[] = {
    "amber",  "analog", "bright", "chrome", "cosmic", "deep",   "drift", "dusty",
    "echo",   "fuzzy",  "golden", "hollow", "lunar",  "mellow", "neon",  "velvet",
};

constexpr const char* kFallbackWords2[] = {
    "arp",   "bass",  "beat",  "chord", "cloud", "delay", "dream", "drone",
    "field", "flare", "grain", "loop",  "pulse", "shift", "spark", "wave",
};

constexpr size_t kFallbackCount = sizeof(kFallbackWords1) / sizeof(kFallbackWords1[0]);
} // namespace

mtRandomNameGenerator randomNameGenerator;

const char* mtRandomNameGenerator::getRandomName(Timer* timer)
{
    utilMathRandom::randomSeed(timer->micros());

    uint32_t seed1 = utilMathRandom::random(0xFFFF) * 17u + utilMathRandom::random(0xFFFF);
    uint32_t seed2 = utilMathRandom::random(0xFFFF) * 31u + utilMathRandom::random(0xFFFF);
    bool swapOrDash = utilMathRandom::random(10) == 0;

    char word1[RANDOM_NAME_WORD_1_MAX_SIZE + 1];
    char word2[RANDOM_NAME_WORD_2_MAX_SIZE + 1];

    const char* path1 = swapOrDash ? kNounsPath : kAdjectivesPath;
    if (!SdTextFile::readRandomWord(fileSystem_, path1, seed1, word1, sizeof(word1)))
    {
        const char* fallback = (swapOrDash ? kFallbackWords2 : kFallbackWords1)[seed1 % kFallbackCount];
        strncpy(word1, fallback, sizeof(word1) - 1);
        word1[sizeof(word1) - 1] = '\0';
    }

    if (!SdTextFile::readRandomWord(fileSystem_, kNounsPath, seed2, word2, sizeof(word2)))
    {
        const char* fallback = kFallbackWords2[seed2 % kFallbackCount];
        strncpy(word2, fallback, sizeof(word2) - 1);
        word2[sizeof(word2) - 1] = '\0';
    }

    snprintf(randomName, sizeof(randomName), swapOrDash ? "%s-%s" : "%s %s", word1, word2);
    return randomName;
}
