#include "core/clock_trust.h"

#include <cstring>

const char* timeSourceKindToString(TimeSourceKind kind) {
    switch (kind) {
        case TimeSourceKind::DS1307_RTC: return "DS1307_RTC";
        case TimeSourceKind::SYSTEM_NTP: return "SYSTEM_NTP";
        case TimeSourceKind::BACKEND: return "BACKEND";
        case TimeSourceKind::INVALID: break;
        case TimeSourceKind::UNKNOWN: break;
    }
    return "INVALID";
}

TimeSourceKind timeSourceKindFromString(const char* token) {
    if (!token || token[0] == '\0') return TimeSourceKind::UNKNOWN;
    if (std::strcmp(token, "DS1307_RTC") == 0) return TimeSourceKind::DS1307_RTC;
    if (std::strcmp(token, "SYSTEM_NTP") == 0) return TimeSourceKind::SYSTEM_NTP;
    if (std::strcmp(token, "BACKEND") == 0) return TimeSourceKind::BACKEND;
    if (std::strcmp(token, "INVALID") == 0) return TimeSourceKind::INVALID;
    return TimeSourceKind::UNKNOWN;
}

bool ClockTrustResolver::isHardwareTrusted(const ClockTrustInputs& in) {
    return in.hardware_trusted;
}

TimeSourceKind ClockTrustResolver::resolve(const ClockTrustInputs& in) {
    if (isHardwareTrusted(in)) return TimeSourceKind::DS1307_RTC;
    if (in.system_time_valid) return TimeSourceKind::SYSTEM_NTP;
    return TimeSourceKind::INVALID;
}

bool ClockTrustResolver::hasAnyValidTime(const ClockTrustInputs& in) {
    return resolve(in) != TimeSourceKind::INVALID;
}
