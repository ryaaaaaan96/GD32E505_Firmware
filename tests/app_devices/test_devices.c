#include "system_device.h"
#include <assert.h>
#if ASHELL_ENABLE
#include "aDev_usart.h"
#include "aShell.h"
#endif

static unsigned led_calls, usart_calls;
aStatus_t aDevLedInit(const aDevLedConfig_t *config, aDevLedHandle_t *handle)
{
    assert(config != NULL && handle != NULL);
    assert(++led_calls == 1);
#ifdef LED_FAILURE
    return A_STATUS_ERROR;
#else
    return A_STATUS_OK;
#endif
}
#if ASHELL_ENABLE
static aDevUsartHandle_t *expected_handle;
static aSSize_t io_result = 2;
static unsigned shell_calls, deinit_calls;
static unsigned rx_error_clears;
static aStatus_t rx_error = A_STATUS_OK;
void aDevUsartClearRxError(aDevUsartHandle_t *handle)
{
    assert(handle == expected_handle);
    rx_error_clears++;
    rx_error = A_STATUS_OK;
}
aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle)
{
    assert(handle == expected_handle);
    return rx_error;
}
static aStream_t stream;
static char data[4];
void aShellConfigStructInit(aShellConfig_t *config)
{
    aStreamStructInit(&config->stream);
    config->read_timeout = A_TIMEOUT_NO_WAIT;
    config->write_timeout = A_TIMEOUT_MS(100U);
}
aStatus_t aShellInit(const aShellConfig_t *config)
{
    assert(usart_calls == 1 && expected_handle != NULL);
    assert(++shell_calls == 1);
    assert(config->stream.read && config->stream.write);
    assert(config->stream.flush == NULL);
    assert(config->read_timeout.milliseconds ==
           A_TIMEOUT_MS(20U).milliseconds);
    assert(config->write_timeout.milliseconds ==
           A_TIMEOUT_MS(20U).milliseconds);
    stream = config->stream;
#ifdef SHELL_FAILURE
    return A_STATUS_NO_MEMORY;
#else
    return A_STATUS_OK;
#endif
}
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{
    assert(handle == expected_handle);
    ++deinit_calls;
    return A_STATUS_OK;
}
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t size, aTimeout_t timeout)
{
    assert(handle == expected_handle && buffer == data && size == sizeof(data));
    assert(timeout.type == A_TIMEOUT_TYPE_RELATIVE && timeout.milliseconds == 7U);
    return io_result;
}
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *buffer,
                        size_t size, aTimeout_t timeout)
{
    assert(handle == expected_handle && buffer == data && size == sizeof(data));
    assert(timeout.type == A_TIMEOUT_TYPE_RELATIVE && timeout.milliseconds == 9U);
    return io_result;
}
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                             aDevUsartHandle_t **handle)
{
    assert(config != NULL && handle != NULL);
    assert(++usart_calls == 1);
#ifdef USART_FAILURE
    return A_STATUS_ERROR;
#else
    expected_handle = (aDevUsartHandle_t *)(void *)data;
    *handle = expected_handle;
    return A_STATUS_OK;
#endif
}
#endif
int main(void)
{
    aDevLedHandle_t *led = NULL;
    assert(led_calls == 0 && usart_calls == 0);
    assert(appSystemStatusLedInit(NULL) == A_STATUS_INVALID_PARAM);
#if ASHELL_ENABLE
    aStreamStructInit(&stream);
    assert(led_calls == 0 && usart_calls == 0);
    /* USART can initialize independently, before LED. */
#if defined(USART_FAILURE)
    assert(appSystemConsoleInit() == A_STATUS_ERROR);
    assert(shell_calls == 0 && deinit_calls == 0);
    assert(usart_calls == 1);
#elif defined(SHELL_FAILURE)
    assert(appSystemConsoleInit() == A_STATUS_NO_MEMORY);
    assert(shell_calls == 1 && deinit_calls == 1);
#else
    assert(appSystemConsoleInit() == A_STATUS_OK);
    assert(shell_calls == 1 && deinit_calls == 0);
    assert(stream.read && stream.write && !stream.flush);
    assert(stream.read(data, sizeof(data), A_TIMEOUT_MS(7U)) == 2);
    assert(stream.write(data, sizeof(data), A_TIMEOUT_MS(9U)) == 2);
    io_result = -1;
    assert(stream.read(data, sizeof(data), A_TIMEOUT_MS(7U)) == -1);
    assert(rx_error_clears == 0U);
    rx_error = A_STATUS_ERROR;
    assert(stream.read(data, sizeof(data), A_TIMEOUT_MS(7U)) == -1);
    assert(rx_error_clears == 1U && rx_error == A_STATUS_OK);
    assert(stream.write(data, sizeof(data), A_TIMEOUT_MS(9U)) == -1);
    assert(usart_calls == 1);
#endif
#else
    assert(usart_calls == 0);
#endif
    assert(led_calls == 0);
#ifdef LED_FAILURE
    assert(appSystemStatusLedInit(&led) == A_STATUS_ERROR && led == NULL);
#else
    assert(appSystemStatusLedInit(&led) == A_STATUS_OK && led != NULL);
#endif
    assert(led_calls == 1);
    return 0;
}
