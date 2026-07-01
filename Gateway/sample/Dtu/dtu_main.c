/**
 * @file dtu_main.c
 * @brief DTU 应用入口，创建一次性初始化任务。
 */

#include "app_init.h"
#include "soc_osal.h"

#include "dtu_build_config.h"
#include "dtu_service.h"

/* 初始化放到独立任务中执行，避免阻塞 SDK app 入口上下文。 */
static void dtu_init_task(void)
{
    osal_printk("[DTU LOG] %s begin\r\n", DTU_CFG_INIT_TASK_NAME);

    if (dtu_service_init() != ERRCODE_SUCC) {
        osal_printk("DTU sample init failed\r\n");
        return;
    }

    osal_printk("[DTU LOG] %s end\r\n", DTU_CFG_INIT_TASK_NAME);
}

static void dtu_main_entry(void)
{
    osal_task *task = NULL;

    osal_kthread_lock();

    task = osal_kthread_create((osal_kthread_handler)dtu_init_task, 0,
        DTU_CFG_INIT_TASK_NAME, DTU_CFG_INIT_TASK_STACK_SIZE);

    if (task != NULL) {
        osal_kthread_set_priority(task, DTU_CFG_INIT_TASK_PRIO);
    }

    osal_kthread_unlock();
}

app_run(dtu_main_entry);
