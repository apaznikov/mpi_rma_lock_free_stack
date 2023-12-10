//
// Created by denis on 22.01.23.
//

#ifndef SOURCES_RMATREIBERCENTRALSTACK_H
#define SOURCES_RMATREIBERCENTRALSTACK_H

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
    class RmaTreiberCentralStack: public stack_interface::IStack<RmaTreiberCentralStack<T>>
    {
        friend class stack_interface::IStack_traits<rma_stack::RmaTreiberCentralStack<T>>;
    public:
        typedef typename stack_interface::IStack_traits<RmaTreiberCentralStack>::ValueType ValueType;

        explicit RmaTreiberCentralStack(MPI_Comm comm, MPI_Info info,
                                        const std::chrono::nanoseconds &t_rBackoffMinDelay,
                                        const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                                        ref_counting::InnerStack &&t_innerStack,
                                        std::shared_ptr<spdlog::logger> t_logger);
        static RmaTreiberCentralStack <T> create(
                MPI_Comm comm,
                MPI_Info info,
                const std::chrono::nanoseconds &t_rBackoffMinDelay,
                const std::chrono::nanoseconds &t_rBackoffMaxDelay,
                int elemsUpLimit,
                std::shared_ptr<spdlog::sinks::sink> loggerSink
        );

        RmaTreiberCentralStack(RmaTreiberCentralStack&) = delete;
        RmaTreiberCentralStack(RmaTreiberCentralStack&&)  noexcept = default;
        RmaTreiberCentralStack& operator=(RmaTreiberCentralStack&) = delete;
        RmaTreiberCentralStack& operator=(RmaTreiberCentralStack&&)  noexcept = default;
        ~RmaTreiberCentralStack() = default;

        void release();

    private:
        // public stack interface begin
        void pushImpl(const T &rValue);
        void  popImpl(T &rValue, const T &rDefaultValue);
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
        MPI_Aint m_userDataBaseAddress{(MPI_Aint)MPI_BOTTOM};
        std::shared_ptr<spdlog::logger> m_logger;
    };

    template<typename T>
    void RmaTreiberCentralStack<T>::release()
    {
        m_innerStack.release();

        MPI_Free_mem(m_pUserDataArr);
        m_pUserDataArr = nullptr;
        m_logger->trace("freed up data arr RMA memory");

        MPI_Win_free(&m_userDataWin);
        m_logger->trace("freed up data win RMA memory");
    }

    template<typename T>
    RmaTreiberCentralStack<T>::RmaTreiberCentralStack(MPI_Comm comm, MPI_Info info,
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
    void RmaTreiberCentralStack<T>::pushImpl(const T &rValue)
    {
        ExponentialBackoff backoff(m_backoffMinDelay, m_backoffMaxDelay);
        m_innerStack.push([&rValue, &win = m_userDataWin, &dataBaseAddress = m_userDataBaseAddress](
                const ref_counting::GlobalAddress &dataAddress) {
                if (ref_counting::isGlobalAddressDummy(dataAddress))
                    return;

                constexpr auto valueSize = sizeof(rValue);
                const auto displacement = dataAddress.offset * valueSize;
                const auto offset = MPI_Aint_add(dataBaseAddress, displacement);

                MPI_Win_lock(MPI_LOCK_SHARED, dataAddress.rank, MPI_MODE_NOCHECK, win);
                MPI_Put(&rValue,
                        valueSize,
                        MPI_UNSIGNED_CHAR,
                        dataAddress.rank,
                        offset,
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

        m_logger->trace("finished 'push'",m_rank);
    }

    template<typename T>
    void RmaTreiberCentralStack<T>::popImpl(T &rValue, const T &rDefaultValue)
    {
        ExponentialBackoff backoff(m_backoffMinDelay, m_backoffMaxDelay);
        m_innerStack.pop([&rValue, &rDefaultValue, &win = m_userDataWin, &dataBaseAddress = m_userDataBaseAddress](
                const ref_counting::GlobalAddress &dataAddress) {
                if (ref_counting::isGlobalAddressDummy(dataAddress))
                {
                    rValue = rDefaultValue;
                    return;
                }

                constexpr auto valueSize = sizeof(rValue);
                const auto offset = dataAddress.offset * valueSize;
                const auto displacement = MPI_Aint_add(dataBaseAddress, offset);
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
    T &rma_stack::RmaTreiberCentralStack<T>::topImpl() {
        T v{};
        return v;
    }

    template<typename T>
    size_t RmaTreiberCentralStack<T>::sizeImpl()
    {
        return 0;
    }

    template<typename T>
    bool RmaTreiberCentralStack<T>::isEmptyImpl()
    {
        return true;
    }

    template<typename T>
    void RmaTreiberCentralStack<T>::initRemoteAccessMemory(MPI_Comm comm, MPI_Info info)
    {
        {
            auto mpiStatus = MPI_Win_create_dynamic(info, comm, &m_userDataWin);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to create RMA window for head", __FILE__, __func__, __LINE__, mpiStatus);
        }

        if (m_rank == ref_counting::InnerStack::HEAD_RANK)
        {
            m_logger->trace("started to initialize user data array");
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
            m_logger->trace("initialized user data array");
            {
                auto mpiStatus = MPI_Win_attach(m_userDataWin, m_pUserDataArr, elemSize * elemsUpLimit);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException("failed to attach RMA window", __FILE__, __func__, __LINE__, mpiStatus);
            }
            m_logger->trace("attached user data RMA window");
            MPI_Get_address(m_pUserDataArr, &m_userDataBaseAddress);
        }
        m_logger->trace("started to broadcast user data base address");
        auto mpiStatus = MPI_Bcast(&m_userDataBaseAddress, 1, MPI_AINT, ref_counting::InnerStack::HEAD_RANK, comm);
        if (mpiStatus != MPI_SUCCESS)
            throw custom_mpi::MpiException("failed to broadcast head address", __FILE__, __func__ , __LINE__, mpiStatus);
        m_logger->trace("broadcasted user data base address");
    }

    template<typename T>
    RmaTreiberCentralStack<T> RmaTreiberCentralStack<T>::create(MPI_Comm comm, MPI_Info info,
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
                true,
                elemsUpLimit,
                std::move(pInnerStackLogger)
        );

        auto pOuterStackLogger = std::make_shared<spdlog::logger>("RmaTreiberCentralStack", loggerSink);
        spdlog::register_logger(pOuterStackLogger);
        pOuterStackLogger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
        pOuterStackLogger->flush_on(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
        
        RmaTreiberCentralStack<T> stack(
                comm,
                info,
                t_rBackoffMinDelay,
                t_rBackoffMaxDelay,
                std::move(innerStack),
                std::move(pOuterStackLogger)
        );

        MPI_Barrier(comm);
        stack.m_logger->trace("finished RmaTreiberCentralStack construction");
        return stack;
    }
} // rma_stack


namespace stack_interface
{
    template<typename T>
    struct IStack_traits<rma_stack::RmaTreiberCentralStack < T>>
    {
        friend class IStack<rma_stack::RmaTreiberCentralStack < T>>;
        friend class rma_stack::RmaTreiberCentralStack<T>;
        typedef T ValueType;

    private:
        static void pushImpl(rma_stack::RmaTreiberCentralStack<T>& stack, const T &value)
        {
            stack.pushImpl(value);
        }
        static void popImpl(rma_stack::RmaTreiberCentralStack<T>& stack, ValueType &rValue, const ValueType &rDefaultValue)
        {
            stack.popImpl(rValue, rDefaultValue);
        }
        static ValueType& topImpl(rma_stack::RmaTreiberCentralStack<T>& stack)
        {
            return stack.topImpl();
        }
        static size_t sizeImpl(rma_stack::RmaTreiberCentralStack<T>& stack)
        {
            return stack.sizeImpl();
        }
        static bool isEmptyImpl(rma_stack::RmaTreiberCentralStack<T>& stack)
        {
            return stack.isEmptyImpl();
        }
    };
}
#endif //SOURCES_RMATREIBERCENTRALSTACK_H
