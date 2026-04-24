/*------------------------------------------------------------------------------
 * xOS Scheduler
 *
 * Task scheduling and context switching for LoongArch32R
 *----------------------------------------------------------------------------*/

#ifndef __SCHED_H__
#define __SCHED_H__

#include <stdint.h>
#include <trap.h>


/*============================================================================
 * Task Configuration
 *============================================================================*/
#define MAX_TASKS       8           /* Maximum number of tasks */
#define TASK_STACK_SIZE 2048        /* Stack size per task (in words) */
#define TASK_OUTPUT_BUF_SIZE (1024 * 1024)  /* 1MB output buffer per task */

/*============================================================================
 * Task States
 *============================================================================*/
typedef enum {
    TASK_UNUSED = 0,    /* Task slot not used */
    TASK_READY,         /* Task ready to run */
    TASK_RUNNING,       /* Task currently running */
    TASK_DEAD           /* Task finished, waiting for cleanup */
} task_state_t;

/*============================================================================
 * Task Output Buffer (Ring Buffer)
 *============================================================================*/
typedef struct {
    char *buffer;           /* Pointer to 1MB buffer */
    uint32_t write_pos;     /* Write position (circular) */
    uint32_t total_bytes;   /* Total bytes written (for overflow detection) */
    int is_foreground;      /* 1 = foreground (print to UART), 0 = background */
} task_output_t;

/*============================================================================
 * Task Control Block (TCB)
 *============================================================================*/
typedef struct {
    /* Saved context (must match trap_frame_t layout for easy copying) */
    uint32_t regs[32];      /* General purpose registers $r0-$r31 */
    uint32_t era;           /* Exception Return Address */
    uint32_t prmd;          /* Pre-exception Mode */

    /* Task metadata */
    task_state_t state;     /* Task state */
    int task_id;            /* Task ID */
    char name[32];          /* Task name (for debugging) */

    /* Output buffer */
    task_output_t output;   /* Task output buffer */

    /* Stack */
    uint32_t stack[TASK_STACK_SIZE];  /* Task's stack */

} task_t;

/* Task table */
extern task_t tasks[MAX_TASKS];

/* Current running task ID (-1 means no task running yet) */
extern int current_task;

/* Number of tasks created */
extern int num_tasks;

/* Current foreground task (for output redirection) */
extern int foreground_task;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * Find an unused task slot
 * Can reuse TASK_UNUSED or TASK_DEAD slots
 * @return: Task ID, or -1 if no slot available
 */
int find_free_slot(void);

/**
 * Copy string safely
 */
void safe_strcpy(char *dst, const char *src, int max_len);
/*============================================================================
 * Scheduler API
 *============================================================================*/

/**
 * Initialize the scheduler
 * Must be called before any other scheduler functions
 */
void sched_init(void);

/**
 * Create a new task
 * @param entry: Task entry point function
 * @param name: Task name (for debugging)
 * @return: Task ID on success, -1 on failure
 */
int task_create(void (*entry)(void), const char *name);

/**
 * Exit current task
 * Marks the task as DEAD and triggers immediate reschedule
 * This function never returns
 */
void task_exit(void) __attribute__((noreturn));

/**
 * Yield CPU to other tasks
 * Current task remains READY and will be scheduled again later
 */
void task_yield(void);

/**
 * Simple yield function for tasks that do not need to save context
 * Just triggers a reschedule
 */
void task_yield_simple(void);

/**
 * Kill a task by ID
 * @param tid: Task ID to kill
 * @return: 0 on success, -1 on failure
 */
int task_kill(int tid);

/**
 * Schedule next task
 * Called from timer interrupt handler
 * @param tf: Trap frame pointer (NULL for first schedule)
 * This function never returns (uses switch_to)
 */
void schedule(trap_frame_t *tf) __attribute__((noreturn));

/**
 * Get current task ID
 * @return: Current task ID, or -1 if no task running
 */
int get_current_task(void);

/**
 * Get number of active tasks
 * @return: Number of tasks in READY or RUNNING state
 */
int get_num_tasks(void);

/**
 * Get task information by ID
 * @param tid: Task ID
 * @return: Pointer to task_t, or NULL if invalid
 */
const task_t* get_task_info(int tid);

/**
 * Write character to task output buffer
 * If task is foreground, also write to UART
 * @param tid: Task ID
 * @param c: Character to write
 */
void task_output_putc(int tid, char c);

/**
 * Set task foreground/background mode
 * @param tid: Task ID
 * @param foreground: 1 = foreground, 0 = background
 * @return: 0 on success, -1 on error
 */
int task_set_foreground(int tid, int foreground);

/**
 * Get current foreground task ID
 * @return: Foreground task ID, or -1 if none
 */
int get_foreground_task(void);

/*============================================================================
 * Assembly Functions (defined in sched_switch.S)
 *============================================================================*/

/**
 * Switch to a task
 * Restores context from TCB and jumps to the task
 * This function never returns
 * @param task: Pointer to task_t
 */
void switch_to(task_t *task) __attribute__((noreturn));

/**
 * Start first task (called from main)
 * Sets up initial context and jumps to first task
 * This function never returns
 */
void start_first_task(void) __attribute__((noreturn));

#endif /* __SCHED_H__ */
