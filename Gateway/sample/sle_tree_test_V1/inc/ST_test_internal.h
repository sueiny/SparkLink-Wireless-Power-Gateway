#ifndef SLE_TREE_TEST_INTERNAL_H
#define SLE_TREE_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "errcode.h"
#include "nv_common_cfg.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_ssap_stru.h"
#include "ST_test.h"

/*--------------------------------------------------------------------------
 * Common limits and buffer sizes
 * 公共容量与缓冲区大小
 *--------------------------------------------------------------------------*/
/* Max text length for local_name / parent_name stored in NV.
 * NV 中 local_name / parent_name 的最大文本长度。 */
#define SLE_TREE_NAME_MAX_LEN                         SLE_TREE_NAME_MAX
/* Max child slot array size. Root can use all 8; relay keeps one link for parent.
 * 子节点槽位数组大小。root 可用满 8 个；relay 需要预留 1 条链路给父节点。 */
#define SLE_TREE_MAX_CHILDREN                         8
/* Route table size learned from heartbeat and uplink traffic.
 * 通过心跳和上行流量学习得到的路由表容量。 */
#define SLE_TREE_MAX_ROUTES                           64
/* Temporary parent candidates collected while relays scan parents.
 * relay 扫描父节点时临时保存的候选父节点数量，需覆盖现场可广播节点数量。 */
#define SLE_TREE_MAX_CANDIDATES                       24
#define SLE_TREE_MAX_FAILURE_STATS                    8
#define SLE_TREE_MAX_TOPO_NODES                       64

/*--------------------------------------------------------------------------
 * UART test interface
 * UART 测试接口
 *--------------------------------------------------------------------------*/
/* Use default system bus0 directly, no extra UART init in this sample.
 * 直接使用系统默认 bus0，本 sample 不再额外初始化 UART。 */
#define SLE_TREE_UART_BUS_ID                          0
/* Keep normal logs on printk only. Enabling direct echo duplicates logs on boards
 * where printk also uses bus0.
 * 普通日志默认只走 printk；若 printk 同样走 bus0，直接回写 UART 会让日志重复并增加压测负载。 */
#define SLE_TREE_UART_DIRECT_ECHO                     0
/* Ring buffer size used by UART RX callback before worker parses full lines.
 * UART RX 回调先把字节搬进环形缓冲区，再由工作线程按行解析。 */
#define SLE_TREE_UART_RX_RING_SIZE                    512
/* Max line buffer for "<dst_node_id> <HEX_PAYLOAD>" command.
 * "<dst_node_id> <HEX_PAYLOAD>" 文本命令的最大行缓冲区。 */
#define SLE_TREE_UART_LINE_MAX                        256

/*--------------------------------------------------------------------------
 * Internal tree frame format
 * 树状网络内部帧格式
 *--------------------------------------------------------------------------*/
/* Max encoded frame length transported over SLE property value.
 * 通过 SLE property 传输的最大编码帧长度。 */
#define SLE_TREE_MAX_FRAME_LEN                        256
/* Fixed frame header: magic/version/type/src/dst/seq/len.
 * 固定帧头长度：magic/version/type/src/dst/seq/len。 */
#define SLE_TREE_FRAME_HEADER_LEN                     13
/* Payload bytes available after removing fixed frame header.
 * 去掉固定帧头之后可承载的 payload 长度。 */
#define SLE_TREE_MAX_PAYLOAD_LEN                      (SLE_TREE_MAX_FRAME_LEN - SLE_TREE_FRAME_HEADER_LEN)

/*--------------------------------------------------------------------------
 * Invalid markers and wildcard node IDs
 * 无效标记与通配节点 ID
 *--------------------------------------------------------------------------*/
/* Invalid connection marker used before SLE link is established.
 * SLE 链路尚未建立时使用的无效连接标记。 */
#define SLE_TREE_INVALID_CONN_ID                      0xFFFF
/* Invalid node_id marker.
 * 无效 node_id 标记。 */
#define SLE_TREE_INVALID_NODE_ID                      0x0000
/* Wildcard destination used by heartbeat frame.
 * 心跳帧使用的通配目的节点。 */
#define SLE_TREE_ANY_NODE_ID                          0x0000
/* Invalid last-parent marker stored in NV.
 * NV 中 last_parent 的无效标记。 */
#define SLE_TREE_INVALID_LAST_PARENT                  0xFFFF

/*--------------------------------------------------------------------------
 * Advertising / connection parameters
 * 广播与连接参数
 *--------------------------------------------------------------------------*/
/* Fixed announce handle used by this sample.
 * 本 sample 固定使用的广播 handle。 */
#define SLE_TREE_ADV_HANDLE                           1
/* Max advertising payload supported by current SLE API.
 * 当前 SLE API 支持的最大广播数据长度。 */
#define SLE_TREE_ADV_DATA_LEN_MAX                     251
/* TX power used by root / relay when advertising child access info.
 * root / relay 广播可接入信息时使用的发送功率。 */
#define SLE_TREE_ADV_TX_POWER                         20
/* Fixed advertising interval for demo topology.
 * demo 拓扑固定使用的广播间隔。 */
#define SLE_TREE_ADV_INTERVAL_MIN                     0xC8
#define SLE_TREE_ADV_INTERVAL_MAX                     0xC8
/* Fixed connection interval for demo topology.
 * demo 拓扑固定使用的连接间隔。 */
#define SLE_TREE_CONN_INTERVAL_MIN                    0x64
#define SLE_TREE_CONN_INTERVAL_MAX                    0x64
/* Supervision timeout and max latency for demo links.
 * demo 链路的 supervision timeout 和最大 latency。
 * 多跳网络中转发延迟+重传容易超过500ms，增大到2秒避免误断连。
 * max_latency=4 允许跳过最多4个连接事件，容忍突发延迟。 */
#define SLE_TREE_CONN_SUPERVISION_TIMEOUT             0x5DC
#define SLE_TREE_CONN_MAX_LATENCY                     0//这个对连接没影响
/* Scan interval/window used by relay / leaf while selecting parent.
 * relay / leaf 选父节点时使用的扫描 interval/window。
 * 占空比从100%降到30%，减少空口冲突，提高扫描发现率。 */
#define SLE_TREE_SCAN_INTERVAL                        160
#define SLE_TREE_SCAN_WINDOW                          48
/* Candidate collection window for leaf scoring.
 * leaf 收集候选父节点并打分的时间窗口。从3秒增加到5秒，提高扫描发现率。 */
#define SLE_TREE_SCAN_COLLECT_WINDOW_MS               5000
/* Base delay before next scan attempt after disconnect/failure.
 * 断链或失败后下一次重扫前的基础延时。实际延时会叠加 node_id 相关抖动，避免大量节点同步抢连。 */
#define SLE_TREE_RESCAN_DELAY_MS                      1000
#define SLE_TREE_RESCAN_JITTER_MS                     8000
/* Max wait time after connect request succeeds but no connect callback arrives.
 * 发起连接成功后等待 Connected/Disconnected 回调的最长时间。
 * 52节点争抢root 8槽位时协议栈排队可能超过5秒，增大到15秒避免误超时。 */
#define SLE_TREE_CONNECT_CALLBACK_TIMEOUT_MS          15000
/* Max supported tree depth for dynamic multi-hop relay attachment.
 * 动态多跳 relay 挂载允许的最大树深度。 */
#define SLE_TREE_MAX_DEPTH                            4
/* Periodic optimization interval for connected leaf/relay reparent attempts.
 * 已入网 leaf/relay 的周期性优化重选父节点间隔。
 * Keep disabled during large-scale pressure tests to avoid reparent storms before
 * the tree is stable.
 * 大规模压测阶段默认关闭，避免树尚未稳定时主动切父形成重连风暴。 */
#define SLE_TREE_ENABLE_REPARENT_OPTIMIZE             0
#define SLE_TREE_OPTIMIZE_SCAN_PERIOD_MS              5000
/* Dynamic reparent threshold: Threshold = BASE + K × Stability.
 * 动态切父门槛：连接越稳定，门槛越高，拒绝频繁切换。 */
#define SLE_TREE_REPARENT_BASE_THRESHOLD              200
#define SLE_TREE_REPARENT_K                           4
/* Base threshold used when new parent shortens tree depth (lower bar to encourage shallower tree).
 * 新父节点能降低树层级时使用较低门槛，鼓励向浅层迁移。 */
#define SLE_TREE_REPARENT_DEPTH_BASE                  100
/* Failure penalty weight in parent score (negative percentage contribution).
 * 父节点评分里的连接失败惩罚权重（负向占比）。 */
#define SLE_TREE_FAILURE_PENALTY_WEIGHT               5
/* Saturation cap for connect-failure count when converted into penalty score.
 * 连接失败次数转换成罚分时的饱和上限。 */
#define SLE_TREE_FAILURE_COUNT_CAP                    5
/* Relay periodic topology-summary period to root.
 * relay 周期性向 root 上报拓扑摘要的周期。 */
#define SLE_TREE_TOPO_SUMMARY_PERIOD_MS               10000
/* Root periodic topology tree print period.
 * root 周期性打印拓扑树的周期。 */
#define SLE_TREE_ROOT_TOPO_PRINT_PERIOD_MS            5000
/* Root topology entry timeout before pruning stale nodes.
 * root 拓扑项超时清理时间，保底机制。大于心跳周期(30s)，确保正常心跳能刷新拓扑。 */
#define SLE_TREE_TOPO_TIMEOUT_MS                      60000

/*--------------------------------------------------------------------------
 * Periodic timing and task settings
 * 周期时序与任务配置
 *--------------------------------------------------------------------------*/
/* Heartbeat period from leaf->relay and relay->root.
 * leaf->relay 和 relay->root 的心跳周期。
 * 30秒一次，仅做业务层存活确认和路由刷新，不依赖心跳做频繁路由学习。 */
#define SLE_TREE_HEARTBEAT_PERIOD_MS                  30000
/* Learned route timeout, exceeded routes are dropped.
 * 路由超时时间，保底清理机制。大于心跳周期(30s)，确保正常心跳能刷新路由。 */
#define SLE_TREE_ROUTE_TIMEOUT_MS                     60000
/* Worker thread stack size and priority.
 * 主工作线程的栈大小与优先级。
 * 优先级15-20：保证SLE回调及时响应，又不抢占关键系统任务(HCC/DFX优先级5)。 */
#define SLE_TREE_TASK_STACK_SIZE                      0x2400
#define SLE_TREE_TASK_PRIO                            18
/* Preferred MTU exchanged after link setup.
 * 建链后协商的目标 MTU。 */
#define SLE_TREE_MTU_SIZE                             520

/*--------------------------------------------------------------------------
 * UUIDs and frame markers
 * UUID 与帧标记
 *--------------------------------------------------------------------------*/
/* Demo service / property / app UUID short values.
 * demo 中 service / property / app 使用的短 UUID。 */
#define SLE_TREE_SERVICE_UUID                         0xA001
#define SLE_TREE_PROPERTY_UUID                        0xA002
#define SLE_TREE_APP_UUID                             0xA003
/* Shared protocol marker and version for both tree frames and advertise metadata.
 * 树状网络内部帧和广播元数据共用的协议标记与版本。 */
#define SLE_TREE_MAGIC0                               0x53
#define SLE_TREE_MAGIC1                               0x54
#define SLE_TREE_PROTO_VERSION                        0x01

/*--------------------------------------------------------------------------
 * Transfer / log helpers
 * 传输与日志辅助
 *--------------------------------------------------------------------------*/
/* Single demo client instance ID.
 * demo 中固定使用的 client 实例 ID。 */
#define SLE_TREE_CLIENT_ID                            0
/* Log prefix printed on startup.
 * 启动时打印日志使用的前缀。 */
#define SLE_TREE_VERSION                              "5.7.1"
#define SLE_TREE_SERVER_LOG_PREFIX                    "[sle_tree_" SLE_TREE_VERSION "]"

/*--------------------------------------------------------------------------
 * Advertising data field types
 * 广播字段类型
 *--------------------------------------------------------------------------*/
/* Standard/extended field types used to encode root / relay capability.
 * 用于编码 root / relay 能力信息的广播字段类型。 */
#define SLE_TREE_DATA_TYPE_DISCOVERY_LEVEL            0x01  /* 发现等级 */
#define SLE_TREE_DATA_TYPE_ACCESS_MODE                0x02  /* 接入层能力 */
#define SLE_TREE_DATA_TYPE_COMPLETE_LOCAL_NAME        0x0B  /* 设备完整本地名称 */
#define SLE_TREE_DATA_TYPE_TX_POWER_LEVEL             0x0C  /* 广播发送功率 */
#define SLE_TREE_DATA_TYPE_MANUFACTURER_SPECIFIC      0xFF  /* 厂商自定义信息 */

/*--------------------------------------------------------------------------
 * Fixed advertise mode values
 * 固定广播模式值
 *--------------------------------------------------------------------------*/
/* Discovery/announce defaults used by this demo.
 * 本 demo 固定使用的 discovery / announce 参数。 */
#define SLE_TREE_DISCOVERY_LEVEL_NORMAL               SLE_ANNOUNCE_LEVEL_NORMAL
#define SLE_TREE_ADV_CHANNEL_MAP_DEFAULT              0x07

typedef enum {
    /* Heartbeat used for route learning and liveness refresh.
     * 心跳帧，用于路由学习和在线刷新。 */
    SLE_TREE_FRAME_TYPE_HEARTBEAT = 1,
    /* Real transparent payload carried end-to-end in the tree.
     * 真实业务透传帧。 */
    SLE_TREE_FRAME_TYPE_DATA = 2,
    /* Relay topology summary uploaded toward root.
     * relay 向 root 上报的拓扑摘要帧。 */
    SLE_TREE_FRAME_TYPE_TOPO_SUMMARY = 3,
    /* Depth update notification from parent to children after reconnection.
     * 父节点重连后深度变化时通知子节点更新深度。payload: 1 byte new_depth */
    SLE_TREE_FRAME_TYPE_DEPTH_UPDATE = 4,
} sle_tree_frame_type_t;

typedef struct {
    uint8_t frame_type;
    uint8_t src_role;
    uint16_t src_node_id;
    uint16_t dst_node_id;
    uint16_t seq;
    uint16_t payload_len;
    const uint8_t *payload;
} sle_tree_frame_view_t;

typedef struct {
    bool in_use;
    uint16_t conn_id;
    sle_addr_t addr;
    uint16_t direct_node_id;
    uint8_t direct_role;
} sle_tree_child_link_t;

typedef struct {
    bool in_use;
    uint16_t node_id;
    uint16_t next_hop_conn_id;
    uint8_t node_role;
    uint32_t last_seen_ms;
} sle_tree_route_t;

typedef struct {
    bool in_use;
    sle_addr_t addr;
    uint16_t node_id;
    uint16_t root_node_id;
    uint8_t role;
    uint8_t depth;
    uint8_t free_slots;
    int8_t rssi;
    char local_name[SLE_TREE_NAME_MAX_LEN + 1];
} sle_tree_candidate_t;

typedef struct {
    bool in_use;
    uint16_t node_id;
    uint8_t fail_count;
} sle_tree_failure_stat_t;

#define SLE_TREE_MAX_LOSS_STATS       16
#define SLE_TREE_LOSS_REPORT_PERIOD_MS 10000

typedef struct {
    bool in_use;
    uint16_t src_node_id;
    uint16_t last_seq;
    bool seq_initialized;
    uint32_t received_count;
    uint32_t lost_count;
    uint32_t out_of_order_count;
} sle_tree_loss_stat_t;

typedef struct {
    bool in_use;
    uint16_t node_id;
    uint16_t parent_node_id;
    uint8_t node_role;
    uint32_t last_seen_ms;
} sle_tree_topo_entry_t;

typedef struct {
    bool valid;
    sle_addr_t addr;
    uint16_t node_id;
    uint16_t root_node_id;
    uint8_t role;
    uint8_t depth;
    uint8_t free_slots;
    int8_t rssi;
} sle_tree_pending_parent_t;

typedef struct {
    bool connected;
    bool handle_ready;
    uint16_t conn_id;
    uint16_t write_handle;
    sle_addr_t addr;
    uint16_t parent_node_id;
    uint16_t root_node_id;
    uint8_t parent_role;
    uint8_t depth;
    uint8_t parent_free_slots;
    int8_t parent_rssi;
    int16_t rssi_ema;          /* RSSI 指数移动平均（×8 定点），用于平滑抖动 */
    uint64_t connected_at_ms;  /* handle_ready 时刻，用于计算连接稳定性 */
} sle_tree_uplink_t;

typedef struct {
    bool valid;
    uint16_t node_id;
    uint16_t root_node_id;
    uint8_t role;
    uint8_t depth;
    uint8_t free_slots;
    char local_name[SLE_TREE_NAME_MAX_LEN + 1];
} sle_tree_adv_info_t;

/* Uplink frame queue: buffers frames when relay uplink is unavailable. */
#define SLE_TREE_FRAME_QUEUE_LEN 16

typedef struct {
    uint8_t data[SLE_TREE_MAX_FRAME_LEN];
    uint16_t len;
} sle_tree_frame_entry_t;

typedef struct {
    sle_tree_frame_entry_t entries[SLE_TREE_FRAME_QUEUE_LEN];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} sle_tree_frame_queue_t;

typedef struct {
    sle_tree_cfg_t cfg;
    sle_tree_uplink_t uplink;
    sle_tree_child_link_t children[SLE_TREE_MAX_CHILDREN];
    sle_tree_route_t routes[SLE_TREE_MAX_ROUTES];
    sle_tree_candidate_t candidates[SLE_TREE_MAX_CANDIDATES];
    sle_tree_failure_stat_t failure_stats[SLE_TREE_MAX_FAILURE_STATS];
    sle_tree_topo_entry_t topo_entries[SLE_TREE_MAX_TOPO_NODES];
    sle_tree_frame_queue_t frame_queue;
    sle_tree_pending_parent_t pending_parent;
    uint8_t role;
    uint8_t prev_depth; /* relay 断连前的深度，用于重连后检测深度变化 */
    uint8_t server_id;
    uint16_t service_handle;
    uint16_t property_handle;
    bool announce_started;
    bool seeking;
    bool sle_enabled;
    bool server_ready;
    bool start_scan_pending;
    bool reparent_pending;
    bool optimize_scan_active;
    uint16_t next_seq;
    uint16_t next_data_seq;
    uint64_t scan_start_ms;
    uint64_t parent_connect_start_ms;
    uint64_t rescan_due_ms;
    uint64_t last_heartbeat_ms;
    uint64_t last_auto_traffic_ms;
    uint64_t last_optimize_scan_ms;
    uint64_t last_topo_summary_ms;
    uint64_t last_topo_print_ms;
    uint8_t uart_rx_ring[SLE_TREE_UART_RX_RING_SIZE];
    uint16_t uart_rx_read_pos;
    uint16_t uart_rx_write_pos;
    char uart_line[SLE_TREE_UART_LINE_MAX];
    uint16_t uart_line_len;
    bool uart_rx_overflow;
    sle_tree_loss_stat_t loss_stats[SLE_TREE_MAX_LOSS_STATS];
    uint64_t last_loss_report_ms;
} sle_tree_ctx_t;

/*--------------------------------------------------------------------------
 * Global runtime objects
 * 全局运行时对象
 *--------------------------------------------------------------------------*/
extern sle_tree_ctx_t g_sle_tree_ctx;
extern const uint8_t g_sle_tree_uuid_base[SLE_UUID_LEN];

/*--------------------------------------------------------------------------
 * Common runtime helpers
 * 通用运行时辅助接口
 *--------------------------------------------------------------------------*/
/* Return role name string for startup log and debug print.
 * 返回角色名字符串，用于启动日志和调试打印。 */
const char *sle_tree_role_name(uint8_t role);
/* Read current millisecond tick from systick.
 * 读取当前毫秒级 systick。 */
uint64_t sle_tree_now_ms(void);
uint16_t sle_tree_node_id_from_addr(const sle_addr_t *addr);
/* Decode/encode uint16 in little-endian tree frame format.
 * 按树状网络帧格式进行 uint16 小端编解码。 */
uint16_t sle_tree_get_le16(const uint8_t *data);
void sle_tree_put_le16(uint8_t *data, uint16_t value);
/* Count current direct child links on root / relay.
 * 统计当前 root / relay 的直连子节点数。 */
uint8_t sle_tree_count_children(void);
uint8_t sle_tree_max_children_for_role(void);
/* Compare two SLE addresses by type and address bytes.
 * 比较两个 SLE 地址是否完全相同。 */
bool sle_tree_addr_equal(const sle_addr_t *left, const sle_addr_t *right);
/* Check whether current role owns server/client behavior.
 * 判断当前角色是否具备 server/client 行为。 */
bool sle_tree_role_has_server(void);
bool sle_tree_role_has_client(void);

/*--------------------------------------------------------------------------
 * Config / NV helpers
 * 配置与 NV 辅助接口
 *--------------------------------------------------------------------------*/
/* Load and store tree config from NV, applying defaults if needed.
 * 从 NV 加载/保存树状网络配置，必要时回退默认值。 */
void sle_tree_load_cfg(void);
void sle_tree_save_cfg(void);
/* Read factory SLE MAC from NV into address struct for logging/setup.
 * 从 NV 读取工厂 SLE MAC，转换成地址结构体，供日志和地址设置使用。 */
bool sle_tree_get_factory_sle_addr(sle_addr_t *addr);
/* Set local SLE MAC from factory NV so one-to-many demo can distinguish nodes.
 * 从工厂 NV 设置本机 SLE MAC，保证一对多 demo 能区分不同设备。 */
void sle_tree_set_local_addr_from_nv(void);
/* Fill base UUID with 16-bit short UUID value at tail bytes.
 * 将 16-bit 短 UUID 写入基础 UUID 的尾部。 */
void sle_tree_uuid_set_u16(uint16_t value, sle_uuid_t *uuid);

/*--------------------------------------------------------------------------
 * UART / protocol helpers
 * UART / 协议辅助接口
 *--------------------------------------------------------------------------*/
/* Print to kernel log and default UART bus0 at the same time.
 * 同时输出到 kernel log 和默认 UART bus0。 */
void sle_tree_uart_printf(const char *fmt, ...);
/* Convert NV name buffer to printable C string.
 * 把 NV 中的定长 name 缓冲区转成普通 C 字符串。 */
void sle_tree_get_cfg_name(const uint8_t *src, char *dst, uint32_t dst_len);
/* Parse advertisement payload into role/node/free_slots/local_name view.
 * 解析广播数据，提取 role/node_id/free_slots/local_name。 */
bool sle_tree_parse_adv_data(const uint8_t *data, uint8_t data_len, sle_tree_adv_info_t *info);
/* Route table CRUD and child link management helpers.
 * 路由表和子连接管理接口。 */
sle_tree_route_t *sle_tree_find_route(uint16_t node_id);
void sle_tree_learn_route(uint16_t node_id, uint8_t node_role, uint16_t conn_id);
void sle_tree_remove_stale_routes(void);
sle_tree_child_link_t *sle_tree_alloc_child(uint16_t conn_id, const sle_addr_t *addr);
sle_tree_child_link_t *sle_tree_find_child_by_conn(uint16_t conn_id);
void sle_tree_remove_child(uint16_t conn_id);
void sle_tree_relay_drop_all_children(const char *reason);
void sle_tree_send_depth_update_to_children(uint8_t new_depth);
void sle_tree_frame_queue_enqueue(const uint8_t *data, uint16_t len);
bool sle_tree_frame_queue_dequeue(uint8_t *buf, uint16_t *len);
void sle_tree_frame_queue_flush(void);
bool sle_tree_mark_child_identity(uint16_t conn_id, uint16_t node_id, uint8_t node_role);
void sle_tree_root_touch_direct_child(uint16_t node_id, uint8_t node_role);
void sle_tree_root_remove_direct_child(uint16_t node_id);
void sle_tree_root_touch_route_activity(uint16_t node_id, uint8_t node_role, uint16_t conn_id);
bool sle_tree_root_process_topo_summary(const uint8_t *payload, uint16_t payload_len);
void sle_tree_root_refresh_topo_activity(uint16_t node_id);
void sle_tree_root_remove_stale_topology(void);
void sle_tree_root_print_topology_tree(void);

/* Build/parse internal tree frames used over SLE property transport.
 * 构造/解析通过 SLE property 传输的内部树状帧。 */
uint16_t sle_tree_build_frame(uint8_t frame_type, uint16_t dst_node_id, const uint8_t *payload,
    uint16_t payload_len, uint8_t *buffer, uint16_t buffer_len);
bool sle_tree_parse_frame(const uint8_t *buffer, uint16_t buffer_len, sle_tree_frame_view_t *frame);

/* UART HEX helpers and high-level transfer/report functions.
 * UART HEX 解析和高层收发/打印接口。 */
int32_t sle_tree_hex_value(char value);
void sle_tree_report_data(uint16_t src_node_id, const uint8_t *data, uint16_t data_len);
void sle_tree_report_heartbeat(uint16_t src_node_id, uint16_t via_conn_id);
void sle_tree_report_unreachable(uint16_t dst_node_id);

/* Packet loss rate tracking / 丢包率统计 */
void sle_tree_loss_update(uint16_t src_node_id, uint16_t seq);
void sle_tree_loss_report(void);
void sle_tree_loss_reset(void);
errcode_t sle_tree_send_notify(uint16_t conn_id, const uint8_t *data, uint16_t data_len);
errcode_t sle_tree_send_uplink_write(const uint8_t *data, uint16_t data_len);
sle_tree_route_t *sle_tree_pick_first_node_route(void);
void sle_tree_send_local_data(uint16_t dst_node_id, const uint8_t *payload, uint16_t payload_len);
void sle_tree_send_heartbeat(void);
void sle_tree_send_topo_summary(void);

/*--------------------------------------------------------------------------
 * Link management helpers
 * 链路管理辅助接口
 *--------------------------------------------------------------------------*/
/* Advertising, scan candidate, and reconnect helpers.
 * 广播、扫描候选、重连相关接口。 */
void sle_tree_refresh_advertising(void);
void sle_tree_store_candidate(const sle_addr_t *addr, const sle_tree_adv_info_t *info, int8_t rssi);
sle_tree_candidate_t *sle_tree_pick_best_candidate(void);
void sle_tree_prepare_pending_parent(const sle_tree_candidate_t *candidate);
void sle_tree_cache_pending_parent_to_uplink(void);
void sle_tree_start_scan(void);
void sle_tree_schedule_rescan(uint32_t delay_ms);
void sle_tree_reset_rescan_count(void);
bool sle_tree_should_optimize_parent(uint64_t now_ms);
void sle_tree_try_migrate_parent(const sle_tree_candidate_t *best);
uint8_t sle_tree_get_parent_fail_count(uint16_t node_id);
void sle_tree_mark_parent_connect_failed(uint16_t node_id);
void sle_tree_reset_parent_fail_count(uint16_t node_id);
void sle_tree_connect_param_init(void);
void sle_tree_uart_register_rx_callback(void);
void sle_tree_handle_uart_input(void);

/*--------------------------------------------------------------------------
 * Common data path dispatch
 * 通用数据路径分发
 *--------------------------------------------------------------------------*/
/* Deliver parsed frame to local application / role specific forwarding path.
 * 把已解析帧分发给本地处理或角色专属转发逻辑。 */
void sle_tree_handle_frame_to_self(const sle_tree_frame_view_t *frame, uint16_t via_conn_id);
void sle_tree_handle_frame_from_child(uint16_t conn_id, const uint8_t *data, uint16_t data_len);
void sle_tree_handle_frame_from_parent(const uint8_t *data, uint16_t data_len);
bool sle_tree_is_uplink_addr(const sle_addr_t *addr);
void sle_tree_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state);
void sle_tree_handle_uplink_disconnected(void);
void sle_tree_try_send_auto_traffic(void);
void sle_tree_role_tick(uint64_t now_ms);
void sle_tree_handle_seek_result(sle_seek_result_info_t *result);

/*--------------------------------------------------------------------------
 * Role-specific behavior: root
 * 角色专属逻辑：root
 *--------------------------------------------------------------------------*/
void sle_tree_root_handle_frame_from_child(uint16_t conn_id, const sle_tree_frame_view_t *frame,
    const uint8_t *data, uint16_t data_len);
void sle_tree_root_handle_frame_from_parent(const sle_tree_frame_view_t *frame);
bool sle_tree_root_is_uplink_addr(const sle_addr_t *addr);
void sle_tree_root_try_send_auto_traffic(void);
void sle_tree_root_role_tick(uint64_t now_ms);

/*--------------------------------------------------------------------------
 * Role-specific behavior: relay
 * 角色专属逻辑：relay
 *--------------------------------------------------------------------------*/
void sle_tree_relay_handle_frame_from_child(uint16_t conn_id, const sle_tree_frame_view_t *frame,
    const uint8_t *data, uint16_t data_len);
void sle_tree_relay_handle_frame_from_parent(const sle_tree_frame_view_t *frame, const uint8_t *data,
    uint16_t data_len);
bool sle_tree_relay_is_uplink_addr(const sle_addr_t *addr);
void sle_tree_relay_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state);
void sle_tree_relay_handle_uplink_disconnected(void);
void sle_tree_relay_try_send_auto_traffic(void);
void sle_tree_relay_role_tick(uint64_t now_ms);
void sle_tree_relay_handle_seek_result(sle_seek_result_info_t *result, const sle_tree_adv_info_t *adv_info);

/*--------------------------------------------------------------------------
 * Role-specific behavior: leaf
 * 角色专属逻辑：leaf
 *--------------------------------------------------------------------------*/
void sle_tree_leaf_handle_frame_from_parent(const sle_tree_frame_view_t *frame);
bool sle_tree_leaf_is_uplink_addr(const sle_addr_t *addr);
void sle_tree_leaf_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state);
void sle_tree_leaf_handle_uplink_disconnected(void);
void sle_tree_leaf_try_send_auto_traffic(void);
void sle_tree_leaf_role_tick(uint64_t now_ms);
void sle_tree_leaf_handle_seek_result(sle_seek_result_info_t *result, const sle_tree_adv_info_t *adv_info);

#endif
