// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstring>

struct WAVHeader {
    char riff[4];
    uint32_t size;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t format;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

std::vector<int16_t> readWav(const std::string& path, int& sampleRate) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open WAV file");

    char riff[4], wave[4];
    file.read(riff, 4);
    file.seekg(4, std::ios::cur);
    file.read(wave, 4);

    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0)
        throw std::runtime_error("Not a valid WAV file");

    std::vector<int16_t> samples;
    while (!file.eof()) {
        char chunkId[4];
        uint32_t chunkSize;
        file.read(chunkId, 4);
        if (file.eof()) break;
        file.read(reinterpret_cast<char*>(&chunkSize), 4);

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            file.seekg(4, std::ios::cur);
            uint16_t channels;
            file.read(reinterpret_cast<char*>(&channels), 2);
            file.read(reinterpret_cast<char*>(&sampleRate), 4);
            file.seekg(chunkSize - 10, std::ios::cur);
        }
        else if (std::strncmp(chunkId, "data", 4) == 0) {
            int numSamples = chunkSize / 2;
            samples.resize(numSamples);
            file.read(reinterpret_cast<char*>(samples.data()), chunkSize);
            break;
        }
        else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    return samples;
}

void writeWav(const std::string& path, const std::vector<int16_t>& samples, int sampleRate) {
    std::ofstream file(path, std::ios::binary);
    
    WAVHeader header;
    std::memcpy(header.riff, "RIFF", 4);
    std::memcpy(header.wave, "WAVE", 4);
    std::memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;
    header.format = 1;
    header.channels = 1;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 16;
    header.blockAlign = header.channels * header.bitsPerSample / 8;
    header.byteRate = sampleRate * header.blockAlign;
    std::memcpy(header.data, "data", 4);
    header.dataSize = samples.size() * sizeof(int16_t);
    header.size = 36 + header.dataSize;
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(samples.data()), header.dataSize);
}

enum class ZeroCrossingType {
    RISING,
    FALLING
};

struct ZeroCrossing {
    int position;
    ZeroCrossingType type;
    float slope;
};

std::vector<ZeroCrossing> findZeroCrossings(const std::vector<int16_t>& samples, int start, int end) {
    std::vector<ZeroCrossing> crossings;
    
    for (int i = start; i < end - 1 && i < samples.size() - 1; i++) {
        if (samples[i] <= 0 && samples[i + 1] > 0) {
            float slope = samples[i + 1] - samples[i];
            crossings.push_back({i, ZeroCrossingType::RISING, slope});
        }
        else if (samples[i] >= 0 && samples[i + 1] < 0) {
            float slope = samples[i + 1] - samples[i];
            crossings.push_back({i, ZeroCrossingType::FALLING, slope});
        }
    }
    
    return crossings;
}

struct ZeroCrossingPattern {
    ZeroCrossingType type;
    int distance1;
    int distance2;
    int distance3;
};

ZeroCrossingPattern extractPattern(const std::vector<ZeroCrossing>& crossings, int index) {
    ZeroCrossingPattern pattern;
    pattern.type = crossings[index].type;
    pattern.distance1 = 0;
    pattern.distance2 = 0;
    pattern.distance3 = 0;
    
    if (index >= 1) {
        pattern.distance1 = crossings[index].position - crossings[index - 1].position;
    }
    if (index >= 2) {
        pattern.distance2 = crossings[index].position - crossings[index - 2].position;
    }
    if (index >= 3) {
        pattern.distance3 = crossings[index].position - crossings[index - 3].position;
    }
    
    return pattern;
}

float comparePatterns(const ZeroCrossingPattern& p1, const ZeroCrossingPattern& p2) {
    if (p1.type != p2.type) {
        return std::numeric_limits<float>::max();
    }
    
    float diff1 = (p1.distance1 > 0) ? std::abs(p1.distance1 - p2.distance1) / (float)p1.distance1 : 0.0f;
    float diff2 = (p1.distance2 > 0) ? std::abs(p1.distance2 - p2.distance2) / (float)p1.distance2 : 0.0f;
    float diff3 = (p1.distance3 > 0) ? std::abs(p1.distance3 - p2.distance3) / (float)p1.distance3 : 0.0f;
    
    float score = diff1 * 3.0f + diff2 * 2.0f + diff3 * 1.0f;
    
    return score;
}

struct LoopPoint {
    int loopStart;
    int loopEnd;
    float score;
    int periodSamples;
    int numPeriods;
};

LoopPoint findLoopByPatternMatching(const std::vector<int16_t>& samples, 
                                    const std::vector<ZeroCrossing>& allCrossings,
                                    const ZeroCrossing& endCrossing,
                                    const ZeroCrossingPattern& endPattern,
                                    float estimatedPeriod,
                                    int sampleRate, 
                                    int targetPeriods,
                                    bool verbose = true) {
    
    float targetDistance = estimatedPeriod * targetPeriods;
    int searchCenter = endCrossing.position - (int)targetDistance;
    int searchRadius = (int)(estimatedPeriod * 2.0f);
    
    int searchStart = std::max(0, searchCenter - searchRadius);
    int searchEnd = searchCenter + searchRadius;
    
    if (verbose) {
        std::cout << "\n=== Testing " << targetPeriods << " periods ===\n";
        std::cout << "Target distance: " << targetDistance << " samples\n";
        std::cout << "Search range: " << searchStart << " - " << searchEnd << "\n";
    }
    
    LoopPoint best;
    best.loopStart = searchCenter;
    best.loopEnd = endCrossing.position;
    best.score = std::numeric_limits<float>::max();
    best.periodSamples = 0;
    best.numPeriods = targetPeriods;
    
    for (int i = 0; i < allCrossings.size() - 4; i++) {
        int pos = allCrossings[i].position;
        
        if (pos < searchStart || pos > searchEnd) continue;
        
        ZeroCrossingPattern candidatePattern = extractPattern(allCrossings, i);
        float score = comparePatterns(endPattern, candidatePattern);
        
        if (score < best.score) {
            best.loopStart = pos;
            best.loopEnd = endCrossing.position;
            best.score = score;
            best.periodSamples = endCrossing.position - pos;
        }
    }
    
    float actualPeriods = best.periodSamples / estimatedPeriod;
    
    if (verbose) {
        std::cout << "Best match:\n";
        std::cout << "  Loop start: " << best.loopStart << "\n";
        std::cout << "  Loop length: " << best.periodSamples << " samples\n";
        std::cout << "  Actual periods: " << actualPeriods << "\n";
        std::cout << "  Score: " << best.score << "\n";
    }
    
    return best;
}

LoopPoint findOptimalLoop(const std::vector<int16_t>& samples, int sampleRate, int initialPeriods) {
    std::cout << "\n=== PATTERN-BASED LOOP FINDER ===\n";
    
    auto allCrossings = findZeroCrossings(samples, 0, samples.size());
    
    if (allCrossings.size() < 10) {
        std::cerr << "Error: Not enough zero-crossings found!\n";
        return {0, (int)samples.size() - 1, 999.0f, 0, 0};
    }
    
    std::cout << "Found " << allCrossings.size() << " zero-crossings total\n";
    
    // Referenz-Pattern vom Sample-Ende
    int endIndex = allCrossings.size() - 1;
    ZeroCrossing endCrossing = allCrossings[endIndex];
    ZeroCrossingPattern endPattern = extractPattern(allCrossings, endIndex);
    
    std::cout << "\n=== END PATTERN ===\n";
    std::cout << "Position: " << endCrossing.position << "\n";
    std::cout << "Type: " << (endCrossing.type == ZeroCrossingType::RISING ? "RISING" : "FALLING") << "\n";
    std::cout << "Distance to prev: " << endPattern.distance1 << " samples\n";
    std::cout << "Distance to 2nd prev: " << endPattern.distance2 << " samples\n";
    std::cout << "Distance to 3rd prev: " << endPattern.distance3 << " samples\n";
    
    float estimatedPeriod = endPattern.distance1;
    if (endPattern.distance1 < 20) {
        estimatedPeriod = endPattern.distance2 / 2.0f;
    }
    
    std::cout << "\nEstimated period: ~" << estimatedPeriod << " samples ("
              << (estimatedPeriod / sampleRate * 1000.0f) << "ms, "
              << (sampleRate / estimatedPeriod) << " Hz)\n";
    
    // Teste initial
    std::cout << "\n=== OPTIMIZATION ===\n";
    std::cout << "Starting with " << initialPeriods << " periods...\n";
    
    LoopPoint bestOverall = findLoopByPatternMatching(samples, allCrossings, endCrossing, 
                                                       endPattern, estimatedPeriod, 
                                                       sampleRate, initialPeriods, true);
    
    const float SCORE_THRESHOLD = 0.01f;
    const int MAX_PERIODS = 30;
    const int MIN_PERIODS = 3;
    
    if (bestOverall.score > SCORE_THRESHOLD) {
        std::cout << "\n⚠ Score " << bestOverall.score << " > " << SCORE_THRESHOLD 
                  << " - Optimizing...\n";
        
        // Teste verschiedene Periodenzahlen
        for (int periods = initialPeriods + 1; periods <= MAX_PERIODS; periods++) {
            auto candidate = findLoopByPatternMatching(samples, allCrossings, endCrossing,
                                                        endPattern, estimatedPeriod,
                                                        sampleRate, periods, false);
            
            std::cout << "  " << periods << " periods: score = " << candidate.score;
            
            if (candidate.score < bestOverall.score) {
                bestOverall = candidate;
                std::cout << " ← NEW BEST!";
            }
            std::cout << "\n";
            
            // Wenn Score gut genug, abbrechen
            if (bestOverall.score <= SCORE_THRESHOLD) {
                std::cout << "\n✓ Score " << bestOverall.score << " <= " << SCORE_THRESHOLD 
                          << " - Good enough!\n";
                break;
            }
        }
        
        // Falls immer noch schlecht, versuche weniger Perioden
        if (bestOverall.score > SCORE_THRESHOLD && initialPeriods > MIN_PERIODS) {
            std::cout << "\nTrying fewer periods...\n";
            
            for (int periods = initialPeriods - 1; periods >= MIN_PERIODS; periods--) {
                auto candidate = findLoopByPatternMatching(samples, allCrossings, endCrossing,
                                                            endPattern, estimatedPeriod,
                                                            sampleRate, periods, false);
                
                std::cout << "  " << periods << " periods: score = " << candidate.score;
                
                if (candidate.score < bestOverall.score) {
                    bestOverall = candidate;
                    std::cout << " ← NEW BEST!";
                }
                std::cout << "\n";
                
                if (bestOverall.score <= SCORE_THRESHOLD) {
                    std::cout << "\n✓ Score " << bestOverall.score << " <= " << SCORE_THRESHOLD 
                              << " - Good enough!\n";
                    break;
                }
            }
        }
    } else {
        std::cout << "\n✓ Initial score " << bestOverall.score << " <= " << SCORE_THRESHOLD 
                  << " - Already optimal!\n";
    }
    
    float actualPeriods = bestOverall.periodSamples / estimatedPeriod;
    
    std::cout << "\n=== FINAL BEST MATCH ===\n";
    std::cout << "Optimized periods: " << bestOverall.numPeriods << "\n";
    std::cout << "Loop start: " << bestOverall.loopStart << " ("
              << (bestOverall.loopStart / (float)sampleRate * 1000.0f) << "ms)\n";
    std::cout << "Loop end: " << bestOverall.loopEnd << " ("
              << (bestOverall.loopEnd / (float)sampleRate * 1000.0f) << "ms)\n";
    std::cout << "Loop length: " << bestOverall.periodSamples << " samples ("
              << (bestOverall.periodSamples / (float)sampleRate * 1000.0f) << "ms)\n";
    std::cout << "Actual periods: " << actualPeriods << "\n";
    std::cout << "Final score: " << bestOverall.score << "\n";
    
    return bestOverall;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: FindLoopPoints <sample.wav> [num_periods]\n";
        std::cerr << "  num_periods: Starting number of periods (default: 10)\n";
        std::cerr << "               Will auto-optimize if score > 0.01\n";
        std::cerr << "Example: FindLoopPoints 060-127.wav 10\n";
        return 1;
    }

    std::string filename = argv[1];
    int initialPeriods = (argc > 2) ? std::stoi(argv[2]) : 10;

    int sampleRate = 32000;

    int bytesread;
    auto samples = readWav(filename, bytesread);
    
    std::cout << "===========================================\n";
    std::cout << "Auto-Optimizing Pattern-Based Loop Finder\n";
    std::cout << "===========================================\n";
    std::cout << "File: " << filename << "\n";
    std::cout << "Samples: " << samples.size() << " (" 
              << (samples.size() / (float)sampleRate) << "s)\n";
    std::cout << "Sample rate: " << sampleRate << " Hz\n";
    std::cout << "Initial periods: " << initialPeriods << "\n";

    auto loopPoint = findOptimalLoop(samples, sampleRate, initialPeriods);
    
    // Beschneide Sample am Loop-Ende
    int originalSize = samples.size();
    int newSize = loopPoint.loopEnd + 1;
    
    if (newSize < originalSize) {
        samples.resize(newSize);
        std::cout << "\n=== TRIMMING ===\n";
        std::cout << "Original: " << originalSize << " samples\n";
        std::cout << "Trimmed to: " << newSize << " samples\n";
        std::cout << "Removed: " << (originalSize - newSize) << " samples\n";
    }
    
    // Speichere
    writeWav(filename, samples, sampleRate);
    
    std::string loopInfoFile = filename.substr(0, filename.find_last_of('.')) + ".loop";
    std::ofstream loopOut(loopInfoFile);
    loopOut << loopPoint.loopStart;
    loopOut.close();
    
    std::cout << "\n=== OUTPUT ===\n";
    std::cout << "Trimmed sample: " << filename << "\n";
    std::cout << "Loop info: " << loopInfoFile << "\n";
    std::cout << "Loop start: " << loopPoint.loopStart << "\n";
    std::cout << "Loop end: " << (newSize - 1) << " (sample end)\n";

    if (loopPoint.score < 0.01) {
        std::cout << "\n✓✓✓ Excellent pattern match!\n";
    } else if (loopPoint.score < 0.05) {
        std::cout << "\n✓✓ Good pattern match\n";
    } else if (loopPoint.score < 0.15) {
        std::cout << "\n✓ Acceptable pattern match\n";
    } else {
        std::cout << "\n⚠ Could not find optimal loop point\n";
        std::cout << "This sample may be problematic for looping\n";
    }

    return 0;
}