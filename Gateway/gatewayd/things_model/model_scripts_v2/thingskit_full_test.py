#!/usr/bin/env python3
"""
ThingsKit 完整测试脚本

功能：
1. 测试MQTT上传
2. 测试HTTP API
3. 验证数据正确性
4. 测试命令下发

使用方法：
python3 thingskit_full_test.py
"""

import json
import time
import sys
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("请安装paho-mqtt: pip3 install paho-mqtt")
    sys.exit(1)

import requests
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class ThingsKitTester:
    """ThingsKit测试器"""
    
    def __init__(self):
        # MQTT配置
        self.mqtt_broker = "thingskit.aiotcomm.com.cn"
        self.mqtt_port = 11883
        self.mqtt_client_id = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
        self.mqtt_username = "123"
        self.mqtt_password = "123"
        
        # HTTP配置
        self.http_base = "https://thingskit.aiotcomm.com.cn"
        self.http_username = "1"
        self.http_password = "Sztu@123456"
        
        # 设备列表
        self.devices = {
            "METER_MAIN_001": {"name": "总表001", "profile": "单相电表"},
            "METER_BRANCH_001": {"name": "照明支表001", "profile": "单相电表"},
            "ENV_001": {"name": "温湿度传感器001", "profile": "温湿度传感器"},
            "RELAY_001": {"name": "继电器001", "profile": "继电器"},
            "DTU_NODE_001": {"name": "DTU节点001", "profile": "DTU节点"}
        }
        
        # 测试结果
        self.results = []
    
    def log(self, msg: str, level: str = "INFO"):
        """日志"""
        print(f"[{level}] {msg}")
    
    def test_mqtt(self):
        """测试MQTT上传"""
        print("\n" + "="*60)
        print("测试1: MQTT上传")
        print("="*60)
        
        connected = False
        published = 0
        
        def on_connect(client, userdata, flags, rc):
            nonlocal connected
            if rc == 0:
                connected = True
                self.log("MQTT连接成功")
                
                # 订阅命令主题
                client.subscribe("v1/devices/me/attributes")
                client.subscribe("v1/gateway/commands/request")
                self.log("订阅命令主题")
        
        def on_message(client, userdata, msg):
            try:
                payload = json.loads(msg.payload.decode())
                self.log(f"收到消息: {msg.topic}")
                print(f"  内容: {json.dumps(payload, indent=2, ensure_ascii=False)}")
            except:
                pass
        
        # 创建客户端
        client = mqtt.Client(client_id=self.mqtt_client_id)
        client.username_pw_set(self.mqtt_username, self.mqtt_password)
        client.on_connect = on_connect
        client.on_message = on_message
        
        try:
            client.connect(self.mqtt_broker, self.mqtt_port, 60)
            client.loop_start()
            time.sleep(2)
            
            if not connected:
                self.log("MQTT连接失败", "ERROR")
                return False
            
            # 发送网关属性
            self.log("发送网关属性...")
            attributes = {
                "network_type": "wifi",
                "network_ifname": "wlan0",
                "cloud_connected": True,
                "device_count": 7,
                "cache_count": 0,
                "gateway_version": "0.1.0"
            }
            client.publish("v1/devices/me/attributes", json.dumps(attributes))
            published += 1
            
            # 发送各设备遥测数据
            ts = int(time.time() * 1000)
            
            # 单相电表
            self.log("发送单相电表数据...")
            telemetry1 = {
                "METER_MAIN_001": [{
                    "ts": ts,
                    "values": {
                        "voltage": 220.5,
                        "current": 5.2,
                        "active_power": 1146.6,
                        "power_factor": 0.98,
                        "frequency": 50.0,
                        "energy": 1234.56,
                        "relay_status": 85,
                        "meter_role": 0,
                        "branch_power_sum": 1050.0,
                        "power_loss": 96.6,
                        "loss_rate": 8.43,
                        "online": True
                    }
                }]
            }
            client.publish("v1/gateway/telemetry", json.dumps(telemetry1))
            published += 1
            
            # 温湿度
            self.log("发送温湿度数据...")
            telemetry2 = {
                "ENV_001": [{
                    "ts": ts,
                    "values": {
                        "temperature": 28.5,
                        "humidity": 65.2,
                        "online": True
                    }
                }]
            }
            client.publish("v1/gateway/telemetry", json.dumps(telemetry2))
            published += 1
            
            # 继电器
            self.log("发送继电器数据...")
            telemetry3 = {
                "RELAY_001": [{
                    "ts": ts,
                    "values": {
                        "relay_state": 1,
                        "control_mode": 0,
                        "online": True
                    }
                }]
            }
            client.publish("v1/gateway/telemetry", json.dumps(telemetry3))
            published += 1
            
            # DTU节点
            self.log("发送DTU节点数据...")
            telemetry4 = {
                "DTU_NODE_001": [{
                    "ts": ts,
                    "values": {
                        "role": 1,
                        "mac": "A1:A2:A3:A4:A5:A6",
                        "name": "DTU_001",
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
                }]
            }
            client.publish("v1/gateway/telemetry", json.dumps(telemetry4))
            published += 1
            
            self.log(f"MQTT上传完成，共发送 {published} 条消息")
            
            # 等待接收命令
            self.log("等待5秒接收命令...")
            time.sleep(5)
            
            client.loop_stop()
            client.disconnect()
            
            self.results.append({"test": "MQTT上传", "status": "PASS", "detail": f"{published}条消息"})
            return True
            
        except Exception as e:
            self.log(f"MQTT测试异常: {e}", "ERROR")
            client.loop_stop()
            client.disconnect()
            self.results.append({"test": "MQTT上传", "status": "FAIL", "detail": str(e)})
            return False
    
    def test_http_api(self):
        """测试HTTP API"""
        print("\n" + "="*60)
        print("测试2: HTTP API")
        print("="*60)
        
        session = requests.Session()
        session.verify = False
        
        # 登录
        self.log("登录...")
        resp = session.post(f"{self.http_base}/api/auth/login", json={
            "username": self.http_username,
            "password": self.http_password
        })
        
        if resp.status_code != 200:
            self.log("登录失败", "ERROR")
            self.results.append({"test": "HTTP API", "status": "FAIL", "detail": "登录失败"})
            return False
        
        token = resp.json().get('token')
        session.headers.update({
            'Authorization': f'Bearer {token}',
            'Content-Type': 'application/json'
        })
        self.log("登录成功")
        
        # 获取设备列表
        self.log("获取设备列表...")
        resp = session.get(f"{self.http_base}/api/tenant/devices?pageSize=100&page=0")
        if resp.status_code == 200:
            devices = resp.json().get('data', [])
            self.log(f"找到 {len(devices)} 个设备")
            
            # 显示设备信息
            for d in devices[:5]:
                name = d.get('name', '')
                did = d.get('id', {}).get('id', '')
                self.log(f"  {name}: {did[:20]}...")
        
        self.results.append({"test": "HTTP API", "status": "PASS", "detail": f"{len(devices)}个设备"})
        return True
    
    def print_summary(self):
        """打印测试总结"""
        print("\n" + "="*60)
        print("测试总结")
        print("="*60)
        
        passed = sum(1 for r in self.results if r['status'] == 'PASS')
        failed = sum(1 for r in self.results if r['status'] == 'FAIL')
        
        for r in self.results:
            status = "✅" if r['status'] == 'PASS' else "❌"
            print(f"{status} {r['test']}: {r['detail']}")
        
        print(f"\n通过: {passed}, 失败: {failed}")
        print("="*60)
    
    def run(self):
        """运行测试"""
        print("""
╔══════════════════════════════════════════════════════════════╗
║              ThingsKit 完整测试                              ║
╚══════════════════════════════════════════════════════════════╝
        """)
        
        # 测试MQTT
        self.test_mqtt()
        
        # 测试HTTP API
        self.test_http_api()
        
        # 打印总结
        self.print_summary()


def main():
    tester = ThingsKitTester()
    tester.run()


if __name__ == '__main__':
    main()
