#!/usr/bin/env python3
"""
ThingsKit 综合管理工具

功能：
1. 物模型管理（创建、更新、同步）
2. 设备管理（创建、查询、删除）
3. 数据上传（遥测、属性、事件）
4. 命令下发（服务调用）
5. 监控和日志

使用方法：
python3 thingskit_manager.py [command] [options]

命令：
  sync-model    同步物模型
  create-device 创建设备
  send-data     发送数据
  call-service  调用服务
  monitor       监控设备状态
  full-test     完整测试
"""

import json
import os
import sys
import time
import argparse
from pathlib import Path
from typing import Dict, List, Optional
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class ThingsKitManager:
    """ThingsKit管理器"""
    
    def __init__(self, config_path: str = None):
        # 加载配置
        self.config = self._load_config(config_path)
        self.base_url = self.config.get('base_url', 'https://thingskit.aiotcomm.com.cn')
        self.username = self.config.get('username', '1')
        self.password = self.config.get('password', 'Sztu@123456')
        
        # 初始化
        self.token = None
        self.session = requests.Session()
        self.session.verify = False
        
        # 物模型目录
        self.model_dir = Path(self.config.get('model_dir', 
            '/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model'))
    
    def _load_config(self, config_path: str) -> dict:
        """加载配置文件"""
        default_config = {
            'base_url': 'https://thingskit.aiotcomm.com.cn',
            'username': '1',
            'password': 'Sztu@123456',
            'model_dir': '/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model'
        }
        
        if config_path and os.path.exists(config_path):
            try:
                with open(config_path, 'r') as f:
                    return json.load(f)
            except:
                pass
        
        # 尝试从gateway配置读取
        gateway_config_path = '/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/config/gateway_config.json'
        if os.path.exists(gateway_config_path):
            try:
                with open(gateway_config_path, 'r') as f:
                    gateway_config = json.load(f)
                    thingskit = gateway_config.get('thingskit', {})
                    default_config['mqtt_host'] = thingskit.get('host', '')
                    default_config['mqtt_port'] = thingskit.get('port', 1883)
                    default_config['mqtt_client_id'] = thingskit.get('client_id', '')
                    default_config['mqtt_username'] = thingskit.get('username', '')
                    default_config['mqtt_password'] = thingskit.get('password', '')
            except:
                pass
        
        return default_config
    
    def login(self) -> bool:
        """登录获取Token"""
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
        except Exception as e:
            print(f"[ERROR] 登录异常: {e}")
            return False
    
    # ==================== 物模型管理 ====================
    
    def sync_models(self):
        """同步所有物模型"""
        print("\n" + "="*60)
        print("同步物模型")
        print("="*60)
        
        # 加载产品配置
        config_file = self.model_dir / "all_product_models.json"
        with open(config_file, 'r', encoding='utf-8') as f:
            products_config = json.load(f)
        
        # 获取已有配置文件
        existing_profiles = self._get_existing_profiles()
        
        # 同步每个产品
        for product in products_config.get('products', []):
            self._sync_single_product(product, existing_profiles)
    
    def _get_existing_profiles(self) -> dict:
        """获取已有配置文件"""
        result = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        profiles = {}
        if result.status_code == 200:
            for p in result.json().get('data', []):
                profiles[p.get('name')] = p
        return profiles
    
    def _sync_single_product(self, product: dict, existing_profiles: dict):
        """同步单个产品"""
        product_name = product.get('productName')
        model_file = product.get('modelFile')
        
        print(f"\n同步: {product_name}")
        
        # 加载物模型
        model_data = self._load_model_file(model_file)
        if not model_data:
            print(f"  [SKIP] 无法加载物模型文件")
            return
        
        # 转换格式
        things_model = self._convert_to_thingskit_format(model_data)
        
        # 统计
        props = len([t for t in things_model if t.get('functionType') == 'properties'])
        events = len([t for t in things_model if t.get('functionType') == 'events'])
        services = len([t for t in things_model if t.get('functionType') == 'services'])
        print(f"  属性: {props}, 事件: {events}, 服务: {services}")
        
        # 更新或创建
        if product_name in existing_profiles:
            profile = existing_profiles[product_name]
            profile_id = profile.get('id', {}).get('id', '')
            self._update_profile(profile, things_model)
        else:
            self._create_profile(product_name, things_model)
    
    def _load_model_file(self, filename: str) -> Optional[dict]:
        """加载物模型文件"""
        filepath = self.model_dir / filename
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"  [ERROR] 加载失败: {e}")
            return None
    
    def _convert_to_thingskit_format(self, model_data: dict) -> list:
        """转换为ThingsKit格式"""
        things_model = []
        
        # 属性
        for prop in model_data.get('properties', []):
            things_model.append(self._normalize_model_item(prop, 'properties'))
        
        # 事件
        for event in model_data.get('events', []):
            if 'eventType' not in event:
                event = {**event, 'eventType': 'ALERT'}
            things_model.append(self._normalize_model_item(event, 'events'))
        
        # 服务
        for service in model_data.get('services', []):
            if 'callType' not in service:
                service = {**service, 'callType': 'ASYNC'}
            things_model.append(self._normalize_model_item(service, 'services'))
        
        return things_model

    def _normalize_struct_child(self, child: dict) -> dict:
        """规范化 STRUCT 子字段，供同步和测试数据生成使用。"""
        data_type = child.get('dataType', {})
        if isinstance(data_type, str):
            data_type = {'type': data_type, 'specs': child.get('specs', {})}

        return {
            'functionName': child.get('functionName') or child.get('name') or child.get('identifier', ''),
            'identifier': child.get('identifier', ''),
            'remark': child.get('remark'),
            'dataType': data_type
        }

    def _sample_value_for_type(self, data_type: dict):
        """为 STRUCT 的 json 信息生成示例值。"""
        type_name = (data_type or {}).get('type')
        if type_name in ('DOUBLE', 'FLOAT'):
            return 1.23
        if type_name in ('INT', 'LONG'):
            return 1
        if type_name == 'BOOL':
            return True
        if type_name == 'ENUM':
            specs = data_type.get('specs') or {}
            if specs:
                first_key = next(iter(specs.keys()))
                try:
                    return int(first_key)
                except ValueError:
                    return first_key
            return 0
        return ''

    def _normalize_function_json(self, item: dict) -> dict:
        """保留当前 functionJson，并为 STRUCT 补充 json 示例对象。"""
        function_json = dict(item.get('functionJson') or {})

        if not function_json:
            if item.get('functionType') == 'properties':
                function_json = {'dataType': item.get('specs', {}).get('dataType', {})}
            elif item.get('functionType') == 'events':
                function_json = {'outputData': item.get('outputData', [])}
            elif item.get('functionType') == 'services':
                function_json = {
                    'inputData': item.get('inputData', []),
                    'outputData': item.get('outputData', [])
                }

        data_type = function_json.get('dataType')
        if isinstance(data_type, dict) and data_type.get('type') == 'STRUCT':
            children = data_type.get('specs') or data_type.get('specsList') or []
            normalized_children = [self._normalize_struct_child(child) for child in children]
            data_type['specs'] = normalized_children
            data_type['specsList'] = normalized_children
            function_json['dataType'] = data_type
            function_json['json'] = {
                child['identifier']: self._sample_value_for_type(child.get('dataType', {}))
                for child in normalized_children
                if child.get('identifier')
            }

        return function_json

    def _normalize_model_item(self, item: dict, function_type: str) -> dict:
        """整理为 ThingsKit profileData.thingsModel 条目。"""
        return {
            'functionType': function_type,
            'functionName': item.get('functionName', ''),
            'identifier': item.get('identifier', ''),
            'callType': item.get('callType') if function_type == 'services' else None,
            'accessMode': item.get('accessMode') if function_type == 'properties' else None,
            'eventType': item.get('eventType') if function_type == 'events' else None,
            'functionJson': self._normalize_function_json({**item, 'functionType': function_type}),
            'extensionDesc': item.get('extensionDesc'),
            'status': item.get('status', 1) or 1,
            'deviceProfileId': None,
            'remark': item.get('remark')
        }
    
    def _update_profile(self, profile: dict, things_model: list):
        """更新配置文件"""
        profile_id = profile.get('id', {}).get('id', '')
        
        update_data = {
            'id': profile.get('id'),
            'name': profile.get('name'),
            'deviceType': profile.get('deviceType'),
            'type': profile.get('type', 'DEFAULT'),
            'transportType': profile.get('transportType', 'DEFAULT'),
            'provisionType': profile.get('provisionType', 'DISABLED'),
            'profileData': {
                'configuration': profile.get('profileData', {}).get('configuration', {'type': 'DEFAULT'}),
                'transportConfiguration': profile.get('profileData', {}).get('transportConfiguration', {'type': 'DEFAULT'}),
                'provisionConfiguration': profile.get('profileData', {}).get('provisionConfiguration', {'type': 'DISABLED'}),
                'thingsModel': things_model
            }
        }
        
        result = self.session.post(f"{self.base_url}/api/deviceProfile", json=update_data)
        if result.status_code in [200, 201]:
            print(f"  [OK] 更新成功")
        else:
            print(f"  [ERROR] 更新失败: {result.status_code}")
    
    def _create_profile(self, name: str, things_model: list):
        """创建配置文件"""
        create_data = {
            'name': name,
            'deviceType': 'SENSOR',
            'type': 'DEFAULT',
            'transportType': 'DEFAULT',
            'provisionType': 'DISABLED',
            'profileData': {
                'configuration': {'type': 'DEFAULT'},
                'transportConfiguration': {'type': 'DEFAULT'},
                'provisionConfiguration': {'type': 'DISABLED'},
                'thingsModel': things_model
            }
        }
        
        result = self.session.post(f"{self.base_url}/api/deviceProfile", json=create_data)
        if result.status_code in [200, 201]:
            print(f"  [OK] 创建成功")
        else:
            print(f"  [ERROR] 创建失败: {result.status_code}")
    
    # ==================== 设备管理 ====================
    
    def list_devices(self):
        """列出所有设备"""
        print("\n" + "="*60)
        print("设备列表")
        print("="*60)
        
        result = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if result.status_code == 200:
            devices = result.json().get('data', [])
            print(f"\n共 {len(devices)} 个设备:")
            for d in devices:
                name = d.get('name', '')
                device_id = d.get('id', {}).get('id', '')
                profile = d.get('deviceProfileId', {}).get('id', '')
                print(f"  - {name}: {device_id[:20]}...")
    
    def create_device(self, name: str, profile_name: str):
        """创建设备"""
        print(f"\n创建设备: {name}")
        
        # 获取配置文件ID
        profiles = self._get_existing_profiles()
        profile = profiles.get(profile_name)
        if not profile:
            print(f"  [ERROR] 未找到配置文件: {profile_name}")
            return
        
        profile_id = profile.get('id', {}).get('id', '')
        
        data = {
            'name': name,
            'type': profile_name,
            'deviceProfileId': {'entityType': 'DEVICE_PROFILE', 'id': profile_id}
        }
        
        result = self.session.post(f"{self.base_url}/api/device", json=data)
        if result.status_code in [200, 201]:
            print(f"  [OK] 创建成功")
        else:
            print(f"  [INFO] 设备可能已存在")
    
    # ==================== 数据上传 ====================
    
    def send_telemetry(self, device_name: str, data: dict):
        """发送遥测数据"""
        print(f"\n发送遥测数据: {device_name}")
        print(f"  数据: {json.dumps(data, indent=2)}")
        
        # 获取设备ID
        device_id = self._get_device_id(device_name)
        if not device_id:
            print(f"  [ERROR] 未找到设备")
            return
        
        # 通过API发送
        url = f"{self.base_url}/api/plugins/telemetry/{device_id}/timeseries/any"
        result = self.session.post(url, json={'values': data})
        
        if result.status_code in [200, 201]:
            print(f"  [OK] 发送成功")
        else:
            print(f"  [ERROR] 发送失败: {result.status_code}")
    
    def send_attributes(self, device_name: str, data: dict):
        """发送属性数据"""
        print(f"\n发送属性数据: {device_name}")
        print(f"  数据: {json.dumps(data, indent=2)}")
        
        device_id = self._get_device_id(device_name)
        if not device_id:
            print(f"  [ERROR] 未找到设备")
            return
        
        url = f"{self.base_url}/api/plugins/telemetry/{device_id}/attributes/any"
        result = self.session.post(url, json={'values': data})
        
        if result.status_code in [200, 201]:
            print(f"  [OK] 发送成功")
        else:
            print(f"  [ERROR] 发送失败: {result.status_code}")
    
    def _get_device_id(self, device_name: str) -> Optional[str]:
        """获取设备ID"""
        result = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if result.status_code == 200:
            for d in result.json().get('data', []):
                if d.get('name') == device_name:
                    return d.get('id', {}).get('id', '')
        return None
    
    # ==================== 命令下发 ====================
    
    def call_service(self, device_name: str, method: str, params: dict = None):
        """调用设备服务"""
        print(f"\n调用服务: {device_name}")
        print(f"  方法: {method}")
        print(f"  参数: {json.dumps(params or {}, indent=2)}")
        
        device_id = self._get_device_id(device_name)
        if not device_id:
            print(f"  [ERROR] 未找到设备")
            return
        
        url = f"{self.base_url}/api/rpc/oneway/{device_id}"
        data = {
            'method': method,
            'params': params or {}
        }
        
        result = self.session.post(url, json=data)
        if result.status_code in [200, 201]:
            print(f"  [OK] 调用成功")
        else:
            print(f"  [ERROR] 调用失败: {result.status_code}")
    
    # ==================== 监控 ====================
    
    def get_device_telemetry(self, device_name: str, keys: list = None):
        """获取设备遥测数据"""
        print(f"\n获取遥测数据: {device_name}")
        
        device_id = self._get_device_id(device_name)
        if not device_id:
            print(f"  [ERROR] 未找到设备")
            return
        
        url = f"{self.base_url}/api/plugins/telemetry/{device_id}/values/timeseries"
        if keys:
            url += f"?keys={','.join(keys)}"
        
        result = self.session.get(url)
        if result.status_code == 200:
            data = result.json()
            print(f"  数据:")
            for key, values in data.items():
                if values:
                    latest = values[0]
                    print(f"    {key}: {latest.get('value')} ({latest.get('ts')})")
        else:
            print(f"  [ERROR] 获取失败: {result.status_code}")
    
    # ==================== 完整测试 ====================
    
    def full_test(self):
        """完整测试流程"""
        print("\n" + "="*60)
        print("ThingsKit 完整测试")
        print("="*60)
        
        # 1. 同步物模型
        print("\n[步骤1] 同步物模型")
        self.sync_models()
        
        # 2. 创建设备
        print("\n[步骤2] 创建设备")
        devices = [
            ("METER_MAIN_001", "单相电表"),
            ("ENV_001", "温湿度传感器"),
            ("RELAY_001", "继电器"),
            ("DTU_NODE_001", "DTU节点")
        ]
        for name, profile in devices:
            self.create_device(name, profile)
        
        # 3. 发送测试数据
        print("\n[步骤3] 发送测试数据")
        
        # 单相电表
        self.send_telemetry("METER_MAIN_001", {
            "voltage": 220.5,
            "current": 5.2,
            "active_power": 1146.6,
            "energy": 1234.56,
            "online": True
        })
        
        # 温湿度
        self.send_telemetry("ENV_001", {
            "temperature": 28.5,
            "humidity": 65.2,
            "online": True
        })
        
        # 继电器
        self.send_telemetry("RELAY_001", {
            "relay_state": 1,
            "control_mode": 0,
            "online": True
        })
        
        # 4. 验证数据
        print("\n[步骤4] 验证数据")
        self.get_device_telemetry("METER_MAIN_001")
        self.get_device_telemetry("ENV_001")
        
        print("\n" + "="*60)
        print("测试完成！")
        print("="*60)


def main():
    parser = argparse.ArgumentParser(description='ThingsKit 综合管理工具')
    parser.add_argument('command', nargs='?', default='full-test',
                       choices=['sync-model', 'create-device', 'send-data', 
                               'call-service', 'monitor', 'full-test', 'list-devices'],
                       help='执行的命令')
    parser.add_argument('--config', help='配置文件路径')
    parser.add_argument('--device', help='设备名称')
    parser.add_argument('--profile', help='配置文件名称')
    parser.add_argument('--method', help='服务方法名')
    parser.add_argument('--params', help='参数(JSON格式)')
    
    args = parser.parse_args()
    
    # 创建管理器
    manager = ThingsKitManager(args.config)
    
    # 登录
    if not manager.login():
        print("[ERROR] 登录失败")
        sys.exit(1)
    
    print("[OK] 登录成功")
    
    # 执行命令
    if args.command == 'sync-model':
        manager.sync_models()
    elif args.command == 'list-devices':
        manager.list_devices()
    elif args.command == 'create-device':
        if not args.device or not args.profile:
            print("[ERROR] 需要指定 --device 和 --profile")
            sys.exit(1)
        manager.create_device(args.device, args.profile)
    elif args.command == 'send-data':
        if not args.device or not args.params:
            print("[ERROR] 需要指定 --device 和 --params")
            sys.exit(1)
        manager.send_telemetry(args.device, json.loads(args.params))
    elif args.command == 'call-service':
        if not args.device or not args.method:
            print("[ERROR] 需要指定 --device 和 --method")
            sys.exit(1)
        params = json.loads(args.params) if args.params else {}
        manager.call_service(args.device, args.method, params)
    elif args.command == 'monitor':
        if not args.device:
            print("[ERROR] 需要指定 --device")
            sys.exit(1)
        manager.get_device_telemetry(args.device)
    elif args.command == 'full-test':
        manager.full_test()


if __name__ == '__main__':
    main()
