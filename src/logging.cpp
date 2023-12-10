//
// Created by denis on 19.03.23.
//

#include <sstream>
#include <chrono>
#include "include/logging.h"

std::string getLoggingFilename(int rank, std::string_view info)
{
    constexpr auto timestampBufferSize{sizeof("%Y-%m-%d-%H-%M-%S")};
    char timestampBuff[timestampBufferSize]{0};
    tm tmBuffer{};

    const auto currentTime = std::chrono::system_clock::now();
    const std::time_t utcTime = std::chrono::system_clock::to_time_t(currentTime);
    std::strftime(timestampBuff, timestampBufferSize, "%Y-%m-%d-%H-%M-%S", gmtime_r(&utcTime, &tmBuffer));

    std::ostringstream oss;
    oss << "Rank_"
        << rank
        << '_'
        << info
        << '_'
        << timestampBuff
        << ".log";
    return oss.str();
}