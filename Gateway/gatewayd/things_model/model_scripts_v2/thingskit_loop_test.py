#!/usr/bin/env python3
"""
ThingsKit 循环数据上传测试

功能：
1. 循环发送不同数据
2. 验证数据是否正确显示

使用方法：
python3 thingskit_loop_test.py [循环次数]
"""

import json
import time
import sys
import random
try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False
    print("[ERROR] 请安装paho-mqtt: pip3 install paho-mqtt")
    sys.exit(1)

import requests
import urllib3
urllib3.disable_warnings()


class LoopTester:
    """循环测试器"""
    
    def __init__(self):
        # MQTT配置
        self.mqtt_host = "thingskit.aiotcomm.com.cn"
        self.mqtt_port = 11883
        self.mqtt_client_id = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
        self.mqtt_username = "123"
        self.mqtt_password = "123"
        
        # HTTP配置
        self.http_base = "https://thingskit.aiotcomm.com.cn"
        self.http_username = "1"
        self.http_password = "Sztu@123456"
        
        self.connected = False
    
    def mqtt_connect(self):
        """连接MQTT"""
        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                self.connected = True
        
        self.client = mqtt.Client(client_id=self.mqtt_client_id)
        self.client.username_pw_set(self.mqtt_username, self.mqtt_password)
        self.client.on_connect = on_connect
        self.client.connect(self.mqtt_host, self.mqtt_port, 60)
        self.client.loop_start()
        time.sleep(2)
        return self.connected
    
    def mqtt_disconnect(self):
        """断开MQTT"""
        self.client.loop_stop()
        self.client.disconnect()
    
    def generate_meter_data(self, device_id: str, is_main: bool = False) -> dict:
        """生成电表数据"""
        ts = int(time.time() * 1000)
        voltage = round(random.uniform(218, 222), 1)
        current = round(random.uniform(2, 8), 1)
        power = round(voltage * current * random.uniform(0.9, 1.0), 1)
        power_factor = round(random.uniform(0.95, 0.99), 2)
        energy = round(random.uniform(100, 2000), 2)
        
        data = {
            "voltage": voltage,
            "current": current,
            "active_power": power,
            "power_factor": power_factor,
            "frequency": round(random.uniform(49.8, 50.2), 1),
            "energy": energy,
            "online": True,
            "relay_status": True,
            "meter_role": True if is_main else False
        }
        
        if is_main:
            data["branch_power_sum"] = round(power * 0.85, 1)
            data["power_loss"] = round(power * 0.15, 1)
            data["loss_rate"] = round(random.uniform(5, 15), 1)
        else:
            # 支表：需要parent_meter_id
            data["parent_meter_id"] = "METER_001"
        
        return {"ts": ts, "values": data}
    
    def generate_env_data(self) -> dict:
        """生成温湿度数据"""
        ts = int(time.time() * 1000)
        return {
            "ts": ts,
            "values": {
                "temperature": round(random.uniform(20, 35), 1),
                "humidity": round(random.uniform(40, 80), 1),
                "online": True
            }
        }
    
    def generate_relay_data(self) -> dict:
        """生成继电器数据"""
        ts = int(time.time() * 1000)
        return {
            "ts": ts,
            "values": {
                "relay_state": random.choice([0, 1]),
                "control_mode": random.choice([0, 1]),
                "online": True
            }
        }
    
    def generate_dtu_data(self) -> dict:
        """生成DTU节点数据"""
        ts = int(time.time() * 1000)
        return {
            "ts": ts,
            "values": {
                "role": 0,
                "mac": "A1:A2:A3:A4:A5:A6",
                "name": "DTU_001",
                "online": True,
                "uptime": random.randint(1000, 10000),
                "topology": {
                    "parent_mac": "",
                    "child_count": 2,
                    "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6"
                },
                "collect_config": {
                    "modbus_count": 3,
                    "collect_cycle": 5000,
                    "addr_1": 1, "type_1": 2,
                    "addr_2": 2, "type_2": 3,
                    "addr_3": 3, "type_3": 4,
                    "addr_4": 0, "type_4": 0,
                    "addr_5": 0, "type_5": 0,
                    "addr_6": 0, "type_6": 0,
                    "addr_7": 0, "type_7": 0,
                    "addr_8": 0, "type_8": 0
                }
            }
        }
    
    def send_data(self, round_num: int):
        """发送一轮数据"""
        # DTU网关属性
        gw_data = {
            "network_type": random.choice(["ethernet", "wifi", "4g"]),
            "network_ifname": random.choice(["eth0", "wlan0", "ppp0"]),
            "cloud_connected": True,
            "device_count": 22,
            "cache_count": random.randint(0, 5),
            "gateway_version": "1.0.0"
        }
        
        # MQTT发送到CLIENT_SCOPE
        self.client.publish("v1/devices/me/attributes", json.dumps(gw_data), qos=1)
        
        # HTTP API发送到SERVER_SCOPE
        try:
            token_resp = requests.post(f"{self.http_base}/api/auth/login", 
                json={"username": self.http_username, "password": self.http_password}, verify=False)
            token = token_resp.json().get('token')
            h = {'Authorization': f'Bearer {token}', 'Content-Type': 'application/json'}
            gw_id = self.get_device_id(h, "dtu网关")
            if gw_id:
                requests.post(f"{self.http_base}/api/plugins/telemetry/DEVICE/{gw_id}/attributes/SERVER_SCOPE",
                    json=gw_data, headers=h, verify=False)
        except:
            pass
        
        # 子设备遥测
        payload = {
            "METER_001": [self.generate_meter_data("METER_001", True)],
            "METER_002": [self.generate_meter_data("METER_002")],
            "METER_003": [self.generate_meter_data("METER_003")],
            "METER_004": [self.generate_meter_data("METER_004")],
            "METER_005": [self.generate_meter_data("METER_005")],
            "METER_006": [self.generate_meter_data("METER_006")],
            "METER_007": [self.generate_meter_data("METER_007")],
            "ENV_001": [self.generate_env_data()],
            "ENV_002": [self.generate_env_data()],
            "RELAY_001": [self.generate_relay_data()],
            "RELAY_002": [self.generate_relay_data()],
            "DTU_001": [self.generate_dtu_data()]
        }
        self.client.publish("v1/gateway/telemetry", json.dumps(payload))
        
        meter = payload["METER_001"][0]["values"]
        env = payload["ENV_001"][0]["values"]
        
        print(f"\n[Round {round_num}]")
        print(f"  网关: network={gw_data['network_type']}, ifname={gw_data['network_ifname']}, devices={gw_data['device_count']}")
        print(f"  总表: V={meter['voltage']}V, I={meter['current']}A, P={meter['active_power']}W, E={meter['energy']}kWh")
        print(f"  支表: parent_meter_id=METER_001")
        print(f"  温湿度: T={env['temperature']}°C, H={env['humidity']}%")
    
    def get_device_id(self, headers: dict, name: str) -> str:
        """按设备名查询当前平台ID，避免旧硬编码ID失效。"""
        r = requests.get(f"{self.http_base}/api/tenant/devices?pageSize=200&page=0",
                         headers=headers, verify=False)
        if r.status_code != 200:
            return ""
        for d in r.json().get("data", []):
            if d.get("name") == name:
                return d.get("id", {}).get("id", "")
        return ""
    
    def verify_data(self):
        """验证数据"""
        token_resp = requests.post(f"{self.http_base}/api/auth/login", 
            json={"username": self.http_username, "password": self.http_password},
            verify=False)
        token = token_resp.json().get('token')
        h = {'Authorization': f'Bearer {token}'}
        
        device_names = ["METER_001", "ENV_001", "RELAY_001", "DTU_001"]
        
        print("\n=== 验证最新数据 ===")
        for name in device_names:
            did = self.get_device_id(h, name)
            if not did:
                print(f"\n{name}: 未找到设备")
                continue
            r = requests.get(f"{self.http_base}/api/plugins/telemetry/DEVICE/{did}/values/timeseries", 
                headers=h, verify=False)
            if r.status_code == 200:
                data = r.json()
                print(f"\n{name}:")
                for k, v in data.items():
                    if v and k != 'values':
                        print(f"  {k}: {v[0].get('value')}")
    
    def run(self, rounds: int = 5):
        """运行循环测试"""
        print("="*60)
        print("ThingsKit 循环数据上传测试")
        print("="*60)
        
        if not self.mqtt_connect():
            print("[ERROR] MQTT连接失败")
            return
        
        print("[OK] MQTT连接成功")
        
        for i in range(1, rounds + 1):
            self.send_data(i)
            time.sleep(2)
        
        print("\n[OK] 数据发送完成")
        self.mqtt_disconnect()
        
        # 验证数据
        self.verify_data()
        
        print("\n" + "="*60)
        print("测试完成！请检查ThingsKit平台数据")
        print("="*60)


def main():
    rounds = 5
    if len(sys.argv) > 1:
        rounds = int(sys.argv[1])
    
    tester = LoopTester()
    tester.run(rounds)


if __name__ == '__main__':
    main()
