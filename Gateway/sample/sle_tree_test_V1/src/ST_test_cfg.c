/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test config helpers.
 */
#include "ST_test_internal.h"

#include <string.h>

#include "key_id.h"
#include "nv.h"
#include "securec.h"

/*--------------------------------------------------------------------------
 * Static local SLE MAC configuration
 * 代码里显式配置本机 SLE MAC
 *--------------------------------------------------------------------------*/
/* Root 固定从 ...01 开始。
 * Relay node 建议按 ...11、...12、...13 这样手工递增。
 * 现在用户侧只烧 root / relay 两类固件，末端 relay 在拓扑上自然就是 leaf 位置。 */
static const uint8_t g_sle_tree_root_mac[SLE_ADDR_LEN]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t g_sle_tree_relay_mac[SLE_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11};
/* Kept only for internal compatibility if old leaf builds are re-enabled. */
static const uint8_t g_sle_tree_leaf_mac[SLE_ADDR_LEN]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x37};

static bool sle_tree_mac_is_valid(const uint8_t *mac, uint16_t mac_len);

/**
 * @brief Return the statically configured local SLE MAC for current role.
 * @brief 返回当前角色在代码里显式配置的本机 SLE MAC。
 */
static const uint8_t *sle_tree_get_static_sle_mac(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return g_sle_tree_root_mac;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        return g_sle_tree_relay_mac;
    }
    return g_sle_tree_leaf_mac;
}

/**
 * @brief Read SLE MAC from NV first, and fall back to the compiled static MAC when NV is empty.
 * @brief 优先从 NV 读取 SLE MAC，若 NV 无效则回退到代码里写死的静态 MAC。
 */
static const uint8_t *sle_tree_get_effective_sle_mac(uint8_t *nv_mac_buf, bool *from_nv)
{
    uint16_t nv_len = 0;
    errcode_t ret;

    if (from_nv != NULL) {
        *from_nv = false;
    }
    if (nv_mac_buf != NULL) {
        ret = uapi_nv_read(NV_ID_SYSTEM_FACTORY_SLE_MAC, SLE_ADDR_LEN, &nv_len, nv_mac_buf);
        if (ret == ERRCODE_SUCC && nv_len == SLE_ADDR_LEN && sle_tree_mac_is_valid(nv_mac_buf, SLE_ADDR_LEN)) {
            if (from_nv != NULL) {
                *from_nv = true;
            }
            return nv_mac_buf;
        }
    }
    return sle_tree_get_static_sle_mac();
}

/**
 * @brief Check whether one MAC buffer is valid for runtime SLE use.
 * @brief 检查一组 MAC 字节是否可作为运行时 SLE 地址使用。
 */
static bool sle_tree_mac_is_valid(const uint8_t *mac, uint16_t mac_len)
{
    uint8_t i;
    bool all_zero = true;
    bool all_ff = true;

    if (mac == NULL || mac_len < SLE_ADDR_LEN) {
        return false;
    }
    for (i = 0; i < SLE_ADDR_LEN; i++) {
        if (mac[i] != 0x00U) {
            all_zero = false;
        }
        if (mac[i] != 0xFFU) {
            all_ff = false;
        }
    }
    return (!all_zero && !all_ff);
}

/*--------------------------------------------------------------------------
 * Role / name helpers
 * 角色与名称辅助
 *--------------------------------------------------------------------------*/

/**
 * @brief Convert role enum to printable role string.
 * @brief 将角色枚举转成可打印的角色字符串。
 */
const char *sle_tree_role_name(uint8_t role)
{
    switch (role) {
        case SLE_TREE_ROLE_ROOT:
            return "root";
        case SLE_TREE_ROLE_RELAY:
            return "relay";
        case SLE_TREE_ROLE_LEAF:
            return "leaf";
        default:
            return "unknown";
    }
}

/**
 * @brief Write a zero-padded config name buffer into the runtime config field.
 * @brief 向运行时配置字段写入定长、补零后的名称缓冲区。
 */
static void sle_tree_set_cfg_name(uint8_t *dst, const char *src)
{
    if (dst == NULL) {
        return;
    }
    (void)memset_s(dst, SLE_TREE_NAME_MAX_LEN, 0, SLE_TREE_NAME_MAX_LEN);
    if (src == NULL) {
        return;
    }
    (void)memcpy_s(dst, SLE_TREE_NAME_MAX_LEN, src, strlen(src));
}

/**
 * @brief Convert fixed-length config name buffer to a normal C string.
 * @brief 把定长配置名称缓冲区转换成普通 C 字符串。
 */
void sle_tree_get_cfg_name(const uint8_t *src, char *dst, uint32_t dst_len)
{
    uint32_t i;

    if (dst == NULL || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    for (i = 0; i + 1 < dst_len && i < SLE_TREE_NAME_MAX_LEN; i++) {
        if (src[i] == '\0') {
            break;
        }
        dst[i] = (char)src[i];
        dst[i + 1] = '\0';
    }
}

/**
 * @brief Return default local_name for current compiled role.
 * @brief 返回当前角色的默认 local_name。
 */
static const char *sle_tree_default_local_name(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return "TREE_ROOT";
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        return "TREE_RELAY";
    }
    return "TREE_LEAF";
}

/**
 * @brief Return default parent_name for current compiled role.
 * @brief 返回当前角色默认要寻找的 parent_name。
 */
static const char *sle_tree_default_parent_name(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        return "TREE_ROOT";
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        return "TREE_RELAY";
    }
    return "";
}

/**
 * @brief Build a default node_id from the static SLE MAC tail bytes.
 * @brief 使用代码里显式配置的 SLE MAC 末两字节生成默认 node_id。
 */
static uint16_t sle_tree_make_default_node_id(void)
{
    sle_addr_t addr = {0};
    uint16_t node_id;

    if (sle_tree_get_factory_sle_addr(&addr)) {
        node_id = ((uint16_t)addr.addr[SLE_ADDR_LEN - 2] << 8) | addr.addr[SLE_ADDR_LEN - 1];
        if (node_id != SLE_TREE_INVALID_NODE_ID) {
            return node_id;
        }
    }
    return 1;
}

/**
 * @brief Fill one fresh runtime config from the current role and static MAC.
 * @brief 根据当前角色和静态 MAC 生成一份新的运行时配置。
 */
static void sle_tree_fill_default_cfg(sle_tree_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    (void)memset_s(cfg, sizeof(*cfg), 0, sizeof(*cfg));
    cfg->node_id = sle_tree_make_default_node_id();
    cfg->last_parent_node_id = SLE_TREE_INVALID_LAST_PARENT;
    sle_tree_set_cfg_name(cfg->local_name, sle_tree_default_local_name());
    sle_tree_set_cfg_name(cfg->parent_name, sle_tree_default_parent_name());
}

/**
 * @brief Save runtime config.
 * @brief 保存运行时配置。
 */
void sle_tree_save_cfg(void)
{
    return;
}

/**
 * @brief Load runtime config.
 * @brief 加载运行时配置。
 */
void sle_tree_load_cfg(void)
{
    sle_tree_fill_default_cfg(&g_sle_tree_ctx.cfg);
}

/**
 * @brief Read local SLE MAC, preferring NV and falling back to the compiled static MAC.
 * @brief 读取本机 SLE MAC，优先使用 NV，NV 无效时再回退到代码里的静态 MAC。
 */
bool sle_tree_get_factory_sle_addr(sle_addr_t *addr)
{
    uint8_t nv_mac[SLE_ADDR_LEN] = {0};
    const uint8_t *sle_mac;
    bool from_nv = false;

    if (addr == NULL) {
        return false;
    }
    sle_mac = sle_tree_get_effective_sle_mac(nv_mac, &from_nv);
    if (!sle_tree_mac_is_valid(sle_mac, SLE_ADDR_LEN)) {
        sle_tree_uart_printf("%s sle_mac is invalid in both NV and static fallback, please check config\r\n",
            SLE_TREE_SERVER_LOG_PREFIX);
        return false;
    }
    addr->type = 0;
    (void)memcpy_s(addr->addr, sizeof(addr->addr), sle_mac, SLE_ADDR_LEN);
    if (from_nv) {
        sle_tree_uart_printf("%s use NV SLE MAC\r\n", SLE_TREE_SERVER_LOG_PREFIX);
    }
    return true;
}

/**
 * @brief Set local SLE address from the effective SLE MAC source.
 * @brief 使用最终生效的 SLE MAC 来源设置本机 SLE 地址。
 */
void sle_tree_set_local_addr_from_nv(void)
{
    sle_addr_t addr = {0};

    if (!sle_tree_get_factory_sle_addr(&addr)) {
        return;
    }
    (void)sle_set_local_addr(&addr);
}

/**
 * @brief Fill base UUID with one 16-bit short UUID at the tail bytes.
 * @brief 将 16 位短 UUID 写入基础 UUID 的尾部，生成当前样例实际使用的 UUID。
 */
void sle_tree_uuid_set_u16(uint16_t value, sle_uuid_t *uuid)
{
    if (uuid == NULL) {
        return;
    }

    uuid->len = SLE_UUID_LEN;
    (void)memcpy_s(uuid->uuid, sizeof(uuid->uuid), g_sle_tree_uuid_base, SLE_UUID_LEN);
    uuid->uuid[12] = (uint8_t)(value & 0xFFU);
    uuid->uuid[13] = (uint8_t)((value >> 8U) & 0xFFU);
}
