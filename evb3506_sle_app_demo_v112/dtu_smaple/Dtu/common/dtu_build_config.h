/**
 * @file dtu_build_config.h
 * @brief DTU编译期配置总入口
 * @details 本文件集中管理DTU项目的所有编译期配置参数，包括：
 *          - UART串口配置（波特率、数据位、停止位、校验位）
 *          - 任务线程配置（栈大小、优先级、任务名）
 *          - 缓冲区配置（驱动缓冲、环形缓冲、批处理大小）
 *          - 协议参数配置（帧大小、包容量、白名单容量）
 *          - 硬件引脚配置（UART引脚、GPIO、LED）
 *          - 协议常量（命令字、状态码、NV版本）
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @note 配置修改指南：
 *       1. 快速配置：修改"常改配置区"中的宏定义
 *       2. 硬件适配：修改"硬件映射区"中的引脚定义
 *       3. 协议扩展：在"协议常量区"添加新的命令字和状态码
 *       4. 高级调优：调整缓冲区大小和任务优先级
 *
 * @warning 修改协议常量区会影响PC/Web/脚本工具的兼容性，
 *          修改前请确保所有相关工具同步更新。
 *
 * @par 配置分区说明：
 *       - 常改配置区：最常需要修改的参数，如串口、线程、缓存
 *       - 硬件映射区：板级引脚和外设编号，随硬件板型变化
 *       - Kconfig派生区：由menuconfig生成，可通过配置菜单修改
 *       - 协议常量区：对外协议ABI，修改需谨慎
 */

#ifndef DTU_BUILD_CONFIG_H
#define DTU_BUILD_CONFIG_H

/* DTU 编译期配置总入口。
 *
 * 读者优先看"常改配置区"：
 * 1. 改默认串口：DTU_CFG_UART0_DEFAULT_* / DTU_CFG_485_DEFAULT_*。
 * 2. 改线程：DTU_CFG_*_TASK_NAME / STACK_SIZE / PRIO。
 * 3. 改接收缓存：DTU_CFG_TRANSPORT_RX_BATCH_SIZE / RX_DRIVER_BUFFER_SIZE / RING_BUFFER_SIZE。
 * 4. 改协议包大小：DTU_CFG_MAX_FRAME_BODY / DTU_CFG_RUN_PACKET_MAX_PAYLOAD。
 *
 * 下面的"协议常量区"是对外协议 ABI，修改会影响 PC/Web/脚本工具兼容性，默认不要动。
 */

/**
 * @brief 硬件抽象层头文件
 * @details 提供GPIO、引脚复用、UART等硬件操作接口
 */
#include "gpio.h"
#include "pinctrl.h"
#include "uart.h"

/* ========================================================================== */
/* 常改配置区：默认 UART、线程、缓存、协议包大小                              */
/* @brief 最常需要修改的配置参数                                              */
/* @details 这些参数直接影响DTU的性能和资源占用，可根据实际需求调整            */
/* ========================================================================== */

/* ---------- 默认 UART 配置 ---------- */
/* @brief UART0 默认配置（配置口/PC调试口）
 * @details UART0用于配置模式下的参数设置和运行模式下的调试信息输出。
 *          该配置为固定配置，不从NV加载，确保配置口始终可用。
 *
 * @par 波特率等级映射：
 *       - 0x00: 1200 bps
 *       - 0x01: 2400 bps
 *       - 0x02: 4800 bps
 *       - 0x03: 9600 bps
 *       - 0x04: 19200 bps
 *       - 0x05: 38400 bps
 *       - 0x06: 57600 bps
 *       - 0x07: 115200 bps（默认）
 *
 * @note 修改后影响 UART0 初始化参数；UART0 不走 NV 配置。
 *       baud_level 映射见 storage/dtu_storage.c 的 dtu_storage_uart_baudrate()。
 */
#define DTU_CFG_UART0_DEFAULT_BAUD_LEVEL      0x07
#define DTU_CFG_UART0_DEFAULT_PARITY          0x00
#define DTU_CFG_UART0_DEFAULT_STOP_BITS       0x01
#define DTU_CFG_UART0_DEFAULT_DATA_BITS       0x08
#define DTU_CFG_UART0_DEFAULT_CFG_INIT        { \
    .baud_level = DTU_CFG_UART0_DEFAULT_BAUD_LEVEL, \
    .parity = DTU_CFG_UART0_DEFAULT_PARITY, \
    .stop_bits = DTU_CFG_UART0_DEFAULT_STOP_BITS, \
    .data_bits = DTU_CFG_UART0_DEFAULT_DATA_BITS \
}

/* @brief UART1/485 默认配置（运行态业务口）
 * @details UART1用于RUN模式下的485总线通信，连接外部设备（如传感器、PLC等）。
 *          首次烧录、NV无效、恢复出厂时使用这里的默认值。
 *
 * @note CONFIG模式下 SET_UART_CFG 命令只修改RAM中的配置；
 *       COMMIT 命令将配置写入NV；重启后UART1/485才按NV配置生效。
 *
 * @par 默认配置说明：
 *       - 波特率：9600 bps（0x03）
 *       - 校验位：偶校验（0x01）
 *       - 停止位：1位（0x01）
 *       - 数据位：8位（0x08）
 */
#define DTU_CFG_485_DEFAULT_BAUD_LEVEL        0x03
#define DTU_CFG_485_DEFAULT_PARITY            0x01
#define DTU_CFG_485_DEFAULT_STOP_BITS         0x01
#define DTU_CFG_485_DEFAULT_DATA_BITS         0x08
#define DTU_CFG_485_DEFAULT_CFG_INIT          { \
    .baud_level = DTU_CFG_485_DEFAULT_BAUD_LEVEL, \
    .parity = DTU_CFG_485_DEFAULT_PARITY, \
    .stop_bits = DTU_CFG_485_DEFAULT_STOP_BITS, \
    .data_bits = DTU_CFG_485_DEFAULT_DATA_BITS \
}

/* ---------- 线程配置 ---------- */
/* @brief 线程配置参数
 * @details DTU系统使用多线程架构，每个传输通道有独立的处理线程。
 *
 * @par 线程架构：
 *       - DtuInitTask：初始化任务，完成后自动退出
 *       - DtuUartTask：UART0处理任务，处理配置口数据
 *       - Dtu485Task：UART1/485处理任务，处理485总线数据
 *       - DtuBleTask：BLE处理任务，处理蓝牙配置数据
 *       - DtuSleTask：SLE处理任务，处理SLE组网数据
 *
 * @par 优先级设计：
 *       - 初始化任务优先级(24)：低于传输任务，确保数据收发实时性
 *       - 传输任务优先级(25)：高于初始化任务，保证数据处理及时
 *
 * @note 如果后续parser/mesh变重，优先调大 DTU_CFG_TRANSPORT_TASK_STACK_SIZE。
 */
/* init task：只负责启动 DTU manager，启动结束后退出。 */
#define DTU_CFG_INIT_TASK_NAME                "DtuInitTask"
#define DTU_CFG_INIT_TASK_STACK_SIZE          0x2000
#define DTU_CFG_INIT_TASK_PRIO                24

/* transport task：UART0、UART1/485、BLE、SLE 都使用同一栈大小和优先级。
 * 如果后续 parser/mesh 变重，优先调大 DTU_CFG_TRANSPORT_TASK_STACK_SIZE。
 */
#define DTU_CFG_UART0_TASK_NAME               "DtuUartTask"
#define DTU_CFG_485_TASK_NAME                 "Dtu485Task"
#define DTU_CFG_BLE_TASK_NAME                 "DtuBleTask"
#define DTU_CFG_SLE_TASK_NAME                 "DtuSleTask"
#define DTU_CFG_TRANSPORT_TASK_STACK_SIZE     0x1200
#define DTU_CFG_TRANSPORT_TASK_PRIO           25

/* ---------- 接收缓存与批处理 ---------- */
/* @brief 接收缓冲区和批处理配置
 * @details 这些参数影响DTU的数据接收性能和内存占用。
 *
 * @par 缓冲区架构：
 *       - driver buffer：UART驱动内部RX缓冲，由硬件DMA使用
 *       - ring buffer：DTU自己的跨callback/task缓冲，用于异步数据处理
 *       - batch size：transport task每次从ring取出的最大字节数
 *
 * @par 性能调优建议：
 *       - 小batch size：CONFIG响应更及时，适合配置模式
 *       - 大batch size：RUN透明转发批量更好，适合运行模式
 *       - 如果出现trace里的ring overflow，优先调大RING_BUFFER_SIZE
 *
 * @note CONFIG使用1字节触发，方便协议状态机尽快返回错误/响应。
 *       RUN使用批量触发，减少透明转发任务唤醒次数。
 */
/* RX batch：transport task 每次从 ring 取出的最大字节数。
 * 小：CONFIG 响应更及时；大：RUN 透明转发批量更好。
 */
#define DTU_CFG_TRANSPORT_RX_BATCH_SIZE       64

/* driver buffer：UART 驱动内部 RX 缓冲。
 * ring buffer：DTU 自己的跨 callback/task 缓冲，BLE/SLE/UART 都使用这个大小。
 * 如果出现 trace 里的 ring overflow，优先调大 RING_BUFFER_SIZE。
 */
#define DTU_CFG_RX_DRIVER_BUFFER_SIZE         512
#define DTU_CFG_RING_BUFFER_SIZE              2048

/* CONFIG 和 RUN 的 UART RX 触发策略。
 * CONFIG 使用 1 字节触发，方便协议状态机尽快返回错误/响应。
 * RUN 使用批量触发，减少透明转发任务唤醒次数。
 */
#define DTU_CFG_MODE_CONFIG_RX_NOTIFY_LENGTH  1
#define DTU_CFG_MODE_RUN_RX_NOTIFY_LENGTH     32
#define DTU_CFG_MODE_CONFIG_RX_INT_THRESHOLD  UART_FIFO_INT_RX_LEVEL_1_CHARACTER
#define DTU_CFG_MODE_RUN_RX_INT_THRESHOLD     UART_FIFO_INT_RX_LEVEL_1_2

/* ---------- 协议包大小与容量 ---------- */
/* @brief 协议包大小和容量配置
 * @details 这些参数定义了DTU协议的数据包大小限制和配置表容量。
 *
 * @par 帧大小说明：
 *       - CONFIG单帧body上限：192字节
 *       - RUN业务包payload上限：192字节
 *       - 修改这些值会影响dtu_frame_t结构体大小、parser临时栈、响应帧大小
 *
 * @par 配置表容量：
 *       - MODBUS_ITEMS：每个节点保存的modbus条目数（最多8个）
 *       - WL_ITEMS：ROOT白名单容量（最多128个节点）
 *       - NV_WL_SHARD：白名单NV分片存储参数
 *
 * @warning 修改DTU_CFG_MAX_FRAME_BODY会影响PC/Web/工具协议上限，
 *          需要同步更新上位机工具。
 */
/* CONFIG 单帧 body 上限。
 * 修改它会影响 dtu_frame_t、parser 临时栈、响应帧大小和 PC/Web 工具协议上限。
 */
#define DTU_CFG_MAX_FRAME_BODY                192

/* RUN 业务包预留参数。
 * 当前透明桥接只做基础校验，后续 mesh/组网封包时优先复用这组限制。
 */
#define DTU_CFG_RUN_PACKET_MAX_PAYLOAD        192
#define DTU_CFG_RUN_PACKET_HEADER_SIZE        10

/* 配置表容量。
 * MODBUS_ITEMS 影响每个节点保存的 modbus 条目数。
 * WL_ITEMS 影响 ROOT 白名单容量；调整时同步检查 NV shard 配置。
 */
#define DTU_CFG_MAX_NAME_LEN                  31
#define DTU_CFG_MAX_MODBUS_ITEMS              8
#define DTU_CFG_MAX_WL_ITEMS                  128
#define DTU_CFG_NV_WL_SHARD_COUNT             8
#define DTU_CFG_NV_WL_ITEMS_PER_SHARD         16
#define DTU_CFG_WL_FRAGMENT_BODY_MAX          89

/* REBOOT 命令回包后延迟重启，避免回包还没发完就复位。 */
#define DTU_CFG_REBOOT_DELAY_MS               20

/* task 空转/异常等待间隔。这个值越小越灵敏，但空转开销越高。 */
#define DTU_CFG_TASK_IDLE_RETRY_MS            1

/* ========================================================================== */
/* 硬件映射区：板级引脚和外设编号                                            */
/* @brief 硬件引脚和外设配置                                                  */
/* @details 这些参数定义了DTU硬件板的引脚分配和外设连接                        */
/* @note 修改前请确认硬件原理图，确保引脚配置与实际硬件一致                    */
/* ========================================================================== */

/* @brief UART0引脚配置（配置口/PC观察口）
 * @details UART0用于配置模式下的参数设置和运行模式下的调试信息输出
 *          引脚21为TXD，引脚22为RXD，使用PIN_MODE_1复用功能
 */
#define DTU_CFG_UART_BUS                      UART_BUS_0
#define DTU_CFG_UART_TX_PIN                   21
#define DTU_CFG_UART_RX_PIN                   22
#define DTU_CFG_UART_PIN_MODE                 PIN_MODE_1

/* @brief UART1引脚配置（485总线口）
 * @details UART1用于RUN模式下的485总线通信，连接外部设备
 *          使用S_MGPIO15和S_MGPIO16引脚，支持485总线通信
 */
#define DTU_CFG_485_UART_BUS                  UART_BUS_1
#define DTU_CFG_485_UART_TX_PIN               S_MGPIO15
#define DTU_CFG_485_UART_RX_PIN               S_MGPIO16
#define DTU_CFG_485_UART_PIN_MODE             PIN_MODE_1

/* @brief 模式选择拨码开关配置
 * @details GPIO13用于检测DTU工作模式：
 *          - 高电平：CONFIG模式（配置模式）
 *          - 低电平：RUN模式（运行模式）
 * @note 使用内部上拉电阻，默认为高电平（CONFIG模式）
 */
#define DTU_CFG_MODE_SWITCH_PIN               S_MGPIO13
#define DTU_CFG_MODE_SWITCH_PIN_MODE          PIN_MODE_0
#define DTU_CFG_MODE_SWITCH_PIN_PULL          PIN_PULL_TYPE_UP

/* @brief LED指示灯配置
 * @details DTU使用两组三色LED指示灯：
 *          - 状态灯：显示当前工作模式和设备角色
 *          - 活动灯：提示数据收发活动
 *
 * @par 状态灯含义：
 *       - 红色：CONFIG模式
 *       - 绿色：RUN模式且为ROOT角色
 *       - 蓝色：RUN模式且为NODE角色
 *
 * @par 活动灯含义：
 *       - 蓝色闪烁：UART数据收发
 *       - 绿色闪烁：BLE/SLE数据收发
 *       - 闪烁持续时间：120ms
 */
#define DTU_CFG_STATE_LED_BLUE_PIN            S_MGPIO0
#define DTU_CFG_STATE_LED_GREEN_PIN           S_MGPIO1
#define DTU_CFG_STATE_LED_RED_PIN             S_MGPIO2
#define DTU_CFG_ACTIVITY_LED_BLUE_PIN         7
#define DTU_CFG_ACTIVITY_LED_GREEN_PIN        8
#define DTU_CFG_ACTIVITY_LED_RED_PIN          9
#define DTU_CFG_LED_PIN_MODE                  PIN_MODE_0
#define DTU_CFG_ACTIVITY_LED_HOLD_MS          120

/* ========================================================================== */
/* Kconfig 派生配置区                                                         */
/* @brief 由menuconfig生成的配置参数                                          */
/* @details 这些参数通过Kconfig配置系统管理，可通过make menuconfig修改         */
/* @note 修改后需要重新编译项目才能生效                                        */
/* ========================================================================== */

/* @brief TRACE日志开关
 * @details 控制DTU详细日志的输出，避免高频接收路径打印拖慢transport task。
 *          默认关闭，调试时可通过menuconfig打开。
 *
 * @par 使用场景：
 *       - 开发调试：打开TRACE日志，查看详细的数据流转过程
 *       - 生产环境：关闭TRACE日志，提高系统性能
 */
#if defined(CONFIG_DTU_TRACE_LOG)
#define DTU_CFG_LOG_TRACE_ENABLE              1
#else
#define DTU_CFG_LOG_TRACE_ENABLE              0
#endif

/* @brief 设备名称配置
 * @details DTU设备的唯一标识名称，用于BLE/SLE广播和日志输出。
 *          优先使用Kconfig配置值，未配置时使用默认值"DTU_N01"。
 *
 * @note 设备名称在BLE/SLE广播中作为设备标识，便于上位机发现和连接。
 */
#if defined(CONFIG_DTU_DEVICE_NAME)
#define DTU_CFG_DEVICE_NAME                   CONFIG_DTU_DEVICE_NAME
#else
#define DTU_CFG_DEVICE_NAME                   "DTU_N01"
#endif

/* @brief 固定MAC地址配置
 * @details DTU设备的固定MAC地址，用于设备身份标识。
 *          支持两种格式：AABBCCDDEEFF 或 AA:BB:CC:DD:EE:FF
 *
 * @par 配置说明：
 *       - 默认优先级最低：AT/system SLE MAC 读不到时才使用这里
 *       - 打开 CONFIG_DTU_FORCE_MENUCONFIG_MAC 后：优先使用这里
 *       - 格式错误：继续尝试下一优先级来源
 *
 * @warning MAC地址是设备唯一标识，修改后会影响设备在网络中的身份。
 */
#if defined(CONFIG_DTU_FORCE_MENUCONFIG_MAC)
#define DTU_CFG_FORCE_MENUCONFIG_MAC          1
#else
#define DTU_CFG_FORCE_MENUCONFIG_MAC          0
#endif

#if defined(CONFIG_DTU_FIXED_MAC)
#define DTU_CFG_FIXED_MAC                     CONFIG_DTU_FIXED_MAC
#else
#define DTU_CFG_FIXED_MAC                     "A1:A2:A3:A4:A5:A6"
#endif

/* ========================================================================== */
/* 协议常量区：命令字、状态码、NV 版本                                        */
/* @brief 对外协议ABI定义                                                      */
/* @details 这些常量定义了DTU配置协议的命令字、状态码和NV存储格式               */
/* @warning 修改这些常量会影响PC/Web/脚本工具的兼容性，修改前请确保所有工具同步更新 */
/* ========================================================================== */

/* @brief NV存储魔术字和版本号
 * @details NV magic/version是持久化结构兼容标识，用于检测NV数据有效性。
 *          修改runtime/NV布局时必须升级DTU_CFG_NV_VERSION，否则旧NV会被按新结构误读。
 *
 * @par 版本管理规则：
 *       - 修改NV结构体布局时必须升级版本号
 *       - 版本号不匹配时会使用默认配置
 *       - 魔术字用于快速检测NV数据是否有效
 */
#define DTU_CFG_NV_MAGIC                      0x44545532U
#define DTU_CFG_NV_VERSION                    0x0005

/* @brief 设备角色定义
 * @details DTU设备在网络中可以扮演两种角色：
 *          - NODE（普通节点）：从设备，接收ROOT的管理和数据转发
 *          - ROOT（根节点）：主设备，管理多个NODE节点，负责数据汇聚和上传
 *
 * @par 角色切换：
 *       - 通过SET_ROLE命令切换角色
 *       - 角色切换需要COMMIT并重启后生效
 *       - ROOT角色需要配置白名单管理NODE节点
 */
#define DTU_CFG_ROLE_NODE                     0x00
#define DTU_CFG_ROLE_ROOT                     0x01

/* @brief 配置协议状态码
 * @details 状态码用于响应配置命令的执行结果，新增状态码需要同步PC/Web侧解析。
 *
 * @par 状态码说明：
 *       - 0x00：成功
 *       - 0x01：CRC校验错误
 *       - 0x02：长度错误
 *       - 0x03：命令错误（不支持的命令）
 *       - 0x04：参数错误
 *       - 0x05：未配置（设备未初始化）
 *       - 0x06：角色不匹配（当前角色不允许执行该命令）
 *       - 0x07：白名单已满
 *       - 0x08：未找到（指定的MAC地址不存在）
 *       - 0x09：保存失败（NV写入失败）
 *       - 0x0A：忙（设备正在重启）
 */
#define DTU_CFG_STATUS_SUCC                   0x00
#define DTU_CFG_STATUS_CRC_ERR                0x01
#define DTU_CFG_STATUS_LEN_ERR                0x02
#define DTU_CFG_STATUS_CMD_ERR                0x03
#define DTU_CFG_STATUS_PARAM_ERR              0x04
#define DTU_CFG_STATUS_NOT_CONFIG             0x05
#define DTU_CFG_STATUS_ROLE_MISMATCH          0x06
#define DTU_CFG_STATUS_WL_FULL                0x07
#define DTU_CFG_STATUS_NOT_FOUND              0x08
#define DTU_CFG_STATUS_SAVE_FAIL              0x09
#define DTU_CFG_STATUS_BUSY                   0x0A

/* @brief 配置协议命令字
 * @details 命令字定义了配置协议支持的所有操作，handler注册表在config/dtu_config_commands.c文件顶部。
 *
 * @par 命令分类：
 *       - 读取命令(0x01-0x07)：读取设备配置信息
 *       - 设置命令(0x10-0x17)：修改设备配置参数
 *       - 控制命令(0x20-0x22)：执行设备控制操作
 *
 * @par 命令说明：
 *       - READ_DEV_INFO(0x01)：读取设备信息（角色、名称、MAC等）
 *       - READ_UART_CFG(0x02)：读取串口配置
 *       - READ_MODBUS_CFG(0x03)：读取Modbus配置
 *       - READ_ROOT_WL_ALL(0x04)：读取ROOT白名单（所有MAC地址）
 *       - READ_ROOT_POWER(0x05)：读取ROOT功率配置
 *       - GET_MODE_STATUS(0x06)：获取当前模式状态
 *       - READ_WL_NODE_CFG(0x07)：读取白名单节点配置
 *       - SET_ROLE(0x10)：设置设备角色
 *       - SET_UART_CFG(0x11)：设置串口配置
 *       - SET_MODBUS_CFG(0x12)：设置Modbus配置
 *       - SET_ROOT_POWER(0x13)：设置ROOT功率
 *       - ADD_WL_ITEM(0x14)：添加白名单条目
 *       - DEL_WL_ITEM(0x15)：删除白名单条目
 *       - CLEAR_WL(0x16)：清空白名单
 *       - SET_WL_NODE_CFG(0x17)：设置白名单节点配置
 *       - COMMIT(0x20)：提交配置到NV
 *       - REBOOT(0x21)：重启设备
 *       - FACTORY_RESET(0x22)：恢复出厂设置
 */
#define DTU_CFG_CMD_READ_DEV_INFO             0x01
#define DTU_CFG_CMD_READ_UART_CFG             0x02
#define DTU_CFG_CMD_READ_MODBUS_CFG           0x03
#define DTU_CFG_CMD_READ_ROOT_WL_ALL          0x04
#define DTU_CFG_CMD_READ_ROOT_POWER           0x05
#define DTU_CFG_CMD_GET_MODE_STATUS           0x06
#define DTU_CFG_CMD_READ_WL_NODE_CFG          0x07
#define DTU_CFG_CMD_SET_ROLE                  0x10
#define DTU_CFG_CMD_SET_UART_CFG              0x11
#define DTU_CFG_CMD_SET_MODBUS_CFG            0x12
#define DTU_CFG_CMD_SET_ROOT_POWER            0x13
#define DTU_CFG_CMD_ADD_WL_ITEM               0x14
#define DTU_CFG_CMD_DEL_WL_ITEM               0x15
#define DTU_CFG_CMD_CLEAR_WL                  0x16
#define DTU_CFG_CMD_SET_WL_NODE_CFG           0x17
#define DTU_CFG_CMD_COMMIT                    0x20
#define DTU_CFG_CMD_REBOOT                    0x21
#define DTU_CFG_CMD_FACTORY_RESET             0x22

/* @brief 配置协议帧头标识
 * @details AA55是配置协议的帧头标识，用于帧同步和识别。
 *          所有配置协议帧都以AA55开头。
 *
 * @par 帧格式：
 *       - SOF: AA 55（帧头）
 *       - CMD: 1字节（命令字）
 *       - SEQ: 1字节（序列号）
 *       - LEN: 2字节（数据长度）
 *       - DATA: N字节（数据体）
 *       - CRC: 2字节（校验和）
 */
#define DTU_CFG_SOF0                          0xAA
#define DTU_CFG_SOF1                          0x55

#endif
