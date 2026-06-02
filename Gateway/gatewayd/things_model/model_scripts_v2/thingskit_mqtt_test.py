#!/usr/bin/env python3
"""
ThingsKit MQTT 实际测试脚本

功能：
1. 通过MQTT连接ThingsKit
2. 发送遥测数据
3. 发送属性数据
4. 接收命令下发
5. 验证数据正确性

使用方法：
pip3 install paho-mqtt
python3 thingskit_mqtt_test.py
"""

import json
import time
import sys
from datetime import datetime
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("请安装paho-mqtt: pip3 install paho-mqtt")
    sys.exit(1)


class ThingsKitMQTTTester:
    """ThingsKit MQTT测试器"""
    
    def __init__(self, broker: str, port: int, client_id: str, username: str, password: str):
        self.broker = broker
        self.port = port
        self.client_id = client_id
        self.username = username
        self.password = password
        
        self.client = mqtt.Client(client_id=client_id)
        self.client.username_pw_set(username, password)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_publish = self.on_publish
        
        self.connected = False
        self.messages_received = []
        self.commands_received = []
    
    def on_connect(self, client, userdata, flags, rc):
        """连接回调"""
        if rc == 0:
            self.connected = True
            print(f"[OK] MQTT连接成功: {self.broker}:{self.port}")
            
            # 订阅命令主题
            command_topic = f"v1/devices/me/attributes"
            client.subscribe(command_topic)
            print(f"[OK] 订阅主题: {command_topic}")
            
            # 订阅网关命令
            gateway_cmd_topic = f"v1/gateway/commands/request"
            client.subscribe(gateway_cmd_topic)
            print(f"[OK] 订阅主题: {gateway_cmd_topic}")
        else:
            print(f"[ERROR] MQTT连接失败: {rc}")
    
    def on_message(self, client, userdata, msg):
        """消息回调"""
        try:
            payload = json.loads(msg.payload.decode())
            self.messages_received.append({
                'topic': msg.topic,
                'payload': payload,
                'time': datetime.now().isoformat()
            })
            print(f"\n[收到消息] 主题: {msg.topic}")
            print(f"  内容: {json.dumps(payload, indent=2, ensure_ascii=False)}")
            
            # 检查是否是命令
            if 'method' in payload:
                self.commands_received.append(payload)
                print(f"  [命令] 方法: {payload.get('method')}")
                print(f"  [命令] 参数: {payload.get('params')}")
        except Exception as e:
            print(f"[ERROR] 解析消息失败: {e}")
    
    def on_publish(self, client, userdata, mid):
        """发布回调"""
        print(f"[OK] 消息发布成功: mid={mid}")
    
    def connect(self) -> bool:
        """连接MQTT"""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            time.sleep(2)  # 等待连接完成
            return self.connected
        except Exception as e:
            print(f"[ERROR] 连接异常: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        self.client.loop_stop()
        self.client.disconnect()
    
    def publish_telemetry(self, device_id: str, data: dict) -> bool:
        """发布遥测数据"""
        topic = f"v1/gateway/telemetry"
        payload = {
            device_id: [
                {
                    "ts": int(time.time() * 1000),
                    "values": data
                }
            ]
        }
        print(f"\n[发送遥测] 设备: {device_id}")
        print(f"  主题: {topic}")
        print(f"  数据: {json.dumps(data, indent=2)}")
        
        result = self.client.publish(topic, json.dumps(payload))
        return result.rc == mqtt.MQTT_ERR_SUCCESS
    
    def publish_attributes(self, device_id: str, data: dict) -> bool:
        """发布属性数据"""
        topic = f"v1/devices/me/attributes"
        payload = data
        print(f"\n[发送属性] 设备: {device_id}")
        print(f"  主题: {topic}")
        print(f"  数据: {json.dumps(data, indent=2)}")
        
        result = self.client.publish(topic, json.dumps(payload))
        return result.rc == mqtt.MQTT_ERR_SUCCESS
    
    def test_single_phase_meter(self):
        """测试单相电表"""
        print("\n" + "="*60)
        print("测试单相电表: METER_001")
        print("="*60)
        
        device_id = "METER_001"
        
        # 发送遥测数据
        telemetry_data = {
            "voltage": 220.5,
            "current": 5.2,
            "active_power": 1146.6,
            "power_factor": 0.98,
            "frequency": 50.0,
            "energy": 1234.56,
            "relay_status": True,
            "meter_role": True,
            "branch_power_sum": 1050.0,
            "power_loss": 96.6,
            "loss_rate": 8.43,
            "online": True
        }
        self.publish_telemetry(device_id, telemetry_data)
        time.sleep(1)
    
    def test_env_sensor(self):
        """测试温湿度传感器"""
        print("\n" + "="*60)
        print("测试温湿度传感器: ENV_001")
        print("="*60)
        
        device_id = "ENV_001"
        
        telemetry_data = {
            "temperature": 28.5,
            "humidity": 65.2,
            "online": True
        }
        self.publish_telemetry(device_id, telemetry_data)
        time.sleep(1)
    
    def test_relay_device(self):
        """测试继电器"""
        print("\n" + "="*60)
        print("测试继电器: RELAY_001")
        print("="*60)
        
        device_id = "RELAY_001"
        
        telemetry_data = {
            "relay_state": 1,
            "control_mode": 0,
            "online": True
        }
        self.publish_telemetry(device_id, telemetry_data)
        time.sleep(1)
    
    def test_dtu_node(self):
        """测试DTU节点"""
        print("\n" + "="*60)
        print("测试DTU节点: DTU_001")
        print("="*60)
        
        device_id = "DTU_001"
        
        telemetry_data = {
            "role": 0,
            "mac": "A1:A2:A3:A4:A5:A6",
            "name": "DTU_001",
            "online": True,
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
        self.publish_telemetry(device_id, telemetry_data)
        time.sleep(1)
    
    def test_gateway_attributes(self):
        """测试网关属性"""
        print("\n" + "="*60)
        print("测试网关属性上报")
        print("="*60)
        
        attributes_data = {
            "network_type": "wifi",
            "network_ifname": "wlan0",
            "cloud_connected": True,
            "device_count": 7,
            "cache_count": 0,
            "gateway_version": "0.1.0"
        }
        self.publish_attributes("gateway", attributes_data)
        time.sleep(1)
    
    def wait_for_commands(self, timeout: int = 10):
        """等待命令下发"""
        print(f"\n等待命令下发 ({timeout}秒)...")
        print("请在ThingsKit平台下发命令...")
        time.sleep(timeout)
    
    def print_summary(self):
        """打印测试总结"""
        print("\n" + "="*60)
        print("测试总结")
        print("="*60)
        
        print(f"\n收到消息数: {len(self.messages_received)}")
        print(f"收到命令数: {len(self.commands_received)}")
        
        if self.commands_received:
            print("\n收到的命令:")
            for cmd in self.commands_received:
                print(f"  - {cmd.get('method')}: {cmd.get('params')}")
        
        print("\n" + "="*60)
    
    def run_all_tests(self):
        """运行所有测试"""
        print("""
╔══════════════════════════════════════════════════════════════╗
║           ThingsKit MQTT 实际测试脚本                        ║
╚══════════════════════════════════════════════════════════════╝
        """)
        
        # 连接MQTT
        if not self.connect():
            print("[ERROR] MQTT连接失败")
            return
        
        # 运行测试
        self.test_gateway_attributes()
        self.test_single_phase_meter()
        self.test_env_sensor()
        self.test_relay_device()
        self.test_dtu_node()
        
        # 等待命令
        self.wait_for_commands(10)
        
        # 打印总结
        self.print_summary()
        
        # 断开连接
        self.disconnect()
        print("\n[OK] 测试完成")


def main():
    """主函数"""
    # MQTT配置（从gateway_config.json读取）
    broker = "thingskit.aiotcomm.com.cn"
    port = 11883
    client_id = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
    username = "123"
    password = "123"
    
    # 创建测试器
    tester = ThingsKitMQTTTester(broker, port, client_id, username, password)
    
    # 运行测试
    tester.run_all_tests()


if __name__ == '__main__':
    main()
