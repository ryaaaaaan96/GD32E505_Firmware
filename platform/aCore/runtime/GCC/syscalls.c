#include "aCore_runtime.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/times.h>

static char *s_environment[] = {NULL};
char **environ = s_environment;

__attribute__((weak)) aSSize_t aCoreRuntimeRead(void *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak)) aSSize_t aCoreRuntimeWrite(const void *data, size_t size)
{
    (void)data;
    (void)size;
    errno = ENOSYS;
    return -1;
}

void initialise_monitor_handles(void)
{
}

int _getpid(void)
{
    return 1;
}

int _kill(int process_id, int signal_number)
{
    (void)process_id;
    (void)signal_number;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

int _read(int file, char *buffer, int length)
{
    aSSize_t result;

    if ((file != 0) || (length < 0) ||
        ((buffer == NULL) && (length != 0))) {
        errno = EINVAL;
        return -1;
    }
    if (length == 0) {
        return 0;
    }

    result = aCoreRuntimeRead(buffer, (size_t)length);
    if ((result < 0) || (result > (aSSize_t)length)) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    return (int)result;
}

int _write(int file, char *buffer, int length)
{
    aSSize_t result;

    if (((file != 1) && (file != 2)) || (length < 0) ||
        ((buffer == NULL) && (length != 0))) {
        errno = EINVAL;
        return -1;
    }
    if (length == 0) {
        return 0;
    }

    result = aCoreRuntimeWrite(buffer, (size_t)length);
    if ((result < 0) || (result > (aSSize_t)length)) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }
    return (int)result;
}

int _close(int file)
{
    (void)file;
    errno = ENOSYS;
    return -1;
}

int _fstat(int file, struct stat *status)
{
    if ((status == NULL) || ((file < 0) || (file > 2))) {
        errno = EINVAL;
        return -1;
    }

    status->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    if ((file < 0) || (file > 2)) {
        errno = EBADF;
        return 0;
    }
    return 1;
}

int _lseek(int file, int offset, int origin)
{
    (void)file;
    (void)offset;
    (void)origin;
    errno = ESPIPE;
    return -1;
}

int _open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int _wait(int *status)
{
    (void)status;
    errno = ECHILD;
    return -1;
}

int _unlink(const char *name)
{
    (void)name;
    errno = ENOENT;
    return -1;
}

clock_t _times(struct tms *buffer)
{
    (void)buffer;
    errno = ENOSYS;
    return (clock_t)-1;
}

int _stat(const char *path, struct stat *status)
{
    (void)path;
    if (status == NULL) {
        errno = EINVAL;
        return -1;
    }

    status->st_mode = S_IFCHR;
    return 0;
}

int _link(const char *old_path, const char *new_path)
{
    (void)old_path;
    (void)new_path;
    errno = EMLINK;
    return -1;
}

int _fork(void)
{
    errno = EAGAIN;
    return -1;
}

int _execve(const char *name, char *const arguments[],
            char *const environment[])
{
    (void)name;
    (void)arguments;
    (void)environment;
    errno = ENOMEM;
    return -1;
}
