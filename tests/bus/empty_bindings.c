#include "aBus.h"
#include "aOS.h"
#include <assert.h>
#include <stdlib.h>

void *aOSAlloc(size_t size)
{
    return malloc(size);
}

void aOSFree(void *pointer)
{
    free(pointer);
}

int main(void)
{
    const aBusSig_t sig = {.sigKey = 77, .size = sizeof(uint32_t)};
    const aBusTable_t table = {.sigs = &sig, .sig_count = 1};
    aBusHandle_t *handle = NULL;
    aBusGetIndexRequest_t request;
    uint32_t output = 1;

    assert(aBusCreate(&table, &handle) == A_STATUS_OK);
    aBusGetIndexRequestStructInit(&request);
    request.dst = &output;
    request.size = sizeof(output);
    assert(aBusGetByIndex(handle, &request) == A_STATUS_OK);
    assert(output == 0);
    assert(aBusDestroy(handle) == A_STATUS_OK);
    return 0;
}
