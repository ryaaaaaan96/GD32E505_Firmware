/**
 * @file aShell.h
 * @brief 基于流的单例命令控制台；由应用任务调用 Process。
 * Init/DeInit 必须外部串行化；销毁前退出所有 API 用户。
 * 除配置初始化和 IsEnabled 外，仅允许任务上下文调用。
 */
#ifndef A_SHELL_H
#define A_SHELL_H

#include "aStream.h"
#include <stdint.h>

/** argc 包含命令名；argv 仅在回调期间有效，不保证 argv[argc] 为 NULL。
 * 回调在 Process 调用者任务中同步运行，返回 0 表示成功。
 * 回复使用 ASHELL_REPLY；禁止递归调用 Process 或在回调中 Init/DeInit。
 */
typedef int (*aShellCommandFn_t)(int argc, char **argv);

/** 在文件作用域导出命令；name 为标识符，description 为非空静态字符串。
 * callback 可以为 static，签名为 int(int, char **)。
 * 链接收集后 Init 校验重名；help/version 为内置名称。
 * 命令对象文件必须参与最终链接，普通静态库不保证自动提取。
 * 当前构建仅支持 GCC/ELF；编译器相关实现保留在 detail 目录。
 */
#if ASHELL_ENABLE
#if defined(__GNUC__) && !defined(__ARMCC_VERSION)
#include "detail/aShell_export_gcc.h"
#else
#error "aShell command export currently requires GCC/ELF"
#endif
#define ASHELL_PRINT(...) aShellPrintf(__VA_ARGS__)
#define ASHELL_REPLY(...) aShellReplyf(__VA_ARGS__)
#else
/* No descriptor, callback reference or argument evaluation when disabled. */
#define ASHELL_CMD_EXPORT(name, callback, description) \
    _Static_assert(sizeof(&(callback)) != 0U, "Shell command disabled")
static inline aStatus_t aShellPrintDisabled(void)
{
    return A_STATUS_OK;
}
#define ASHELL_PRINT(...) aShellPrintDisabled()
#define ASHELL_REPLY(...) aShellPrintDisabled()
#endif

typedef struct {
    aStream_t stream; /**< read/write 必填；flush 可空且不会自动调用。 */
    aTimeout_t read_timeout; /**< 单次读取预算，默认 NO_WAIT，禁止 FOREVER。 */
    aTimeout_t write_timeout; /**< 每次排队数据发送的总预算，默认 100 ms。 */
} aShellConfig_t;

/** 默认空流；NULL 不操作。编辑容量见 aShell_config.h。 */
void aShellConfigStructInit(aShellConfig_t *config);

/** 校验命令表并初始化队列和锁，不创建任务，不访问流。
 * 初始提示符仅入队，由首次 Process 发送。
 * 返回 OK / INVALID_PARAM / BUSY 或锁创建错误。
 * 禁用构建为空操作，返回 OK。
 */
aStatus_t aShellInit(const aShellConfig_t *config);

/** 输入处理前后发送排队数据，每次读取至多 64 字节并同步执行命令。
 * 只允许一个调用者；业务命令和实际流读写均不持队列锁。
 * 输入前后各用一个 write_timeout，命令 Reply 各自使用新的发送预算。
 * 命令执行及队列短临界区的互斥等待不受整轮时间上限约束。
 * 无输入返回 BUSY；输出错误优先返回，未发送字节留待下次处理。
 * 输入 I/O 故障丢弃至下一个换行并重置编辑/历史；超时与 BUSY 不重置。
 * OK 不表示业务成功或输出队列为空；入队丢弃通过 Print/Stats 报告。
 */
aStatus_t aShellProcess(void);

/** 丢弃未发送数据并释放内部锁；返回 OK 或 NOT_READY。
 * 必须先停止处理任务和所有并发 API 用户。
 */
aStatus_t aShellDeInit(void);
aBool_t aShellIsEnabled(void);

typedef struct {
    size_t pending_bytes; /**< 包含流 write 正在读取但尚未确认的字节。 */
    unsigned dropped_messages; /**< 拒绝入队或回复中断次数，溢出回绕。 */
} aShellOutputStats_t;

/** 获取队列快照；返回 OK / INVALID_PARAM / NOT_READY 或锁错误。
 * 统计从 Init 开始；nr 的一个输出片段计一次消息，不等于一整行。
 */
aStatus_t aShellGetOutputStats(aShellOutputStats_t *stats);

/** 原始字节整条复制入队，不格式化，不要求 NUL，不等待空间或发送。
 * 用于日志等输出适配；只允许任务上下文调用。
 * 满队列或锁争用返回 BUSY 并整条丢弃；size 为 0 允许 data 为 NULL。
 * 未初始化返回 NOT_READY；禁用实现返回 OK，不访问数据。
 */
aStatus_t aShellWrite(const char *data, size_t size);

/** ASHELL_PRINT 的实现：格式化并复制入队，任务上下文，不等待空间/发送。
 * OK 表示完整入队；BUSY 表示空间不足或锁争用，本条整条丢弃。
 * 文本超过 255 字节返回 INVALID_PARAM，格式化失败返回 ERROR，均不截断。
 * NULL 返回 INVALID_PARAM，未初始化返回 NOT_READY；禁用宏返回 OK。
 * 消费者只有 Process；长命令期间输出积压，不能靠重试等待自己消费。
 */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
aStatus_t aShellPrintf(const char *format, ...);

/** 命令输出：仅 Process 调用链内使用，队列不足时主动发送再继续。
 * 每次调用使用 write_timeout 预算；后台任务继续使用 ASHELL_PRINT。
 * 失败锁存至本轮 Process 返回，后续回复停止，避免反复等待故障接口。
 * 计一次丢弃，保留已入队部分；输出恢复后提示重试，不自动重放命令。
 * 只发送输出，不递归处理输入，也不调用 stream.flush。
 * Replyf 与 Printf 的格式长度限制相同；更长原始文本使用 ReplyWrite。 */
aStatus_t aShellReplyWrite(const char *data, size_t size);
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
aStatus_t aShellReplyf(const char *format, ...);

#endif
