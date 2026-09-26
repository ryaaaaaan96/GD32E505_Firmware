#include "app_led.h"
#include "app_usart.h"
#include <assert.h>

static unsigned led_calls, usart_calls;
aStatus_t aDevLedInit(const aDevLedConfig_t *config, aDevLedHandle_t *handle)
{
    assert(config != NULL && handle != NULL);
    assert(++led_calls == 1);
    aDevLedHandle_t *nested = handle;
    assert(appLedInit(APP_LED_STATUS, &nested) == A_STATUS_BUSY);
    assert(nested == NULL);
#ifdef LED_FAILURE
    return A_STATUS_ERROR;
#else
    return A_STATUS_OK;
#endif
}
aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                             aDevUsartStorage_t *storage,
                             aDevUsartHandle_t **out)
{
    assert(config != NULL && storage != NULL);
    assert(++usart_calls == 1);
    aDevUsartHandle_t *nested = NULL;
    assert(appUsartInit(APP_USART_CONSOLE, &nested) == A_STATUS_BUSY);
    assert(nested == NULL);
#ifdef USART_FAILURE
    *out = NULL;
    return A_STATUS_ERROR;
#else
    *out = (aDevUsartHandle_t *)(void *)storage;
    return A_STATUS_OK;
#endif
}
int main(void)
{
    aDevLedHandle_t *led = NULL;
    aDevUsartHandle_t *usart = NULL;
    assert(led_calls == 0 && usart_calls == 0);
    assert(appLedInit(APP_LED_STATUS, NULL) == A_STATUS_INVALID_PARAM);
    assert(appUsartInit(APP_USART_CONSOLE, NULL) == A_STATUS_INVALID_PARAM);
    assert(appLedInit((appLedId_t)999, &led) == A_STATUS_NOT_FOUND && led == NULL);
    assert(appUsartInit((appUsartId_t)999, &usart) == A_STATUS_NOT_FOUND && usart == NULL);
    assert(led_calls == 0 && usart_calls == 0);
    /* USART can initialize independently, before LED. */
#if !ASHELL_ENABLED
    assert(appUsartInit(APP_USART_CONSOLE, &usart) == A_STATUS_NOT_FOUND);
    assert(usart_calls == 0);
#elif defined(USART_FAILURE)
    assert(appUsartInit(APP_USART_CONSOLE, &usart) == A_STATUS_ERROR && usart == NULL);
    assert(appUsartInit(APP_USART_CONSOLE, &usart) == A_STATUS_ERROR && usart == NULL);
    assert(usart_calls == 1);
#else
    assert(appUsartInit(APP_USART_CONSOLE, &usart) == A_STATUS_OK && usart != NULL);
    aDevUsartHandle_t *same_usart = NULL;
    assert(appUsartInit(APP_USART_CONSOLE, &same_usart) == A_STATUS_OK);
    assert(same_usart == usart && usart_calls == 1);
#endif
    assert(led_calls == 0);
#ifdef LED_FAILURE
    assert(appLedInit(APP_LED_STATUS, &led) == A_STATUS_ERROR && led == NULL);
    assert(appLedInit(APP_LED_STATUS, &led) == A_STATUS_ERROR && led == NULL);
#else
    assert(appLedInit(APP_LED_STATUS, &led) == A_STATUS_OK && led != NULL);
    aDevLedHandle_t *same_led = NULL;
    assert(appLedInit(APP_LED_STATUS, &same_led) == A_STATUS_OK && same_led == led);
    assert(appLedInit((appLedId_t)999, &same_led) == A_STATUS_NOT_FOUND && same_led == NULL);
#endif
    assert(led_calls == 1);
    return 0;
}
