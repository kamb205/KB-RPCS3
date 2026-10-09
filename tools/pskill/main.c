/*
 * pskill - list the PS5's processes to klog and stop every process whose name begins
 * with the given prefix (built-in: "kstuff"). Send with: nc <ps5> 9021 < pskill.elf
 * Output goes to klog (read it with tools/klogsrv). After the method of the payload
 * SDK's "ps" sample (John Törnblom, GPL-3.0-or-later).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TARGET
#define TARGET "kstuff"
#endif

int main(void)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0)
        return 1;
    size += size / 4;
    char *buffer = malloc(size);
    if (!buffer || sysctl(mib, 4, buffer, &size, NULL, 0) != 0)
        return 1;
    const pid_t self = getpid();
    int killed = 0;
    for (char *p = buffer; p < buffer + size;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize <= 0)
            break;
        p += ki->ki_structsize;
        printf("pskill: pid %d ppid %d %s\n", ki->ki_pid, ki->ki_ppid, ki->ki_comm);
#ifdef KILL_PID
        // one process by its ID (built with -DKILL_PID=<pid>), and only if its name starts with TARGET
        if (ki->ki_pid == KILL_PID && strncmp(ki->ki_comm, TARGET, strlen(TARGET)) == 0) {
#else
        if (ki->ki_pid != self && strncmp(ki->ki_comm, TARGET, strlen(TARGET)) == 0) {
#endif
            const int result = kill(ki->ki_pid, SIGKILL);
            printf("pskill: KILL pid %d (%s): %s\n", ki->ki_pid, ki->ki_comm, result == 0 ? "ok" : "failed");
            if (result == 0)
                killed++;
        }
    }
    printf("pskill: done, %d process(es) named %s* stopped\n", killed, TARGET);
    free(buffer);
    return 0;
}
