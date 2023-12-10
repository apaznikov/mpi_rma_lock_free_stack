//
// Created by denis on 20.04.23.
//

#ifndef SOURCES_REF_COUNTING_H
#define SOURCES_REF_COUNTING_H

#include <cstdint>

namespace rma_stack::ref_counting
{
    constexpr uint64_t RankBitsLimit            = 13; // Кол-во бит под счётчик количества процессов.
    constexpr uint64_t ExternalCounterBitsLimit = RankBitsLimit; // Кол-во бит под внешний счётчик ссылок.

    // Оставшееся кол-во бит отводится под адресацию памяти внутри вычислительного узла.
    constexpr uint64_t OffsetBitsLimit          = 64 - RankBitsLimit * 2;
    constexpr uint64_t InternalCounterBitsLimit = RankBitsLimit;
    // Необходимо для обозначения глобального указателя на NULL - (DummyRank, любое смещение).
    constexpr uint64_t DummyRank                = (1 << RankBitsLimit) - 1;

    struct GlobalAddress
    {
        uint64_t offset   : OffsetBitsLimit;
        uint64_t rank     : RankBitsLimit;
        uint64_t reserved   : 64 - OffsetBitsLimit - RankBitsLimit;
    };

    bool isGlobalAddressDummy(GlobalAddress globalAddress);
    bool isValidRank(uint64_t rank);
}
#endif //SOURCES_REF_COUNTING_H
