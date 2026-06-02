#!/usr/bin/env python3
"""
DTU节点一一对应设备测试

每个设备配一个DTU节点，都挂载到DTU网关
电表形成二叉树
"""

import json, time, sys, random
import paho.mqtt.client as mqtt
import requests, urllib3
urllib3.disable_warnings()

BROKER = "thingskit.aiotcomm.com.cn"
PORT = 11883
CLIENT_ID = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
USERNAME = "123"
PASSWORD = "123"
HTTP_BASE = "https://thingskit.aiotcomm.com.cn"

# 设备和DTU一一对应
DEVICE_DTU_MAP = {
    "METER_001": "DTU_001",
    "METER_002": "DTU_002",
    "METER_003": "DTU_003",
    "METER_004": "DTU_004",
    "METER_005": "DTU_005",
    "METER_006": "DTU_006",
    "METER_007": "DTU_007",
    "ENV_001": "DTU_008",
    "ENV_002": "DTU_009",
    "RELAY_001": "DTU_010",
    "RELAY_002": "DTU_011",
}

# 电表二叉树
METER_TREE = {
    "METER_001": None,
    "METER_002": "METER_001",
    "METER_003": "METER_001",
    "METER_004": "METER_002",
    "METER_005": "METER_002",
    "METER_006": "METER_003",
    "METER_007": "METER_003",
}

# DTU拓扑树（parent_mac, child_macs）
GW_MAC = "00:11:22:33:44:55"
DTU_TOPOLOGY = {
    "DTU_001": {"parent": GW_MAC, "children": "AA:BB:CC:DD:02:01,AA:BB:CC:DD:03:01"},
    "DTU_002": {"parent": "AA:BB:CC:DD:01:01", "children": "AA:BB:CC:DD:04:01,AA:BB:CC:DD:05:01"},
    "DTU_003": {"parent": "AA:BB:CC:DD:01:01", "children": "AA:BB:CC:DD:06:01,AA:BB:CC:DD:07:01"},
    "DTU_004": {"parent": "AA:BB:CC:DD:02:01", "children": ""},
    "DTU_005": {"parent": "AA:BB:CC:DD:02:01", "children": ""},
    "DTU_006": {"parent": "AA:BB:CC:DD:03:01", "children": ""},
    "DTU_007": {"parent": "AA:BB:CC:DD:03:01", "children": ""},
    "DTU_008": {"parent": GW_MAC, "children": ""},
    "DTU_009": {"parent": GW_MAC, "children": ""},
    "DTU_010": {"parent": GW_MAC, "children": ""},
    "DTU_011": {"parent": GW_MAC, "children": ""},
}


def get_device_id(headers, name):
    r = requests.get(f"{HTTP_BASE}/api/tenant/devices?pageSize=300&page=0",
                     headers=headers, verify=False)
    if r.status_code != 200:
        return ""
    for device in r.json().get("data", []):
        if device.get("name") == name:
            return device.get("id", {}).get("id", "")
    return ""


def gen_meter(name):
    v = round(random.uniform(218, 222), 1)
    i = round(random.uniform(2, 8), 2)
    data = {
        "voltage": v, "current": i,
        "active_power": round(v * i * 0.96, 1),
        "power_factor": round(random.uniform(0.95, 0.99), 2),
        "frequency": round(random.uniform(49.8, 50.2), 1),
        "energy": round(random.uniform(100, 5000), 2),
        "online": True,
        "relay_status": True,  # 合闸
    }
    parent = METER_TREE.get(name)
    if parent:
        data["parent_meter_id"] = parent
        data["meter_role"] = False  # 支表
    else:
        data["meter_role"] = True  # 总表
        data["branch_power_sum"] = round(data["active_power"] * 0.85, 1)
        data["power_loss"] = round(data["active_power"] * 0.15, 1)
        data["loss_rate"] = round(random.uniform(5, 15), 1)
    return data


def gen_env():
    return {"temperature": round(random.uniform(20, 35), 1), "humidity": round(random.uniform(40, 80), 1), "online": True}


def gen_relay():
    return {"relay_state": random.choice([0, 1]), "control_mode": 0, "online": True}


def gen_dtu(dtu_name, child_device_name):
    # 从DTU拓扑表获取父节点和子节点
    topo = DTU_TOPOLOGY.get(dtu_name, {"parent": GW_MAC, "children": ""})
    child_count = len([c for c in topo["children"].split(",") if c]) if topo["children"] else 0
    
    # STRUCT类型 - 拓扑信息
    topology = {
        "parent_mac": topo["parent"],
        "child_count": child_count,
        "child_macs": topo["children"]
    }
    if child_device_name.startswith("METER_"):
        device_type = 2
    elif child_device_name.startswith("ENV_"):
        device_type = 3
    elif child_device_name.startswith("RELAY_"):
        device_type = 4
    else:
        device_type = 0

    # STRUCT类型 - 采集配置（包含预设地址和类型）
    collect_config = {
        "modbus_count": 1,
        "collect_cycle": 5000,
        "addr_1": 1, "type_1": device_type,
        "addr_2": 0, "type_2": 0,
        "addr_3": 0, "type_3": 0,
        "addr_4": 0, "type_4": 0,
        "addr_5": 0, "type_5": 0,
        "addr_6": 0, "type_6": 0,
        "addr_7": 0, "type_7": 0,
        "addr_8": 0, "type_8": 0
    }
    
    idx = int(dtu_name.split("_")[1])
    return {
        "role": 1 if topo["parent"] != GW_MAC else 0,
        "mac": f"AA:BB:CC:DD:{idx:02d}:01",
        "name": dtu_name,
        "online": True,
        "uptime": random.randint(1000, 50000),
        "topology": topology,
        "collect_config": collect_config
    }


def run(rounds=5):
    connected = False

    def on_connect(c, ud, fl, rc):
        nonlocal connected
        if rc == 0:
            connected = True

    client = mqtt.Client(client_id=CLIENT_ID, protocol=mqtt.MQTTv311)
    client.username_pw_set(USERNAME, PASSWORD)
    client.on_connect = on_connect
    client.connect(BROKER, PORT, keepalive=60)
    client.loop_start()
    time.sleep(3)

    if not connected:
        print("[ERROR] MQTT连接失败")
        return

    print("[OK] MQTT连接成功\n")

    for i in range(1, rounds + 1):
        ts = int(time.time() * 1000)

        # 网关属性
        gw = {
            "network_type": random.choice(["ethernet", "wifi"]),
            "network_ifname": random.choice(["eth0", "wlan0"]),
            "cloud_connected": True,
            "device_count": 22,
            "cache_count": 0,
            "gateway_version": "1.0.0"
        }
        # MQTT发送到CLIENT_SCOPE
        client.publish("v1/devices/me/attributes", json.dumps(gw), qos=1)
        # 同时发送遥测，方便在设备遥测数据页验证网关测试数据。
        client.publish("v1/devices/me/telemetry", json.dumps(gw), qos=1)
        
        # HTTP API发送到SERVER_SCOPE
        try:
            token_resp = requests.post(f"{HTTP_BASE}/api/auth/login", 
                json={"username":"1","password":"Sztu@123456"}, verify=False)
            token = token_resp.json().get('token')
            h = {'Authorization': f'Bearer {token}', 'Content-Type': 'application/json'}
            gw_id = get_device_id(h, "dtu网关")
            if gw_id:
                requests.post(f"{HTTP_BASE}/api/plugins/telemetry/DEVICE/{gw_id}/attributes/SERVER_SCOPE",
                    json=gw, headers=h, verify=False)
        except:
            pass

        # 子设备数据
        payload = {}
        for dev_name, dtu_name in DEVICE_DTU_MAP.items():
            # DTU节点数据
            payload[dtu_name] = [{"ts": ts, "values": gen_dtu(dtu_name, dev_name)}]

            # 设备数据
            if "METER" in dev_name:
                payload[dev_name] = [{"ts": ts, "values": gen_meter(dev_name)}]
            elif "ENV" in dev_name:
                payload[dev_name] = [{"ts": ts, "values": gen_env()}]
            elif "RELAY" in dev_name:
                payload[dev_name] = [{"ts": ts, "values": gen_relay()}]

        client.publish("v1/gateway/telemetry", json.dumps(payload), qos=1)

        print(f"[Round {i}]")
        print(f"  网关: {gw['network_type']}/{gw['network_ifname']}")
        print(f"  设备数: 11个DTU + 7电表 + 2变送器 + 2继电器 = 22台")
        print(f"  电表树: METER_001→METER_002/003→METER_004/005/006/007")
        print()
        time.sleep(3)

    client.loop_stop()
    client.disconnect()
    print("[OK] 测试完成")


if __name__ == "__main__":
    run(int(sys.argv[1]) if len(sys.argv) > 1 else 5)
