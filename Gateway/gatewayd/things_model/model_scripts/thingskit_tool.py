#!/usr/bin/env python3
"""
ThingsKit 完整管理工具

功能：
1. 物模型同步
2. 设备管理
3. 数据上传（HTTP API）
4. 数据查询
5. 命令下发

使用方法：
python3 thingskit_tool.py [command]
"""

import json
import time
import sys
import requests
import urllib3
from pathlib import Path

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False


class ThingsKitTool:
    """ThingsKit工具类"""
    
    def __init__(self):
        self.base_url = "https://thingskit.aiotcomm.com.cn"
        self.username = "1"
        self.password = "Sztu@123456"
        self.token = None
        self.session = requests.Session()
        self.session.verify = False
        
        # MQTT配置
        self.mqtt_host = "thingskit.aiotcomm.com.cn"
        self.mqtt_port = 11883
        self.mqtt_client_id = "46dc3ebf25bf4cdb9cd01deb6092b7ef"
        self.mqtt_username = "123"
        self.mqtt_password = "123"
        
        self.model_dir = Path("/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model")
        
        # 设备配置（只使用用户指定的产品）
        self.devices = {
            "METER_001": "单相电表",
            "METER_002": "单相电表",
            "METER_003": "单相电表",
            "METER_004": "单相电表",
            "METER_005": "单相电表",
            "METER_006": "单相电表",
            "METER_007": "单相电表",
            "ENV_001": "温湿度变送器",
            "ENV_002": "温湿度变送器",
            "RELAY_001": "继电器",
            "RELAY_002": "继电器",
            "DTU_001": "DTU节点",
            "DTU_002": "DTU节点",
            "DTU_003": "DTU节点",
            "DTU_004": "DTU节点",
            "DTU_005": "DTU节点",
            "DTU_006": "DTU节点",
            "DTU_007": "DTU节点",
            "DTU_008": "DTU节点",
            "DTU_009": "DTU节点",
            "DTU_010": "DTU节点",
            "DTU_011": "DTU节点",
            "dtu网关": "DTU网关v1"
        }
        
        # 产品类型映射
        self.product_types = {
            "测试": "GATEWAY",
            "DTU网关v1": "GATEWAY",
            "DTU节点": "SENSOR",
            "继电器": "SENSOR",
            "单相电表": "SENSOR",
            "温湿度变送器": "SENSOR"
        }
    
    def login(self) -> bool:
        """登录"""
        resp = self.session.post(f"{self.base_url}/api/auth/login", json={
            "username": self.username,
            "password": self.password
        })
        if resp.status_code == 200:
            self.token = resp.json().get('token')
            self.session.headers.update({
                'Authorization': f'Bearer {self.token}',
                'Content-Type': 'application/json'
            })
            return True
        return False
    
    def get_device_id(self, name: str) -> str:
        """获取设备ID"""
        resp = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if resp.status_code == 200:
            for d in resp.json().get('data', []):
                if d.get('name') == name:
                    return d.get('id', {}).get('id', '')
        return ""
    
    def get_profile_id(self, name: str) -> str:
        """获取配置文件ID"""
        resp = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        if resp.status_code == 200:
            for p in resp.json().get('data', []):
                if p.get('name') == name:
                    return p.get('id', {}).get('id', '')
        return ""
    
    # ==================== 数据上传 ====================
    
    def send_telemetry(self, device_name: str, data: dict) -> bool:
        """发送遥测数据（通过MQTT网关代发）"""
        if not HAS_MQTT:
            print("[ERROR] 未安装paho-mqtt: pip3 install paho-mqtt")
            return False
        
        # 检查是否是网关子设备
        device_id = self.get_device_id(device_name)
        if not device_id:
            print(f"[ERROR] 未找到设备: {device_name}")
            return False
        
        ts = int(time.time() * 1000)
        payload = {device_name: [{"ts": ts, "values": data}]}
        
        return self._mqtt_publish("v1/gateway/telemetry", json.dumps(payload))
    
    def send_gateway_attributes(self, data: dict) -> bool:
        """发送网关属性"""
        if not HAS_MQTT:
            print("[ERROR] 未安装paho-mqtt")
            return False
        
        return self._mqtt_publish("v1/devices/me/attributes", json.dumps(data))
    
    def _mqtt_publish(self, topic: str, payload: str) -> bool:
        """通过MQTT发送数据"""
        connected = False
        
        def on_connect(client, userdata, flags, rc):
            nonlocal connected
            if rc == 0:
                connected = True
        
        client = mqtt.Client(client_id=self.mqtt_client_id)
        client.username_pw_set(self.mqtt_username, self.mqtt_password)
        client.on_connect = on_connect
        
        try:
            client.connect(self.mqtt_host, self.mqtt_port, 10)
            client.loop_start()
            time.sleep(1)
            
            if connected:
                client.publish(topic, payload)
                time.sleep(1)
                client.loop_stop()
                client.disconnect()
                return True
            else:
                client.loop_stop()
                client.disconnect()
                return False
        except Exception as e:
            print(f"[ERROR] MQTT异常: {e}")
            return False
    
    # ==================== 数据查询 ====================
    
    def get_telemetry(self, device_name: str, keys: list = None) -> dict:
        """获取遥测数据"""
        device_id = self.get_device_id(device_name)
        if not device_id:
            print(f"[ERROR] 未找到设备: {device_name}")
            return {}
        
        url = f"{self.base_url}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries"
        if keys:
            url += f"?keys={','.join(keys)}"
        
        resp = self.session.get(url)
        if resp.status_code == 200:
            return resp.json()
        return {}
    
    # ==================== 设备管理 ====================
    
    def list_devices(self):
        """列出设备"""
        resp = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if resp.status_code == 200:
            devices = resp.json().get('data', [])
            print(f"\n设备列表 ({len(devices)} 个):")
            for d in devices:
                name = d.get('name', '')
                did = d.get('id', {}).get('id', '')
                device_type = d.get('deviceType', '')
                print(f"  - {name}: {did[:20]}... 类型={device_type}")
    
    def create_device(self, name: str, profile_name: str) -> bool:
        """创建设备"""
        profile_id = self.get_profile_id(profile_name)
        if not profile_id:
            print(f"[ERROR] 未找到配置文件: {profile_name}")
            return False
        
        # 检查设备是否已存在
        device_id = self.get_device_id(name)
        if device_id:
            # 更新设备配置
            resp = self.session.get(f"{self.base_url}/api/device/{device_id}")
            if resp.status_code == 200:
                device = resp.json()
                device['type'] = profile_name
                device['deviceType'] = 'DIRECT_CONNECTION'
                device['deviceProfileId'] = {"entityType": "DEVICE_PROFILE", "id": profile_id}
                device['deviceData'] = {
                    "configuration": {"type": "DEFAULT"},
                    "transportConfiguration": {"type": "MQTT"}
                }
                
                resp2 = self.session.post(f"{self.base_url}/api/device", json=device)
                if resp2.status_code in [200, 201]:
                    print(f"[OK] 更新设备配置: {name}")
                    return True
            print(f"[INFO] 设备已存在: {name}")
            return True
        
        # 创建新设备
        resp = self.session.post(f"{self.base_url}/api/device", json={
            "name": name,
            "type": profile_name,
            "deviceType": "DIRECT_CONNECTION",
            "deviceProfileId": {"entityType": "DEVICE_PROFILE", "id": profile_id},
            "deviceData": {
                "configuration": {"type": "DEFAULT"},
                "transportConfiguration": {"type": "MQTT"}
            }
        })
        
        if resp.status_code in [200, 201]:
            print(f"[OK] 创建设备成功: {name}")
            return True
        else:
            print(f"[ERROR] 创建设备失败: {name}")
            return False
    
    def fix_devices(self):
        """修复设备配置"""
        print("\n修复设备配置...")
        
        # 获取网关设备ID
        gateway_id = self.get_gateway_device_id()
        if not gateway_id:
            print("[WARN] 未找到网关设备")
        
        for name, profile_name in self.devices.items():
            device_id = self.get_device_id(name)
            profile_id = self.get_profile_id(profile_name)
            
            if not device_id or not profile_id:
                continue
            
            # 获取设备详情
            resp = self.session.get(f"{self.base_url}/api/device/{device_id}")
            if resp.status_code != 200:
                continue
            
            device = resp.json()
            
            # 根据产品类型设置设备类型
            device_type = self.product_types.get(profile_name, "SENSOR")
            
            # 更新配置
            device['type'] = profile_name
            device['deviceType'] = device_type
            device['deviceProfileId'] = {"entityType": "DEVICE_PROFILE", "id": profile_id}
            
            # 子设备设置网关关系
            if gateway_id and device_type == "SENSOR":
                device['gatewayId'] = {"entityType": "DEVICE", "id": gateway_id}
            
            resp2 = self.session.post(f"{self.base_url}/api/device", json=device)
            if resp2.status_code in [200, 201]:
                # 创建设备关系
                if gateway_id:
                    self._create_relation(gateway_id, device_id)
                print(f"  [OK] {name}: 类型={device_type}, 配置={profile_name}")
            else:
                print(f"  [ERROR] {name}: {resp2.status_code}")
    
    def _create_relation(self, from_id: str, to_id: str):
        """创建设备关系"""
        relation_data = {
            "from": {"id": from_id, "entityType": "DEVICE"},
            "to": {"id": to_id, "entityType": "DEVICE"},
            "type": "Contains",
            "typeGroup": "COMMON"
        }
        self.session.post(f"{self.base_url}/api/relation", json=relation_data)
    
    def get_gateway_device_id(self) -> str:
        """获取网关设备ID"""
        # 查找dtu网关设备
        resp = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if resp.status_code == 200:
            for d in resp.json().get('data', []):
                if d.get('name') == 'dtu网关' and d.get('deviceType') == 'GATEWAY':
                    return d.get('id', {}).get('id', '')
        return ""
    
    # ==================== 物模型同步 ====================
    
    def sync_models(self):
        """同步物模型"""
        config_file = self.model_dir / "all_product_models.json"
        with open(config_file, 'r', encoding='utf-8') as f:
            products_config = json.load(f)
        
        # 获取已有配置文件
        existing_profiles = {}
        resp = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        if resp.status_code == 200:
            for p in resp.json().get('data', []):
                existing_profiles[p.get('name')] = p
        
        for product in products_config.get('products', []):
            name = product.get('productName')
            model_file = product.get('modelFile')
            
            print(f"\n同步: {name}")
            
            # 检查是否已存在
            if name in existing_profiles:
                profile = existing_profiles[name]
                profile_id = profile.get('id', {}).get('id', '')
            else:
                profile = None
                profile_id = None
            
            # 加载物模型（传递profile_id）
            things_model = self._load_model(model_file, profile_id)
            if not things_model:
                print(f"  [SKIP] 无法加载物模型文件")
                continue
            
            # 统计
            props = len([t for t in things_model if t.get('functionType') == 'properties'])
            events = len([t for t in things_model if t.get('functionType') == 'events'])
            services = len([t for t in things_model if t.get('functionType') == 'services'])
            print(f"  属性: {props}, 事件: {events}, 服务: {services}")
            
            # 更新或创建
            if profile:
                self._update_profile(profile, things_model)
            else:
                self._create_profile(name, things_model)
    
    def _sample_value_for_type(self, data_type: dict):
        """为 STRUCT 的 json 信息生成一个示例值。"""
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

    def _normalize_struct_child(self, child: dict) -> dict:
        """规范化 STRUCT 子字段。"""
        data_type = child.get('dataType', {})
        if isinstance(data_type, str):
            data_type = {'type': data_type, 'specs': child.get('specs', {})}
        return {
            'functionName': child.get('functionName') or child.get('name') or child.get('identifier', ''),
            'identifier': child.get('identifier', ''),
            'remark': child.get('remark'),
            'dataType': data_type
        }

    def _normalize_function_json(self, item: dict) -> dict:
        """保留当前 functionJson，并为 STRUCT 补 json 示例对象。"""
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

    def _normalize_model_item(self, item: dict, function_type: str, profile_id: str = None) -> dict:
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
            'status': 1,
            'deviceProfileId': profile_id,
            'remark': item.get('remark')
        }

    def _load_model(self, filename: str, profile_id: str = None) -> list:
        """加载物模型文件并转换为平台格式。"""
        filepath = self.model_dir / filename
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                data = json.load(f)
            return self._convert_model(data, profile_id)
        except Exception as e:
            print(f"  [ERROR] 加载失败: {e}")
            return []
    
    def _convert_model(self, model_data: dict, profile_id: str = None) -> list:
        """转换物模型格式"""
        things_model = []
        
        for prop in model_data.get('properties', []):
            things_model.append(self._normalize_model_item(prop, 'properties', profile_id))
        
        for event in model_data.get('events', []):
            if 'eventType' not in event:
                event = {**event, 'eventType': 'ALERT'}
            things_model.append(self._normalize_model_item(event, 'events', profile_id))
        
        for service in model_data.get('services', []):
            if 'callType' not in service:
                service = {**service, 'callType': 'ASYNC'}
            things_model.append(self._normalize_model_item(service, 'services', profile_id))
        
        return things_model
    
    def _update_profile(self, profile: dict, things_model: list):
        """更新配置文件（删除重建方式，包括设备）"""
        profile_id = profile.get('id', {})
        profile_name = profile.get('name', '')
        pid = profile_id.get('id', '') if isinstance(profile_id, dict) else str(profile_id)
        
        print(f"  [重建] 删除旧产品和设备并重建...")
        
        # 步骤1: 获取引用该产品的设备并删除
        devices = self._get_devices_by_profile(pid)
        if devices:
            print(f"  [重建] 删除 {len(devices)} 个旧设备...")
            for dev in devices:
                dev_id = dev.get('id', {}).get('id', '')
                dev_name = dev.get('name', '')
                self.session.delete(f"{self.base_url}/api/device/{dev_id}")
                print(f"    删除设备: {dev_name}")
        
        # 步骤2: 删除旧产品
        resp = self.session.delete(f"{self.base_url}/api/deviceProfile/{pid}")
        if resp.status_code == 200:
            print(f"  [重建] 旧产品已删除")
        else:
            print(f"  [WARN] 删除产品失败: {resp.status_code}")
            self._update_profile_inplace(profile, things_model)
            return
        
        # 步骤3: 创建新产品
        create_data = {
            'name': profile_name,
            'deviceType': profile.get('deviceType', 'SENSOR'),
            'type': profile.get('type', 'DEFAULT'),
            'transportType': profile.get('transportType', 'DEFAULT'),
            'provisionType': profile.get('provisionType', 'DISABLED'),
            'profileData': {
                'configuration': {'type': 'DEFAULT'},
                'transportConfiguration': {'type': 'DEFAULT'},
                'provisionConfiguration': {'type': 'DISABLED'},
                'thingsModel': things_model
            }
        }
        
        resp2 = self.session.post(f"{self.base_url}/api/deviceProfile", json=create_data)
        if resp2.status_code in [200, 201]:
            new_profile = resp2.json()
            new_id = new_profile.get('id')
            print(f"  [重建] 新产品已创建")
            
            # 步骤4: 重新创建设备
            if devices:
                print(f"  [重建] 重新创建 {len(devices)} 个设备...")
                for dev in devices:
                    dev_name = dev.get('name', '')
                    dev_type = dev.get('type', '')
                    gateway_id = dev.get('gatewayId')
                    is_gateway = dev.get('deviceType') == 'GATEWAY'
                    
                    create_dev = {
                        'name': dev_name,
                        'type': dev_type,
                        'deviceType': 'GATEWAY' if is_gateway else 'SENSOR',
                        'deviceProfileId': new_id,
                    }
                    if gateway_id:
                        create_dev['gatewayId'] = gateway_id
                    if is_gateway:
                        create_dev['additionalInfo'] = {'gateway': True}
                    
                    r = self.session.post(f"{self.base_url}/api/device", json=create_dev)
                    if r.status_code in [200, 201]:
                        new_dev = r.json()
                        new_dev_id = new_dev.get('id', {}).get('id', '')
                        
                        # 网关设备需要设置MQTT凭证
                        if is_gateway:
                            self._set_gateway_credentials(new_dev_id)
                        
                        # 重建关系
                        if gateway_id:
                            gw_id = gateway_id.get('id', '')
                            self._create_relation(gw_id, new_dev_id)
                        print(f"    创建设备: {dev_name}")
                    else:
                        print(f"    [ERROR] 创建失败: {dev_name}")
            
            props = len([t for t in things_model if t.get('functionType') == 'properties'])
            events = len([t for t in things_model if t.get('functionType') == 'events'])
            services = len([t for t in things_model if t.get('functionType') == 'services'])
            print(f"  [OK] 更新成功: {props}属性, {events}事件, {services}服务")
        else:
            print(f"  [ERROR] 创建产品失败: {resp2.status_code}")
    
    def _set_gateway_credentials(self, device_id: str):
        """设置网关MQTT凭证"""
        # 获取当前凭证
        resp = self.session.get(f"{self.base_url}/api/device/{device_id}/credentials")
        if resp.status_code != 200:
            return
        
        cred = resp.json()
        cred_id = cred.get('id', {}).get('id', '')
        
        # 更新凭证为MQTT_BASIC
        cred_data = {
            'id': cred.get('id'),
            'deviceId': {'id': device_id, 'entityType': 'DEVICE'},
            'credentialsType': 'MQTT_BASIC',
            'credentialsId': device_id,
            'credentialsValue': json.dumps({
                'clientId': self.mqtt_client_id,
                'userName': self.mqtt_username,
                'password': self.mqtt_password
            })
        }
        
        resp2 = self.session.post(f"{self.base_url}/api/device/credentials", json=cred_data)
        if resp2.status_code == 200:
            print(f"    设置MQTT凭证成功")
        else:
            print(f"    [WARN] 设置凭证失败: {resp2.status_code}")
    
    def _update_profile_inplace(self, profile: dict, things_model: list):
        """原地更新配置文件（备用方案）"""
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
        
        resp = self.session.post(f"{self.base_url}/api/deviceProfile", json=update_data)
        if resp.status_code in [200, 201]:
            props = len([t for t in things_model if t.get('functionType') == 'properties'])
            events = len([t for t in things_model if t.get('functionType') == 'events'])
            services = len([t for t in things_model if t.get('functionType') == 'services'])
            print(f"  [OK] 更新成功: {props}属性, {events}事件, {services}服务")
        else:
            print(f"  [ERROR] 更新失败: {resp.status_code}")
    
    def _get_default_profile_id(self) -> dict:
        """获取default配置文件ID"""
        resp = self.session.get(f"{self.base_url}/api/deviceProfiles?pageSize=100&page=0")
        if resp.status_code == 200:
            for p in resp.json().get('data', []):
                if p.get('name') == 'default':
                    return p.get('id')
        return {"entityType": "DEVICE_PROFILE", "id": "7a23c940-1b73-11f0-a95d-07b2a804c205"}
    
    def _get_devices_by_profile(self, profile_id: str) -> list:
        """获取使用指定配置文件的设备"""
        devices = []
        resp = self.session.get(f"{self.base_url}/api/tenant/devices?pageSize=100&page=0")
        if resp.status_code == 200:
            for d in resp.json().get('data', []):
                pid = d.get('deviceProfileId', {})
                if isinstance(pid, dict) and pid.get('id') == profile_id:
                    devices.append(d)
        return devices
    
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
        
        resp = self.session.post(f"{self.base_url}/api/deviceProfile", json=create_data)
        if resp.status_code in [200, 201]:
            print(f"  [OK] 创建成功")
        else:
            print(f"  [ERROR] 创建失败: {resp.status_code}")
    
    # ==================== 完整测试 ====================
    
    def full_test(self):
        """完整测试"""
        print("="*60)
        print("ThingsKit 完整测试")
        print("="*60)
        
        # 1. 同步物模型
        print("\n[1] 同步物模型...")
        self.sync_models()
        
        # 2. 修复设备配置
        print("\n[2] 修复设备配置...")
        self.fix_devices()
        
        # 3. 发送测试数据
        print("\n[3] 发送测试数据...")
        
        # 网关属性
        self.send_gateway_attributes({
            "network_type": "wifi",
            "cloud_connected": 1,
            "device_count": 7
        })
        print("[OK] 网关属性发送成功")
        
        # 子设备遥测
        ts = int(time.time() * 1000)
        payload = {
            "METER_MAIN_001": [{"ts": ts, "values": {
                "voltage": 220.5, "current": 5.2, "active_power": 1146.6,
                "power_factor": 0.98, "frequency": 50.0, "energy": 1234.56,
                "relay_status": 1, "meter_role": 0,
                "branch_power_sum": 1050.0, "power_loss": 96.6, "loss_rate": 8.43,
                "online": 1
            }}],
            "METER_BRANCH_001": [{"ts": ts, "values": {
                "voltage": 220.1, "current": 3.2, "active_power": 704.3,
                "power_factor": 0.97, "frequency": 50.0, "energy": 567.89,
                "relay_status": 1, "meter_role": 1, "online": 1
            }}],
            "METER_BRANCH_002": [{"ts": ts, "values": {
                "voltage": 219.8, "current": 2.8, "active_power": 615.4,
                "power_factor": 0.96, "frequency": 50.0, "energy": 456.78,
                "relay_status": 1, "meter_role": 1, "online": 1
            }}],
            "METER_BRANCH_003": [{"ts": ts, "values": {
                "voltage": 220.3, "current": 4.1, "active_power": 903.2,
                "power_factor": 0.95, "frequency": 50.0, "energy": 789.01,
                "relay_status": 1, "meter_role": 1, "online": 1
            }}],
            "ENV_001": [{"ts": ts, "values": {
                "temperature": 28.5, "humidity": 65.2, "online": 1
            }}],
            "RELAY_001": [{"ts": ts, "values": {
                "relay_state": 1, "control_mode": 0, "online": 1
            }}],
            "DTU_NODE_001": [{"ts": ts, "values": {
                "role": 1, "mac": "A1:A2:A3:A4:A5:A6", "name": "DTU_001",
                "online": 1, "uptime": 3600, "parent_mac": "",
                "child_count": 2, "child_macs": "B1:B2:B3:B4:B5:B6,C1:C2:C3:C4:C5:C6",
                "modbus_count": 3, "collect_cycle": 5000
            }}]
        }
        
        if self._mqtt_publish("v1/gateway/telemetry", json.dumps(payload)):
            print("[OK] 子设备遥测发送成功")
        else:
            print("[ERROR] 子设备遥测发送失败")
        
        # 4. 查询数据验证
        print("\n[4] 查询数据验证...")
        
        print("\nMETER_MAIN_001 遥测数据:")
        data = self.get_telemetry("METER_MAIN_001", ["voltage", "current", "energy"])
        for key, values in data.items():
            if values and key != 'values':
                print(f"  {key}: {values[0].get('value')}")
        
        print("\nENV_001 遥测数据:")
        data = self.get_telemetry("ENV_001", ["temperature", "humidity"])
        for key, values in data.items():
            if values and key != 'values':
                print(f"  {key}: {values[0].get('value')}")
        
        print("\n" + "="*60)
        print("测试完成！")
        print("="*60)
        print("ThingsKit 完整测试")
        print("="*60)
        
        # 1. 同步物模型
        print("\n[1] 同步物模型...")
        self.sync_models()
        
        # 2. 修复设备配置
        print("\n[2] 修复设备配置...")
        self.fix_devices()
        
        # 3. 发送测试数据
        print("\n[3] 发送测试数据...")
        
        # 单相电表
        self.send_telemetry("METER_MAIN_001", {
            "voltage": 220.5,
            "current": 5.2,
            "active_power": 1146.6,
            "power_factor": 0.98,
            "frequency": 50.0,
            "energy": 1234.56,
            "relay_status": 85,
            "online": 1
        })
        
        # 温湿度
        self.send_telemetry("ENV_001", {
            "temperature": 28.5,
            "humidity": 65.2,
            "online": 1
        })
        
        # 继电器
        self.send_telemetry("RELAY_001", {
            "relay_state": 1,
            "control_mode": 0,
            "online": 1
        })
        
        # DTU节点
        self.send_telemetry("DTU_NODE_001", {
            "role": 1,
            "mac": "A1:A2:A3:A4:A5:A6",
            "online": 1,
            "uptime": 3600,
            "child_count": 2,
            "modbus_count": 3,
            "collect_cycle": 5000
        })
        
        # 4. 查询数据验证
        print("\n[4] 查询数据验证...")
        
        print("\nMETER_MAIN_001 遥测数据:")
        data = self.get_telemetry("METER_MAIN_001", ["voltage", "current"])
        for key, values in data.items():
            if values:
                print(f"  {key}: {values[0].get('value')}")
        
        print("\nENV_001 遥测数据:")
        data = self.get_telemetry("ENV_001", ["temperature", "humidity"])
        for key, values in data.items():
            if values:
                print(f"  {key}: {values[0].get('value')}")
        
        print("\n" + "="*60)
        print("测试完成！")
        print("="*60)


def main():
    tool = ThingsKitTool()
    
    if not tool.login():
        print("[ERROR] 登录失败")
        sys.exit(1)
    
    print("[OK] 登录成功")
    
    if len(sys.argv) > 1:
        cmd = sys.argv[1]
        if cmd == "sync":
            tool.sync_models()
        elif cmd == "devices":
            tool.list_devices()
        elif cmd == "fix":
            tool.fix_devices()
        elif cmd == "test":
            tool.full_test()
        elif cmd == "send":
            # 发送测试数据
            if len(sys.argv) > 2:
                device_name = sys.argv[2]
                data = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {"test": 1}
                tool.send_telemetry(device_name, data)
            else:
                print("用法: thingskit_tool.py send <device_name> [json_data]")
        elif cmd == "get":
            # 获取设备数据
            if len(sys.argv) > 2:
                device_name = sys.argv[2]
                data = tool.get_telemetry(device_name)
                print(json.dumps(data, indent=2, ensure_ascii=False))
            else:
                print("用法: thingskit_tool.py get <device_name>")
        else:
            print(f"未知命令: {cmd}")
            print("可用命令: sync, devices, fix, test, send, get")
    else:
        tool.full_test()


if __name__ == '__main__':
    main()
