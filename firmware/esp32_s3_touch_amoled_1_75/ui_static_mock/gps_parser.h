#pragma once

#include <stddef.h>
#include <stdint.h>

struct GpsData {
    bool hasFix;
    int fixQuality;
    int satellites;
    float speedKmh;
    double latitude;
    double longitude;
    float hdop;
    float altitudeM;
    char utcTime[16];
};

void gpsDataReset(GpsData* data);
bool gpsParseSentence(const char* sentence, GpsData* data);

class GpsParser {
public:
    GpsParser();

    void reset();
    bool feed(char c);

    const GpsData& data() const;
    const char* lastSentence() const;

private:
    GpsData data_;
    char sentence_[128];
    char lastSentence_[128];
    size_t sentenceLength_;
    bool collecting_;
};
