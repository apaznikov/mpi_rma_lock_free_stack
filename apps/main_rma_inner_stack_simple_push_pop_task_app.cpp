//
// Created by denis on 22.01.23.
//

/*
 * Программа для элементарной отладки внутреннего стека.
 */

#include <chrono>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/dup_filter_sink.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "inner/InnerStack.h"
#include "include/stack_tasks.h"
#include "include/logging.h"
#include "MpiException.h"

using namespace std::literals;

int main(int argc, char *argv[])
{
    auto returnCode{EXIT_SUCCESS};

    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Info info = MPI_INFO_NULL;

    int rank{-1};
    MPI_Comm_rank(comm, &rank);

    const auto minBackoffDelay = 1ns;
    const auto maxBackoffDelay = 100ns;
    const auto elemsUpLimit{100};

    /*
     * Сообщения, которые поступили подряд в течение 1 с,
     * будут объединены в один лог с информацией об их
     * количестве.
     */
    auto duplicatingFilterSink = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(
            1s
    );
    /*
     * default - лог отладки операций со стеком.
     */
    auto loggingDefaultFilename = getLoggingFilename(rank, "default");
    auto fileDefaultSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            loggingDefaultFilename.data()
    );
    duplicatingFilterSink->add_sink(fileDefaultSink);
    auto pDefaultLogger = std::make_shared<spdlog::logger>(defaultLoggerName.data(), duplicatingFilterSink);
    spdlog::set_default_logger(pDefaultLogger);
    spdlog::set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    spdlog::flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

    auto pInnerStackLogger = std::make_shared<spdlog::logger>("InnerStack", duplicatingFilterSink);
    spdlog::register_logger(pInnerStackLogger);
    pInnerStackLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    pInnerStackLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

    try
    {
        auto innerStack = rma_stack::ref_counting::InnerStack(
                comm,
                info,
                true,
                elemsUpLimit,
                std::move(pInnerStackLogger)
        );
        runInnerStackSimplePushPopTask(innerStack, comm);

        MPI_Barrier(comm);
        innerStack.release();
    }
    catch (custom_mpi_extensions::MpiException& ex)
    {
        SPDLOG_INFO("MPI exception"s + ex.what());
        returnCode = EXIT_FAILURE;
    }
    catch (std::exception& ex)
    {
        SPDLOG_INFO("Unexpected exception: "s + ex.what());
        returnCode = EXIT_FAILURE;
    }
    MPI_Finalize();
    SPDLOG_INFO("finished program");
    return returnCode;
}