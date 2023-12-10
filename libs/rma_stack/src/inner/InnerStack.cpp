//
// Created by denis on 20.04.23.
//

#include <random>
#include "inner/InnerStack.h"
#include "MpiException.h"

namespace rma_stack::ref_counting
{
    namespace custom_mpi = custom_mpi_extensions;

    void InnerStack::push(const std::function<void(GlobalAddress)> &putDataCallback,
                          const std::function<void()> &backoffCallback)
    {
        m_logger->trace("started 'push'");

        auto nodeAddress = acquireNode(m_centralized ? HEAD_RANK : m_rank);
        if (isGlobalAddressDummy(nodeAddress))
        {
            putDataCallback(nodeAddress);
            m_logger->trace("failed to find free node in 'push'");
            return;
        }
        {
            const auto r = nodeAddress.rank;
            const auto o = nodeAddress.offset;
            m_logger->trace("acquired free node (rank - {}, offset - {}) in 'push'", r, o);
        }

        putDataCallback(nodeAddress);
        m_logger->trace("put data in 'push'");

        CountedNodePtr resHeadCountedNodePtr;

        // Получение текущей головы списка.
        MPI_Win_lock(MPI_LOCK_SHARED, HEAD_RANK, MPI_MODE_NOCHECK, m_headWin);
        MPI_Fetch_and_op(nullptr,
                         &resHeadCountedNodePtr,
                         MPI_UINT64_T,
                         HEAD_RANK,
                         m_headAddress,
                         MPI_NO_OP,
                         m_headWin
        );
        MPI_Win_flush(HEAD_RANK, m_headWin);

        m_logger->trace("fetched head (rank - {}, offset - {})", resHeadCountedNodePtr.getRank(), resHeadCountedNodePtr.getOffset());

        m_logger->trace("started new head pushing in 'push'");

        CountedNodePtr newCountedNodePtr;
        newCountedNodePtr.setRank(nodeAddress.rank);
        newCountedNodePtr.setOffset(nodeAddress.offset);
        newCountedNodePtr.incExternalCounter();

        CountedNodePtr oldHeadCountedNodePtr;
        CountedNodePtr countedNodePtrNext;

        constexpr auto nodeSize                     = sizeof(Node);
        const auto countedNodePtrNextDisplacement   = static_cast<MPI_Aint>(nodeSize * nodeAddress.offset) + 8;
        const MPI_Aint countedNodePtrNextOffset     = MPI_Aint_add(m_pBaseNodeArrAddresses[nodeAddress.rank], countedNodePtrNextDisplacement);

        /*
         * Пока не удастся заменить текущую голову списка операцией на новый узел
         * операцией CAS, перезаписывать глобальный указатель на следующий узел
         * нового узла текущей головой списка.
         */
        MPI_Win_lock(MPI_LOCK_SHARED, nodeAddress.rank, MPI_MODE_NOCHECK, m_nodesWin);
        do
        {
            countedNodePtrNext = resHeadCountedNodePtr;
            MPI_Put(&countedNodePtrNext,
                    1,
                    MPI_UINT64_T,
                    nodeAddress.rank,
                    countedNodePtrNextOffset,
                    1,
                    MPI_UINT64_T,
                    m_nodesWin
            );
            MPI_Win_flush(nodeAddress.rank, m_nodesWin);

            oldHeadCountedNodePtr = resHeadCountedNodePtr;

            MPI_Compare_and_swap(&newCountedNodePtr,
                                 &oldHeadCountedNodePtr,
                                 &resHeadCountedNodePtr,
                                 MPI_UINT64_T,
                                 HEAD_RANK,
                                 m_headAddress,
                                 m_headWin
            );
            MPI_Win_flush(HEAD_RANK, m_headWin);

            if (resHeadCountedNodePtr != oldHeadCountedNodePtr)
            {
                m_logger->trace("started to execute backoff callback");
                backoffCallback();
                m_logger->trace("executed backoff callback");
            }
        }
        while (resHeadCountedNodePtr != oldHeadCountedNodePtr);

        MPI_Win_unlock(HEAD_RANK, m_headWin);
        MPI_Win_unlock(nodeAddress.rank, m_nodesWin);

        m_logger->trace("finished 'push'");
    }

    GlobalAddress InnerStack::acquireNode(int rank) const
    {
        m_logger->trace("started 'acquireNode'");
        GlobalAddress nodeGlobalAddress = {0, DummyRank, 0};

        if (!isValidRank(rank))
        {
            m_logger->trace("finished 'acquireNode'");
            return nodeGlobalAddress;
        }

        std::random_device rd;
        std::mt19937 mt(rd());

        std::uniform_int_distribution<int> dist(0, static_cast<int>(m_elemsUpLimit - 1));

        MPI_Win_lock(MPI_LOCK_SHARED, rank, MPI_MODE_NOCHECK, m_nodesWin);

        /* Поиск свободного адреса работает слишком медленно,
         * если искать его без случайного выбора, поэтому сначала
         * производится несколько попыток угадать, какой адрес не
         * занят. Если угадать не удалось, тогда сканируется весь
         * массив адресов.
         */
        bool foundFreeNodeRandomly{false};
        const auto randomTriesNumber = static_cast<MPI_Aint>(std::floor((double)m_elemsUpLimit * 0.05));
        for (MPI_Aint i = 0; i < randomTriesNumber; ++i)
        {
            const auto idx = static_cast<MPI_Aint>(dist(mt));
            const auto nodeDisplacement = idx * static_cast<MPI_Aint>(sizeof(Node));
            const MPI_Aint nodeOffset = MPI_Aint_add(m_pBaseNodeArrAddresses[rank], nodeDisplacement);

            uint32_t newAcquiredField{1};
            uint32_t oldAcquiredField{0};
            uint32_t resAcquiredField{0};

            MPI_Compare_and_swap(&newAcquiredField,
                                 &oldAcquiredField,
                                 &resAcquiredField,
                                 MPI_UINT32_T,
                                 rank,
                                 nodeOffset,
                                 m_nodesWin
            );
            MPI_Win_flush(rank, m_nodesWin);

            m_logger->trace("resAcquiredField = {} of (rank - {}, offset - {}) in 'acquireNode'",
                            resAcquiredField,
                            rank,
                            i
            );
            assert(resAcquiredField == 0 || resAcquiredField == 1);
            if (!resAcquiredField)
            {
                nodeGlobalAddress.rank = rank;
                nodeGlobalAddress.offset = idx;
                foundFreeNodeRandomly = true;
                break;
            }
        }
        if (!foundFreeNodeRandomly)
        {
            for (MPI_Aint i = 0; i < static_cast<MPI_Aint>(m_elemsUpLimit); ++i)
            {
                constexpr auto nodeSize = static_cast<MPI_Aint>(sizeof(Node));
                const auto nodeDisplacement = i * nodeSize;
                const MPI_Aint nodeOffset = MPI_Aint_add(m_pBaseNodeArrAddresses[rank], nodeDisplacement);

                uint32_t newAcquiredField{1};
                uint32_t oldAcquiredField{0};
                uint32_t resAcquiredField{0};

                MPI_Compare_and_swap(&newAcquiredField,
                                     &oldAcquiredField,
                                     &resAcquiredField,
                                     MPI_UINT32_T,
                                     rank,
                                     nodeOffset,
                                     m_nodesWin
                );
                MPI_Win_flush(rank, m_nodesWin);

                m_logger->trace("resAcquiredField = {} of (rank - {}, offset - {}) in 'acquireNode'",
                                resAcquiredField,
                                rank,
                                i
                );
                assert(resAcquiredField == 0 || resAcquiredField == 1);
                if (!resAcquiredField)
                {
                    nodeGlobalAddress.rank = rank;
                    nodeGlobalAddress.offset = i;
                    break;
                }
            }
        }

        MPI_Win_unlock(rank, m_nodesWin);

        m_logger->trace("finished 'acquireNode'");
        return nodeGlobalAddress;
    }

    void InnerStack::releaseNode(GlobalAddress nodeAddress) const
    {
        constexpr auto nodeSize = sizeof(Node);
        const auto nodeDisplacement = static_cast<MPI_Aint>(nodeAddress.offset * nodeSize);
        const MPI_Aint nodeOffset = MPI_Aint_add(m_pBaseNodeArrAddresses[nodeAddress.rank],
                                                 nodeDisplacement
        );
        int32_t acquiredField{1};
        int32_t acquiredField1{acquiredField};
        {
            const auto r = nodeAddress.rank;
            const auto o = nodeAddress.offset;
            m_logger->trace("started to release node (rank - {}, offset - {})", r, o);
        }
        const MPI_Aint countedNodePtrOffset = nodeOffset + 8;
        CountedNodePtr dummyCountedNodePtr;
        MPI_Put(&dummyCountedNodePtr,
            1,
            MPI_UINT64_T,
            nodeAddress.rank,
            countedNodePtrOffset,
            1,
            MPI_UINT64_T,
            m_nodesWin
        );
        MPI_Fetch_and_op(&acquiredField,
                         &acquiredField1,
                         MPI_UINT32_T,
                         nodeAddress.rank,
                         nodeOffset,
                         MPI_REPLACE,
                         m_nodesWin
        );
        MPI_Win_flush(nodeAddress.rank, m_nodesWin);
        {
            const auto r = nodeAddress.rank;
            const auto o = nodeAddress.offset;
            m_logger->trace("released node (rank - {}, offset - {})", r, o);
        }
    }

    void InnerStack::pop(const std::function<void(GlobalAddress)> &getDataCallback,
                         const std::function<void()> &backoffCallback)
    {
        m_logger->trace("started 'pop'");

        CountedNodePtr oldHeadCountedNodePtr;

        // Чтение текущей головы односвязного списка.
        MPI_Win_lock(MPI_LOCK_SHARED, HEAD_RANK, MPI_MODE_NOCHECK, m_headWin);
        MPI_Fetch_and_op(nullptr,
                         &oldHeadCountedNodePtr,
                         MPI_UINT64_T,
                         HEAD_RANK,
                         m_headAddress,
                         MPI_NO_OP,
                         m_headWin
        );
        MPI_Win_flush(HEAD_RANK, m_headWin);

        {
            const auto r = oldHeadCountedNodePtr.getRank();
            const auto o = oldHeadCountedNodePtr.getOffset();
            const auto e = oldHeadCountedNodePtr.getExternalCounter();
            m_logger->trace("fetched head (rank - {}, offset - {}, ext_cnt - {}) before loop in 'pop'", r, o, e);
        }
        for (;;)
        {
            // Увеличение кол-во внешних ссылок на голову на 1.
            increaseHeadCount(oldHeadCountedNodePtr);
            {
                const auto r = oldHeadCountedNodePtr.getRank();
                const auto o = oldHeadCountedNodePtr.getOffset();
                const auto e = oldHeadCountedNodePtr.getExternalCounter();
                m_logger->trace("head (rank - {}, offset - {}, ext_cnt - {})) after increaseHeadCount in 'pop'", r, o, e);
            }
            GlobalAddress nodeAddress = {
                    oldHeadCountedNodePtr.getOffset(),
                    oldHeadCountedNodePtr.getRank(),
                    0
            };
            if (isGlobalAddressDummy(nodeAddress))
            {
                /*
                 * Если глобальный указатель указывает на NULL, то стек пуст,
                 * и нужно сообщить об этом пользователю, а затем завершить POP.
                 */
                getDataCallback(nodeAddress);
                break;
            }

            /*
             * Получение указателя на следующий за головой списка узел
             * с последующей заменой головы на этот узел операцией CAS.
             */
            CountedNodePtr countedNodePtrNext;

            const auto nodeDisplacement             = static_cast<MPI_Aint>(nodeAddress.offset * sizeof(Node) + sizeof(CountedNodePtr));
            const MPI_Aint countedNodePtrNextOffset = MPI_Aint_add(m_pBaseNodeArrAddresses[nodeAddress.rank], nodeDisplacement);

            MPI_Win_lock(MPI_LOCK_SHARED, nodeAddress.rank, MPI_MODE_NOCHECK, m_nodesWin);
            MPI_Fetch_and_op(nullptr,
                             &countedNodePtrNext,
                             MPI_UINT64_T,
                             nodeAddress.rank,
                             countedNodePtrNextOffset,
                             MPI_NO_OP,
                             m_nodesWin
            );
            MPI_Win_flush(nodeAddress.rank, m_nodesWin);

            {
                const auto r = countedNodePtrNext.getRank();
                const auto o = countedNodePtrNext.getOffset();
                const auto e = countedNodePtrNext.getExternalCounter();
                m_logger->trace("ptr->next (rank - {}, offset - {}, ext_cnt - {})) in 'pop'", r, o, e);
            }

            CountedNodePtr resHeadCountedNodePtr;

            MPI_Compare_and_swap(&countedNodePtrNext,
                                 &oldHeadCountedNodePtr,
                                 &resHeadCountedNodePtr,
                                 MPI_UINT64_T,
                                 HEAD_RANK,
                                 m_headAddress,
                                 m_headWin
            );
            MPI_Win_flush(HEAD_RANK, m_headWin);

            bool popComplete{false};
            if (resHeadCountedNodePtr == oldHeadCountedNodePtr)
            {
                getDataCallback(nodeAddress);

                const auto internalCounterDisplacement  = static_cast<MPI_Aint>(nodeAddress.offset * sizeof(Node) + sizeof(int32_t));
                const auto internalCounterOffset        = MPI_Aint_add(m_pBaseNodeArrAddresses[nodeAddress.rank],
                                                                       internalCounterDisplacement
                );

                const auto externalCount    = static_cast<int32_t>(oldHeadCountedNodePtr.getExternalCounter());
                const int32_t countIncrease = externalCount - 2;
                int32_t resInternalCount{0};
                // Атомарное уменьшение внутреннего счётчика на кол-во внешних ссылок минус 2.
                MPI_Fetch_and_op(
                        &countIncrease,
                        &resInternalCount,
                        MPI_INT32_T,
                        nodeAddress.rank,
                        internalCounterOffset,
                        MPI_SUM,
                        m_nodesWin
                );
                MPI_Win_flush(nodeAddress.rank, m_nodesWin);

                if (resInternalCount == -countIncrease)
                    releaseNode(nodeAddress);

                popComplete = true;
            }
            else
            {
                const auto internalCounterDisplacement  = static_cast<MPI_Aint>(nodeAddress.offset * sizeof(Node) + sizeof(int32_t));
                const auto internalCounterOffset        = MPI_Aint_add(m_pBaseNodeArrAddresses[nodeAddress.rank],
                                                                       internalCounterDisplacement
                );
                const int64_t countIncrease{-1};
                int64_t resInternalCount{0};
                // Атомарное уменьшение внутреннего счётчика на 1.
                MPI_Fetch_and_op(
                        &countIncrease,
                        &resInternalCount,
                        MPI_INT32_T,
                        nodeAddress.rank,
                        internalCounterOffset,
                        MPI_SUM,
                        m_nodesWin
                );
                MPI_Win_flush(nodeAddress.rank, m_nodesWin);

                if (resInternalCount == 1)
                    releaseNode(nodeAddress);
            }
            MPI_Win_unlock(nodeAddress.rank, m_nodesWin);

            if (popComplete)
                break;

            m_logger->trace("started to execute backoff callback");
            backoffCallback();
            m_logger->trace("executed backoff callback");
        }
        MPI_Win_unlock(HEAD_RANK, m_headWin);

        m_logger->trace("finished 'pop'");
    }

    /*
     * Функция используется для увеличения внешнего счётчика ссылок
     * на голову односвязного списка (вершину стека) на 1 для текущего
     * процесса, чтобы другие процессы не освободили память под голову
     * до того, как к ней обратится текущий процесс.
     */
    void InnerStack::increaseHeadCount(CountedNodePtr &oldHeadCountedNodePtr)
    {
        CountedNodePtr newCountedNodePtr;
        CountedNodePtr resCountedNodePtr = oldHeadCountedNodePtr;
        m_logger->trace("started 'increaseHeadCount'");

        do
        {
            newCountedNodePtr = oldHeadCountedNodePtr = resCountedNodePtr;
            newCountedNodePtr.incExternalCounter();

            MPI_Compare_and_swap(&newCountedNodePtr,
                                 &oldHeadCountedNodePtr,
                                 &resCountedNodePtr,
                                 MPI_UINT64_T,
                                 HEAD_RANK,
                                 m_headAddress,
                                 m_headWin
            );
            MPI_Win_flush(HEAD_RANK, m_headWin);

            m_logger->trace("executed CAS in 'increaseHeadCount'");
            m_logger->trace("oldCountedNodePtr is (rank - {}, offset - {}, ext_cnt - {})",
                            oldHeadCountedNodePtr.getRank(),
                            oldHeadCountedNodePtr.getOffset(),
                            oldHeadCountedNodePtr.getExternalCounter()
            );
            m_logger->trace("resCountedNodePtr is (rank - {}, offset - {}, ext_cnt - {})",
                            resCountedNodePtr.getRank(),
                            resCountedNodePtr.getOffset(),
                            resCountedNodePtr.getExternalCounter()
            );
        }
        while (oldHeadCountedNodePtr != resCountedNodePtr);

        oldHeadCountedNodePtr.setExternalCounter(newCountedNodePtr.getExternalCounter());

        m_logger->trace("finished 'increaseHeadCount'");
    }

    InnerStack::InnerStack(MPI_Comm comm, MPI_Info info, bool t_centralized, size_t t_elemsUpLimit,
                           std::shared_ptr<spdlog::logger> t_logger)
    :
    m_elemsUpLimit(t_elemsUpLimit),
    m_centralized(t_centralized),
    m_logger(std::move(t_logger))
    {
        m_logger->trace("getting rank");
        {
            auto mpiStatus = MPI_Comm_rank(comm, &m_rank);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to get rank", __FILE__, __func__, __LINE__, mpiStatus);
        }
        m_logger->trace("got rank {}", m_rank);

        initRemoteAccessMemory(comm, info);
        MPI_Barrier(comm);
        m_logger->trace("finished InnerStack construction");
    }

    void InnerStack::release()
    {
        MPI_Free_mem(m_pNodesArr);
        m_pNodesArr = nullptr;
        m_logger->trace("freed up node arr RMA memory");

        MPI_Win_free(&m_nodesWin);
        m_logger->trace("freed up node win RMA memory");

        MPI_Free_mem(m_pHeadCountedNodePtr);
        m_pHeadCountedNodePtr = nullptr;
        m_logger->trace("freed up head pointer RMA memory");

        MPI_Win_free(&m_headWin);
        m_logger->trace("freed up head win RMA memory");
    }

    void InnerStack::initRemoteAccessMemory(MPI_Comm comm, MPI_Info info)
    {
        {
            auto mpiStatus = MPI_Win_create_dynamic(info, comm, &m_nodesWin);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to create RMA window for nodes", __FILE__, __func__, __LINE__, mpiStatus);
        }
        {
            auto mpiStatus = MPI_Win_create_dynamic(info, comm, &m_headWin);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to create RMA window for head", __FILE__, __func__, __LINE__, mpiStatus);
        }

        const auto nodesSize = static_cast<MPI_Aint>(sizeof(Node) * m_elemsUpLimit);

        if (m_centralized)
        {
            m_pBaseNodeArrAddresses = std::make_unique<MPI_Aint[]>(1);

            if (m_rank == HEAD_RANK)
            {
                m_logger->trace("started to initialize node array");
                {
                    auto mpiStatus = MPI_Alloc_mem(nodesSize, MPI_INFO_NULL,
                                                   &m_pNodesArr);
                    if (mpiStatus != MPI_SUCCESS)
                        throw custom_mpi::MpiException(
                                "failed to allocate RMA memory",
                                __FILE__,
                                __func__,
                                __LINE__,
                                mpiStatus
                        );
                }

                std::fill_n(m_pNodesArr, m_elemsUpLimit, Node());
                m_logger->trace("initialized node array");
                {
                    auto mpiStatus = MPI_Win_attach(m_nodesWin, (void*)m_pNodesArr, nodesSize);
                    if (mpiStatus != MPI_SUCCESS) {
                        throw custom_mpi::MpiException("failed to attach RMA window", __FILE__, __func__, __LINE__, mpiStatus);
                    }
                }
                m_logger->trace("attach nodes RMA window");
                MPI_Get_address(m_pNodesArr, (MPI_Aint*)m_pBaseNodeArrAddresses.get());
            }

            m_logger->trace("started to broadcast node array addresses");
            auto mpiStatus = MPI_Bcast(m_pBaseNodeArrAddresses.get(), 1, MPI_AINT, HEAD_RANK, comm);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to broadcast node array address", __FILE__, __func__ , __LINE__, mpiStatus);
            m_logger->trace("broadcasted node array addresses");
        }
        else
        {
            m_logger->trace("started to initialize node array");

            {
                auto mpiStatus = MPI_Alloc_mem(nodesSize, MPI_INFO_NULL,
                                               &m_pNodesArr);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException(
                            "failed to allocate RMA memory",
                            __FILE__,
                            __func__,
                            __LINE__,
                            mpiStatus
                    );
            }

            std::fill_n(m_pNodesArr, m_elemsUpLimit, Node());
            m_logger->trace("initialized node array");
            {
                auto mpiStatus = MPI_Win_attach(m_nodesWin, (void*)m_pNodesArr, nodesSize);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException("failed to attach RMA window", __FILE__, __func__, __LINE__, mpiStatus);
            }

            m_logger->trace("started to broadcast node array addresses");
            int procNum{0};
            MPI_Comm_size(comm, &procNum);
            m_pBaseNodeArrAddresses = std::make_unique<MPI_Aint[]>(procNum);
            MPI_Get_address(m_pNodesArr, (MPI_Aint*)&m_pBaseNodeArrAddresses[m_rank]);

            for (int i = 0; i < procNum; ++i)
            {
                auto mpiStatus = MPI_Bcast(&m_pBaseNodeArrAddresses[i], 1, MPI_AINT, i, comm);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException("failed to broadcast node array base address", __FILE__, __func__ , __LINE__, mpiStatus);
                m_logger->trace("m_pNodeArrAddresses[{}] = {}", i, m_pBaseNodeArrAddresses[i]);
            }

            m_logger->trace("broadcasted node array addresses");
        }

        if (m_rank == HEAD_RANK)
        {
            m_logger->trace("started to initialize head");

            {
                auto mpiStatus = MPI_Alloc_mem(sizeof(CountedNodePtr), MPI_INFO_NULL,
                                               &m_pHeadCountedNodePtr);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException(
                            "failed to allocate RMA memory",
                            __FILE__,
                            __func__,
                            __LINE__,
                            mpiStatus
                    );
            }
            *m_pHeadCountedNodePtr = CountedNodePtr();
            m_logger->trace("initialized head");
            {
                auto mpiStatus = MPI_Win_attach(m_headWin, (void*)m_pHeadCountedNodePtr, 1);
                if (mpiStatus != MPI_SUCCESS)
                    throw custom_mpi::MpiException("failed to attach RMA window", __FILE__, __func__, __LINE__, mpiStatus);
            }
            m_logger->trace("attached nodes RMA window");
            MPI_Get_address(m_pHeadCountedNodePtr, &m_headAddress);
        }

        {
            m_logger->trace("started to broadcast head address");
            auto mpiStatus = MPI_Bcast(&m_headAddress, 1, MPI_AINT, HEAD_RANK, comm);
            if (mpiStatus != MPI_SUCCESS)
                throw custom_mpi::MpiException("failed to broadcast head address", __FILE__, __func__ , __LINE__, mpiStatus);
            m_logger->trace("broadcasted head address");
        }
    }

    size_t InnerStack::getElemsUpLimit() const
    {
        return m_elemsUpLimit;
    }

    void InnerStack::printStack()
    {
        CountedNodePtr slider;
        MPI_Win_lock(MPI_LOCK_SHARED, HEAD_RANK, MPI_MODE_NOCHECK, m_headWin);
        MPI_Fetch_and_op(nullptr, &slider, MPI_UINT64_T, HEAD_RANK, m_headAddress, MPI_NO_OP, m_headWin);
        MPI_Win_flush(HEAD_RANK, m_headWin);
        MPI_Win_unlock(HEAD_RANK, m_headWin);

        while (slider.getRank() < DummyRank)
        {
            m_logger->info("(rank - {}, offset - {})", slider.getRank(), slider.getOffset());

            int nextRank            = static_cast<int>(slider.getRank());
            auto nextDisplacement   = static_cast<MPI_Aint>(slider.getOffset() * sizeof(Node)) + 8;
            auto nextOffset = MPI_Aint_add(m_pBaseNodeArrAddresses[nextRank], nextDisplacement);

            MPI_Win_lock(MPI_LOCK_SHARED, nextRank, MPI_MODE_NOCHECK, m_nodesWin);
            MPI_Get(&slider, 1, MPI_UINT64_T, nextRank, nextOffset, 1, MPI_UINT64_T, m_nodesWin);
            MPI_Win_flush(nextRank, m_nodesWin);
            MPI_Win_unlock(nextRank, m_nodesWin);
        }
    }
} // ref_counting