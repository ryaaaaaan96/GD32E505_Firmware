#include "aFifo.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int storage[2], value;
    aFifo_t fifo = {0};
    assert(aFifoInit(&fifo, storage, 0, sizeof(int)) == A_STATUS_INVALID_PARAM);
    assert(aFifoInit(&fifo, storage, SIZE_MAX, 2) == A_STATUS_INVALID_PARAM);
    assert(aFifoInit(&fifo, storage, 2, sizeof(int)) == A_STATUS_OK);
    assert(aFifoPop(&fifo, &value) == A_STATUS_NOT_READY);
    for (int round = 0; round < 10; ++round) {
        int a = round * 2, b = a + 1;
        assert(aFifoPush(&fifo, &a) == A_STATUS_OK);
        assert(aFifoPush(&fifo, &b) == A_STATUS_OK);
        assert(aFifoPush(&fifo, &b) == A_STATUS_BUSY);
        assert(aFifoCount(&fifo) == 2);
        assert(aFifoPeek(&fifo, &value) == A_STATUS_OK && value == a);
        assert(aFifoCount(&fifo) == 2);
        assert(aFifoPop(&fifo, &value) == A_STATUS_OK && value == a);
        assert(aFifoPop(&fifo, &value) == A_STATUS_OK && value == b);
    }
    assert(aFifoCount(&fifo) == 0);
    puts("FIFO boundary/wraparound tests passed");
}
