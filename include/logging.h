//
// Created by denis on 19.03.23.
//

#ifndef SOURCES_LOGGING_H
#define SOURCES_LOGGING_H

#include <string>

std::string getLoggingFilename(int rank, std::string_view info);

constexpr inline std::string_view defaultLoggerName{"DefaultLogger"};
constexpr inline std::string_view producerConsumerBenchmarkLoggerName{"ProducerConsumerBenchmarkLogger"};

#endif //SOURCES_LOGGING_H
