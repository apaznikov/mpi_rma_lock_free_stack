//
// Created by denis on 02.02.23.
//

#include <iostream>
#include <type_traits>
#include <chrono>
#include <spdlog/spdlog.h>
#include <ctime>
#include <random>

#include "IStack.h"
#include "inner/InnerStack.h"
#include "logging.h"
using namespace std::literals::chrono_literals;

void runInnerStackSimplePushPopTask(rma_stack::ref_counting::InnerStack &stack, MPI_Comm comm);

template<typename StackImpl>
using EnableIfValueTypeIsInt = std::enable_if_t<std::is_same_v<typename StackImpl::ValueType, int>>;

// Задача для отладки внешнего стека, предназначена только для данных типа 'int'.
template<typename StackImpl,
        typename = EnableIfValueTypeIsInt<StackImpl>>
void runStackSimpleIntPushPopTask(stack_interface::IStack<StackImpl> &stack, MPI_Comm comm)
{
    SPDLOG_INFO("started 'runStackSimpleIntPushPopTask'");

    int rank{-1};
    MPI_Comm_rank(comm, &rank);

    int size{0};
    MPI_Comm_size(comm, &size);

    const int elemsSize = 5;
    int elems[elemsSize]{0};

    for (int i = 0; i < elemsSize; ++i)
    {
        elems[i] = i * size + rank;
    }

    for (auto& elem: elems)
    {
        stack.push(elem);
        SPDLOG_DEBUG("received elem by 'push' {}", elem);
    }

    for (int i = 0; i < elemsSize; ++i)
    {
        int elem;
        int defaultElem{-1};
        stack.pop(elem, defaultElem);
        SPDLOG_DEBUG("brought back elem by 'pop' {}", elem);
    }
    SPDLOG_INFO("finished 'runStackSimpleIntPushPopTask'");
}

/*
 * Задача для измерения продолжительности случайных равновероятных операций PUSH и POP внешнего стека,
 * предназначена только для данных типа 'int'. workload - эмуляция сторонней нагрузки на приложение.
 */
template<typename StackImpl,
        typename = EnableIfValueTypeIsInt<StackImpl>>
void runStackRandomOperationBenchmarkTask(stack_interface::IStack<StackImpl> &stack, MPI_Comm comm,
                                          std::shared_ptr<spdlog::sinks::sink> loggerSink)
{
    SPDLOG_INFO("started 'runStackRandomOperationBenchmarkTask'");

    auto pLogger = std::make_shared<spdlog::logger>(producerConsumerBenchmarkLoggerName.data(), loggerSink);
    pLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    pLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

    const auto workload{1us};
    const auto totalOpsNum{15'000};

    auto procNum{0};
    MPI_Comm_size(comm, &procNum);
    const int opsNum = std::ceil(((double)totalOpsNum) / procNum);

    int rank{-1};
    MPI_Comm_rank(comm, &rank);

    const auto warmUp = std::ceil(opsNum * 0.1);

    for (int i = 0; i < warmUp; ++i)
    {
        stack.push(1);
    }
    MPI_Barrier(comm);
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<int> dist(0, 50);

    size_t pushCnt{0};
    size_t popCnt{0};

    const double tBeginSec = MPI_Wtime();
    for (int i = 0; i < opsNum; ++i)
    {
        int e = dist(mt);
        if (e > 25)
        {
            stack.push(e);
            ++pushCnt;
        }
        else
        {
            int defaultValue = -1;
            stack.pop(e, defaultValue);
            ++popCnt;
        }
        std::this_thread::sleep_for(workload);
    }
    const double tEndSec = MPI_Wtime();

    const double workloadSec = std::chrono::duration_cast<std::chrono::microseconds>(workload).count() / 1'000'000.0f;
    const double tElapsedSec = tEndSec - tBeginSec - (opsNum * workloadSec);

    double tTotalElapsedSec{0};
    MPI_Allreduce(&tElapsedSec, &tTotalElapsedSec, 1, MPI_DOUBLE, MPI_MAX, comm);

    SPDLOG_LOGGER_INFO(pLogger, "procs {}, rank {}, elapsed (sec) {}, total (sec) {}", procNum, rank, tElapsedSec, tTotalElapsedSec);
    SPDLOG_LOGGER_INFO(pLogger, "total ops {}, ops {}", totalOpsNum, opsNum);
    SPDLOG_LOGGER_INFO(pLogger, "push count {}, pop count {}, warm up {}", pushCnt, popCnt, warmUp);

    SPDLOG_INFO("finished 'runStackRandomOperationBenchmarkTask'");
}

/*
 * Задача для измерения продолжительности нескольких операций PUSH внешнего стека,
 * предназначена только для данных типа 'int'. workload - эмуляция сторонней нагрузки на приложение.
 */
template<typename StackImpl,
        typename = EnableIfValueTypeIsInt<StackImpl>>
void runStackOnlyPushBenchmarkTask(stack_interface::IStack<StackImpl> &stack, MPI_Comm comm,
                                          std::shared_ptr<spdlog::sinks::sink> loggerSink)
{
    SPDLOG_INFO("started 'runStackOnlyPushBenchmarkTask'");

    auto pLogger = std::make_shared<spdlog::logger>(producerConsumerBenchmarkLoggerName.data(), loggerSink);
    pLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    pLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

    const auto workload{1us};
    const auto totalOpsNum{15'000};

    auto procNum{0};
    MPI_Comm_size(comm, &procNum);
    const auto opsNum{totalOpsNum / procNum};

    int rank{-1};
    MPI_Comm_rank(comm, &rank);

    const double tBeginSec = MPI_Wtime();
    for (int i = 0; i < opsNum; ++i)
    {
        stack.push(i);
        std::this_thread::sleep_for(workload);
    }
    const double tEndSec = MPI_Wtime();

    const double workloadSec = std::chrono::duration_cast<std::chrono::microseconds>(workload).count() / 1'000'000.0f;
    const double tElapsedSec = tEndSec - tBeginSec - (opsNum * workloadSec);

    double tTotalElapsedSec{0};
    MPI_Allreduce(&tElapsedSec, &tTotalElapsedSec, 1, MPI_DOUBLE, MPI_MAX, comm);

    SPDLOG_LOGGER_INFO(pLogger, "procs {}, rank {}, elapsed (sec) {}, total (sec) {}", procNum, rank, tElapsedSec, tTotalElapsedSec);
    SPDLOG_LOGGER_INFO(pLogger, "total ops {}, ops {}", totalOpsNum, opsNum);

    SPDLOG_INFO("finished 'runStackOnlyPushBenchmarkTask'");
}

/*
 * Задача для измерения продолжительности нескольких операций POP внешнего стека,
 * предназначена только для данных типа 'int'. workload - эмуляция сторонней нагрузки на приложение.
 */
template<typename StackImpl,
        typename = EnableIfValueTypeIsInt<StackImpl>>
void runStackOnlyPopBenchmarkTask(stack_interface::IStack<StackImpl> &stack, MPI_Comm comm,
                                          std::shared_ptr<spdlog::sinks::sink> loggerSink)
{
    SPDLOG_INFO("started 'runStackOnlyPopBenchmarkTask'");

    auto pLogger = std::make_shared<spdlog::logger>(producerConsumerBenchmarkLoggerName.data(), loggerSink);
    pLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    pLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

    const auto workload{1us};
    const auto totalOpsNum{15'000};

    auto procNum{0};
    MPI_Comm_size(comm, &procNum);
    const auto opsNum{totalOpsNum / procNum};

    int rank{-1};
    MPI_Comm_rank(comm, &rank);

    const auto warmUp = std::ceil(opsNum * 0.1);

    for (int i = 0; i < warmUp; ++i)
    {
        stack.push(1);
    }
    MPI_Barrier(comm);

    const double tBeginSec = MPI_Wtime();
    for (int i = 0; i < opsNum; ++i)
    {
        int e{-1};
        int defaultValue = -1;
        stack.pop(e, defaultValue);
        std::this_thread::sleep_for(workload);
    }
    const double tEndSec = MPI_Wtime();

    const double workloadSec = std::chrono::duration_cast<std::chrono::microseconds>(workload).count() / 1'000'000.0f;
    const double tElapsedSec = tEndSec - tBeginSec - (opsNum * workloadSec);

    double tTotalElapsedSec{0};
    MPI_Allreduce(&tElapsedSec, &tTotalElapsedSec, 1, MPI_DOUBLE, MPI_MAX, comm);

    SPDLOG_LOGGER_INFO(pLogger, "procs {}, rank {}, elapsed (sec) {}, total (sec) {}", procNum, rank, tElapsedSec, tTotalElapsedSec);
    SPDLOG_LOGGER_INFO(pLogger, "total ops {}, ops {}", totalOpsNum, opsNum);

    SPDLOG_INFO("finished 'runStackOnlyPopBenchmarkTask'");
}