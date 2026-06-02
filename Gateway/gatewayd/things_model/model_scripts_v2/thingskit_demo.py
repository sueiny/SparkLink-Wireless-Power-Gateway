#!/usr/bin/env python3
"""
ThingsKit 完整演示流程

功能：
1. 同步物模型
2. 创建设备
3. 发送测试数据
4. 验证结果

使用方法：
python3 thingskit_demo.py
"""

import json
import os
import sys
import time
from pathlib import Path
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class ThingsKitDemo:
    """ThingsKit演示类"""
    
    def __init__(self):
        self.base_url = "https://thingskit.aiotcomm.com.cn"
        self.username = "1"
        self.password = "Sztu@123456"
        self.token = None
        self.session = requests.Session()
        self.session.verify = False
        
        # 物模型目录
        self.model_dir = Path("/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model")
        
        # 设备配置（使用ThingsKit平台上的配置文件名称）
        self.devices = [
            {"device_id": "METER_MAIN_001", "name": "总表001", "profile_name": "单相电表"},
            {"device_id": "METER_BRANCH_001", "name": "照明支表001", "profile_name": "单相电表"},
            {"device_id": "METER_BRANCH_002", "name": "插座支表002", "profile_name": "单相电表"},
            {"device_id": "METER_BRANCH_003", "name": "动力支表003", "profile_name": "单相电表"},
            {"device_id": "ENV_001", "name": "温湿度传感器001", "profile_name": "温湿度传感器"},
            {"device_id": "RELAY_001", "name": "继电器001", "profile_name": "继电器"},
            {"device_id": "DTU_NODE_001", "name": "DTU节点001", "profile_name": "DTU节点"}
        ]
    
    def login(self) -> bool:
        """登录"""
        url = f"{self.base_url}/api/auth/login"
        data = {"username": self.username, "password": self.password}
        try:
            resp = self.session.post(url, json=data)
            if resp.status_code == 200:
                self.token = resp.json().get('token')
                self.session.headers.update({
                    'Authorization': f'Bearer {self.token}',
                    'Content-Type': 'application/json'
                })
                return True
            return False
        except:
            return False
    
    def get_profiles(self) -> dict:
        """获取设备配置文件"""
        result = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        if result.status_code == 200:
            profiles = {}
            for p in result.json().get('data', []):
                profiles[p.get('name')] = p
            return profiles
        return {}
    
    def get_devices(self) -> list:
        """获取设备列表"""
        result = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if result.status_code == 200:
            return result.json().get('data', [])
        return []
    
    def create_device(self, name: str, profile_name: str) -> bool:
        """创建设备"""
        # 获取配置文件ID
        profiles = self.get_profiles()
        profile = profiles.get(profile_name)
        if not profile:
            print(f"  [WARN] 未找到配置文件: {profile_name}")
            return False
        
        profile_id = profile.get('id', {}).get('id', '')
        
        # 创建设备
        data = {
            "name": name,
            "type": profile_name,
            "deviceProfileId": {"entityType": "DEVICE_PROFILE", "id": profile_id}
        }
        
        result = self.session.post(f"{self.base_url}/api/device", json=data)
        if result.status_code in [200, 201]:
            print(f"  [OK] 创建设备成功: {name}")
            return True
        else:
            # 可能已存在
            print(f"  [INFO] 设备可能已存在: {name}")
            return True
    
    def send_telemetry_via_api(self, device_name: str, data: dict) -> bool:
        """通过API发送遥测数据"""
        # 获取设备ID
        devices = self.get_devices()
        device = None
        for d in devices:
            if d.get('name') == device_name:
                device = d
                break
        
        if not device:
            print(f"  [WARN] 未找到设备: {device_name}")
            return False
        
        device_id = device.get('id', {}).get('id', '')
        
        # 发送遥测
        url = f"{self.base_url}/api/plugins/telemetry/{device_id}/timeseries/any"
        payload = {
            "values": data
        }
        
        result = self.session.post(url, json=payload)
        return result.status_code in [200, 201]
    
    def demo_sync_models(self):
        """演示：同步物模型"""
        print("\n" + "="*60)
        print("步骤1: 同步物模型")
        print("="*60)
        
        # 调用同步脚本
        os.system(f"cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/scripts && python3 thingskit_model_sync.py --user {self.username} --password '{self.password}'")
    
    def demo_create_devices(self):
        """演示：创建设备"""
        print("\n" + "="*60)
        print("步骤2: 创建设备")
        print("="*60)
        
        for device in self.devices:
            self.create_device(
                device['name'],
                device['profile_name']
            )
            time.sleep(0.5)
    
    def demo_send_data(self):
        """演示：发送测试数据"""
        print("\n" + "="*60)
        print("步骤3: 发送测试数据")
        print("="*60)
        
        # 单相电表数据
        print("\n[单相电表] 发送数据...")
        meter_data = {
            "voltage": 220.5,
            "current": 5.2,
            "active_power": 1146.6,
            "power_factor": 0.98,
            "frequency": 50.0,
            "energy": 1234.56,
            "relay_status": 85,
            "online": True
        }
        self.send_telemetry_via_api("总表001", meter_data)
        print(f"  数据: {json.dumps(meter_data, indent=2)}")
        
        # 温湿度数据
        print("\n[温湿度传感器] 发送数据...")
        env_data = {
            "temperature": 28.5,
            "humidity": 65.2,
            "online": True
        }
        self.send_telemetry_via_api("温湿度传感器001", env_data)
        print(f"  数据: {json.dumps(env_data, indent=2)}")
        
        # 继电器数据
        print("\n[继电器] 发送数据...")
        relay_data = {
            "relay_state": 1,
            "control_mode": 0,
            "online": True
        }
        self.send_telemetry_via_api("继电器001", relay_data)
        print(f"  数据: {json.dumps(relay_data, indent=2)}")
        
        # DTU节点数据
        print("\n[DTU节点] 发送数据...")
        dtu_data = {
            "role": 1,
            "mac": "A1:A2:A3:A4:A5:A6",
            "online": True,
            "uptime": 3600,
            "parent_mac": "",
            "child_count": 2,
            "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6",
            "topology": {
                "parent_mac": "",
                "child_count": 2,
                "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6"
            },
            "modbus_count": 3,
            "collect_cycle": 5000,
            "collect_config": {
                "modbus_count": 3,
                "collect_cycle": 5000
            }
        }
        self.send_telemetry_via_api("DTU节点001", dtu_data)
        print(f"  数据: {json.dumps(dtu_data, indent=2)}")
    
    def demo_verify(self):
        """演示：验证结果"""
        print("\n" + "="*60)
        print("步骤4: 验证结果")
        print("="*60)
        
        devices = self.get_devices()
        print(f"\n平台设备数: {len(devices)}")
        
        for device in devices:
            name = device.get('name', '')
            if any(d['name'] == name for d in self.devices):
                print(f"  - {name}")
    
    def run(self):
        """运行完整演示"""
        print("""
╔══════════════════════════════════════════════════════════════╗
║              ThingsKit 完整演示流程                          ║
╚══════════════════════════════════════════════════════════════╝
        """)
        
        # 登录
        if not self.login():
            print("[ERROR] 登录失败")
            return
        print("[OK] 登录成功")
        
        # 执行演示流程
        self.demo_sync_models()
        self.demo_create_devices()
        self.demo_send_data()
        self.demo_verify()
        
        print("\n" + "="*60)
        print("演示完成！")
        print("="*60)
        print("\n请登录 https://thingskit.aiotcomm.com.cn 查看数据")
        print("进入 设备管理 -> 设备，查看设备遥测数据")


if __name__ == '__main__':
    demo = ThingsKitDemo()
    demo.run()
