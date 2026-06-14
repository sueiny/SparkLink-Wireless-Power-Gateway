#include "modbus_sim.h"
#include <math.h>
#include <string.h>

/* Modbus CRC16 计算 */
static uint16_t crc16_modbus(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* 仿真参数 - 与 gatewayd MockDataSource 对齐 */
#define BASE_VOLTAGE      220.0
#define BASE_CURRENT      2.4
#define CURRENT_STEP      1.05
#define BRANCH_PHASE_OFFSET 0.85
#define BRANCH_TICK_PHASE   0.33
#define BASE_FREQUENCY    50.0
#define BASE_TEMPERATURE  28.0
#define BASE_HUMIDITY     60.0

/* 全局 tick 计数 */
static uint64_t g_tick = 0;

/* 能量累计 */
static double g_energy[32] = {0};

/* 发布间隔（毫秒） */
#define PUBLISH_INTERVAL_MS 5000

void modbus_sim_init(int server_count)
{
    (void)server_count;
    g_tick = 0;
    memset(g_energy, 0, sizeof(g_energy));
}

/* 四舍五入到小数点后 2 位 */
static double round2(double value)
{
    return round(value * 100.0) / 100.0;
}

/* 写入 16 位寄存器（大端） */
static void write_reg(uint8_t *buf, uint16_t val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

/* 写入 32 位浮点数为两个 16 位寄存器（简化：整数部分 * 100） */
static void write_float_as_regs(uint8_t *buf, double val)
{
    /* 防止 int32_t 溢出导致未定义行为 */
    double scaled_d = val * 100.0;
    int32_t scaled;
    if (scaled_d > 2147483647.0)
        scaled = 2147483647;
    else if (scaled_d < -2147483648.0)
        scaled = -2147483648;
    else
        scaled = (int32_t)scaled_d;
    write_reg(buf, (uint16_t)((scaled >> 16) & 0xFFFF));
    write_reg(buf + 2, (uint16_t)(scaled & 0xFFFF));
}

bool modbus_sim_generate(uint64_t tick, int server_index, int modbus_type,
    uint8_t *out_buf, uint16_t *out_len)
{
    if (!out_buf || !out_len || server_index < 0 || server_index >= 32)
        return false;

    g_tick = tick;

    /* Modbus RTU 帧头 */
    uint8_t addr = (uint8_t)(server_index + 1);
    uint8_t func = 0x03; /* Read Holding Registers */

    double phase = tick * BRANCH_TICK_PHASE + server_index * BRANCH_PHASE_OFFSET;

    switch (modbus_type) {
    case MODBUS_TYPE_METER: {
        /* 单相电表：8 个寄存器（与 modbus_parser.cpp 对齐）
         * 寄存器布局：V(1) I(1) P(1) PF(1) F(1) E_hi(1) E_lo(1) relay(1)
         */
        double voltage = BASE_VOLTAGE + sin(phase) * 1.8;
        double current = BASE_CURRENT + server_index * CURRENT_STEP +
                         (sin(phase * 0.7) + 1.0) * 0.75;
        double power_factor = 0.94 + sin(phase * 0.2) * 0.02;
        double active_power = voltage * current * power_factor;
        double frequency = BASE_FREQUENCY + sin(phase * 0.15) * 0.03;

        /* 能量累计 */
        double hours = PUBLISH_INTERVAL_MS / 3600000.0;
        g_energy[server_index] += active_power * hours / 1000.0;

        /* 构建帧 */
        out_buf[0] = addr;
        out_buf[1] = func;
        out_buf[2] = 16; /* 8 个寄存器 * 2 字节 = 16 字节 */

        /* 寄存器 0: 电压 / 0.1 (解析器用 kMeterVoltageScale=0.1) */
        write_reg(out_buf + 3, (uint16_t)(round2(voltage) / 0.1));
        /* 寄存器 1: 电流 / 0.01 (解析器用 kMeterCurrentScale=0.01) */
        write_reg(out_buf + 5, (uint16_t)(round2(current) / 0.01));
        /* 寄存器 2: 有功功率 (16 位，已经是 W) */
        write_reg(out_buf + 7, (uint16_t)round2(active_power));
        /* 寄存器 3: 功率因数 / 0.001 (解析器用 kMeterPowerFactorScale=0.001) */
        write_reg(out_buf + 9, (uint16_t)(round2(power_factor) / 0.001));
        /* 寄存器 4: 频率 / 0.01 (解析器用 kMeterFrequencyScale=0.01) */
        write_reg(out_buf + 11, (uint16_t)(round2(frequency) / 0.01));
        /* 寄存器 5-6: 能量 (32 位) / 0.01 (解析器用 kMeterEnergyScale=0.01) */
        uint32_t energy_scaled = (uint32_t)(round2(g_energy[server_index]) / 0.01);
        write_reg(out_buf + 13, (uint16_t)((energy_scaled >> 16) & 0xFFFF));
        write_reg(out_buf + 15, (uint16_t)(energy_scaled & 0xFFFF));
        /* 寄存器 7: 继电器状态 (0x55 = 开) */
        write_reg(out_buf + 17, 0x55);

        /* CRC16 */
        uint16_t frame_len = 3 + 16; /* addr + func + bytes(1) + 8 寄存器 * 2 字节 */
        uint16_t crc = crc16_modbus(out_buf, frame_len);
        out_buf[frame_len] = crc & 0xFF;
        out_buf[frame_len + 1] = (crc >> 8) & 0xFF;
        *out_len = frame_len + 2;
        break;
    }

    case MODBUS_TYPE_ENV: {
        /* 温湿度传感器：2 个寄存器（与 modbus_parser.cpp 对齐）
         * 寄存器布局：humidity(1) temperature(1)
         * 解析器先读 humidity 后读 temperature
         */
        double temperature = BASE_TEMPERATURE + sin(phase) * 1.6;
        double humidity = BASE_HUMIDITY + cos(phase * 0.8) * 3.2;

        /* 构建帧 */
        out_buf[0] = addr;
        out_buf[1] = func;
        out_buf[2] = 4; /* 2 个寄存器 * 2 字节 = 4 字节 */

        /* 寄存器 0: 湿度 / 0.1 (解析器先读 humidity) */
        write_reg(out_buf + 3, (uint16_t)(round2(humidity) / 0.1));
        /* 寄存器 1: 温度 / 0.1 (解析器后读 temperature) */
        write_reg(out_buf + 5, (uint16_t)(round2(temperature) / 0.1));

        /* CRC16 */
        uint16_t frame_len = 3 + 4;
        uint16_t crc = crc16_modbus(out_buf, frame_len);
        out_buf[frame_len] = crc & 0xFF;
        out_buf[frame_len + 1] = (crc >> 8) & 0xFF;
        *out_len = frame_len + 2;
        break;
    }

    case MODBUS_TYPE_RELAY: {
        /* 继电器：使用 func_code 0x01（读线圈状态） */
        uint8_t relay_state = (tick / 12) % 2;

        /* 构建帧 */
        out_buf[0] = addr;
        out_buf[1] = 0x01; /* func_code: read coils */
        out_buf[2] = 1;    /* 1 字节 = 8 个线圈 */

        /* 线圈数据：bit0 = 继电器状态 */
        out_buf[3] = relay_state;

        /* CRC16 */
        uint16_t frame_len = 3 + 1;
        uint16_t crc = crc16_modbus(out_buf, frame_len);
        out_buf[frame_len] = crc & 0xFF;
        out_buf[frame_len + 1] = (crc >> 8) & 0xFF;
        *out_len = frame_len + 2;
        break;
    }

    default:
        return false;
    }

    return true;
}
