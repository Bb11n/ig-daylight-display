#include "gps_parser.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr float kKnotsToKmh = 1.852f;

bool isEmpty(const char* text)
{
    return text == nullptr || text[0] == '\0';
}

bool isSentenceType(const char* type, const char* suffix)
{
    const size_t typeLen = strlen(type);
    const size_t suffixLen = strlen(suffix);
    if (typeLen < suffixLen) {
        return false;
    }
    return strcmp(type + typeLen - suffixLen, suffix) == 0;
}

void copyUtcTime(char* dest, size_t destSize, const char* src)
{
    if (destSize == 0) {
        return;
    }

    if (isEmpty(src)) {
        strncpy(dest, "--", destSize);
        dest[destSize - 1] = '\0';
        return;
    }

    strncpy(dest, src, destSize);
    dest[destSize - 1] = '\0';

    char* decimal = strchr(dest, '.');
    if (decimal != nullptr) {
        *decimal = '\0';
    }
}

double parseCoordinate(const char* value, const char* hemisphere)
{
    if (isEmpty(value) || isEmpty(hemisphere)) {
        return 0.0;
    }

    const double raw = atof(value);
    const int degrees = static_cast<int>(raw / 100.0);
    const double minutes = raw - (static_cast<double>(degrees) * 100.0);
    double decimal = static_cast<double>(degrees) + (minutes / 60.0);

    if (hemisphere[0] == 'S' || hemisphere[0] == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

float parseFloatOrDefault(const char* value, float fallback)
{
    if (isEmpty(value)) {
        return fallback;
    }
    return static_cast<float>(atof(value));
}

int parseIntOrDefault(const char* value, int fallback)
{
    if (isEmpty(value)) {
        return fallback;
    }
    return atoi(value);
}

bool tokenizeSentence(char* sentence, char** fields, size_t maxFields, size_t* fieldCount)
{
    *fieldCount = 0;

    if (sentence[0] == '$') {
        sentence++;
    }

    char* checksum = strchr(sentence, '*');
    if (checksum != nullptr) {
        *checksum = '\0';
    }

    char* cursor = sentence;
    while (*cursor != '\0' && *fieldCount < maxFields) {
        fields[*fieldCount] = cursor;
        (*fieldCount)++;

        char* comma = strchr(cursor, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }

    return *fieldCount > 0;
}

bool parseRmc(char** fields, size_t fieldCount, GpsData* data)
{
    if (fieldCount < 8) {
        return false;
    }

    copyUtcTime(data->utcTime, sizeof(data->utcTime), fields[1]);

    const char status = isEmpty(fields[2]) ? 'V' : fields[2][0];
    data->hasFix = status == 'A';

    if (!isEmpty(fields[3]) && !isEmpty(fields[4])) {
        data->latitude = parseCoordinate(fields[3], fields[4]);
    }
    if (!isEmpty(fields[5]) && !isEmpty(fields[6])) {
        data->longitude = parseCoordinate(fields[5], fields[6]);
    }

    data->speedKmh = parseFloatOrDefault(fields[7], 0.0f) * kKnotsToKmh;
    return true;
}

bool parseGga(char** fields, size_t fieldCount, GpsData* data)
{
    if (fieldCount < 10) {
        return false;
    }

    copyUtcTime(data->utcTime, sizeof(data->utcTime), fields[1]);

    if (!isEmpty(fields[2]) && !isEmpty(fields[3])) {
        data->latitude = parseCoordinate(fields[2], fields[3]);
    }
    if (!isEmpty(fields[4]) && !isEmpty(fields[5])) {
        data->longitude = parseCoordinate(fields[4], fields[5]);
    }

    data->fixQuality = parseIntOrDefault(fields[6], 0);
    data->satellites = parseIntOrDefault(fields[7], 0);
    data->hdop = parseFloatOrDefault(fields[8], 99.9f);
    data->altitudeM = parseFloatOrDefault(fields[9], 0.0f);
    data->hasFix = data->fixQuality > 0;
    return true;
}

} // namespace

void gpsDataReset(GpsData* data)
{
    if (data == nullptr) {
        return;
    }

    data->hasFix = false;
    data->fixQuality = 0;
    data->satellites = 0;
    data->speedKmh = 0.0f;
    data->latitude = 0.0;
    data->longitude = 0.0;
    data->hdop = 99.9f;
    data->altitudeM = 0.0f;
    strncpy(data->utcTime, "--", sizeof(data->utcTime));
    data->utcTime[sizeof(data->utcTime) - 1] = '\0';
}

bool gpsParseSentence(const char* sentence, GpsData* data)
{
    if (sentence == nullptr || data == nullptr || sentence[0] == '\0') {
        return false;
    }

    char copy[128];
    strncpy(copy, sentence, sizeof(copy));
    copy[sizeof(copy) - 1] = '\0';

    char* fields[24] = {};
    size_t fieldCount = 0;
    if (!tokenizeSentence(copy, fields, 24, &fieldCount)) {
        return false;
    }

    if (isSentenceType(fields[0], "RMC")) {
        return parseRmc(fields, fieldCount, data);
    }
    if (isSentenceType(fields[0], "GGA")) {
        return parseGga(fields, fieldCount, data);
    }

    return false;
}

GpsParser::GpsParser()
{
    reset();
}

void GpsParser::reset()
{
    gpsDataReset(&data_);
    sentence_[0] = '\0';
    lastSentence_[0] = '\0';
    sentenceLength_ = 0;
    collecting_ = false;
}

bool GpsParser::feed(char c)
{
    if (c == '$') {
        collecting_ = true;
        sentenceLength_ = 0;
        sentence_[sentenceLength_++] = c;
        sentence_[sentenceLength_] = '\0';
        return false;
    }

    if (!collecting_) {
        return false;
    }

    if (c == '\r' || c == '\n') {
        if (sentenceLength_ > 1) {
            sentence_[sentenceLength_] = '\0';
            strncpy(lastSentence_, sentence_, sizeof(lastSentence_));
            lastSentence_[sizeof(lastSentence_) - 1] = '\0';
            collecting_ = false;
            sentenceLength_ = 0;
            gpsParseSentence(lastSentence_, &data_);
            return true;
        }

        collecting_ = false;
        sentenceLength_ = 0;
        return false;
    }

    if (sentenceLength_ < sizeof(sentence_) - 1) {
        sentence_[sentenceLength_++] = c;
        sentence_[sentenceLength_] = '\0';
    } else {
        collecting_ = false;
        sentenceLength_ = 0;
    }

    return false;
}

const GpsData& GpsParser::data() const
{
    return data_;
}

const char* GpsParser::lastSentence() const
{
    return lastSentence_;
}
