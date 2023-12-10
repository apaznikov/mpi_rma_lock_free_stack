//
// Created by denis on 20.04.23.
//

#ifndef SOURCES_NODE_H
#define SOURCES_NODE_H

#include "ref_counting.h"
#include "CountedNodePtr.h"

#include <cstdint>

namespace rma_stack::ref_counting
{
    /*
     * Узел односвязного списка, который содержит глобальный указатель
     * на следующий узел, внешний счётчик ссылок на следующий узел и
     * внутренний счётчик ссылок на текущий узел. Глобальный адрес
     * этого узла также используется для адресации данных пользователя.
     * То есть, если некоторый Node имеет адрес (M, N) в массиве узлов,
     * то соответствующие ему данные пользователя будут находиться в
     * массиве данных пользователя по адресу (M, N).
     *
     * Первые 8 байт модифицируются не с помощью методов класса,
     * а обыкновенной записью в ячейку памяти с приведением типа.
     * Чаще всего модифицируются эти поля операциями односторонней
     * коммуникации.
     */
    class Node
    {
    public:
        Node();
        [[nodiscard]] const CountedNodePtr &getCountedNodePtr() const;
        void setCountedNodePtrNext(const CountedNodePtr &t_countedNodePtr);

    private:
        // Первые 8 байт.
        uint32_t m_acquired : 1; // 0 - узел свободен, 1 - узел занят.
        uint32_t m_reserved : 32 - 1; // Выравнивание данных.
        int32_t m_internalCounter; // Внутренний счётчик ссылок

        // Вторые 8 байт.
        CountedNodePtr m_countedNodePtrNext;
    };
} // rma_stack

#endif //SOURCES_NODE_H
