/*
 * Declaraciones del ABI Linux moderno.
 */
#ifndef RIDUX_COMPAT6_H
#define RIDUX_COMPAT6_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"

#define C6_IOURING_MAX 32

typedef struct {
    bool     used;
    int      fd;
    int      owner_pid;
    uint32_t entries;
    uint32_t cq_entries;
    uint32_t flags;
    uint32_t feature_bits;
    uint32_t reg_files;
    uint32_t reg_buffers;
    int      eventfd;
    bool     eventfd_async;
    uint32_t enter_wakeups;
    uint64_t submit_count;
    uint64_t complete_count;
} compat6_io_uring_t;

extern compat6_io_uring_t g_compat6_io_urings[C6_IOURING_MAX];

void compat6_rewire_syscalls(void);
void compat6_register_shell_cmds(void);
void compat6_init_all(void);
int  compat6_registered_syscalls(void);
bool compat6_core_browser_abi_ready(void);

#endif /* RIDUX_COMPAT6_H */
