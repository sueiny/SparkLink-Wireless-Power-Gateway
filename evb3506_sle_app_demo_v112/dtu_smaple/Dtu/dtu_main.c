/**
 * @file dtu_main.c
 * @brief DTU（数据传输单元）应用程序主入口文件
 * @details 本文件是DTU应用程序的入口点，负责启动系统初始化任务。
 *          DTU设备主要用于将串口设备（如传感器、PLC等）的数据通过无线网络
 *          （BLE/SLE）传输到云端或上位机，实现设备远程监控和数据采集。
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @note 启动流程：
 *       1. 系统调用 app_run() 注册主入口函数
 *       2. 主入口函数创建初始化任务线程
 *       3. 初始化任务调用 dtu_service_init() 完成：
 *          - 从NV加载配置参数
 *          - 检测DIP拨码开关确定工作模式
 *          - 初始化UART串口（配置口和485总线口）
 *          - 根据模式初始化BLE或SLE无线通道
 *          - 打印启动配置快照
 *
 * @par 工作模式：
 *       - CONFIG模式（DIP高电平）：用于配置DTU参数，通过UART0或BLE进行配置
 *       - RUN模式（DIP低电平）：正常工作模式，进行数据透传和组网
 *
 * @par 架构说明：
 *       - dtu_main.c：本文件，仅负责启动初始化任务
 *       - dtu_service：服务管理器，负责初始化顺序和输入分流
 *       - dtu_config：配置协议处理（AA55帧格式）
 *       - dtu_run：运行态业务逻辑
 *       - dtu_storage：配置持久化（NV存储）
 *       - dtu_transport：传输通道抽象（UART/BLE/SLE）
 */

#include "app_init.h"
#include "soc_osal.h"

#include "dtu_build_config.h"
#include "dtu_service.h"

/**
 * @brief DTU初始化任务函数
 * @details 该任务是DTU系统的第一个执行任务，主要职责：
 *          1. 调用 dtu_service_init() 完成所有硬件和协议初始化
 *          2. 初始化失败时打印错误并返回（不重启系统）
 *          3. 初始化成功后任务自动结束（由系统回收资源）
 *
 * @note 该任务使用独立的栈空间（DTU_CFG_INIT_TASK_STACK_SIZE），
 *       避免在系统主线程中执行较重的初始化操作。
 *
 * @warning 如果初始化失败，DTU将无法正常工作，需要检查：
 *          - NV存储是否正常
 *          - UART引脚配置是否正确
 *          - BLE/SLE驱动是否正常
 */
static void dtu_init_task(void)
{
    /* 打印初始化任务开始标记，便于日志追踪启动流程 */
    osal_printk("[DTU LOG] %s begin\r\n", DTU_CFG_INIT_TASK_NAME);

    /* 调用服务管理器初始化，这是DTU系统的核心初始化入口 */
    if (dtu_service_init() != ERRCODE_SUCC) {
        osal_printk("DTU sample init failed\r\n");
        return;
    }

    /* 打印初始化任务结束标记 */
    osal_printk("[DTU LOG] %s end\r\n", DTU_CFG_INIT_TASK_NAME);
}

/**
 * @brief DTU应用程序主入口函数
 * @details 该函数是app_run()注册的回调函数，由系统在应用启动时调用。
 *          主要职责：
 *          1. 创建DTU初始化任务线程
 *          2. 设置任务优先级
 *          3. 确保初始化在系统完全启动后执行
 *
 * @note 使用 osal_kthread_lock()/unlock() 保护任务创建过程，
 *       防止多线程竞争条件。
 *
 * @par 任务优先级说明：
 *       - DTU_CFG_INIT_TASK_PRIO (24)：初始化任务优先级
 *       - DTU_CFG_TRANSPORT_TASK_PRIO (25)：传输任务优先级（更高）
 *       - 传输任务优先级高于初始化任务，确保数据收发实时性
 */
static void dtu_main_entry(void)
{
    osal_task *task = NULL;

    /* 锁定内核线程调度，确保任务创建过程的原子性 */
    osal_kthread_lock();

    /* 创建DTU初始化任务线程 */
    task = osal_kthread_create((osal_kthread_handler)dtu_init_task, 0,
        DTU_CFG_INIT_TASK_NAME, DTU_CFG_INIT_TASK_STACK_SIZE);

    /* 设置任务优先级 */
    if (task != NULL) {
        osal_kthread_set_priority(task, DTU_CFG_INIT_TASK_PRIO);
    }

    /* 解锁内核线程调度，允许任务调度 */
    osal_kthread_unlock();
}

/**
 * @brief 注册DTU应用程序入口
 * @details app_run() 宏将 dtu_main_entry 注册为系统应用入口。
 *          系统初始化完成后会自动调用该函数启动DTU服务。
 *
 * @note 这是HiSilicon SDK的标准应用注册方式，
 *       每个应用程序只能有一个 app_run() 调用。
 */
app_run(dtu_main_entry);
