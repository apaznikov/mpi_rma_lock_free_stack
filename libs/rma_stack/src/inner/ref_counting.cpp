//
// Created by denis on 20.04.23.
//

#include "inner/ref_counting.h"

bool rma_stack::ref_counting::isGlobalAddressDummy(GlobalAddress globalAddress)
{
    return globalAddress.rank == DummyRank;
}

bool rma_stack::ref_counting::isValidRank(uint64_t rank)
{
    return rank < DummyRank;
}


