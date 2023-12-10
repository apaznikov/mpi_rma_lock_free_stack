//
// Created by denis on 07.05.23.
//

#ifndef SOURCES_RMATREIBERDECENTRALIZEDSTACK_H
#define SOURCES_RMATREIBERDECENTRALIZEDSTACK_H

#include <mpi.h>
#include <memory>
#include <optional>

#include "IStack.h"

#include "outer/ExponentialBackoff.h"
#include "inner/InnerStack.h"
#include "MpiException.h"

namespace rma_stack
{
    namespace custom_mpi = custom_mpi_extensions;

    template<typename T>
    class RmaTreiberDecentralizedStack: public stack_interface::IStack<RmaTreiberDecentralizedStack<T>>
    {
        friend class stack_interface::IStack_traits<rma_stack::RmaTreiberDecentralizedStack<T>>;
    public:
        typedef typename stack_interface::IStack_traits<RmaTreiberDecentralizedStack>::ValueType ValueType;

        explicit RmaTreiberDecentralizedStack(MPI_Comm comm, MPI_Info info,
                                              const std::chrono::nanoseconds &t_rBackoffMinDelay,
                                              const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                                              ref_counting::InnerStack &&t_innerStack,
                                              std::shared_ptr<spdlog::logger> t_logger);
        static RmaTreiberDecentralizedStack <T> create(
                MPI_Comm comm,
                MPI_Info info,
                const std::chrono::nanoseconds &t_rBackoffMinDelay,
                const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                int elemsUpLimit,
                std::shared_ptr<spdlog::sinks::sink> loggerSink
        );

        RmaTreiberDecentralizedStack(RmaTreiberDecentralizedStack&) = delete;
        RmaTreiberDecentralizedStack(RmaTreiberDecentralizedStack&&)  noexcept = default;
        RmaTreiberDecentralizedStack& operator=(RmaTreiberDecentralizedStack&) = delete;
        RmaTreiberDecentralizedStack& operator=(RmaTreiberDecentralizedStack&&)  noexcept = default;
        ~RmaTreiberDecentralizedStack() = default;

        void release();

    private:
        // public stack interface begin
        void pushImpl(const T &rValue);
        void popImpl(T &rValue, const T &rDefaultValue);
        T& topImpl();
        size_t sizeImpl();
        bool isEmptyImpl();
        // public stack interface end

        void initRemoteAccessMemory(MPI_Comm comm, MPI_Info info);

    private:
        std::chrono::nanoseconds m_backoffMinDelay;
        std::chrono::nanoseconds m_backoffMaxDelay;

        ref_counting::InnerStack m_innerStack;
        int m_rank{-1};
        MPI_Win m_userDataWin{MPI_WIN_NULL};
        T* m_pUserDataArr{nullptr};
        std::unique_ptr<MPI_Aint[]> m_pUserDataBaseAddresses;
        std::shared_ptr<spdlog::logger> m_logger;
    };

    template<typename T>
    void RmaTreiberDecentralizedStack<T>::release()
    {
        m_innerStack.release();

        MPI_Free_mem(m_pUserDataArr);
        m_pUserDataArr = nullptr;
        m_logger->trace("freed up data arr RMA memory");

        MPI_Win_free(&m_userDataWin);
        m_logger->trace("freed up data win RMA memory");
    }

    template<typename T>
    RmaTreiberDecentralizedStack<T>::RmaTreiberDecentralizedStack(MPI_Comm comm, MPI_Info info,
                                                                  const std::chrono::nanoseconds &t_rBackoffMinDelay,
                                                                  const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                                                                  ref_counting::InnerStack &&t_innerStack,
                                                                  std::shared_ptr<spdlog::logger> t_logger)
            :
            m_backoffMinDelay(t_rBackoffMinDelay),
            m_backoffMaxDelay(t_rBackoffMaxDelay),
            m_innerStack(std::move(t_innerStack)),
            m_logger(std::move(t_logger))
    {
        MPI_Comm_rank(comm, &m_rank);

        initRemoteAccessMemory(comm, info);
    }

    template<typename T>
    void RmaTreiberDecentralizedStack<T>::pushImpl(const T &rValue)
    {
        ExponentialBackoff backoff(m_backoffMinDelay, m_backoffMaxDelay);
        m_innerStack.push([&rValue, &win = m_userDataWin, &pDataBaseAddresses = m_pUserDataBaseAddresses](
                                  const ref_counting::GlobalAddress &dataAddress) {
                constexpr auto valueSize = sizeof(rValue);
                const auto offset = dataAddress.offset * valueSize;
                const auto displacement = MPI_Aint_add(pDataBaseAddresses[dataAddress.rank], offset);
                MPI_Win_lock(MPI_LOCK_SHARED, dataAddress.rank, MPI_MODE_NOCHECK, win);
                MPI_Put(&rValue,
                      valueSize,
                      MPI_UNSIGNED_CHAR,
                      dataAddress.rank,
                      displacement,
                      valueSize,
                      MPI_UNSIGNED_CHAR,
                      win
                );
                MPI_Win_flush(dataAddress.rank, win);
                MPI_Win_unlock(dataAddress.rank, win);
            },
            [&backoff] () {
                backoff.backoff();
            }
        );

        m_logger->trace("finished 'pushImpl'", m_rank);
    }

    template<typename T>
    void RmaTreiberDecentralizedStack<T>::popImpl(T &rValue, const T &rDefaultValue)
    {
        ExponentialBackoff backoff(m_backoffMinDelay, m_backoffMaxDelay);
        m_innerStack.pop([&rValue, &rDefaultValue, &win = m_userDataWin, &pDataBaseAddresses = m_pUserDataBaseAddresses](
                                 const ref_counting::GlobalAddress &dataAddress) {
            if (ref_counting::isGlobalAddressDummy(dataAddress))
            {
                rValue = rDefaultValue;
                return;
            }

            constexpr auto valueSize = sizeof(rValue);
            const auto offset = dataAddress.offset * valueSize;
            const auto displacement = MPI_Aint_add(pDataBaseAddresses[dataAddress.rank], offset);
            MPI_Win_lock(MPI_LOCK_SHARED, dataAddress.rank, MPI_MODE_NOCHECK, win);
            MPI_Get(&rValue,
                 valueSize,
                 MPI_UNSIGNED_CHAR,
                 dataAddress.rank,
                 displacement,
                 valueSize,
                 MPI_UNSIGNED_CHAR,
                 win
            );
            MPI_Win_flush(dataAddress.rank, win);
            MPI_Win_unlock(dataAddress.rank, win);
            },
            [&backoff] () {
                backoff.backoff();
            }
        );
        m_logger->trace("finished 'popImpl'",m_rank);
    }

    template<typename T>
    T &rma_stack::RmaTreiberDecentralizedStack<T>::topImpl() {
        T v{};
        return v;
    }

    template<typename T>
    size_t RmaTreiberDecentralizedStack<T>::sizeImpl()
    {
        return 0;
    }

    template<typename T>
    bool RmaTreiberDecentralizedStack<T>::isEmptyImpl()
    {
        return true;
    }

    template<typename T>
    void RmaTreiberDecentralizedStack<T>::initRemoteAccessMemory(MPI_Comm comm, MPI_Info info)
    {
        {
            auto mpiStatus = MPI_Win_create_dynamic(info, comm, &m_userDataWin);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to create RMA window for head", __FILE__, __func__, __LINE__, mpiStatus);
        }

        auto elemsUpLimit = m_innerStack.getElemsUpLimit();
        constexpr auto elemSize = sizeof(T);
        {
            auto mpiStatus = MPI_Alloc_mem(elemSize * elemsUpLimit, MPI_INFO_NULL,
                                           &m_pUserDataArr);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException(
                        "failed to allocate RMA memory",
                        __FILE__,
                        __func__,
                        __LINE__,
                        mpiStatus
                );
        }
        std::fill_n(m_pUserDataArr, elemsUpLimit, T());
        {
            auto mpiStatus = MPI_Win_attach(m_userDataWin, m_pUserDataArr, elemSize * elemsUpLimit);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to attach RMA window", __FILE__, __func__, __LINE__, mpiStatus);
        }
        int procNum{0};
        MPI_Comm_size(comm, &procNum);
        m_pUserDataBaseAddresses = std::make_unique<MPI_Aint[]>(procNum);
        MPI_Get_address(m_pUserDataArr, &m_pUserDataBaseAddresses[m_rank]);

        for (int i = 0; i < procNum; ++i)
        {
            auto mpiStatus = MPI_Bcast(&m_pUserDataBaseAddresses[i], 1, MPI_AINT, i, comm);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to broadcast data array base address", __FILE__, __func__ , __LINE__, mpiStatus);
        }
    }

    template<typename T>
    RmaTreiberDecentralizedStack<T> RmaTreiberDecentralizedStack<T>::create(MPI_Comm comm, MPI_Info info,
                                                                const std::chrono::nanoseconds &t_rBackoffMinDelay,
                                                                const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                                                                int elemsUpLimit,
                                                                std::shared_ptr<spdlog::sinks::sink> loggerSink) {
        auto pInnerStackLogger = std::make_shared<spdlog::logger>("InnerStack", loggerSink);
        spdlog::register_logger(pInnerStackLogger);
        pInnerStackLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
        pInnerStackLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

        ref_counting::InnerStack innerStack(
                comm,
                info,
                false,
                elemsUpLimit,
                std::move(pInnerStackLogger)
        );

        auto pOuterStackLogger = std::make_shared<spdlog::logger>("RmaTreiberDecentralizedStack", loggerSink);
        spdlog::register_logger(pOuterStackLogger);
        pOuterStackLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
        pOuterStackLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));

        RmaTreiberDecentralizedStack<T> stack(
                comm,
                info,
                t_rBackoffMinDelay,
                t_rBackoffMaxDelay,
                std::move(innerStack),
                std::move(pOuterStackLogger)
        );

        MPI_Barrier(comm);
        stack.m_logger->trace("finished RmaTreiberDecentralizedStack construction");
        return stack;
    }
} // rma_stack


namespace stack_interface
{
    template<typename T>
    struct IStack_traits<rma_stack::RmaTreiberDecentralizedStack < T>>
    {
        friend class IStack<rma_stack::RmaTreiberDecentralizedStack < T>>;
        friend class rma_stack::RmaTreiberDecentralizedStack<T>;
        typedef T ValueType;

    private:
        static void pushImpl(rma_stack::RmaTreiberDecentralizedStack<T>& stack, const T &value)
        {
            stack.pushImpl(value);
        }
        static void popImpl(rma_stack::RmaTreiberDecentralizedStack<T>& stack, ValueType &rValue, const ValueType &rDefaultValue)
        {
            stack.popImpl(rValue, rDefaultValue);
        }
        static ValueType& topImpl(rma_stack::RmaTreiberDecentralizedStack<T>& stack)
        {
            return stack.topImpl();
        }
        static size_t sizeImpl(rma_stack::RmaTreiberDecentralizedStack<T>& stack)
        {
            return stack.sizeImpl();
        }
        static bool isEmptyImpl(rma_stack::RmaTreiberDecentralizedStack<T>& stack)
        {
            return stack.isEmptyImpl();
        }
    };
}
#endif //SOURCES_RMATREIBERDECENTRALIZEDSTACK_H
