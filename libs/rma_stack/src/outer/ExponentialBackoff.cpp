//
// Created by denis on 19.02.23.
//

#include <thread>

#include "include/outer/ExponentialBackoff.h"

namespace rma_stack
{
    ExponentialBackoff::ExponentialBackoff(const std::chrono::nanoseconds &t_rMinDelayNs, const std::chrono::nanoseconds &t_rMaxDelayNs)
    :
    m_maxDelayNs(t_rMaxDelayNs),
    m_limitDelayInt(0),
    m_randomEngine(std::chrono::steady_clock::now().time_since_epoch().count())
    {
        const auto upperBoundDelayInt = std::numeric_limits<int>::max();
        const auto upperBoundDelayNs = std::chrono::nanoseconds(upperBoundDelayInt);

        if (t_rMinDelayNs > upperBoundDelayNs)
            throw std::invalid_argument("the min delay is out of bounds");

        if (t_rMaxDelayNs > upperBoundDelayNs)
            throw std::invalid_argument("the max delay is out of bounds");

        if (t_rMinDelayNs > t_rMaxDelayNs)
            throw std::invalid_argument("the max delay is lower than min delay");

        m_limitDelayInt = static_cast<int>(t_rMinDelayNs.count());
    }

    void ExponentialBackoff::backoff()
    {
        const auto delayInt = std::uniform_int_distribution<int>(0, m_limitDelayInt)(m_randomEngine);
        const auto delayNs = std::chrono::nanoseconds (delayInt);

        const long limitDelayLong = 2 * m_limitDelayInt;
        const long maxIntLong = std::numeric_limits<int>::max();
        const int maxDelayInt = static_cast<int>(m_maxDelayNs.count());

        if (limitDelayLong > maxIntLong)
        {
            m_limitDelayInt = maxDelayInt;
        }
        else
        {
            m_limitDelayInt = std::min(maxDelayInt, static_cast<int>(limitDelayLong));
        }

        std::this_thread::sleep_for(delayNs);
    }
} // rma_stack