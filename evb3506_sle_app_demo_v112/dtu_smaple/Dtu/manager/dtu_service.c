/**
 * @file dtu_service.c
 * @brief DTU服务管理器实现
 * @details 本模块是DTU系统的核心管理器，主要职责：
 *          1. 管理transport注册表与初始化顺序
 *          2. 统一承接transport输入并按CONFIG/RUN分流
 *          3. 提供配置协议统一发送出口和telemetry统计接口
 *          4. 不保存配置、不解析配置协议细节、不承载RUN业务逻辑
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 架构说明：
 *       - 服务管理器是DTU系统的调度中心
 *       - 负责初始化顺序：storage -> board -> uart -> ble/sle
 *       - 负责输入分流：根据当前模式将数据分发到config或run模块
 *       - 负责输出统一：通过transport接口发送数据
 *
 * @par 初始化流程：
 *       1. 加载storage配置（从NV读取或使用默认值）
 *       2. 初始化board（DIP检测、LED控制）
 *       3. 初始化UART传输通道
 *       4. 根据模式初始化BLE或SLE传输通道
 *       5. 打印启动配置快照
 *
 * @par 输入分流逻辑：
 *       - CONFIG模式：所有输入数据 -> dtu_config模块处理
 *       - RUN模式：UART输入 -> dtu_run_on_uart0()
 *                  SLE输入 -> dtu_run_on_sle()
 *                  485输入 -> dtu_run_on_485()
 */

#include "dtu_service.h"

#ifdef AT_COMMAND
#include "at_product.h"
#endif
#include "dtu_board.h"
#include "dtu_config.h"
#include "dtu_log.h"
#include "dtu_run.h"
#include "dtu_service_internal.h"
#include "dtu_storage.h"
#include "dtu_transport.h"
#include "osal_debug.h"

/* manager 职责：
 * 1. 管理 transport 注册表与初始化顺序
 * 2. 统一承接 transport 输入并按 CONFIG/RUN 分流
 * 3. 提供配置协议统一发送出口和 telemetry 统计接口
 * 4. 不保存配置、不解析配置协议细节、不承载 RUN 业务逻辑
 */

/** @brief 最大帧大小（帧体+协议头+校验） */
#define DTU_SERVICE_MAX_FRAME_SIZE (DTU_CFG_MAX_FRAME_BODY + 8)

/* ========================================================================== */
/* Trace 统计区                                                                */
/* @brief 性能统计和调试信息                                                  */
/* @details 这些变量用于收集DTU系统的运行时统计信息，便于性能分析和调试        */
/* @note trace默认关闭，只在Kconfig打开后生效，避免高频接收路径打印拖慢系统   */
/* ========================================================================== */

#if (DTU_CFG_LOG_TRACE_ENABLE != 0)
/** @brief 接收批量最大值 */
static uint32_t g_dtu_rx_batch_max = 0;
/** @brief 最近一次接收批量大小 */
static uint32_t g_dtu_rx_batch_last = 0;
/** @brief 接收总字节数 */
static uint32_t g_dtu_rx_total_bytes = 0;
/** @brief 接收回调次数 */
static uint32_t g_dtu_rx_callback_count = 0;
/** @brief 环形缓冲区高水位标记 */
static uint32_t g_dtu_rx_ring_high_watermark = 0;
/** @brief 环形缓冲区溢出次数 */
static uint32_t g_dtu_rx_ring_overflow_count = 0;
/** @brief 任务唤醒次数 */
static uint32_t g_dtu_rx_task_wakeup_count = 0;
#endif

/* ========================================================================== */
/* UART0 AT前置解析区                                                         */
/* ========================================================================== */

static void dtu_service_route_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

static uint8_t g_dtu_uart0_at_head = 0;
static bool g_dtu_uart0_at_active = false;

static bool dtu_service_is_at_head_a(uint8_t byte)
{
    return (byte == 'A' || byte == 'a');
}

static bool dtu_service_is_at_head_t(uint8_t byte)
{
    return (byte == 'T' || byte == 't');
}

static bool dtu_service_at_has_line_end(const uint8_t *data, uint16_t len)
{
    if (data == NULL) {
        return false;
    }

    for (uint16_t i = 0; i < len; i++) {
        if (data[i] == '\r' || data[i] == '\n') {
            return true;
        }
    }
    return false;
}

static bool dtu_service_try_forward_uart0_at(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return false;
    }

#ifndef AT_COMMAND
    return false;
#else
    if (g_dtu_uart0_at_active) {
        (void)uapi_at_channel_data_recv(AT_UART_PORT, (uint8_t *)data, len);
        if (dtu_service_at_has_line_end(data, len)) {
            g_dtu_uart0_at_active = false;
        }
        return true;
    }

    if (g_dtu_uart0_at_head != 0) {
        if (dtu_service_is_at_head_t(data[0])) {
            uint8_t at_head[2] = { g_dtu_uart0_at_head, data[0] };
            g_dtu_uart0_at_head = 0;
            g_dtu_uart0_at_active = true;
            (void)uapi_at_channel_data_recv(AT_UART_PORT, at_head, sizeof(at_head));
            if (len > 1) {
                (void)uapi_at_channel_data_recv(AT_UART_PORT, (uint8_t *)&data[1], (uint32_t)(len - 1));
                if (dtu_service_at_has_line_end(&data[1], (uint16_t)(len - 1))) {
                    g_dtu_uart0_at_active = false;
                }
            }
            return true;
        }

        uint8_t pending = g_dtu_uart0_at_head;
        g_dtu_uart0_at_head = 0;
        dtu_service_route_bytes(DTU_TRANSPORT_UART, &pending, 1);
    }

    if (!dtu_service_is_at_head_a(data[0])) {
        return false;
    }
    if (len == 1) {
        g_dtu_uart0_at_head = data[0];
        return true;
    }
    if (!dtu_service_is_at_head_t(data[1])) {
        return false;
    }

    g_dtu_uart0_at_active = !dtu_service_at_has_line_end(data, len);
    (void)uapi_at_channel_data_recv(AT_UART_PORT, (uint8_t *)data, len);
    return true;
#endif
}

/* ========================================================================== */
/* Transport 注册表区                                                         */
/* @brief 传输通道注册表管理                                                  */
/* @details 当前只默认注册UART，后续接BLE/SLE时，只需要在这里补transport接口对象 */
/*          service本身不关心具体通道内部实现，只拿到统一的init/send接口       */
/* ========================================================================== */

/**
 * @brief 传输通道接口注册表
 * @details 按传输通道ID索引，存储各传输通道的接口对象指针
 *
 * @par 注册表说明：
 *       - DTU_TRANSPORT_UART：UART串口传输通道
 *       - DTU_TRANSPORT_BLE：蓝牙低功耗传输通道
 *       - DTU_TRANSPORT_SLE：SLE传输通道
 *
 * @note 未注册的传输通道对应位置为NULL
 */
static const dtu_transport_if_t *g_dtu_transport_table[DTU_TRANSPORT_MAX] = {
    [DTU_TRANSPORT_UART] = &g_dtu_uart_transport,
    [DTU_TRANSPORT_BLE] = &g_dtu_ble_transport,
    [DTU_TRANSPORT_SLE] = &g_dtu_sle_transport,
};

/**
 * @brief 获取传输通道接口对象
 * @details 根据传输通道ID获取对应的接口对象，这是service层访问通道的统一入口
 *
 * @param[in] transport_id 传输通道ID
 * @return 传输通道接口对象指针，未注册时返回NULL
 *
 * @note 调用方不需要知道UART/BLE的具体文件，只需通过此接口访问
 */
const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id)
{
    if (transport_id >= DTU_TRANSPORT_MAX) {
        return NULL;
    }
    return g_dtu_transport_table[transport_id];
}

/* ========================================================================== */
/* 名称映射区                                                                 */
/* @brief 枚举/命令值到可读字符串的映射                                       */
/* @details 这些函数只做"枚举/命令值 -> 可读字符串"的映射，主要给统一日志层和错误日志使用 */
/*          这样日志输出不会在各模块里各写一套字符串常量                       */
/* ========================================================================== */

/**
 * @brief 获取配置命令名称
 * @details 根据命令字返回对应的可读字符串，供日志和错误信息复用
 *
 * @param[in] cmd 配置命令字
 * @return 命令名称字符串，未知命令返回"UNKNOWN_CMD"
 *
 * @note 命令字定义在dtu_build_config.h中
 *       新增命令时需要在此函数中添加对应的case
 */
const char *dtu_service_cmd_name(uint8_t cmd)
{
    switch (cmd) {
        case DTU_CFG_CMD_READ_DEV_INFO:
            return "READ_DEV_INFO";
        case DTU_CFG_CMD_READ_UART_CFG:
            return "READ_UART_CFG";
        case DTU_CFG_CMD_READ_MODBUS_CFG:
            return "READ_MODBUS_CFG";
        case DTU_CFG_CMD_READ_ROOT_WL_ALL:
            return "READ_ROOT_WL_ALL";
        case DTU_CFG_CMD_READ_ROOT_POWER:
            return "READ_ROOT_POWER";
        case DTU_CFG_CMD_GET_MODE_STATUS:
            return "GET_MODE_STATUS";
        case DTU_CFG_CMD_READ_WL_NODE_CFG:
            return "READ_WL_NODE_CFG";
        case DTU_CFG_CMD_SET_ROLE:
            return "SET_ROLE";
        case DTU_CFG_CMD_SET_UART_CFG:
            return "SET_UART_CFG";
        case DTU_CFG_CMD_SET_MODBUS_CFG:
            return "SET_MODBUS_CFG";
        case DTU_CFG_CMD_SET_ROOT_POWER:
            return "SET_ROOT_POWER";
        case DTU_CFG_CMD_ADD_WL_ITEM:
            return "ADD_WL_ITEM";
        case DTU_CFG_CMD_DEL_WL_ITEM:
            return "DEL_WL_ITEM";
        case DTU_CFG_CMD_CLEAR_WL:
            return "CLEAR_WL";
        case DTU_CFG_CMD_SET_WL_NODE_CFG:
            return "SET_WL_NODE_CFG";
        case DTU_CFG_CMD_COMMIT:
            return "COMMIT";
        case DTU_CFG_CMD_REBOOT:
            return "REBOOT";
        case DTU_CFG_CMD_FACTORY_RESET:
            return "FACTORY_RESET";
        default:
            return "UNKNOWN_CMD";
    }
}

/* 统一返回 transport 名称。
 * 如果 transport 已经注册了 name，就直接使用接口对象里的名字；
 * 否则回退到本地默认名称，避免未注册 transport 打日志时变成空字符串。
 */
const char *dtu_service_transport_name(dtu_transport_id_t transport_id)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);

    if (transport_if != NULL && transport_if->name != NULL) {
        return transport_if->name;
    }
    switch (transport_id) {
        case DTU_TRANSPORT_UART:
            return "UART";
        case DTU_TRANSPORT_BLE:
            return "BLE";
        case DTU_TRANSPORT_SLE:
            return "SLE";
        default:
            return "UNKNOWN";
    }
}

/* ========================================================================== */
/* 命令表辅助区                                                               */
/* @brief 表驱动命令分发辅助函数                                              */
/* @details config/run模式都采用表驱动分发，这里提供一个通用查表函数           */
/*          避免每个mode文件重复写一遍for循环                                  */
/* ========================================================================== */

/**
 * @brief 在命令表中查找命令并执行对应handler
 * @details 遍历命令表，查找与帧命令字匹配的条目并执行对应的处理函数
 *
 * @param[in] table 命令表数组
 * @param[in] table_size 命令表大小
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的帧结构体
 * @return true: 命令已处理, false: 未找到匹配命令
 *
 * @note 命中即执行handler，未命中返回false
 *       如果table或frame为NULL，或table_size为0，直接返回false
 */
bool dtu_service_dispatch_table(const dtu_cmd_entry_t *table, uint32_t table_size,
    dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (table == NULL || frame == NULL || table_size == 0) {
        return false;
    }

    for (uint32_t i = 0; i < table_size; i++) {
        if (table[i].cmd_id == frame->cmd) {
            table[i].handler(transport_id, frame);
            return true;
        }
    }
    return false;
}

/* ========================================================================== */
/* Trace 统计入口区                                                           */
/* @brief 性能统计接口                                                        */
/* @details trace默认关闭，只在Kconfig打开后生效，这里不主动打印大量日志       */
/*          而是先累计指标，避免串口被统计信息刷屏                             */
/* @par 当前主要观测指标：                                                     */
/*       - 接收批量大小                                                        */
/*       - ring高水位                                                          */
/*       - 任务唤醒次数                                                        */
/*       - 溢出量                                                              */
/* ========================================================================== */

/**
 * @brief 累计接收批量统计
 * @details 在trace打开时统计接收数据的批量信息
 *
 * @param[in] length 原始数据长度
 * @param[in] accepted 实际接收数据长度
 * @param[in] ring_used 环形缓冲区已使用大小
 *
 * @note trace关闭时此函数为空操作
 */
void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
    unused(length);
    unused(accepted);
    unused(ring_used);
#else
    g_dtu_rx_callback_count++;
    g_dtu_rx_total_bytes += accepted;
    g_dtu_rx_batch_last = accepted;
    if (accepted > g_dtu_rx_batch_max) {
        g_dtu_rx_batch_max = accepted;
    }
    if (ring_used > g_dtu_rx_ring_high_watermark) {
        g_dtu_rx_ring_high_watermark = ring_used;
    }
    if (accepted < length) {
        g_dtu_rx_ring_overflow_count += (uint32_t)(length - accepted);
    }
#endif
}

/**
 * @brief 累计解析任务唤醒次数
 * @details 在trace打开时统计任务唤醒次数
 *
 * @note trace关闭时此函数为空操作
 */
void dtu_service_trace_rx_task_wakeup(void)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
#else
    g_dtu_rx_task_wakeup_count++;
#endif
}

/* ========================================================================== */
/* 输入分流和输出入口区                                                       */
/* @brief 数据输入分流和输出统一接口                                          */
/* @details 根据当前模式将输入数据分发到config或run模块处理                    */
/*          通过transport接口统一发送数据                                      */
/* ========================================================================== */

/**
 * @brief 处理接收到的字节数据
 * @details 根据当前模式和传输通道ID将数据分发到对应的处理模块
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 *
 * @par 分流逻辑：
 *       - CONFIG模式：所有输入数据 -> dtu_config模块处理
 *       - RUN模式：UART输入 -> dtu_run_on_uart0()
 *                  SLE输入 -> dtu_run_on_sle()
 *
 * @note 该函数会更新活动LED指示灯状态
 */
static void dtu_service_route_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    if (dtu_storage_current_mode() == DTU_MODE_RUN) {
        if (transport_id == DTU_TRANSPORT_UART) {
            dtu_run_on_uart0(data, len);
        } else if (transport_id == DTU_TRANSPORT_SLE) {
            dtu_run_on_sle(data, len);
        }
        return;
    }

    dtu_config_on_bytes(transport_id, data, len);
}

void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    if (transport_id >= DTU_TRANSPORT_MAX || data == NULL || len == 0) {
        return;
    }

    if (transport_id == DTU_TRANSPORT_UART || transport_id == DTU_TRANSPORT_SLE || transport_id == DTU_TRANSPORT_BLE) {
        dtu_board_mark_data_activity();
    }

    if (transport_id == DTU_TRANSPORT_UART && dtu_service_try_forward_uart0_at(data, len)) {
        return;
    }

    dtu_service_route_bytes(transport_id, data, len);
}

/**
 * @brief 处理UART1/485接收到的字节数据
 * @details 在RUN模式下处理485总线接收到的数据
 *
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 *
 * @note 只在RUN模式下处理485数据，CONFIG模式下直接忽略
 *       该函数会更新活动LED指示灯状态
 */
void dtu_service_on_uart485_bytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    if (dtu_storage_current_mode() != DTU_MODE_RUN) {
        return;
    }

    dtu_board_mark_data_activity();
    dtu_run_on_485(data, len);
}

/**
 * @brief 发送配置协议响应帧
 * @details 通过指定的传输通道发送配置协议响应帧
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] cmd 命令字
 * @param[in] seq 序列号
 * @param[in] body 响应体数据指针
 * @param[in] body_len 响应体长度
 *
 * @note 该函数会自动添加协议帧头、校验等信息
 *       如果传输通道不支持发送，会记录错误日志
 */
void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);
    uint8_t frame[DTU_SERVICE_MAX_FRAME_SIZE];
    uint16_t frame_len = 0;
    errcode_t ret;

    if (transport_if == NULL || transport_if->send == NULL) {
        dtu_log_error("tx transport unsupported: %s", dtu_service_transport_name(transport_id));
        return;
    }

    ret = dtu_config_pack_response(cmd, seq, body, body_len, frame, sizeof(frame), &frame_len);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx pack failed: cmd=%s ret=0x%X", dtu_service_cmd_name(cmd), ret);
        return;
    }

    ret = transport_if->send(frame, frame_len);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx send failed: transport=%s cmd=%s ret=0x%X",
            dtu_service_transport_name(transport_id), dtu_service_cmd_name(cmd), ret);
    }
}

/**
 * @brief 发送仅状态码响应帧
 * @details 发送只包含状态码的响应帧，用于简单的成功/失败响应
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] cmd 命令字
 * @param[in] seq 序列号
 * @param[in] status 状态码
 *
 * @note 该函数是dtu_service_reply的简化版本，用于发送单字节状态码
 */
void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status)
{
    dtu_service_reply(transport_id, cmd, seq, &status, 1);
}

/* ========================================================================== */
/* 初始化入口区                                                               */
/* @brief DTU服务初始化流程                                                   */
/* @details 先加载storage，让运行配置和模式先稳定下来                         */
/*          再按注册表初始化各transport                                        */
/*          所有transport都起来以后，最后再打印boot日志快照                    */
/*          这样日志里看到的配置就是当前真实会生效的配置                       */
/* ========================================================================== */

/**
 * @brief 初始化单个传输通道
 * @details 统一处理传输通道初始化失败的日志记录
 *
 * @param[in] transport_id 传输通道ID
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 这里故意不展开UART/BLE/SLE细节，service只认transport_if->init()
 */
static errcode_t dtu_service_init_transport(dtu_transport_id_t transport_id)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);
    errcode_t ret;

    if (transport_if == NULL || transport_if->init == NULL) {
        return ERRCODE_SUCC;
    }

    ret = transport_if->init();
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("transport init failed: %s ret=0x%x", dtu_service_transport_name(transport_id), ret);
    }
    return ret;
}

/**
 * @brief DTU服务总初始化入口
 * @details 启动顺序固定为storage -> UART0 -> BLE/SLE：
 *          1. storage_load先采样DIP和加载NV，决定当前模式与串口参数
 *          2. UART0始终启动，CONFIG下做配置口，RUN下做PC调试/观察口
 *          3. CONFIG模式启动BLE配置通道；RUN模式启动SLE业务通道
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 初始化失败时会记录错误日志并返回错误码
 *       调用方需要检查返回值并决定是否继续运行
 */
errcode_t dtu_service_init(void)
{
    errcode_t load_ret;
    errcode_t ret;

    dtu_log_info("manager init begin");
    load_ret = dtu_storage_load(); /* 加载配置，同时读取 DIP 得到当前模式。 */
    dtu_log_info("manager storage load done: ret=0x%x mode=%u",
        load_ret, (uint32_t)dtu_storage_current_mode());

    ret = dtu_board_init(); /* 初始化 DIP/状态灯/活动灯：CONFIG 红，RUN ROOT 绿，RUN NODE 蓝。 */
    dtu_log_info("manager board init end: ret=0x%X", ret);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    dtu_log_info("manager uart init begin");
    ret = dtu_service_init_transport(DTU_TRANSPORT_UART);
    dtu_log_info("manager uart init end: ret=0x%X", ret);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (dtu_storage_current_mode() == DTU_MODE_RUN) {
        dtu_log_info("manager sle init begin");
        ret = dtu_service_init_transport(DTU_TRANSPORT_SLE); /* RUN：启动 SLE 组网/透明桥接通道。 */
        dtu_log_info("manager sle init end: ret=0x%X", ret);
    } else {
        dtu_log_info("manager ble init begin");
        ret = dtu_service_init_transport(DTU_TRANSPORT_BLE); /* CONFIG：启动 BLE 无线配置通道。 */
        dtu_log_info("manager ble init end: ret=0x%X", ret);
    }
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    dtu_log_boot(load_ret);
    dtu_log_info("manager init end");
    return ERRCODE_SUCC;
}
