//
// Created by denis on 20.04.23.
//

#include "inner/CountedNodePtr.h"

namespace rma_stack::ref_counting
{
    uint64_t CountedNodePtr::getExternalCounter() const
    {
        return m_externalCounter;
    }

    uint64_t CountedNodePtr::getRank() const
    {
        return m_rank;
    }

    bool CountedNodePtr::isDummy() const
    {
        return m_rank >= DummyRank;
    }

    bool CountedNodePtr::incExternalCounter()
    {
        if (m_externalCounter + 1u > DummyRank)
            return false;

        ++m_externalCounter;
        return true;
    }

    CountedNodePtr::CountedNodePtr():
    m_offset(0),
    m_rank(DummyRank),
    m_externalCounter(0)
    {

    }

    uint64_t CountedNodePtr::getOffset() const
    {
        return m_offset;
    }

    bool CountedNodePtr::setRank(uint64_t t_rank)
    {
        if (t_rank >= DummyRank)
            return false;
        m_rank = t_rank;
        return true;
    }

    bool CountedNodePtr::setOffset(uint64_t t_offset) {
        if (t_offset + 1> (1ul << OffsetBitsLimit))
            return false;

        m_offset = t_offset;
        return true;
    }

    bool operator==(CountedNodePtr& lhs, CountedNodePtr& rhs)
    {
        return
        lhs.m_rank == rhs.m_rank
        && lhs.m_offset == rhs.m_offset
        && lhs.m_externalCounter == rhs.m_externalCounter;
    }

    bool operator!=(CountedNodePtr &lhs, CountedNodePtr &rhs) {
        return !(lhs == rhs);
    }

    bool CountedNodePtr::setExternalCounter(uint64_t t_externalCounter)
    {
        if (t_externalCounter + 1 > DummyRank)
            return false;

        m_externalCounter = t_externalCounter;

        return true;
    }
} // rma_stack