/* 真实控制台适配 + Shell 解析器，模拟 USART 满缓冲丢新字节及读取窗口中的 ISR。 */
#include "system_device.h"
#include "aDev_usart.h"
#include "aShell.h"
#include "os_mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char device_storage;
static uint8_t *ring;
static size_t capacity, head, tail, used;
static aBool_t overflow, hardware_error, overflow_on_empty;
static const char *during_read, *after_discard;
static aStatus_t empty_status = A_STATUS_BUSY;
static aStatus_t discard_status = A_STATUS_OK;
static unsigned reads, commands, discards;
static aTimeout_t last_read_timeout;
static char output[8192];
static size_t output_size;

static aDevUsartHandle_t *device(void)
{ return (aDevUsartHandle_t *)(void *)&device_storage; }

static void receive_byte(uint8_t byte)
{
    if (used == capacity) { overflow = A_TRUE; return; }
    ring[head] = byte;
    head = (head + 1U) % capacity;
    ++used;
}

static void receive(const char *text)
{ while (*text) receive_byte((uint8_t)*text++); }

static void fill(void)
{ while (used < capacity) receive_byte('x'); }

aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                         aDevUsartHandle_t **handle)
{
    assert((config->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_INTERRUPT_BUFFERED);
    assert(config->drv_config.interrupt_priority == 6U);
    ring = config->rx_buffer; capacity = config->rx_buffer_size;
    assert(ring && capacity == 256U);
    head = tail = used = 0U;
    overflow = hardware_error = A_FALSE;
    *handle = device();
    return A_STATUS_OK;
}

aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{ assert(handle == device()); return A_STATUS_OK; }
aStatus_t aDevUsartDiscardRx(aDevUsartHandle_t *handle)
{
    assert(handle == device());
    ++discards;
    if (discard_status != A_STATUS_OK) return discard_status;
    tail = head; used = 0U;
    overflow = hardware_error = A_FALSE;
    /* 丢弃边界之后的数据属于新输入，不能要求返回时缓冲仍为空。 */
    if (after_discard) { receive(after_discard); after_discard = NULL; }
    return A_STATUS_OK;
}

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer, size_t size,
                      aTimeout_t timeout)
{
    ++reads; last_read_timeout = timeout;
    if (handle == NULL || !aTimeoutIsValid(timeout) ||
        size > (size_t)PTRDIFF_MAX || (buffer == NULL && size != 0U))
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    assert(handle == device());
    if (size == 0U) return 0;
    if (overflow || hardware_error) return aOSFailWithStatus(A_STATUS_ERROR);
    if (used == 0U) {
        /* 读空后、aDev 返回之前，ISR 又填满并丢字节。 */
        if (overflow_on_empty) {
            overflow_on_empty = A_FALSE;
            receive("\rgo\r"); fill(); receive("lost");
        }
        return aOSFailWithStatus(overflow ? A_STATUS_ERROR : empty_status);
    }
    size_t count = used < size ? used : size;
    for (size_t i = 0U; i < count; ++i)
        ((uint8_t *)buffer)[i] = ring[(tail + i) % capacity];
    /* 实际驱动复制后才释放 ring 占用，此时 ISR 仍可能观察到满缓冲。 */
    if (during_read) { receive(during_read); during_read = NULL; }
    tail = (tail + count) % capacity;
    used -= count;
    if (overflow) return aOSFailWithStatus(A_STATUS_ERROR);
    return (aSSize_t)count;
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *buffer, size_t size,
                       aTimeout_t timeout)
{
    assert(handle == device() && aTimeoutIsValid(timeout));
    assert(output_size + size < sizeof(output));
    memcpy(output + output_size, buffer, size);
    output_size += size; output[output_size] = '\0';
    return (aSSize_t)size;
}

static int command(int argc, char **argv)
{ assert(argc == 1 && strcmp(argv[0], "go") == 0); ++commands; return 0; }
ASHELL_CMD_EXPORT(go, command, "Count executed commands");

static void process(aStatus_t expected)
{
    unsigned before = reads;
    assert(aShellProcess() == expected);
    assert(reads == before + 1U); /* 一次读取即可完成丢弃，不循环排空。 */
    assert(last_read_timeout.type == A_TIMEOUT_TYPE_RELATIVE);
    assert(last_read_timeout.milliseconds == 20U);
}

static void begin(void)
{
    commands = reads = discards = 0U;
    during_read = after_discard = NULL; overflow_on_empty = A_FALSE;
    empty_status = A_STATUS_BUSY;
    discard_status = A_STATUS_OK;
    output_size = 0U; output[0] = '\0';
    assert(appSystemConsoleInit() == A_STATUS_OK);
    process(A_STATUS_BUSY);
    output_size = 0U; output[0] = '\0';
}

static void end(void)
{
    assert(appSystemConsoleDeInit() == A_STATUS_OK);
    assert(testMutexCount() == 0U);
}

static void check_read_parameters(void)
{
    uint8_t byte;
    const aTimeout_t invalid = {.type = (aTimeoutType_t)-1, .milliseconds = 17U};
    const struct {
        void *buffer;
        size_t size;
        aTimeout_t timeout;
        aSSize_t result;
    } cases[] = {
        {NULL, 0U, A_TIMEOUT_NO_WAIT, 0},
        {&byte, 0U, A_TIMEOUT_FOREVER, 0},
        {NULL, 1U, A_TIMEOUT_NO_WAIT, -1},
        {&byte, (size_t)PTRDIFF_MAX + 1U, A_TIMEOUT_NO_WAIT, -1},
        {&byte, 1U, invalid, -1},
        {NULL, 0U, invalid, -1},
    };
    unsigned before = reads, saved_discards = discards;
    size_t saved_used = used, saved_head = head, saved_tail = tail;
    aBool_t saved_overflow = overflow, saved_error = hardware_error;
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(app_system_console_stream.read(cases[i].buffer, cases[i].size,
                                              cases[i].timeout) == cases[i].result);
        if (cases[i].result < 0) assert(aOSGetErrno() == A_EINVAL);
        assert(reads == before + i + 1U); /* 每次都由 aDev 处理参数。 */
        assert(last_read_timeout.type == cases[i].timeout.type);
        assert(last_read_timeout.milliseconds == cases[i].timeout.milliseconds);
        assert(used == saved_used && head == saved_head && tail == saved_tail);
        assert(overflow == saved_overflow && hardware_error == saved_error);
        assert(discards == saved_discards);
    }
}

static void check_discarded(void)
{
    assert(used == 0U && !overflow && !hardware_error);
    check_read_parameters();
    process(empty_status);
    assert(commands == 0U);
    const char *notice = strstr(output, "[shell] input error;");
    assert(notice && !strstr(notice + 1, "[shell] input error;"));
}

static void recover(void)
{
    /* 丢弃之后的尾部仍需跳过；跨两次读取的 CRLF 只能结束一次恢复。 */
    receive("go\r"); process(A_STATUS_OK);
    assert(commands == 0U);
    receive("\ngo\r"); process(A_STATUS_OK);
    assert(commands == 1U);
}

int main(void)
{
    begin();
    receive("g"); process(A_STATUS_OK);
    process(A_STATUS_BUSY);
    empty_status = A_STATUS_TIMEOUT;
    process(A_STATUS_TIMEOUT); /* 暂无数据不能破坏正常半行。 */
    (void)aOSFailWithStatus(A_STATUS_ERROR); /* 正长度成功忽略旧 errno。 */
    receive("o\r"); process(A_STATUS_OK);
    assert(commands == 1U && !strstr(output, "input error"));
    end();

    begin();
    receive("g"); process(A_STATUS_OK);
    receive("o\rgo\r"); fill(); receive("lost");
    check_read_parameters(); /* 尚未开始恢复时不能清除溢出标志。 */
    process(A_STATUS_ERROR);
    assert(discards == 1U && commands == 0U);
    check_discarded(); recover(); end();

    begin();
    receive("go\r"); fill();
    assert(!overflow);
    during_read = "lost";
    process(A_STATUS_ERROR); /* 仅做读取前检查会错误执行 go。 */
    assert(discards == 1U && commands == 0U && !overflow);
    receive("go\r");
    hardware_error = A_TRUE;
    check_read_parameters(); /* 参数错误不能清除硬件错误。 */
    process(A_STATUS_ERROR);
    assert(discards == 2U && commands == 0U);
    check_discarded(); recover(); end();

    begin();
    for (unsigned i = 0U; i < 8U; ++i) {
        receive("\rgo\r"); fill(); receive("lost again");
        process(A_STATUS_ERROR);
        assert(discards == i + 1U && commands == 0U && used == 0U && !overflow);
    }
    empty_status = A_STATUS_TIMEOUT;
    check_discarded(); recover(); end();

    begin();
    /* 等待结束时才溢出，aDev 仍须优先返回输入错误。 */
    overflow_on_empty = A_TRUE;
    process(A_STATUS_ERROR);
    assert(discards == 1U && commands == 0U);
    check_discarded(); recover(); end();

    begin();
    receive("go\r"); fill(); receive("lost");
    after_discard = "\rgo\r";
    process(A_STATUS_ERROR);
    assert(discards == 1U && commands == 0U && used == 4U);
    process(A_STATUS_OK); /* 新行边界之后的完整命令可以正常执行。 */
    assert(commands == 1U); end();

    begin();
    receive("go\r"); fill(); receive("lost");
    discard_status = A_STATUS_BUSY;
    process(A_STATUS_ERROR); /* 丢弃失败不能把已损坏输入交给 Shell。 */
    assert(discards == 1U && overflow && used != 0U && commands == 0U);
    discard_status = A_STATUS_OK;
    process(A_STATUS_ERROR);
    assert(discards == 2U && commands == 0U);
    check_discarded(); recover(); end();

    begin();
    receive("go\r"); hardware_error = A_TRUE;
    discard_status = A_STATUS_ERROR;
    process(A_STATUS_ERROR);
    assert(discards == 1U && hardware_error && used == 3U && commands == 0U);
    discard_status = A_STATUS_OK;
    process(A_STATUS_ERROR);
    check_discarded(); recover(); end();

    begin();
    fill(); receive("lost"); process(A_STATUS_ERROR);
    end(); /* Shell 等待新行边界时重新初始化，不能继续丢弃新实例输入。 */
    char byte;
    assert(app_system_console_stream.read(&byte, 1U, A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EINVAL); /* 空句柄沿用 aDev 的错误约定。 */
    assert(app_system_console_stream.read(NULL, 0U, A_TIMEOUT_NO_WAIT) == -1);
    assert(aOSGetErrno() == A_EINVAL);
    begin(); receive("go\r"); process(A_STATUS_OK);
    assert(commands == 1U); end();
    puts("Console RX overflow: stale delimiters, read races, repeated loss and recovery passed");
    return 0;
}
