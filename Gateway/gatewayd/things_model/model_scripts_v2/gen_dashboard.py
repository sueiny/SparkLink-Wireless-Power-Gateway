# -*- coding: utf-8 -*-
"""生成DTU监控大屏JSON"""
import json, copy, uuid

def short_id():
    return uuid.uuid4().hex[:15]

def page_id():
    return short_id()

def make_component(x, y, w, h, text, font_size=18, font_color="#00ffcc", bg="#00000000", border_radius=5, z_index=1):
    """创建文字组件"""
    cid = short_id()
    return {
        "id": cid, "isGroup": False,
        "attr": {"x": x, "y": y, "w": w, "h": h, "offsetX": 0, "offsetY": 0, "zIndex": z_index},
        "styles": {"filterShow": False, "hueRotate": 0, "saturate": 1, "contrast": 1, "brightness": 1, "opacity": 1,
                    "rotateZ": 0, "rotateX": 0, "rotateY": 0, "skewX": 0, "skewY": 0, "blendMode": "normal",
                    "animations": [], "animationsStyleConfig": {"animationPlayState": "paused", "animationTimingFunction": "ease",
                    "animationDuration": 0, "animationDelay": 0, "animationIterationCount": "infinite"}},
        "preview": {"overFlowHidden": False},
        "status": {"lock": False, "hide": False},
        "request": {"type": 2, "static": {}, "subscription": {"type": "TIMESERIES", "advanced": False},
                    "api": {}, "requestInterval": 0, "requestIntervalUnit": "second"},
        "filter": "",
        "events": {"baseEvent": {"click": None, "dblclick": None, "mouseenter": None, "mouseleave": None},
                    "advancedEvents": {"vnodeMounted": None, "vnodeBeforeMount": None}, "interactEvents": []},
        "key": "TextCommon",
        "chartConfig": {"key": "TextCommon", "title": "charts.chart.text", "category": "Text",
                        "categoryName": "packages.basic.text", "package": "Basic", "dataFrame": 7, "designConfig": 3,
                        "image": "text_static.png", "platform": 0},
        "option": {"link": "", "linkHead": "http://", "fontSize": font_size, "fontColor": font_color,
                    "paddingX": 10, "paddingY": 10, "textAlign": "center", "fontWeight": "normal",
                    "borderWidth": 0, "borderColor": "#ffffff", "borderRadius": border_radius,
                    "letterSpacing": 2, "writingMode": "horizontal-tb", "backgroundColor": bg},
        "requestDataset": "",
        "rawDataset": {"type": {"type": "string"}, "dataset": text}
    }

# 基础模板（从用户导出的JSON中提取公共部分）
base = json.load(open("c:/Users/傅仁杰/Desktop/things_model/温湿度.json", "r", encoding="utf-8"))

components = []

# ====== 标题栏 ======
components.append(make_component(60, 20, 800, 60, "DTU网关综合监控大屏", 32, "#ffffff"))

# ====== 左上：网关状态 (x:20-400, y:100-300) ======
components.append(make_component(20, 100, 180, 40, "【网关状态】", 18, "#51d6a9"))
components.append(make_component(20, 150, 170, 80, "在线状态\n🔗 已连接", 16, "#00ff88", "#1a2a3a"))
components.append(make_component(200, 150, 170, 80, "网关版本\nv1.0.0", 16, "#00ccff", "#1a2a3a"))
components.append(make_component(20, 240, 170, 80, "网络接口\neth0", 16, "#00ff88", "#1a2a3a"))
components.append(make_component(200, 240, 170, 80, "下挂设备\n22 台", 16, "#ffaa00", "#1a2a3a"))

# ====== 中上：电表总览 (x:410-1100, y:100-320) ======
components.append(make_component(420, 100, 200, 40, "【电表总览】", 18, "#51d6a9"))
meter_cards = [
    (420, 150, "总有功功率\n2.8 kW", "#ffaa00"),
    (630, 150, "总电能累计\n1234 kWh", "#00ccff"),
    (840, 150, "线损功率\n0.4 kW", "#ff6688"),
    (420, 240, "电压范围\n218~222 V", "#00ff88"),
    (630, 240, "电流范围\n2.0~8.0 A", "#00ff88"),
    (840, 240, "在线电表\n7 / 7", "#00ff88"),
]
for x, y, text, color in meter_cards:
    components.append(make_component(x, y, 190, 80, text, 15, color, "#1a2a3a"))

# ====== 右上：告警事件 (x:1120-1920, y:100-550) ======
components.append(make_component(1120, 100, 200, 40, "【告警事件】", 18, "#ff4466"))
alerts = [
    "⚠ 过压告警    METER_003  252V",
    "⚠ 高温告警    ENV_001    45.0°C",
    "⚠ 高湿告警    ENV_001    92.5%",
    "⚠ 过载告警    RELAY_001  65.5A",
    "⚠ 异常跳闸    RELAY_001  过载保护",
    "⚠ 断线告警    dtu网关    网络超时",
    "ℹ 设备数量变更  dtu网关   22→21",
    "ℹ 采集失败    DTU_005   设备地址3",
]
for i, alert in enumerate(alerts):
    color = "#ff4466" if "⚠" in alert else "#ffaa00"
    components.append(make_component(1120, 150 + i * 50, 750, 42, alert, 14, color, "#1a2030"))

# ====== 中右：环境监测 (x:1120-1920, y:580-880) ======
components.append(make_component(1120, 580, 200, 40, "【环境监测】", 18, "#51d6a9"))
env_cards = [
    (1120, 630, "ENV_001 温度\n28.5 °C", "#00ff88"),
    (1310, 630, "ENV_001 湿度\n65.3 %RH", "#00ccff"),
    (1500, 630, "ENV_002 温度\n26.1 °C", "#00ff88"),
    (1690, 630, "ENV_002 湿度\n58.2 %RH", "#00ccff"),
]
for x, y, text, color in env_cards:
    components.append(make_component(x, y, 180, 80, text, 15, color, "#1a2a3a"))

# ====== 左下：DTU拓扑 (x:20-400, y:350-750) ======
components.append(make_component(20, 340, 200, 40, "【DTU拓扑网络】", 18, "#51d6a9"))
topo_lines = [
    "dtu网关 ← 根节点",
    "├─ DTU_001 ● root",
    "│  ├─ DTU_002 ● node",
    "│  │  ├─ DTU_004 ● node",
    "│  │  └─ DTU_005 ● node",
    "│  └─ DTU_003 ● node",
    "│     ├─ DTU_006 ● node",
    "│     └─ DTU_007 ● node",
    "├─ DTU_008 ● node",
    "├─ DTU_009 ● node",
    "├─ DTU_010 ● node",
    "└─ DTU_011 ● node",
]
for i, line in enumerate(topo_lines):
    color = "#00ff88" if "●" in line else "#B9B8CE"
    components.append(make_component(20, 390 + i * 32, 380, 28, line, 14, color, "#0a1520", 0))

# ====== 中下：设备状态栏 (x:420-1920, y:350-550) ======
components.append(make_component(420, 340, 200, 40, "【设备运行状态】", 18, "#51d6a9"))
status_devices = [
    ("电表", ["METER_001●", "METER_002●", "METER_003●", "METER_004●", "METER_005●", "METER_006●", "METER_007●"]),
    ("变送器", ["ENV_001●", "ENV_002●"]),
    ("继电器", ["RELAY_001●", "RELAY_002●"]),
    ("DTU节点", ["DTU_001●", "DTU_002●", "DTU_003●", "DTU_004●", "DTU_005●", "DTU_006●",
                 "DTU_007●", "DTU_008●", "DTU_009●", "DTU_010●", "DTU_011●"]),
]
y_base = 390
for group_name, devices in status_devices:
    line = " | ".join(devices)
    color = "#00ff88"
    components.append(make_component(420, y_base, 750, 32, f"{group_name}: {line}", 13, color, "#0a1520", 0))
    y_base += 36

# ====== 底部：图例 ======
components.append(make_component(500, 630, 400, 30, "● 在线  |  ○ 离线  |  ⚠ 告警  |  ℹ 通知", 14, "#B9B8CE"))

# 组装最终JSON
dashboard = json.loads(json.dumps(base))  # deep copy
dashboard["name"] = "DTU网关监控大屏"
dashboard["template"] = True

# 设置深色画布背景，确保亮色文字可见
canvas = dashboard["content"]["pageConfig"]["pageList"][0]["editCanvasConfig"]
canvas["background"] = "#0d1b2a"
canvas["chartThemeColor"] = "dark"
dashboard["content"]["pageConfig"]["pageList"][0]["componentList"] = components

output_path = "c:/Users/傅仁杰/Desktop/things_model/DTU网关监控大屏.json"
with open(output_path, "w", encoding="utf-8") as f:
    json.dump(dashboard, f, indent=2, ensure_ascii=False)

print(f"[OK] 大屏JSON已生成: {output_path}")
print(f"     共 {len(components)} 个组件")
print(f"     布局: 1920x1080")
print(f"     导入路径: ThingsKit -> 看板设计 -> 导入")
