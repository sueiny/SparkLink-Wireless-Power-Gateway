/**
 * @file dtu_storage.c
 * @brief DTU存储中心实现
 * @details 本模块是DTU系统的存储中心，主要职责：
 *          1. 成为DTU运行时状态的唯一拥有者
 *          2. 统一管理默认值、NV加载、提交、恢复出厂
 *          3. 为service/mode/transport提供只读或受控访问接口
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 架构说明：
 *       - 存储中心是DTU系统的数据核心
 *       - 所有配置数据都通过存储中心访问和修改
 *       - 存储中心负责数据的持久化（NV存储）
 *       - 存储中心提供数据校验和默认值管理
 *
 * @par 数据存储结构：
 *       - base NV：存储基础配置（角色、串口、Modbus、功率、白名单数量）
 *       - white list NV：白名单数据分片存储（每片16条，共8片）
 *       - runtime：运行时配置缓存，所有模块通过接口访问
 *
 * @par NV存储策略：
 *       - 使用魔术字和版本号检测NV数据有效性
 *       - 白名单分片存储，避免单key过大
 *       - 提交时先写base NV，再写白名单分片
 */

#include "dtu_storage.h"

#include "dtu_board.h"
#include "dtu_log.h"
#include "key_id.h"
#include "mac_addr.h"
#include "nv.h"
#include "nv_common_cfg.h"
#include "securec.h"

/* 存储中心职责：
 * 1. 成为 DTU 运行时状态的唯一拥有者
 * 2. 统一管理默认值、NV 加载、提交、恢复出厂
 * 3. 为 service / mode / transport 提供只读或受控访问接口
 */

/**
 * @brief 存储中心上下文结构体
 * @details 管理DTU系统的所有运行时状态
 *
 * @par 字段说明：
 *       - runtime：运行时配置，包含所有可配置参数
 *       - current_mode：当前已生效模式，直接决定路由分发
 *       - reboot_pending：重启等待标志，收到REBOOT后置位
 */
typedef struct {
    dtu_runtime_cfg_t runtime;
    /* current_mode: 当前已生效模式，直接决定路由分发。 */
    dtu_mode_t current_mode;
    /* reboot_pending: 收到 REBOOT 后置位，重启前统一拒绝写操作。 */
    bool reboot_pending;
} dtu_storage_ctx_t;

/** @brief 存储中心全局上下文 */
static dtu_storage_ctx_t g_dtu_storage = {
    .current_mode = DTU_MODE_CONFIG,
    .reboot_pending = false
};

/** @brief NV 临时缓存放在静态区，避免 DtuInitTask/协议任务栈承载较大的结构体。 */
static dtu_nv_blob_t g_dtu_nv_blob;
static dtu_nv_wl_shard_blob_t g_dtu_wl_shard_blob;

/** @brief 白名单分片NV key数组 */
static const uint16_t g_dtu_wl_shard_keys[] = {
    NV_ID_DTU_WL_SHARD0,
    NV_ID_DTU_WL_SHARD1,
    NV_ID_DTU_WL_SHARD2,
    NV_ID_DTU_WL_SHARD3,
    NV_ID_DTU_WL_SHARD4,
    NV_ID_DTU_WL_SHARD5,
    NV_ID_DTU_WL_SHARD6,
    NV_ID_DTU_WL_SHARD7,
};

/**
 * @brief 返回唯一状态上下文
 * @details 获取存储中心的全局上下文，仅core内部共享
 *
 * @return 存储中心上下文指针
 */
static dtu_storage_ctx_t *dtu_storage_ctx(void)
{
    return &g_dtu_storage;
}

/**
 * @brief 返回DTU NV临时缓存
 * @details 获取NV读写操作的临时缓存，供load/commit共用
 *
 * @return NV临时缓存指针
 */
static dtu_nv_blob_t *dtu_storage_nv_blob(void)
{
    return &g_dtu_nv_blob;
}

/**
 * @brief 返回白名单分片NV临时缓存
 * @details 获取白名单分片NV读写操作的临时缓存
 *
 * @return 白名单分片NV临时缓存指针
 */
static dtu_nv_wl_shard_blob_t *dtu_storage_wl_shard_blob(void)
{
    return &g_dtu_wl_shard_blob;
}

/**
 * @brief 将单个十六进制字符转换为数值
 * @details 支持0-9、a-f、A-F字符的转换
 *
 * @param[in] ch 十六进制字符
 * @param[out] nibble 转换后的数值
 * @return true: 转换成功, false: 非法字符
 */
static bool dtu_storage_hex_to_nibble(char ch, uint8_t *nibble)
{
    if (nibble == NULL) {
        return false;
    }

    if (ch >= '0' && ch <= '9') {
        *nibble = (uint8_t)(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f') {
        *nibble = (uint8_t)(ch - 'a' + 10);
        return true;
    }
    if (ch >= 'A' && ch <= 'F') {
        *nibble = (uint8_t)(ch - 'A' + 10);
        return true;
    }
    return false;
}

/**
 * @brief 解析Kconfig中配置的固定MAC地址
 * @details 支持两种格式：
 *          1. AABBCCDDEEFF
 *          2. AA:BB:CC:DD:EE:FF
 *
 * @param[out] mac 解析后的MAC地址，长度为WIFI_MAC_LEN(6字节)
 * @return true: 解析成功, false: 格式非法或未配置
 *
 * @note 留空或格式非法时返回false，调用方继续回退到系统MAC
 */
static bool dtu_storage_parse_fixed_mac(uint8_t *mac)
{
    const char *text = DTU_CFG_FIXED_MAC;
    uint8_t hex_digits[WIFI_MAC_LEN * 2];
    uint8_t digit_count = 0;

    if (mac == NULL || text == NULL || text[0] == '\0') {
        return false;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        uint8_t nibble;

        if (*p == ':' || *p == '-' || *p == ' ') {
            continue;
        }
        if (!dtu_storage_hex_to_nibble(*p, &nibble)) {
            return false;
        }
        if (digit_count >= sizeof(hex_digits)) {
            return false;
        }
        hex_digits[digit_count++] = nibble;
    }

    if (digit_count != WIFI_MAC_LEN * 2) {
        return false;
    }

    for (uint8_t i = 0; i < WIFI_MAC_LEN; i++) {
        mac[i] = (uint8_t)((hex_digits[i * 2] << 4) | hex_digits[i * 2 + 1]);
    }
    return true;
}

/* 读取 AT/系统 MAC。
 * AT+EFUSEMAC=xx,3 写入的是系统 SLE NV MAC，init_dev_addr() 会加载到 mac_addr 模块。
 * DTU 当前 SLE 本地地址和 READ_DEV_INFO 共用这一份设备身份。
 */
static bool dtu_storage_get_system_sle_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return false;
    }
    if (get_dev_addr(mac, WIFI_MAC_LEN, IFTYPE_SLE) != ERRCODE_SUCC) {
        return false;
    }
    return (mac_addr_nv_check(mac) == ERRCODE_SUCC);
}

/* 返回可写运行配置。 */
dtu_runtime_cfg_t *dtu_storage_runtime(void)
{
    return &dtu_storage_ctx()->runtime;
}

/* 返回只读运行配置。 */
const dtu_runtime_cfg_t *dtu_storage_runtime_const(void)
{
    return &dtu_storage_ctx()->runtime;
}

/* 读取当前已生效模式。 */
dtu_mode_t dtu_storage_current_mode(void)
{
    return dtu_storage_ctx()->current_mode;
}

/* 判断是否处于重启等待状态。 */
bool dtu_storage_is_reboot_pending(void)
{
    return dtu_storage_ctx()->reboot_pending;
}

/* 更新当前模式。 */
void dtu_storage_set_current_mode(dtu_mode_t mode)
{
    dtu_storage_ctx()->current_mode = mode;
}

/* 更新重启等待标志。 */
void dtu_storage_set_reboot_pending(bool pending)
{
    /* 该标志由 manager/config 检查，用于在重启前冻结配置改写。 */
    dtu_storage_ctx()->reboot_pending = pending;
}

/* 校验模式值是否合法。 */
bool dtu_storage_is_valid_mode(uint8_t mode)
{
    return (mode == DTU_MODE_CONFIG || mode == DTU_MODE_RUN);
}

/* 校验角色值是否合法。 */
bool dtu_storage_is_valid_role(uint8_t role)
{
    return (role == DTU_CFG_ROLE_NODE || role == DTU_CFG_ROLE_ROOT);
}

/* 校验串口参数是否在协议约束范围内。 */
bool dtu_storage_is_valid_uart_cfg(const dtu_uart_cfg_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (cfg->baud_level > 0x07) {
        return false;
    }
    if (cfg->parity > 0x02) {
        return false;
    }
    if (cfg->stop_bits != 0x01 && cfg->stop_bits != 0x02) {
        return false;
    }
    if (cfg->data_bits != 0x07 && cfg->data_bits != 0x08) {
        return false;
    }
    return true;
}

/* 更新当前缓存中的业务串口配置。不会立即重初始化 UART，COMMIT + REBOOT 后生效。 */
errcode_t dtu_storage_set_uart_cfg(const dtu_uart_cfg_t *cfg)
{
    if (!dtu_storage_is_valid_uart_cfg(cfg)) {
        return ERRCODE_FAIL;
    }

    dtu_storage_ctx()->runtime.uart_cfg = *cfg;
    return ERRCODE_SUCC;
}

/* 校验 Modbus 设备类型是否在受支持范围内。 */
bool dtu_storage_is_valid_dev_type(uint8_t dev_type)
{
    return (dev_type >= 0x01 && dev_type <= 0x05);
}

/* 在白名单中按 MAC 查找目标项，不存在时返回 -1。 */
int32_t dtu_storage_find_wl_item(const uint8_t *mac)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();

    for (uint8_t i = 0; i < cfg->wl_count; i++) {
        if (memcmp(cfg->whitelist[i].mac, mac, WIFI_MAC_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

/* 用当前 ROOT 运行配置初始化一个白名单 node 的子配置。
 * 约定：
 * 1. node 不单独保存 power，统一跟随 ROOT 当前 power
 * 2. 新白名单项默认继承当前 runtime 的 uart_cfg / modbus 预设
 */
void dtu_storage_init_wl_item_cfg(dtu_wl_item_t *item)
{
    const dtu_runtime_cfg_t *runtime = dtu_storage_runtime_const();

    if (item == NULL) {
        return;
    }

    item->uart_cfg = runtime->uart_cfg;
    item->modbus_count = runtime->modbus_count;
    if (memcpy_s(item->modbus, sizeof(item->modbus), runtime->modbus, sizeof(runtime->modbus)) != EOK) {
        item->modbus_count = 0;
    }
}

/* 根据当前模式给出 UART RX profile。 */
dtu_rx_profile_t dtu_storage_rx_profile(void)
{
    return (dtu_storage_current_mode() == DTU_MODE_RUN) ? DTU_RX_PROFILE_BATCH : DTU_RX_PROFILE_FAST_RESPONSE;
}

/* 根据模式获取 RX callback 通知长度。 */
uint16_t dtu_storage_rx_notify_length(void)
{
    return (dtu_storage_rx_profile() == DTU_RX_PROFILE_BATCH) ?
        DTU_CFG_MODE_RUN_RX_NOTIFY_LENGTH : DTU_CFG_MODE_CONFIG_RX_NOTIFY_LENGTH;
}

/* 根据模式获取 UART FIFO RX 水位。 */
uint8_t dtu_storage_rx_int_threshold(void)
{
    return (dtu_storage_rx_profile() == DTU_RX_PROFILE_BATCH) ?
        DTU_CFG_MODE_RUN_RX_INT_THRESHOLD : DTU_CFG_MODE_CONFIG_RX_INT_THRESHOLD;
}

/* 把协议中的波特率等级映射成真实波特率。 */
uint32_t dtu_storage_uart_baudrate(uint8_t baud_level)
{
    static const uint32_t baud_table[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

    if (baud_level >= sizeof(baud_table) / sizeof(baud_table[0])) {
        return 115200;
    }
    return baud_table[baud_level];
}

/* 将协议校验位配置转换为底层 UART 枚举。 */
static hal_uart_parity_t dtu_storage_uart_parity(uint8_t parity)
{
    switch (parity) {
        case 0x01:
            return UART_PARITY_EVEN;
        case 0x02:
            return UART_PARITY_ODD;
        case 0x00:
        default:
            return UART_PARITY_NONE;
    }
}

/* 将协议停止位配置转换为底层 UART 枚举。 */
static hal_uart_stop_bit_t dtu_storage_uart_stop_bits(uint8_t stop_bits)
{
    return (stop_bits == 0x02) ? UART_STOP_BIT_2 : UART_STOP_BIT_1;
}

/* 将协议数据位配置转换为底层 UART 枚举。 */
static hal_uart_data_bit_t dtu_storage_uart_data_bits(uint8_t data_bits)
{
    return (data_bits == 0x07) ? UART_DATA_BIT_7 : UART_DATA_BIT_8;
}

/* 把运行配置中的串口参数填充到 SDK UART 属性结构。 */
void dtu_storage_fill_uart_attr(uart_attr_t *uart_attr, const dtu_uart_cfg_t *cfg)
{
    uart_attr->baud_rate = dtu_storage_uart_baudrate(cfg->baud_level);
    uart_attr->data_bits = dtu_storage_uart_data_bits(cfg->data_bits);
    uart_attr->stop_bits = dtu_storage_uart_stop_bits(cfg->stop_bits);
    uart_attr->parity = dtu_storage_uart_parity(cfg->parity);
}

/* 初始化默认配置。 */
void dtu_storage_set_default(dtu_runtime_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    if (memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg)) != EOK) {
        return;
    }

    cfg->role = DTU_CFG_ROLE_NODE;
    cfg->uart_cfg = (dtu_uart_cfg_t)DTU_CFG_485_DEFAULT_CFG_INIT;
    /* 默认预置 8 个 Modbus 项：
     * 1. addr 直接使用数组下标值 + 1，便于和上位机索引一一对应
     * 2. dev_type 统一置为 0x05，表示“保留”类型
     */
    cfg->modbus_count = DTU_CFG_MAX_MODBUS_ITEMS;
    for (uint8_t i = 0; i < DTU_CFG_MAX_MODBUS_ITEMS; i++) {
        cfg->modbus[i].addr = i + 1;
        cfg->modbus[i].dev_type = 0x05;
    }
    cfg->power = 5;
}

/* 读取设备 MAC。
 * 默认策略：AT/系统 SLE MAC 优先，menuconfig 固定 MAC 只做最低优先级兜底。
 * 如果打开 CONFIG_DTU_FORCE_MENUCONFIG_MAC，则优先使用 menuconfig 固定 MAC。
 */
void dtu_storage_get_device_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return;
    }

    if (DTU_CFG_FORCE_MENUCONFIG_MAC != 0 && dtu_storage_parse_fixed_mac(mac)) {
        return;
    }
    if (dtu_storage_get_system_sle_mac(mac)) {
        return;
    }
    if (dtu_storage_parse_fixed_mac(mac)) {
        return;
    }
    (void)memset_s(mac, WIFI_MAC_LEN, 0, WIFI_MAC_LEN);
}

/* 读取设备名称。
 * 现在 DTU 名称同样统一通过 Kconfig 管理：
 * 1. 用户配置了固定名称时，直接使用该名称
 * 2. 用户未配置时，使用编译期默认名称 DTU_N01
 * 3. 不再按 MAC 动态拼接名称，保证身份稳定
 */
uint8_t dtu_storage_get_device_name(uint8_t *name_buf, uint8_t name_buf_len)
{
    uint8_t copy_len;

    if (name_buf == NULL || name_buf_len == 0) {
        return 0;
    }

    copy_len = (uint8_t)(sizeof(DTU_CFG_DEVICE_NAME) - 1);
    if (copy_len > DTU_CFG_MAX_NAME_LEN) {
        copy_len = DTU_CFG_MAX_NAME_LEN;
    }
    if (copy_len > name_buf_len) {
        copy_len = name_buf_len;
    }

    (void)memcpy_s(name_buf, name_buf_len, DTU_CFG_DEVICE_NAME, copy_len);
    return copy_len;
}

/* 将角色值转换成便于阅读的名称。 */
const char *dtu_storage_role_name(uint8_t role)
{
    return (role == DTU_CFG_ROLE_ROOT) ? "ROOT" : "NODE";
}

/* 将串口校验位值转换成便于阅读的名称。 */
const char *dtu_storage_parity_name(uint8_t parity)
{
    switch (parity) {
        case 0x01:
            return "Even";
        case 0x02:
            return "Odd";
        case 0x00:
        default:
            return "None";
    }
}

/* 返回模式名称。 */
const char *dtu_storage_mode_name(dtu_mode_t mode)
{
    return (mode == DTU_MODE_RUN) ? "RUN" : "CONFIG";
}

/* 返回 RX profile 名称。 */
const char *dtu_storage_rx_profile_name(dtu_rx_profile_t profile)
{
    return (profile == DTU_RX_PROFILE_BATCH) ? "batch" : "fast-response";
}

/* 将 base NV 内容应用到 runtime。白名单内容由独立分片恢复。 */
static bool dtu_storage_apply_base_blob(const dtu_nv_blob_t *blob)
{
    dtu_runtime_cfg_t *runtime = dtu_storage_runtime();

    if (blob == NULL || blob->cfg.wl_count > DTU_CFG_MAX_WL_ITEMS ||
        blob->cfg.modbus_count > DTU_CFG_MAX_MODBUS_ITEMS) {
        return false;
    }

    runtime->role = blob->cfg.role;
    runtime->uart_cfg.baud_level = blob->cfg.uart_cfg.baud_level;
    runtime->uart_cfg.parity = blob->cfg.uart_cfg.parity;
    runtime->uart_cfg.stop_bits = blob->cfg.uart_cfg.stop_bits;
    runtime->uart_cfg.data_bits = blob->cfg.uart_cfg.data_bits;
    runtime->modbus_count = blob->cfg.modbus_count;
    runtime->power = blob->cfg.power;
    runtime->wl_count = blob->cfg.wl_count;

    if (memcpy_s(runtime->modbus, sizeof(runtime->modbus), blob->cfg.modbus, sizeof(blob->cfg.modbus)) != EOK) {
        return false;
    }
    (void)memset_s(runtime->whitelist, sizeof(runtime->whitelist), 0, sizeof(runtime->whitelist));
    return true;
}

/* 将 runtime 的基础字段打包到 base NV，白名单数组不进入该 key。 */
static bool dtu_storage_pack_base_blob(dtu_nv_blob_t *blob)
{
    const dtu_runtime_cfg_t *runtime = dtu_storage_runtime_const();

    if (blob == NULL || runtime->wl_count > DTU_CFG_MAX_WL_ITEMS ||
        runtime->modbus_count > DTU_CFG_MAX_MODBUS_ITEMS) {
        return false;
    }

    if (memset_s(blob, sizeof(*blob), 0, sizeof(*blob)) != EOK) {
        return false;
    }
    blob->magic = DTU_CFG_NV_MAGIC;
    blob->version = DTU_CFG_NV_VERSION;
    blob->cfg.role = runtime->role;
    blob->cfg.uart_cfg.baud_level = runtime->uart_cfg.baud_level;
    blob->cfg.uart_cfg.parity = runtime->uart_cfg.parity;
    blob->cfg.uart_cfg.stop_bits = runtime->uart_cfg.stop_bits;
    blob->cfg.uart_cfg.data_bits = runtime->uart_cfg.data_bits;
    blob->cfg.modbus_count = runtime->modbus_count;
    blob->cfg.power = runtime->power;
    blob->cfg.wl_count = runtime->wl_count;
    return (memcpy_s(blob->cfg.modbus, sizeof(blob->cfg.modbus), runtime->modbus, sizeof(runtime->modbus)) == EOK);
}

/* 将一个白名单分片从 NV 恢复到 runtime 的连续白名单数组。 */
static bool dtu_storage_apply_wl_shard(const dtu_nv_wl_shard_blob_t *shard, uint8_t shard_idx, uint8_t *loaded)
{
    dtu_runtime_cfg_t *runtime = dtu_storage_runtime();
    uint8_t remain;

    if (shard == NULL || loaded == NULL || shard->magic != DTU_CFG_NV_MAGIC ||
        shard->version != DTU_CFG_NV_VERSION || shard->shard_index != shard_idx ||
        shard->item_count > DTU_CFG_NV_WL_ITEMS_PER_SHARD || *loaded > runtime->wl_count) {
        return false;
    }

    remain = (uint8_t)(runtime->wl_count - *loaded);
    if (shard->item_count > remain) {
        return false;
    }

    for (uint8_t i = 0; i < shard->item_count; i++) {
        dtu_wl_item_t *rt_item = &runtime->whitelist[*loaded + i];
        const dtu_nv_wl_item_t *nv_item = &shard->items[i];

        if (memset_s(rt_item, sizeof(*rt_item), 0, sizeof(*rt_item)) != EOK ||
            memcpy_s(rt_item->mac, sizeof(rt_item->mac), nv_item->mac, sizeof(nv_item->mac)) != EOK ||
            memcpy_s(&rt_item->uart_cfg, sizeof(rt_item->uart_cfg), &nv_item->uart_cfg,
                sizeof(nv_item->uart_cfg)) != EOK) {
            return false;
        }
        rt_item->modbus_count = nv_item->modbus_count;
        if (rt_item->modbus_count > DTU_CFG_MAX_MODBUS_ITEMS ||
            memcpy_s(rt_item->modbus, sizeof(rt_item->modbus), nv_item->modbus, sizeof(nv_item->modbus)) != EOK) {
            return false;
        }

    }

    *loaded += shard->item_count;
    return true;
}

/* 从 8 个白名单分片 key 中恢复白名单。失败时只清空白名单，保留 base 配置。 */
static void dtu_storage_load_wl_shards(void)
{
    dtu_runtime_cfg_t *runtime = dtu_storage_runtime();
    dtu_nv_wl_shard_blob_t *shard = dtu_storage_wl_shard_blob();
    uint8_t loaded = 0;

    for (uint8_t i = 0; i < DTU_CFG_NV_WL_SHARD_COUNT && loaded < runtime->wl_count; i++) {
        uint16_t real_len = 0;
        errcode_t ret;

        if (memset_s(shard, sizeof(*shard), 0, sizeof(*shard)) != EOK) {
            runtime->wl_count = 0;
            return;
        }
        dtu_log_info("storage wl read begin: key=0x%X size=%u",
            g_dtu_wl_shard_keys[i], (uint32_t)sizeof(*shard));
        ret = uapi_nv_read(g_dtu_wl_shard_keys[i], sizeof(*shard), &real_len, (uint8_t *)shard);
        dtu_log_info("storage wl read end: key=0x%X ret=0x%X real_len=%u",
            g_dtu_wl_shard_keys[i], ret, real_len);
        if (ret != ERRCODE_SUCC || real_len != sizeof(*shard) ||
            !dtu_storage_apply_wl_shard(shard, i, &loaded)) {
            dtu_log_error("load wl shard failed: key=0x%X ret=0x%X", g_dtu_wl_shard_keys[i], ret);
            runtime->wl_count = 0;
            (void)memset_s(runtime->whitelist, sizeof(runtime->whitelist), 0, sizeof(runtime->whitelist));
            return;
        }
    }

    if (loaded != runtime->wl_count) {
        dtu_log_error("load wl shard count mismatch: expect=%u loaded=%u", runtime->wl_count, loaded);
        runtime->wl_count = 0;
        (void)memset_s(runtime->whitelist, sizeof(runtime->whitelist), 0, sizeof(runtime->whitelist));
    }
}

/* 按固定 16 条一片打包白名单分片。读取时以 base.wl_count 为准，旧分片超出部分会被忽略。 */
static bool dtu_storage_pack_wl_shard(dtu_nv_wl_shard_blob_t *shard, uint8_t shard_idx)
{
    const dtu_runtime_cfg_t *runtime = dtu_storage_runtime_const();
    uint16_t start = (uint16_t)(shard_idx * DTU_CFG_NV_WL_ITEMS_PER_SHARD);
    uint16_t remain;

    if (shard == NULL || shard_idx >= DTU_CFG_NV_WL_SHARD_COUNT || runtime->wl_count > DTU_CFG_MAX_WL_ITEMS) {
        return false;
    }
    if (memset_s(shard, sizeof(*shard), 0, sizeof(*shard)) != EOK) {
        return false;
    }

    remain = (uint16_t)(runtime->wl_count > start ? runtime->wl_count - start : 0);
    shard->magic = DTU_CFG_NV_MAGIC;
    shard->version = DTU_CFG_NV_VERSION;
    shard->shard_index = shard_idx;
    shard->item_count = (uint8_t)(remain > DTU_CFG_NV_WL_ITEMS_PER_SHARD ? DTU_CFG_NV_WL_ITEMS_PER_SHARD : remain);

    for (uint8_t i = 0; i < shard->item_count; i++) {
        const dtu_wl_item_t *rt_item = &runtime->whitelist[start + i];
        dtu_nv_wl_item_t *nv_item = &shard->items[i];

        if (memcpy_s(nv_item->mac, sizeof(nv_item->mac), rt_item->mac, sizeof(rt_item->mac)) != EOK ||
            memcpy_s(&nv_item->uart_cfg, sizeof(nv_item->uart_cfg), &rt_item->uart_cfg,
                sizeof(rt_item->uart_cfg)) != EOK) {
            return false;
        }
        nv_item->modbus_count = rt_item->modbus_count;
        if (nv_item->modbus_count > DTU_CFG_MAX_MODBUS_ITEMS ||
            memcpy_s(nv_item->modbus, sizeof(nv_item->modbus), rt_item->modbus, sizeof(rt_item->modbus)) != EOK) {
            return false;
        }
    }

    return true;
}

/**
 * @brief 从NV加载配置
 * @details 从NV存储加载配置，并根据拨码开关决定当前生效模式
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @par 加载流程：
 *       1. 设置默认配置
 *       2. 检测DIP拨码开关确定当前模式
 *       3. 清除重启等待标志
 *       4. 读取base NV数据
 *       5. 校验NV魔术字和版本号
 *       6. 应用base NV数据到runtime
 *       7. 加载白名单分片数据
 *
 * @note NV无效时使用默认配置，但函数仍返回成功，让设备可以正常启动
 */
errcode_t dtu_storage_load(void)
{
    dtu_nv_blob_t *blob = dtu_storage_nv_blob();
    uint16_t real_len = 0;
    errcode_t ret;

    dtu_log_info("storage load begin");
    dtu_storage_set_default(dtu_storage_runtime());
    dtu_log_info("storage default ready");
    dtu_storage_set_current_mode(dtu_board_detect_mode());
    dtu_log_info("storage dip mode ready: %s", dtu_storage_mode_name(dtu_storage_current_mode()));
    dtu_storage_set_reboot_pending(false);

    if (memset_s(blob, sizeof(*blob), 0, sizeof(*blob)) != EOK) {
        return ERRCODE_FAIL;
    }

    dtu_log_info("storage nv read begin: key=0x%X size=%u", NV_ID_DTU_CFG, (uint32_t)sizeof(*blob));
    ret = uapi_nv_read(NV_ID_DTU_CFG, sizeof(*blob), &real_len, (uint8_t *)blob);
    dtu_log_info("storage nv read end: ret=0x%X real_len=%u", ret, real_len);
    if (ret != ERRCODE_SUCC || real_len != sizeof(*blob) || blob->magic != DTU_CFG_NV_MAGIC) {
        return ERRCODE_SUCC;
    }
    if (blob->version != DTU_CFG_NV_VERSION) {
        return ERRCODE_SUCC;
    }

    if (!dtu_storage_apply_base_blob(blob)) {
        dtu_storage_set_default(dtu_storage_runtime());
        return ERRCODE_FAIL;
    }
    dtu_storage_load_wl_shards();
    dtu_log_info("storage nv config applied: wl_count=%u", dtu_storage_runtime_const()->wl_count);
    return ERRCODE_SUCC;
}

/**
 * @brief 将当前配置写入NV
 * @details 把当前runtime配置持久化到NV存储
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @par 写入流程：
 *       1. 打包base NV数据
 *       2. 写入base NV
 *       3. 计算白名单分片数量
 *       4. 逐片打包并写入白名单分片NV
 *
 * @note 当前模式完全由拨码开关决定，不再作为NV生效依据
 *       NV结构中的mode字段为了兼容旧布局保留，但这里不再赋值使用
 */
errcode_t dtu_storage_commit(void)
{
    dtu_nv_blob_t *blob = dtu_storage_nv_blob();
    dtu_nv_wl_shard_blob_t *shard = dtu_storage_wl_shard_blob();
    uint8_t shard_count;
    errcode_t ret;

    if (!dtu_storage_pack_base_blob(blob)) {
        dtu_log_error("commit base pack failed");
        return ERRCODE_FAIL;
    }

    dtu_log_info("storage nv write begin: key=0x%X size=%u", NV_ID_DTU_CFG, (uint32_t)sizeof(*blob));
    ret = uapi_nv_write(NV_ID_DTU_CFG, (const uint8_t *)blob, sizeof(*blob));
    dtu_log_info("storage nv write end: ret=0x%X", ret);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("commit nv write failed: ret=0x%X", ret);
        return ret;
    }

    shard_count = (uint8_t)((dtu_storage_runtime_const()->wl_count + DTU_CFG_NV_WL_ITEMS_PER_SHARD - 1) /
        DTU_CFG_NV_WL_ITEMS_PER_SHARD);
    for (uint8_t i = 0; i < shard_count; i++) {
        if (!dtu_storage_pack_wl_shard(shard, i)) {
            dtu_log_error("commit wl shard pack failed: shard=%u", i);
            return ERRCODE_FAIL;
        }
        dtu_log_info("storage wl write begin: key=0x%X shard=%u count=%u size=%u",
            g_dtu_wl_shard_keys[i], i, shard->item_count, (uint32_t)sizeof(*shard));
        ret = uapi_nv_write(g_dtu_wl_shard_keys[i], (const uint8_t *)shard, sizeof(*shard));
        dtu_log_info("storage wl write end: key=0x%X ret=0x%X", g_dtu_wl_shard_keys[i], ret);
        if (ret != ERRCODE_SUCC) {
            dtu_log_error("commit wl shard write failed: key=0x%X ret=0x%X", g_dtu_wl_shard_keys[i], ret);
            return ret;
        }
    }
    return ERRCODE_SUCC;
}

/**
 * @brief 恢复出厂默认配置
 * @details 恢复默认配置并持久化，当前模式继续由拨码开关决定
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 恢复出厂后会清除所有自定义配置，包括白名单
 */
errcode_t dtu_storage_factory_reset(void)
{
    dtu_storage_set_default(dtu_storage_runtime());
    dtu_storage_set_current_mode(dtu_board_detect_mode());
    dtu_storage_set_reboot_pending(false);
    return dtu_storage_commit();
}
