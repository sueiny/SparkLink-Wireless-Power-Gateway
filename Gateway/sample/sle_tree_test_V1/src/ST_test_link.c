/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test advertise, scan and uplink helpers.
 */
#include "ST_test_internal.h"

#include <string.h>

#include "nv.h"
#include "securec.h"
#include "sle_ssap_client.h"

/* -------------------------------------------------------------------------- */
/* Advertising data build helpers / 广播构建辅助                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Decide whether the current role should keep advertising as a parent.
 * @brief 判断当前角色是否应继续作为父节点广播，综合考虑角色、容量和上行状态。
 */
static bool sle_tree_should_advertise(void)
{
    if (!sle_tree_role_has_server()) {
        return false;
    }
    if (sle_tree_count_children() >= sle_tree_max_children_for_role()) {
        return false;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY &&
        (!g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready)) {
        return false;
    }
    return true;
}

static uint16_t sle_tree_current_root_node_id(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return g_sle_tree_ctx.cfg.node_id;
    }
    if (g_sle_tree_ctx.uplink.connected) {
        return g_sle_tree_ctx.uplink.root_node_id;
    }
    return SLE_TREE_INVALID_NODE_ID;
}

static uint8_t sle_tree_current_depth(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return 0;
    }
    if (g_sle_tree_ctx.uplink.connected) {
        return g_sle_tree_ctx.uplink.depth;
    }
    return SLE_TREE_MAX_DEPTH;
}

static int32_t sle_tree_candidate_score(const sle_tree_candidate_t *candidate);
static uint8_t sle_tree_stability_score(uint64_t connected_ms);
static void sle_tree_rssi_ema_update(int8_t rssi_raw);

static sle_tree_failure_stat_t *sle_tree_find_failure_stat(uint16_t node_id, bool alloc)
{
    uint8_t i;
    sle_tree_failure_stat_t *free_slot = NULL;

    if (node_id == SLE_TREE_INVALID_NODE_ID) {
        return NULL;
    }
    for (i = 0; i < SLE_TREE_MAX_FAILURE_STATS; i++) {
        sle_tree_failure_stat_t *stat = &g_sle_tree_ctx.failure_stats[i];

        if (stat->in_use && stat->node_id == node_id) {
            return stat;
        }
        if (!stat->in_use && free_slot == NULL) {
            free_slot = stat;
        }
    }
    if (!alloc || free_slot == NULL) {
        return NULL;
    }
    free_slot->in_use = true;
    free_slot->node_id = node_id;
    free_slot->fail_count = 0;
    return free_slot;
}

uint8_t sle_tree_get_parent_fail_count(uint16_t node_id)
{
    sle_tree_failure_stat_t *stat = sle_tree_find_failure_stat(node_id, false);

    if (stat == NULL) {
        return 0;
    }
    return stat->fail_count;
}

void sle_tree_mark_parent_connect_failed(uint16_t node_id)
{
    sle_tree_failure_stat_t *stat = sle_tree_find_failure_stat(node_id, true);

    if (stat == NULL) {
        return;
    }
    if (stat->fail_count < UINT8_MAX) {
        stat->fail_count++;
    }
}

void sle_tree_reset_parent_fail_count(uint16_t node_id)
{
    sle_tree_failure_stat_t *stat = sle_tree_find_failure_stat(node_id, false);

    if (stat == NULL) {
        return;
    }
    stat->fail_count = 0;
}

static bool sle_tree_candidate_is_current_parent(const sle_tree_candidate_t *candidate)
{
    if (candidate == NULL || !g_sle_tree_ctx.uplink.connected) {
        return false;
    }
    return candidate->node_id == g_sle_tree_ctx.uplink.parent_node_id;
}

static bool sle_tree_candidate_same_tree(const sle_tree_adv_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    if (!g_sle_tree_ctx.uplink.connected) {
        return true;
    }
    if (g_sle_tree_ctx.uplink.root_node_id == SLE_TREE_INVALID_NODE_ID) {
        return true;
    }
    return info->root_node_id == g_sle_tree_ctx.uplink.root_node_id;
}

static bool sle_tree_parent_name_match(const sle_tree_adv_info_t *info, const char *parent_name)
{
    if (info == NULL) {
        return false;
    }
    if (parent_name == NULL || parent_name[0] == '\0') {
        return true;
    }
    return strcmp(info->local_name, parent_name) == 0;
}

bool sle_tree_should_optimize_parent(uint64_t now_ms)
{
    if (SLE_TREE_ENABLE_REPARENT_OPTIMIZE == 0) {
        return false;
    }
    if (!sle_tree_role_has_client() || !g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready) {
        return false;
    }
    if (g_sle_tree_ctx.seeking || g_sle_tree_ctx.reparent_pending) {
        return false;
    }
    return (now_ms - g_sle_tree_ctx.last_optimize_scan_ms) >= SLE_TREE_OPTIMIZE_SCAN_PERIOD_MS;
}

static int32_t sle_tree_current_parent_score(void)
{
    sle_tree_candidate_t current = {0};

    if (!g_sle_tree_ctx.uplink.connected) {
        return -1;
    }
    current.in_use = true;
    current.node_id = g_sle_tree_ctx.uplink.parent_node_id;
    current.root_node_id = g_sle_tree_ctx.uplink.root_node_id;
    current.role = g_sle_tree_ctx.uplink.parent_role;
    current.depth = (g_sle_tree_ctx.uplink.depth == 0) ? 0 : (uint8_t)(g_sle_tree_ctx.uplink.depth - 1U);
    current.free_slots = g_sle_tree_ctx.uplink.parent_free_slots;
    /* 用 EMA 平滑后的 RSSI 评估当前父节点，避免瞬时抖动误判 */
    current.rssi = (int8_t)(g_sle_tree_ctx.uplink.rssi_ema >> 3);
    return sle_tree_candidate_score(&current);
}

void sle_tree_try_migrate_parent(const sle_tree_candidate_t *best)
{
    int32_t best_score;
    int32_t current_score;
    int32_t score_margin;
    uint8_t current_parent_depth;
    uint16_t required_margin;
    uint8_t stability = 0;
    uint64_t connected_duration;
    errcode_t ret;

    if (best == NULL || !g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready) {
        return;
    }
    if (sle_tree_candidate_is_current_parent(best)) {
        g_sle_tree_ctx.uplink.parent_free_slots = best->free_slots;
        g_sle_tree_ctx.uplink.parent_rssi = best->rssi;
        sle_tree_rssi_ema_update(best->rssi);
        return;
    }

    best_score = sle_tree_candidate_score(best);
    current_score = sle_tree_current_parent_score();
    if (best_score < 0 || current_score < 0) {
        return;
    }

    /* 动态门槛：连接越稳定，门槛越高 */
    current_parent_depth = (g_sle_tree_ctx.uplink.depth == 0) ? 0 : (uint8_t)(g_sle_tree_ctx.uplink.depth - 1U);
    if (best->depth < current_parent_depth) {
        required_margin = SLE_TREE_REPARENT_DEPTH_BASE;
    } else {
        connected_duration = sle_tree_now_ms() - g_sle_tree_ctx.uplink.connected_at_ms;
        stability = sle_tree_stability_score(connected_duration);
        required_margin = (uint16_t)(SLE_TREE_REPARENT_BASE_THRESHOLD + SLE_TREE_REPARENT_K * stability);
    }

    score_margin = best_score - current_score;
    if (score_margin < (int32_t)required_margin) {
        return;
    }

    sle_tree_uart_printf("%s reparent from=%u to=%u cur=%d best=%d margin=%d need=%u stability=%u\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.uplink.parent_node_id, best->node_id, current_score, best_score,
        score_margin, required_margin, stability);
    sle_tree_prepare_pending_parent(best);
    g_sle_tree_ctx.reparent_pending = true;
    ret = sle_disconnect_remote_device(&g_sle_tree_ctx.uplink.addr);
    if (ret != ERRCODE_SUCC) {
        g_sle_tree_ctx.reparent_pending = false;
        (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
            sizeof(g_sle_tree_ctx.pending_parent));
        sle_tree_uart_printf("%s reparent abort: disconnect current parent failed ret=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, ret);
    }
}

/**
 * @brief Append one TLV field into the advertise or scan response buffer.
 * @brief 往广播包或扫描响应包中追加一个 TLV 字段。
 */
static void sle_tree_append_adv_field(uint8_t *buffer, uint16_t max_len, uint16_t *offset, uint8_t type,
    const uint8_t *value, uint8_t value_len)
{
    if (buffer == NULL || offset == NULL || value == NULL) {
        return;
    }
    if ((*offset + 2U + value_len) > max_len) {
        return;
    }
    buffer[(*offset)++] = (uint8_t)(value_len + 1U);
    buffer[(*offset)++] = type;
    (void)memcpy_s(&buffer[*offset], max_len - *offset, value, value_len);
    *offset = (uint16_t)(*offset + value_len);
}

/**
 * @brief Build the advertise payload and scan response payload for this node.
 * @brief 生成当前节点的广播数据和扫描响应数据，供子节点发现并选择父节点。
 */
static void sle_tree_build_adv_data(uint8_t *announce_data, uint16_t *announce_len, uint8_t *seek_rsp_data,
    uint16_t *seek_rsp_len)
{
    uint16_t offset = 0;
    uint8_t discovery_level = SLE_TREE_DISCOVERY_LEVEL_NORMAL;
    uint8_t access_mode = 0;
    uint8_t tx_power = SLE_TREE_ADV_TX_POWER;
    uint8_t local_name[SLE_TREE_NAME_MAX_LEN] = {0};
    char local_name_text[SLE_TREE_NAME_MAX_LEN + 1] = {0};
    uint8_t meta[10] = {0};

    *announce_len = 0;
    *seek_rsp_len = 0;
    (void)memset_s(announce_data, SLE_TREE_ADV_DATA_LEN_MAX, 0, SLE_TREE_ADV_DATA_LEN_MAX);
    (void)memset_s(seek_rsp_data, SLE_TREE_ADV_DATA_LEN_MAX, 0, SLE_TREE_ADV_DATA_LEN_MAX);

    sle_tree_get_cfg_name(g_sle_tree_ctx.cfg.local_name, local_name_text, sizeof(local_name_text));
    (void)memcpy_s(local_name, sizeof(local_name), local_name_text, strlen(local_name_text));
    meta[0] = SLE_TREE_MAGIC0;
    meta[1] = SLE_TREE_MAGIC1;
    meta[2] = SLE_TREE_PROTO_VERSION;
    meta[3] = g_sle_tree_ctx.role;
    sle_tree_put_le16(&meta[4], g_sle_tree_ctx.cfg.node_id);
    meta[6] = (uint8_t)(sle_tree_max_children_for_role() - sle_tree_count_children());
    sle_tree_put_le16(&meta[7], sle_tree_current_root_node_id());
    meta[9] = sle_tree_current_depth();

    sle_tree_append_adv_field(announce_data, SLE_TREE_ADV_DATA_LEN_MAX, &offset,
        SLE_TREE_DATA_TYPE_DISCOVERY_LEVEL, &discovery_level, sizeof(discovery_level));
    sle_tree_append_adv_field(announce_data, SLE_TREE_ADV_DATA_LEN_MAX, &offset,
        SLE_TREE_DATA_TYPE_ACCESS_MODE, &access_mode, sizeof(access_mode));
    sle_tree_append_adv_field(announce_data, SLE_TREE_ADV_DATA_LEN_MAX, &offset,
        SLE_TREE_DATA_TYPE_COMPLETE_LOCAL_NAME, local_name, (uint8_t)strlen(local_name_text));
    sle_tree_append_adv_field(announce_data, SLE_TREE_ADV_DATA_LEN_MAX, &offset,
        SLE_TREE_DATA_TYPE_MANUFACTURER_SPECIFIC, meta, sizeof(meta));
    *announce_len = offset;

    offset = 0;
    sle_tree_append_adv_field(seek_rsp_data, SLE_TREE_ADV_DATA_LEN_MAX, &offset,
        SLE_TREE_DATA_TYPE_TX_POWER_LEVEL, &tx_power, sizeof(tx_power));
    *seek_rsp_len = offset;
}

/**
 * @brief Parse the custom advertise metadata used by the tree sample.
 * @brief 解析树状组网样例自定义的广播元数据，提取角色、节点号和空闲子位。
 */
bool sle_tree_parse_adv_data(const uint8_t *data, uint8_t data_len, sle_tree_adv_info_t *info)
{
    uint8_t offset = 0;

    if (data == NULL || info == NULL) {
        return false;
    }
    (void)memset_s(info, sizeof(*info), 0, sizeof(*info));
    while ((offset + 1U) < data_len) {
        uint8_t field_len = data[offset++];
        uint8_t type;
        const uint8_t *value;
        uint8_t value_len;

        if (field_len == 0) {
            break;
        }
        if ((offset + field_len) > data_len) {
            break;
        }
        type = data[offset++];
        value = &data[offset];
        value_len = (uint8_t)(field_len - 1U);
        if (type == SLE_TREE_DATA_TYPE_COMPLETE_LOCAL_NAME) {
            uint8_t copy_len = value_len;
            if (copy_len > SLE_TREE_NAME_MAX_LEN) {
                copy_len = SLE_TREE_NAME_MAX_LEN;
            }
            (void)memcpy_s(info->local_name, sizeof(info->local_name), value, copy_len);
            info->local_name[copy_len] = '\0';
        } else if (type == SLE_TREE_DATA_TYPE_MANUFACTURER_SPECIFIC && value_len >= 10 &&
            value[0] == SLE_TREE_MAGIC0 && value[1] == SLE_TREE_MAGIC1 && value[2] == SLE_TREE_PROTO_VERSION) {
            info->valid = true;
            info->role = value[3];
            info->node_id = sle_tree_get_le16(&value[4]);
            info->free_slots = value[6];
            info->root_node_id = sle_tree_get_le16(&value[7]);
            info->depth = value[9];
        }
        offset = (uint8_t)(offset + value_len);
    }
    return info->valid && info->local_name[0] != '\0';
}

/**
 * @brief Fill connectable advertise parameters for the local parent role.
 * @brief 填充本机作为父节点时的可连接广播参数。
 */
static void sle_tree_set_announce_params(void)
{
    sle_announce_param_t param = {0};
    sle_addr_t local_addr = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_TREE_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_TREE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_TREE_ADV_INTERVAL_MIN;
    param.announce_interval_max = SLE_TREE_ADV_INTERVAL_MAX;
    param.conn_interval_min = SLE_TREE_CONN_INTERVAL_MIN;
    param.conn_interval_max = SLE_TREE_CONN_INTERVAL_MAX;
    param.conn_max_latency = SLE_TREE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_TREE_CONN_SUPERVISION_TIMEOUT;
    param.announce_tx_power = SLE_TREE_ADV_TX_POWER;
    if (sle_tree_get_factory_sle_addr(&local_addr)) {
        (void)memcpy_s(&param.own_addr, sizeof(param.own_addr), &local_addr, sizeof(local_addr));
    }
    (void)sle_set_announce_param(SLE_TREE_ADV_HANDLE, &param);
}

/**
 * @brief Refresh parent advertising according to current capacity and role state.
 * @brief 根据当前角色状态和可用子节点容量刷新父节点广播。
 */
void sle_tree_refresh_advertising(void)
{
    sle_announce_data_t announce_data = {0};
    uint8_t adv_buf[SLE_TREE_ADV_DATA_LEN_MAX] = {0};
    uint8_t scan_rsp_buf[SLE_TREE_ADV_DATA_LEN_MAX] = {0};
    uint16_t adv_len = 0;
    uint16_t scan_rsp_len = 0;
    bool should_adv;

    if (!sle_tree_role_has_server() || !g_sle_tree_ctx.sle_enabled || !g_sle_tree_ctx.server_ready) {
        return;
    }
    should_adv = sle_tree_should_advertise();
    if (g_sle_tree_ctx.announce_started) {
        (void)sle_stop_announce(SLE_TREE_ADV_HANDLE);
        g_sle_tree_ctx.announce_started = false;
    }
    if (!should_adv) {
        return;
    }
    sle_tree_set_announce_params();
    sle_tree_build_adv_data(adv_buf, &adv_len, scan_rsp_buf, &scan_rsp_len);
    announce_data.announce_data = adv_buf;
    announce_data.announce_data_len = adv_len;
    announce_data.seek_rsp_data = scan_rsp_buf;
    announce_data.seek_rsp_data_len = scan_rsp_len;
    if (sle_set_announce_data(SLE_TREE_ADV_HANDLE, &announce_data) == ERRCODE_SUCC &&
        sle_start_announce(SLE_TREE_ADV_HANDLE) == ERRCODE_SUCC) {
        g_sle_tree_ctx.announce_started = true;
    }
}

/* -------------------------------------------------------------------------- */
/* Parent candidate collection / 父节点候选收集与评分                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Clear the cached candidate-parent table before a new scan round.
 * @brief 在新一轮扫描前清空父节点候选表。
 */
static void sle_tree_clear_candidates(void)
{
    (void)memset_s(g_sle_tree_ctx.candidates, sizeof(g_sle_tree_ctx.candidates), 0, sizeof(g_sle_tree_ctx.candidates));
}

/**
 * @brief Store or update one candidate parent collected during scanning.
 * @brief 保存或更新扫描过程中发现的一个候选父节点。
 */
void sle_tree_store_candidate(const sle_addr_t *addr, const sle_tree_adv_info_t *info, int8_t rssi)
{
    uint8_t i;
    sle_tree_candidate_t *slot = NULL;

    if (addr == NULL || info == NULL || info->free_slots == 0U) {
        return;
    }
    for (i = 0; i < SLE_TREE_MAX_CANDIDATES; i++) {
        if (g_sle_tree_ctx.candidates[i].in_use &&
            sle_tree_addr_equal(&g_sle_tree_ctx.candidates[i].addr, addr)) {
            slot = &g_sle_tree_ctx.candidates[i];
            break;
        }
    }
    if (slot == NULL) {
        for (i = 0; i < SLE_TREE_MAX_CANDIDATES; i++) {
            if (!g_sle_tree_ctx.candidates[i].in_use) {
                slot = &g_sle_tree_ctx.candidates[i];
                (void)memset_s(slot, sizeof(*slot), 0, sizeof(*slot));
                slot->in_use = true;
                (void)memcpy_s(&slot->addr, sizeof(slot->addr), addr, sizeof(*addr));
                break;
            }
        }
    }
    if (slot == NULL) {
        return;
    }
    slot->node_id = info->node_id;
    slot->root_node_id = info->root_node_id;
    slot->role = info->role;
    slot->depth = info->depth;
    slot->free_slots = info->free_slots;
    slot->rssi = rssi;
    (void)memcpy_s(slot->local_name, sizeof(slot->local_name), info->local_name, strlen(info->local_name) + 1U);
    /* 如果这个候选就是当前父节点，更新 RSSI EMA */
    if (g_sle_tree_ctx.uplink.connected && info->node_id == g_sle_tree_ctx.uplink.parent_node_id) {
        sle_tree_rssi_ema_update(rssi);
    }
    /* [测试模式] 未连接时，发现第一个合法候选就立即连接，不做最优选择 */
    if (!g_sle_tree_ctx.uplink.connected && !g_sle_tree_ctx.pending_parent.valid) {
        sle_tree_prepare_pending_parent(slot);
        (void)sle_stop_seek();
    }
}

/**
 * @brief Normalize RSSI into a 0..100 score used by parent selection.
 * @brief 把 RSSI 归一化为 0..100 分，用于候选父节点打分。
 */
static int32_t sle_tree_rssi_score(int8_t rssi)
{
    if (rssi <= -100) {
        return 0;
    }
    if (rssi >= -40) {
        return 100;
    }
    return ((int32_t)(rssi + 100) * 100) / 60;
}

/*--------------------------------------------------------------------------
 * Stability score: exponential saturation via lookup table.
 * 稳定性评分：通过查表实现指数饱和曲线。
 * λ = 0.005, 采样间隔 30s, 12 个条目覆盖 0~330s.
 * formula: round(100 × (1 − exp(−0.005 × t)))
 *--------------------------------------------------------------------------*/
static const uint8_t g_stability_lut[] = {
      0,  14,  26,  37,  47,  55,  62,  68,  73,  78,  82,  85
};
#define SLE_TREE_STABILITY_LUT_STEP_MS  30000U
#define SLE_TREE_STABILITY_LUT_SIZE     (sizeof(g_stability_lut) / sizeof(g_stability_lut[0]))

static uint8_t sle_tree_stability_score(uint64_t connected_ms)
{
    uint32_t elapsed_s = (uint32_t)(connected_ms / 1000U);
    uint32_t idx = elapsed_s / 30U;
    uint32_t frac = elapsed_s % 30U;
    uint8_t v0;
    uint8_t v1;

    if (idx >= SLE_TREE_STABILITY_LUT_SIZE - 1U) {
        return g_stability_lut[SLE_TREE_STABILITY_LUT_SIZE - 1U];
    }
    v0 = g_stability_lut[idx];
    v1 = g_stability_lut[idx + 1U];
    /* 线性插值: v0 + (v1 - v0) * frac / 30 */
    return (uint8_t)(v0 + (uint8_t)(((uint32_t)(v1 - v0) * frac) / 30U));
}

/**
 * @brief Update RSSI EMA for current parent link (α = 1/8, integer shift).
 * @brief 更新当前父节点链路的 RSSI 指数移动平均（α=1/8，整数移位实现）。
 */
static void sle_tree_rssi_ema_update(int8_t rssi_raw)
{
    int16_t raw_x8 = (int16_t)rssi_raw * 8;
    int16_t ema = g_sle_tree_ctx.uplink.rssi_ema;

    if (g_sle_tree_ctx.uplink.connected_at_ms == 0) {
        /* First sample after connect: initialize EMA to raw value */
        g_sle_tree_ctx.uplink.rssi_ema = raw_x8;
        return;
    }
    /* ema = ema - ema/8 + raw/8 */
    ema = ema - (ema >> 3) + (raw_x8 >> 3);
    g_sle_tree_ctx.uplink.rssi_ema = ema;
}

/**
 * @brief Calculate the weighted score of one candidate parent.
 * @brief 计算单个候选父节点的综合评分。稳定性通过动态门槛单独处理，不参与评分。
 */
static int32_t sle_tree_candidate_score(const sle_tree_candidate_t *candidate)
{
    int32_t free_slots_score;
    int32_t rssi_score;
    int32_t depth_score;
    int32_t failure_score;
    uint8_t fail_count;

    if (candidate == NULL) {
        return -1;
    }
    if (candidate->depth + 1 >= SLE_TREE_MAX_DEPTH) {
        return -1;
    }
    free_slots_score = ((int32_t)candidate->free_slots * 100) / SLE_TREE_MAX_CHILDREN;
    rssi_score = sle_tree_rssi_score(candidate->rssi);
    depth_score = 100 - (((int32_t)candidate->depth * 100) / SLE_TREE_MAX_DEPTH);
    fail_count = sle_tree_get_parent_fail_count(candidate->node_id);
    if (fail_count > SLE_TREE_FAILURE_COUNT_CAP) {
        fail_count = SLE_TREE_FAILURE_COUNT_CAP;
    }
    failure_score = ((int32_t)fail_count * 100) / SLE_TREE_FAILURE_COUNT_CAP;
    return depth_score * 20    /* 深度权重 */
     + free_slots_score * 25   /* 容量权重，负载均衡 */
     + rssi_score * 30         /* 链路质量权重，RSSI 主导 */
     - failure_score * SLE_TREE_FAILURE_PENALTY_WEIGHT;
}

/**
 * @brief Print one candidate-parent score breakdown for debugging parent selection.
 * @brief 打印单个候选父节点的打分明细，便于调试 leaf/relay 的选父过程。
 */
static void sle_tree_log_candidate_score(const sle_tree_candidate_t *candidate, int32_t total_score)
{
    uint8_t fail_count = 0;

    if (candidate == NULL) {
        return;
    }

    fail_count = sle_tree_get_parent_fail_count(candidate->node_id);

    sle_tree_uart_printf(
        "%s candidate node=%u depth=%u slots=%u rssi=%d fail=%u score=%d name=%s\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, candidate->node_id, candidate->depth,
        candidate->free_slots, candidate->rssi, fail_count, total_score, candidate->local_name);
}

/**
 * @brief Pick the best candidate parent according to the configured policy.
 * @brief 按既定评分规则和并列打破规则选出最优父节点。
 */
sle_tree_candidate_t *sle_tree_pick_best_candidate(void)
{
    uint8_t i;
    sle_tree_candidate_t *best = NULL;
    int32_t best_score = -1;

    for (i = 0; i < SLE_TREE_MAX_CANDIDATES; i++) {
        sle_tree_candidate_t *candidate = &g_sle_tree_ctx.candidates[i];
        int32_t score;

        if (!candidate->in_use) {
            continue;
        }
        score = sle_tree_candidate_score(candidate);
        if (score < 0) {
            continue;
        }
        sle_tree_log_candidate_score(candidate, score);
        if (best == NULL || score > best_score ||
            (score == best_score && candidate->depth < best->depth) ||
            (score == best_score && candidate->depth == best->depth && candidate->free_slots > best->free_slots) ||
            (score == best_score && candidate->depth == best->depth &&
                candidate->free_slots == best->free_slots && candidate->rssi > best->rssi) ||
            (score == best_score && candidate->depth == best->depth && candidate->free_slots == best->free_slots &&
                candidate->rssi == best->rssi && candidate->node_id < best->node_id)) {
            best = candidate;
            best_score = score;
        }
    }

    if (best != NULL) {
        sle_tree_uart_printf("%s pick parent node=%u root=%u depth=%u score=%d rssi=%d free_slots=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, best->node_id, best->root_node_id, best->depth, best_score, best->rssi,
            best->free_slots);
    } else if (g_sle_tree_ctx.optimize_scan_active && g_sle_tree_ctx.uplink.connected) {
        sle_tree_uart_printf("%s optimize scan no valid candidate; keep parent node=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.uplink.parent_node_id);
    } else {
        sle_tree_uart_printf("%s pick parent failed: no valid candidate\r\n", SLE_TREE_SERVER_LOG_PREFIX);
    }

    return best;
}

/**
 * @brief Cache the parent selected by the current scan round before connecting.
 * @brief 在真正发起连接前，先缓存本轮扫描选中的目标父节点。
 */
void sle_tree_prepare_pending_parent(const sle_tree_candidate_t *candidate)
{
    if (candidate == NULL) {
        (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
            sizeof(g_sle_tree_ctx.pending_parent));
        g_sle_tree_ctx.parent_connect_start_ms = 0;
        return;
    }
    g_sle_tree_ctx.pending_parent.valid = true;
    (void)memcpy_s(&g_sle_tree_ctx.pending_parent.addr, sizeof(g_sle_tree_ctx.pending_parent.addr),
        &candidate->addr, sizeof(candidate->addr));
    g_sle_tree_ctx.pending_parent.node_id = candidate->node_id;
    g_sle_tree_ctx.pending_parent.root_node_id = candidate->root_node_id;
    g_sle_tree_ctx.pending_parent.role = candidate->role;
    g_sle_tree_ctx.pending_parent.depth = candidate->depth;
    g_sle_tree_ctx.pending_parent.free_slots = candidate->free_slots;
    g_sle_tree_ctx.pending_parent.rssi = candidate->rssi;
    g_sle_tree_ctx.parent_connect_start_ms = 0;
}

void sle_tree_cache_pending_parent_to_uplink(void)
{
    if (!g_sle_tree_ctx.pending_parent.valid) {
        return;
    }
    g_sle_tree_ctx.uplink.parent_node_id = g_sle_tree_ctx.pending_parent.node_id;
    g_sle_tree_ctx.uplink.parent_role = g_sle_tree_ctx.pending_parent.role;
    g_sle_tree_ctx.uplink.root_node_id = g_sle_tree_ctx.pending_parent.root_node_id;
    g_sle_tree_ctx.uplink.depth = (uint8_t)(g_sle_tree_ctx.pending_parent.depth + 1U);
    g_sle_tree_ctx.uplink.parent_free_slots = g_sle_tree_ctx.pending_parent.free_slots;
    g_sle_tree_ctx.uplink.parent_rssi = g_sle_tree_ctx.pending_parent.rssi;
}

/* -------------------------------------------------------------------------- */
/* Scan and connect helpers / 扫描与连接辅助                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start one parent scan round for client-capable roles.
 * @brief 对具备 client 能力的角色启动一轮父节点扫描。
 */
void sle_tree_start_scan(void)
{
    sle_seek_param_t seek_param = {0};

    if (!sle_tree_role_has_client() || g_sle_tree_ctx.seeking || !g_sle_tree_ctx.sle_enabled) {
        return;
    }
    if (g_sle_tree_ctx.uplink.connected && !g_sle_tree_ctx.uplink.handle_ready) {
        return;
    }
    if (!g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.pending_parent.valid) {
        return;
    }
    g_sle_tree_ctx.seeking = true;
    g_sle_tree_ctx.scan_start_ms = sle_tree_now_ms();
    g_sle_tree_ctx.start_scan_pending = false;
    sle_tree_clear_candidates();
    (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
        sizeof(g_sle_tree_ctx.pending_parent));
    g_sle_tree_ctx.parent_connect_start_ms = 0;

    seek_param.own_addr_type = 0;
    seek_param.filter_duplicates = 0;
    seek_param.seek_filter_policy = 0;
    seek_param.seek_phys = 1;
    seek_param.seek_type[0] = 1;
    seek_param.seek_interval[0] = SLE_TREE_SCAN_INTERVAL;
    seek_param.seek_window[0] = SLE_TREE_SCAN_WINDOW;
    (void)sle_set_seek_param(&seek_param);
    (void)sle_start_seek();
}

/**
 * @brief Schedule a delayed rescan instead of starting it immediately.
 * @brief 安排一次延时重扫，而不是立刻开始新的扫描流程。
 * 使用指数退避+抖动随机化，避免大规模节点同步碰撞。
 */
static uint8_t g_rescan_attempt_count = 0;

void sle_tree_schedule_rescan(uint32_t delay_ms)
{
    uint32_t jitter_ms = 0;
    uint32_t node_mix;
    uint32_t backoff_ms;

    /* 指数退避：首次delay_ms，第2次2*delay_ms，第3次4*delay_ms，上限30秒 */
    if (g_rescan_attempt_count < 16) {
        g_rescan_attempt_count++;
    }
    backoff_ms = delay_ms * (1U << (g_rescan_attempt_count - 1));
    if (backoff_ms > 30000) {
        backoff_ms = 30000;
    }

    if (SLE_TREE_RESCAN_JITTER_MS > 0) {
        /* 混入attempt_count打破确定性碰撞 */
        node_mix = (uint32_t)g_sle_tree_ctx.cfg.node_id * 1103515245U + 12345U + g_rescan_attempt_count;
        jitter_ms = (node_mix >> 16U) % SLE_TREE_RESCAN_JITTER_MS;
    }
    g_sle_tree_ctx.rescan_due_ms = sle_tree_now_ms() + backoff_ms + jitter_ms;
    g_sle_tree_ctx.start_scan_pending = true;
}

/**
 * @brief Reset rescan attempt counter after successful connection.
 * @brief 连接成功后重置重扫尝试计数器。
 */
void sle_tree_reset_rescan_count(void)
{
    g_rescan_attempt_count = 0;
}

/* -------------------------------------------------------------------------- */
/* Role-dispatch wrappers / 角色分发封装                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Call the periodic role-specific background hook.
 * @brief 调用各角色的周期后台钩子，用于心跳、重连和自动测试流量。
 */
void sle_tree_role_tick(uint64_t now_ms)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        sle_tree_root_role_tick(now_ms);
    } else if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_role_tick(now_ms);
    } else if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        sle_tree_leaf_role_tick(now_ms);
    }
}

/**
 * @brief Dispatch one scan result to the current role after common filtering.
 * @brief 先做公共过滤，再把一条扫描结果分发给当前角色处理。
 */
void sle_tree_handle_seek_result(sle_seek_result_info_t *result)
{
    sle_tree_adv_info_t adv_info = {0};
    char parent_name[SLE_TREE_NAME_MAX_LEN + 1] = {0};
    bool name_match;

    if (result == NULL || result->data == NULL || !sle_tree_parse_adv_data(result->data, result->data_length, &adv_info)) {
        return;
    }
    sle_tree_get_cfg_name(g_sle_tree_ctx.cfg.parent_name, parent_name, sizeof(parent_name));
    if (adv_info.node_id == g_sle_tree_ctx.cfg.node_id) {
        return;
    }
    if (adv_info.role == SLE_TREE_ROLE_ROOT) {
        if (adv_info.root_node_id == SLE_TREE_INVALID_NODE_ID) {
            adv_info.root_node_id = adv_info.node_id;
        }
        adv_info.depth = 0;
    }
    name_match = sle_tree_parent_name_match(&adv_info, parent_name);
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        if (adv_info.role == SLE_TREE_ROLE_RELAY && !adv_info.valid) {
            return;
        }
        if (adv_info.role == SLE_TREE_ROLE_RELAY && adv_info.depth >= SLE_TREE_MAX_DEPTH - 1U) {
            return;
        }
        if (!sle_tree_candidate_same_tree(&adv_info)) {
            return;
        }
        if (adv_info.role == SLE_TREE_ROLE_ROOT && !name_match && parent_name[0] != '\0') {
            return;
        }
        sle_tree_relay_handle_seek_result(result, &adv_info);
    } else if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        if (!sle_tree_candidate_same_tree(&adv_info)) {
            return;
        }
        if (adv_info.role == SLE_TREE_ROLE_RELAY && adv_info.depth >= SLE_TREE_MAX_DEPTH - 1U) {
            return;
        }
        if (!name_match && parent_name[0] != '\0' && adv_info.role != SLE_TREE_ROLE_RELAY) {
            return;
        }
        sle_tree_leaf_handle_seek_result(result, &adv_info);
    }
}

/**
 * @brief Initialize the default connect parameters used for parent links.
 * @brief 初始化默认连接参数，供本样例建立父子链路时统一使用。
 */
void sle_tree_connect_param_init(void)
{
    sle_default_connect_param_t param = {0};

    param.enable_filter_policy = 0;
    param.gt_negotiate = SLE_ANNOUNCE_ROLE_G_CAN_NEGO;
    param.initiate_phys = 1;
    param.max_interval = SLE_TREE_CONN_INTERVAL_MAX;
    param.min_interval = SLE_TREE_CONN_INTERVAL_MIN;
    param.scan_interval = 400;
    param.scan_window = 20;
    param.timeout = SLE_TREE_CONN_SUPERVISION_TIMEOUT;
    (void)sle_default_connection_param_set(&param);
}

/**
 * @brief Ask the current role whether one address is the active parent address.
 * @brief 询问当前角色：给定地址是否就是当前正在使用的父节点地址。
 */
bool sle_tree_is_uplink_addr(const sle_addr_t *addr)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        return sle_tree_relay_is_uplink_addr(addr);
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        return sle_tree_leaf_is_uplink_addr(addr);
    }
    return sle_tree_root_is_uplink_addr(addr);
}

/**
 * @brief Dispatch one successful uplink connection event to the current role.
 * @brief 把一次上行父连接建立事件分发给当前角色处理。
 */
void sle_tree_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_handle_uplink_connected(conn_id, addr, pair_state);
        return;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        sle_tree_leaf_handle_uplink_connected(conn_id, addr, pair_state);
    }
}

/**
 * @brief Dispatch one uplink disconnect event to the current role.
 * @brief 把一次上行父连接断开事件分发给当前角色处理。
 */
void sle_tree_handle_uplink_disconnected(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_handle_uplink_disconnected();
        return;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        sle_tree_leaf_handle_uplink_disconnected();
    }
}
