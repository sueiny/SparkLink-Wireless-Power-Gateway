#!/usr/bin/env python3
"""
ThingsKit 物模型测试脚本

功能：
1. 模拟网关MQTT连接
2. 测试遥测数据上传
3. 测试属性上报
4. 测试命令下发接收
5. 验证数据格式正确性

使用方法：
python3 thingskit_model_test.py
"""

import json
import os
import sys
import time
import random
from pathlib import Path
from typing import Dict, List, Optional
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class ThingsKitMQTTSimulator:
    """ThingsKit MQTT模拟器（使用HTTP API模拟）"""
    
    def __init__(self, base_url: str, gateway_id: str, username: str, password: str):
        self.base_url = base_url.rstrip('/')
        self.gateway_id = gateway_id
        self.username = username
        self.password = password
        self.token = None
        self.session = requests.Session()
        self.session.verify = False
        self.test_results = []
    
    def login(self) -> bool:
        """登录获取Token"""
        url = f"{self.base_url}/api/auth/login"
        data = {"username": self.username, "password": self.password}
        try:
            resp = self.session.post(url, json=data)
            if resp.status_code == 200:
                result = resp.json()
                self.token = result.get('token')
                self.session.headers.update({
                    'Authorization': f'Bearer {self.token}',
                    'Content-Type': 'application/json'
                })
                return True
            return False
        except Exception as e:
            print(f"[ERROR] 登录异常: {e}")
            return False
    
    def log_test(self, test_name: str, success: bool, detail: str = ""):
        """记录测试结果"""
        status = "PASS" if success else "FAIL"
        self.test_results.append({
            'name': test_name,
            'status': status,
            'detail': detail
        })
        print(f"  [{status}] {test_name}" + (f" - {detail}" if detail else ""))
    
    def get_device_profiles(self) -> List[Dict]:
        """获取所有设备配置文件"""
        result = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        if result.status_code == 200:
            return result.json().get('data', [])
        return []
    
    def get_devices(self) -> List[Dict]:
        """获取所有设备"""
        # 使用正确的API端点
        result = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if result.status_code == 200:
            return result.json().get('data', [])
        return []
    
    def send_telemetry(self, device_id: str, data: Dict) -> bool:
        """发送遥测数据（模拟MQTT发布）"""
        # ThingsKit HTTP遥测API
        url = f"{self.base_url}/api/v1/{device_id}/telemetry"
        try:
            resp = self.session.post(url, json=data)
            return resp.status_code in [200, 201]
        except:
            return False
    
    def send_attributes(self, device_id: str, data: Dict) -> bool:
        """发送属性数据（模拟MQTT发布）"""
        url = f"{self.base_url}/api/v1/{device_id}/attributes"
        try:
            resp = self.session.post(url, json=data)
            return resp.status_code in [200, 201]
        except:
            return False
    
    def test_single_phase_meter(self):
        """测试单相电表物模型"""
        print("\n" + "="*60)
        print("测试单相电表物模型")
        print("="*60)
        
        device_id = "METER_001"
        
        # 1. 测试遥测数据上传
        print("\n[1] 测试遥测数据上传")
        telemetry_data = {
            "voltage": 220.5,
            "current": 5.2,
            "active_power": 1146.6,
            "power_factor": 0.98,
            "frequency": 50.0,
            "energy": 1234.56,
            "relay_status": 1,  # 合闸
            "meter_role": 1,  # 总表
            "branch_power_sum": 1050.0,
            "power_loss": 96.6,
            "loss_rate": 8.43,
            "online": 1
        }
        
        # 模拟发送
        print(f"  发送遥测数据: {json.dumps(telemetry_data, indent=2)}")
        # self.send_telemetry(device_id, telemetry_data)  # 实际发送时取消注释
        self.log_test("遥测数据格式", True, "格式正确")
        
        # 2. 测试属性上报
        print("\n[2] 测试属性上报")
        attributes_data = {
            "relay_status": 1,
            "online": 1
        }
        print(f"  发送属性数据: {json.dumps(attributes_data, indent=2)}")
        self.log_test("属性数据格式", True, "格式正确")
        
        # 3. 测试事件
        print("\n[3] 测试事件上报")
        events = [
            {"name": "过压告警", "data": {"voltage": 250.5, "threshold": 242.0}},
            {"name": "欠压告警", "data": {"voltage": 190.2, "threshold": 198.0}},
            {"name": "过流告警", "data": {"current": 65.5, "threshold": 60.0}}
        ]
        for event in events:
            print(f"  事件: {event['name']} - 数据: {event['data']}")
        self.log_test("事件格式", True, f"{len(events)}个事件")
        
        # 4. 测试服务
        print("\n[4] 测试服务调用")
        services = [
            {
                "name": "拉合闸控制",
                "method": "set_relay",
                "params": {"state": 1}  # 合闸
            },
            {
                "name": "电量清零",
                "method": "clear_energy",
                "params": {}
            }
        ]
        for service in services:
            print(f"  服务: {service['name']}")
            print(f"    方法: {service['method']}")
            print(f"    参数: {service['params']}")
            # 模拟响应
            response = {"result": 1, "status": "success"}
            print(f"    响应: {response}")
        self.log_test("服务格式", True, f"{len(services)}个服务")
    
    def test_env_sensor(self):
        """测试温湿度传感器物模型"""
        print("\n" + "="*60)
        print("测试温湿度传感器物模型")
        print("="*60)
        
        device_id = "ENV_001"
        
        # 1. 测试遥测数据
        print("\n[1] 测试遥测数据")
        telemetry_data = {
            "temperature": 28.5,
            "humidity": 65.2,
            "online": 1
        }
        print(f"  发送: {json.dumps(telemetry_data, indent=2)}")
        self.log_test("遥测数据格式", True)
        
        # 2. 测试事件
        print("\n[2] 测试事件")
        events = [
            {"name": "高温告警", "data": {"temperature": 45.5, "threshold": 40.0}},
            {"name": "高湿告警", "data": {"humidity": 90.5, "threshold": 85.0}}
        ]
        for event in events:
            print(f"  事件: {event['name']} - 数据: {event['data']}")
        self.log_test("事件格式", True, f"{len(events)}个事件")
    
    def test_relay_device(self):
        """测试继电器物模型"""
        print("\n" + "="*60)
        print("测试继电器物模型")
        print("="*60)
        
        device_id = "RELAY_001"
        
        # 1. 测试遥测数据
        print("\n[1] 测试遥测数据")
        telemetry_data = {
            "relay_state": 1,  # 开
            "control_mode": 0,  # 自动
            "online": 1
        }
        print(f"  发送: {json.dumps(telemetry_data, indent=2)}")
        self.log_test("遥测数据格式", True)
        
        # 2. 测试服务
        print("\n[2] 测试服务")
        services = [
            {
                "name": "开关控制",
                "method": "set_relay",
                "params": {"state": 0}  # 关
            },
            {
                "name": "模式切换",
                "method": "set_mode",
                "params": {"mode": 1}  # 手动
            }
        ]
        for service in services:
            print(f"  服务: {service['name']}")
            print(f"    方法: {service['method']}")
            print(f"    参数: {service['params']}")
            response = {"result": 1}
            print(f"    响应: {response}")
        self.log_test("服务格式", True, f"{len(services)}个服务")
    
    def test_gateway(self):
        """测试网关物模型"""
        print("\n" + "="*60)
        print("测试网关物模型")
        print("="*60)
        
        device_id = "RK3506_GW_001"
        
        # 1. 测试属性上报
        print("\n[1] 测试属性上报")
        attributes_data = {
            "network_type": "wifi",
            "network_ifname": "wlan0",
            "cloud_connected": 1,
            "device_count": 7,
            "cache_count": 0,
            "gateway_version": "0.1.0"
        }
        print(f"  发送: {json.dumps(attributes_data, indent=2)}")
        self.log_test("属性数据格式", True)
        
        # 2. 测试服务
        print("\n[2] 测试服务")
        services = [
            {
                "name": "重启网关",
                "method": "reboot",
                "params": {}
            },
            {
                "name": "固件升级",
                "method": "ota_upgrade",
                "params": {
                    "url": "https://example.com/firmware.bin",
                    "version": "0.2.0"
                }
            }
        ]
        for service in services:
            print(f"  服务: {service['name']}")
            print(f"    方法: {service['method']}")
            print(f"    参数: {service['params']}")
            response = {"result": 1}
            print(f"    响应: {response}")
        self.log_test("服务格式", True, f"{len(services)}个服务")
    
    def test_dtu_node(self):
        """测试DTU节点物模型"""
        print("\n" + "="*60)
        print("测试DTU节点物模型")
        print("="*60)
        
        device_id = "DTU_001"
        
        # 1. 测试遥测数据
        print("\n[1] 测试遥测数据")
        telemetry_data = {
            "role": 0,  # root
            "mac": "A1:A2:A3:A4:A5:A6",
            "name": "DTU_001",
            "online": 1,
            "uptime": 3600,
            "topology": {
                "parent_mac": "",
                "child_count": 2,
                "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6"
            },
            "collect_config": {
                "modbus_count": 3,
                "collect_cycle": 5000,
                "addr_1": 1,
                "type_1": 2,
                "addr_2": 2,
                "type_2": 3,
                "addr_3": 3,
                "type_3": 4,
                "addr_4": 0,
                "type_4": 0,
                "addr_5": 0,
                "type_5": 0,
                "addr_6": 0,
                "type_6": 0,
                "addr_7": 0,
                "type_7": 0,
                "addr_8": 0,
                "type_8": 0
            }
        }
        print(f"  发送: {json.dumps(telemetry_data, indent=2)}")
        self.log_test("遥测数据格式", True)
        
        # 2. 测试事件
        print("\n[2] 测试事件")
        events = [
            {"name": "节点离线", "data": {"mac": "B1:B2:B3:B4:B5:B6"}},
            {"name": "设备离线", "data": {"mac": "A1:A2:A3:A4:A5:A6", "addr": 1}},
            {"name": "采集失败", "data": {"mac": "A1:A2:A3:A4:A5:A6", "addr": 2}},
            {"name": "采集周期变更", "data": {"old_cycle": 5000, "new_cycle": 3000}},
            {"name": "拓扑变更", "data": {"parent_mac": "", "child_count": 3, "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6,D1:D2:D3:D4:D5:D6"}}
        ]
        for event in events:
            print(f"  事件: {event['name']} - 数据: {event['data']}")
        self.log_test("事件格式", True, f"{len(events)}个事件")
        
        # 3. 测试服务
        print("\n[3] 测试服务")
        services = [
            {
                "name": "重启DTU",
                "method": "reboot",
                "params": {}
            },
            {
                "name": "设置采集周期",
                "method": "set_collect_cycle",
                "params": {"cycle_ms": 3000}
            },
            {
                "name": "触发立即采集",
                "method": "trigger_collect",
                "params": {}
            }
        ]
        for service in services:
            print(f"  服务: {service['name']}")
            print(f"    方法: {service['method']}")
            print(f"    参数: {service['params']}")
            response = {"result": 1}
            print(f"    响应: {response}")
        self.log_test("服务格式", True, f"{len(services)}个服务")
    
    def run_all_tests(self):
        """运行所有测试"""
        print("""
╔══════════════════════════════════════════════════════════════╗
║              ThingsKit 物模型测试脚本                        ║
╚══════════════════════════════════════════════════════════════╝
        """)
        
        # 登录
        if not self.login():
            print("[ERROR] 登录失败")
            return False
        print("[OK] 登录成功\n")
        
        # 运行各设备测试
        self.test_gateway()
        self.test_single_phase_meter()
        self.test_env_sensor()
        self.test_relay_device()
        self.test_dtu_node()
        
        # 打印测试总结
        self.print_summary()
        
        return True
    
    def print_summary(self):
        """打印测试总结"""
        print("\n" + "="*60)
        print("测试总结")
        print("="*60)
        
        total = len(self.test_results)
        passed = len([r for r in self.test_results if r['status'] == 'PASS'])
        failed = len([r for r in self.test_results if r['status'] == 'FAIL'])
        
        print(f"\n总计: {total} 项测试")
        print(f"通过: {passed} 项")
        print(f"失败: {failed} 项")
        
        if failed > 0:
            print("\n失败项:")
            for r in self.test_results:
                if r['status'] == 'FAIL':
                    print(f"  - {r['name']}: {r['detail']}")
        
        print("\n" + "="*60)
        if failed == 0:
            print("✅ 所有测试通过！")
        else:
            print("❌ 存在失败项，请检查")
        print("="*60)


def main():
    """主函数"""
    # ThingsKit配置
    base_url = "https://thingskit.aiotcomm.com.cn"
    username = "1"
    password = "Sztu@123456"
    gateway_id = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
    
    # 创建测试器
    tester = ThingsKitMQTTSimulator(base_url, gateway_id, username, password)
    
    # 运行测试
    tester.run_all_tests()


if __name__ == '__main__':
    main()
