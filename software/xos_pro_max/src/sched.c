/*------------------------------------------------------------------------------
 * xOS Scheduler Implementation
 *
 * Simple round-robin task scheduler
 *----------------------------------------------------------------------------*/

#include <output.h>
#include <heap.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <uart.h>

/*============================================================================
 * Global Variables
 *============================================================================*/

/* Task table */
task_t tasks[MAX_TASKS];

/* Current running task ID */
// -1 means the mother task
int current_task = -1;

/* Number of tasks created */
int num_tasks = 0;

/* Current foreground task (for output redirection) */
int foreground_task = 0; /* Shell is always foreground initially */

int find_free_slot(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD) {
            return i;
        }
    }
    return -1;
}

void safe_strcpy(char *dst, const char *src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/*============================================================================
 * Scheduler API Implementation
 *============================================================================*/

void sched_init(void) {
    /* Initialize all task slots to UNUSED */
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
        tasks[i].task_id = i;

        /* Output buffer will be allocated when task is created */
        tasks[i].output.buffer = NULL;
        tasks[i].output.write_pos = 0;
        tasks[i].output.total_bytes = 0;
        tasks[i].output.is_foreground = 0;
    }

    current_task = -1;
    num_tasks = 0;
    foreground_task = 0; /* Shell (task 0) is foreground by default */

    printf("[SCHED] Initialized, max tasks: %d\n", MAX_TASKS);
}

int task_create(void (*entry)(void), const char *name) {
    /* Find free slot */
    int tid = find_free_slot();
    if (tid < 0) {
        printf("[SCHED] ERROR: No free task slot\n");
        return -1;
    }

    task_t *task = &tasks[tid];

    /* Initialize registers to 0 */
    for (int i = 0; i < 32; i++) {
        task->regs[i] = 0;
    }

    /* Set stack pointer to top of stack */
    task->regs[3] = (uint32_t)&task->stack[TASK_STACK_SIZE - 1];

    /* Set entry point */
    task->era = (uint32_t)entry;

    /* Set PRMD: PLV0, IE=1 (interrupts enabled) */
    task->prmd = 0x4; /* 0b0100: PLV=0, IE=1 */

    /* Set task metadata */
    task->state = TASK_READY;
    safe_strcpy(task->name, name, sizeof(task->name));

    /* Allocate output buffer dynamically */
    if (task->output.buffer == NULL) {
        task->output.buffer = (char *)malloc(TASK_OUTPUT_BUF_SIZE);
        if (task->output.buffer == NULL) {
            printf(
                "[SCHED] ERROR: Failed to allocate output buffer for task %d\n",
                tid);
            task->state = TASK_UNUSED;
            return -1;
        }
        task->output.write_pos = 0;
        task->output.total_bytes = 0;
    }

    /* Set foreground mode: Shell (task 0) is foreground, others are background
     */
    if (tid == 0) {
        task->output.is_foreground = 1;
        foreground_task = 0;
    } else {
        task->output.is_foreground = 0;
    }

    num_tasks++;

    return tid;
}

void task_exit(void) {
    if (current_task < 0) {
        while (1)
            ;
    }

    /* Free output buffer */
    if (tasks[current_task].output.buffer != NULL) {
        free(tasks[current_task].output.buffer);
        tasks[current_task].output.buffer = NULL;
    }

    /* Mark task as dead */
    tasks[current_task].state = TASK_DEAD;
    num_tasks--;

    /* Trigger immediate reschedule */
    schedule(NULL);

    /* Never returns */
    while (1)
        ;
}

int task_kill(int tid) {
    if (tid < 0 || tid >= MAX_TASKS) {
        return -1;
    }

    task_t *task = &tasks[tid];

    /* 检查任务状态 */
    if (task->state == TASK_UNUSED || task->state == TASK_DEAD) {
        return -1; /* 任务不存在 */
    }

    /* 释放 output buffer */
    if (task->output.buffer != NULL) {
        free(task->output.buffer);
        task->output.buffer = NULL;
    }

    /* 如果是前台任务，重置前台为 shell */
    if (foreground_task == tid) {
        foreground_task = 0;
        tasks[0].output.is_foreground = 1;
    }

    /* 标记为 DEAD */
    task->state = TASK_DEAD;
    num_tasks--;

    return 0;
}

void task_yield(void) {
    /* 触发 SWI0 软中断，让出 CPU */
    // printf("swi0 trigged\n");
    uint32_t estat = csr_read(CSR_ESTAT);
    estat |= ECFG_LIE_SWI0;
    csr_write(CSR_ESTAT, estat);
}

void task_yield_simple(void) {
    // printf("task_yield_simple called\n");
    // 把当前任务设为READY
    task_t *task = &tasks[current_task];
    task->state = TASK_READY;

    // 清零所有寄存器（和task_create一致）
    for (int i = 0; i < 32; i++) {
        task->regs[i] = 0;
    }

    // 重置栈指针
    task->regs[3] = (uint32_t)&task->stack[TASK_STACK_SIZE - 1];

    // era保持不变（函数入口），下次从头开始
    // 直接调度，不保存上下文
    schedule(NULL);
}

void schedule(trap_frame_t *tf) {
    /* Save current task context (if not first schedule) */
    if (current_task >= 0 && tf != NULL) {
        task_t *curr = &tasks[current_task];

        /* Copy trap frame to TCB */
        curr->regs[1] = tf->ra;
        curr->regs[2] = tf->tp;
        curr->regs[3] = tf->sp;
        curr->regs[4] = tf->a0;
        curr->regs[5] = tf->a1;
        curr->regs[6] = tf->a2;
        curr->regs[7] = tf->a3;
        curr->regs[8] = tf->a4;
        curr->regs[9] = tf->a5;
        curr->regs[10] = tf->a6;
        curr->regs[11] = tf->a7;
        curr->regs[12] = tf->t0;
        curr->regs[13] = tf->t1;
        curr->regs[14] = tf->t2;
        curr->regs[15] = tf->t3;
        curr->regs[16] = tf->t4;
        curr->regs[17] = tf->t5;
        curr->regs[18] = tf->t6;
        curr->regs[19] = tf->t7;
        curr->regs[20] = tf->t8;
        curr->regs[21] = tf->r21;
        curr->regs[22] = tf->fp;
        curr->regs[23] = tf->s0;
        curr->regs[24] = tf->s1;
        curr->regs[25] = tf->s2;
        curr->regs[26] = tf->s3;
        curr->regs[27] = tf->s4;
        curr->regs[28] = tf->s5;
        curr->regs[29] = tf->s6;
        curr->regs[30] = tf->s7;
        curr->regs[31] = tf->s8;
        curr->era = tf->era;
        curr->prmd = tf->prmd;

        curr->state = TASK_READY;
    }

    /* Select next task (round-robin) */
    int next = current_task;
    int attempts = 0;

    do {
        /* Move to next task */
        if (next < 0) {
            // first schedule, from mother task to task 0 --- shell
            next = 0;
        } else {
            next = (next + 1) % MAX_TASKS;
        }

        /* Check if this task is ready */
        if (tasks[next].state == TASK_READY) {
            break;
        }

        attempts++;
    } while (attempts < MAX_TASKS);

    /* No ready task found */
    if (attempts >= MAX_TASKS) {
        printf("[SCHED] ERROR: No ready task found!\n");
        while (1)
            ;
    }

    /* Switch to next task */
    current_task = next;
    tasks[next].state = TASK_RUNNING;

    // printf("[SCHED] Switch to task %d (%s)\n", next, tasks[next].name);

    /* Jump to task (never returns) */
    switch_to(&tasks[next]);
}

int get_current_task(void) { return current_task; }

int get_num_tasks(void) { return num_tasks; }

const task_t *get_task_info(int tid) {
    if (tid < 0 || tid >= MAX_TASKS) {
        return NULL;
    }
    return &tasks[tid];
}

/*============================================================================
 * Task Output Management
 *============================================================================*/

void task_output_putc(int tid, char c) {
    if (tid < 0 || tid >= MAX_TASKS) {
        return;
    }
    task_t *task = &tasks[tid];
    task_output_t *out = &task->output;

    // when hdmi ouput is enabled, we do not store output in task buffer again
    // bacause hdmi terminal already store it
    if (!(get_output_target() & OUTPUT_HDMI)) {
        /* Write to task buffer (ring buffer) */
        out->buffer[out->write_pos] = c;
        out->write_pos = (out->write_pos + 1) % TASK_OUTPUT_BUF_SIZE;
        out->total_bytes++;
    }

    /* If foreground, also write to UART */
    if (out->is_foreground) {
        bsp_uart_putc(0, c);
    }
}

int task_set_foreground(int tid, int foreground) {
    if (tid < 0 || tid >= MAX_TASKS) {
        return -1;
    }

    if (tasks[tid].state == TASK_UNUSED || tasks[tid].state == TASK_DEAD) {
        return -1;
    }

    /* If setting to foreground, clear previous foreground task */
    if (foreground) {
        /* Shell (task 0) is always foreground */
        if (tid != 0 && foreground_task != 0) {
            tasks[foreground_task].output.is_foreground = 0;
        }
        foreground_task = tid;
    }

    tasks[tid].output.is_foreground = foreground;
    return 0;
}

int get_foreground_task(void) { return foreground_task; }
