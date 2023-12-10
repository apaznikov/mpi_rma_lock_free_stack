//
// Created by denis on 20.04.23.
//

#include <cmath>

#include "inner/Node.h"
#include "inner/CountedNodePtr.h"

namespace rma_stack::ref_counting
{
    Node::Node()
    :
    m_acquired(0),
    m_reserved(0),
    m_internalCounter(0),
    m_countedNodePtrNext()
    {

    }

    const rma_stack::ref_counting::CountedNodePtr &Node::getCountedNodePtr() const
    {
        return m_countedNodePtrNext;
    }

    void Node::setCountedNodePtrNext(const CountedNodePtr &t_countedNodePtr)
    {
        m_countedNodePtrNext = t_countedNodePtr;
    }
} // rma_stack