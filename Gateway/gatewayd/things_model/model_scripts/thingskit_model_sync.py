#!/usr/bin/env python3
"""
ThingsKit 物模型自动同步脚本

功能：
1. 读取本地物模型JSON文件
2. 通过ThingsKit API创建/更新设备配置文件(Device Profile)
3. 同步物模型（属性、事件、服务）

使用方法：
python3 thingskit_model_sync.py [--dry-run]
"""

import json
import os
import sys
import argparse
from pathlib import Path
from typing import Dict, List, Optional
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class ThingsKitClient:
    """ThingsKit API客户端"""
    
    def __init__(self, base_url: str, username: str, password: str):
        self.base_url = base_url.rstrip('/')
        self.username = username
        self.password = password
        self.token = None
        self.tenant_id = None
        self.session = requests.Session()
        self.session.verify = False
    
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
            else:
                print(f"[ERROR] 登录失败: {resp.status_code}")
                return False
        except Exception as e:
            print(f"[ERROR] 登录异常: {e}")
            return False
    
    def get(self, endpoint: str, params: dict = None) -> Optional[dict]:
        """GET请求"""
        url = f"{self.base_url}{endpoint}"
        try:
            resp = self.session.get(url, params=params)
            if resp.status_code == 200:
                return resp.json()
            return None
        except Exception as e:
            print(f"[ERROR] GET {endpoint} 异常: {e}")
            return None
    
    def post(self, endpoint: str, data: dict = None) -> Optional[dict]:
        """POST请求"""
        url = f"{self.base_url}{endpoint}"
        try:
            resp = self.session.post(url, json=data)
            if resp.status_code in [200, 201]:
                return resp.json()
            else:
                print(f"[WARN] POST {endpoint} 失败: {resp.status_code} {resp.text[:200]}")
                return None
        except Exception as e:
            print(f"[ERROR] POST {endpoint} 异常: {e}")
            return None


class ModelSync:
    """物模型同步器"""
    
    DEVICE_TYPE_MAP = {
        'rk3506_gateway': 'GATEWAY',
        'single_phase_meter': 'SENSOR',
        'env_sensor': 'SENSOR',
        'relay_device': 'SENSOR',
        'dtu_node': 'SENSOR'
    }
    
    def __init__(self, client: ThingsKitClient, model_dir: str, dry_run: bool = False):
        self.client = client
        self.model_dir = Path(model_dir)
        self.dry_run = dry_run
        self.products_config = None
        self.existing_profiles = {}
    
    def load_products_config(self) -> bool:
        """加载产品配置"""
        config_file = self.model_dir / "all_product_models.json"
        try:
            with open(config_file, 'r', encoding='utf-8') as f:
                self.products_config = json.load(f)
            print(f"[OK] 加载产品配置成功: {len(self.products_config.get('products', []))} 个产品")
            return True
        except Exception as e:
            print(f"[ERROR] 加载产品配置失败: {e}")
            return False
    
    def load_existing_profiles(self) -> bool:
        """加载已有的设备配置文件"""
        result = self.client.get('/api/deviceProfiles', {'pageSize': 100, 'page': 0})
        if result and 'data' in result:
            for profile in result['data']:
                name = profile.get('name', '')
                if name:
                    self.existing_profiles[name] = profile
            print(f"[OK] 加载已有配置文件: {len(self.existing_profiles)} 个")
            return True
        print("[WARN] 加载已有配置文件失败")
        return False
    
    def load_model_file(self, filename: str) -> Optional[Dict]:
        """加载物模型文件"""
        filepath = self.model_dir / filename
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"[ERROR] 加载物模型文件失败 {filename}: {e}")
            return None

    def _normalize_struct_child(self, child: Dict) -> Dict:
        """规范化 STRUCT 子字段。

        当前物模型文件可能来自平台导出，也可能是我们手写的中间格式。
        ThingsKit 的 STRUCT 子字段需要 functionName/identifier/dataType，
        测试数据还需要能按 identifier 生成 JSON 对象。
        """
        data_type = child.get('dataType', {})
        if isinstance(data_type, str):
            data_type = {
                'type': data_type,
                'specs': child.get('specs', {})
            }

        return {
            'functionName': child.get('functionName') or child.get('name') or child.get('identifier', ''),
            'identifier': child.get('identifier', ''),
            'remark': child.get('remark'),
            'dataType': data_type
        }

    def _sample_value_for_type(self, data_type: Dict):
        """按物模型类型生成一个可上报的示例值。"""
        type_name = (data_type or {}).get('type')
        if type_name in ('DOUBLE', 'FLOAT'):
            return 1.23
        if type_name in ('INT', 'LONG'):
            return 1
        if type_name == 'BOOL':
            return True
        if type_name == 'ENUM':
            specs_list = data_type.get('specsList') or []
            if specs_list:
                return specs_list[0].get('value', 0)
            specs = data_type.get('specs') or {}
            if specs:
                first_key = next(iter(specs.keys()))
                try:
                    return int(first_key)
                except ValueError:
                    return first_key
            return 0
        return ''

    def _normalize_function_json(self, item: Dict) -> Dict:
        """生成 ThingsKit profileData.thingsModel 中使用的 functionJson。

        优先使用当前模型文件已有的 functionJson。若模型仍是旧的 specs 格式，
        再做兼容转换。STRUCT 类型额外补充 json 示例对象，避免平台只知道
        结构定义但没有 JSON 形态，导致页面无法正确显示结构体字段。
        """
        function_json = dict(item.get('functionJson') or {})

        if not function_json:
            if item.get('functionType') == 'properties':
                function_json = {
                    'dataType': item.get('specs', {}).get('dataType', {})
                }
            elif item.get('functionType') == 'events':
                function_json = {
                    'outputData': item.get('outputData', [])
                }
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

    def _normalize_model_item(self, item: Dict, function_type: str) -> Dict:
        """将当前模型条目整理为 ThingsKit API 的 thingsModel 条目。"""
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
    
    def convert_to_thingskit_format(self, model_data: Dict) -> List[Dict]:
        """将本地物模型格式转换为ThingsKit平台格式"""
        things_model = []
        
        # 转换属性
        for prop in model_data.get('properties', []):
            things_model.append(self._normalize_model_item(prop, 'properties'))
        
        # 转换事件
        for event in model_data.get('events', []):
            if 'eventType' not in event:
                event = {**event, 'eventType': 'ALERT'}
            things_model.append(self._normalize_model_item(event, 'events'))
        
        # 转换服务
        for service in model_data.get('services', []):
            if 'callType' not in service:
                service = {**service, 'callType': 'ASYNC'}
            things_model.append(self._normalize_model_item(service, 'services'))
        
        return things_model
    
    def sync_product(self, product: Dict) -> bool:
        """同步单个产品"""
        product_id = product.get('productId')
        product_name = product.get('productName')
        model_file = product.get('modelFile')
        
        print(f"\n{'='*50}")
        print(f"同步产品: {product_name} ({product_id})")
        print(f"{'='*50}")
        
        # 加载物模型
        model_data = self.load_model_file(model_file)
        if not model_data:
            print(f"[SKIP] 跳过产品 {product_id}，无法加载物模型")
            return False
        
        # 转换格式
        things_model = self.convert_to_thingskit_format(model_data)
        
        # 统计
        properties = len([t for t in things_model if t.get('functionType') == 'properties'])
        events = len([t for t in things_model if t.get('functionType') == 'events'])
        services = len([t for t in things_model if t.get('functionType') == 'services'])
        
        print(f"  属性: {properties} 个")
        print(f"  事件: {events} 个")
        print(f"  服务: {services} 个")
        
        if self.dry_run:
            print(f"[DRY-RUN] 跳过实际创建")
            return True
        
        # 获取设备类型
        device_type = self.DEVICE_TYPE_MAP.get(product_id, 'SENSOR')
        
        # 检查是否已存在
        if product_name in self.existing_profiles:
            profile = self.existing_profiles[product_name]
            profile_id = profile.get('id', {}).get('id', '')
            print(f"  [UPDATE] 更新已有配置文件: {profile_id[:20]}...")
            
            # 构建更新数据（保持原有结构）
            update_data = {
                'id': profile.get('id'),
                'name': product_name,
                'deviceType': device_type,
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
        else:
            print(f"  [CREATE] 创建新配置文件...")
            
            # 构建创建数据
            update_data = {
                'name': product_name,
                'deviceType': device_type,
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
        
        # 使用POST API（ThingsKit用POST创建/更新）
        result = self.client.post('/api/deviceProfile', update_data)
        if result:
            print(f"  [OK] 同步成功")
            return True
        else:
            print(f"  [ERROR] 同步失败")
            return False
    
    def sync_all(self) -> bool:
        """同步所有产品"""
        if not self.products_config:
            print("[ERROR] 产品配置未加载")
            return False
        
        # 加载已有配置
        self.load_existing_profiles()
        
        products = self.products_config.get('products', [])
        success_count = 0
        
        for product in products:
            if self.sync_product(product):
                success_count += 1
        
        print(f"\n{'='*50}")
        print(f"同步完成: {success_count}/{len(products)} 个产品成功")
        print(f"{'='*50}")
        
        return success_count == len(products)


def load_gateway_config(config_path: str) -> dict:
    """加载gateway配置文件"""
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception as e:
        print(f"[ERROR] 加载gateway配置失败: {e}")
        return {}


def find_gateway_config() -> str:
    """查找gateway配置文件"""
    possible_paths = [
        "gatewayd/config/gateway_config.json",
        "../gatewayd/config/gateway_config.json",
        "../../gatewayd/config/gateway_config.json",
        "/userdata/gateway/config/gateway_config.json",
        os.path.expanduser("~/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/config/gateway_config.json")
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    return None


def main():
    parser = argparse.ArgumentParser(description='ThingsKit 物模型自动同步工具')
    parser.add_argument('--gateway-config', help='gateway配置文件路径')
    parser.add_argument('--dry-run', action='store_true', help='预览模式，不实际执行')
    parser.add_argument('--url', help='ThingsKit地址')
    parser.add_argument('--user', help='用户名')
    parser.add_argument('--password', help='密码')
    parser.add_argument('--model-dir', default=None, help='物模型目录')
    
    args = parser.parse_args()
    
    # 查找gateway配置
    config_path = args.gateway_config or find_gateway_config()
    if not config_path:
        print("[ERROR] 未找到gateway配置文件，请使用 --gateway-config 指定")
        sys.exit(1)
    
    print(f"[OK] 使用gateway配置: {config_path}")
    gateway_config = load_gateway_config(config_path)
    
    # 获取ThingsKit配置
    thingskit_config = gateway_config.get('thingskit', {})
    mqtt_host = thingskit_config.get('host', '')
    base_url = f"https://{mqtt_host}" if mqtt_host else "https://thingskit.aiotcomm.com.cn"
    
    # 命令行参数优先
    base_url = args.url or base_url
    username = args.user or '1'  # HTTP API使用账号1
    password = args.password or 'Sztu@123456'  # HTTP API密码
    
    # 物模型目录
    script_dir = Path(__file__).parent
    model_dir = args.model_dir or str(script_dir.parent)
    
    print(f"""
╔══════════════════════════════════════════════════╗
║       ThingsKit 物模型自动同步工具                ║
╚══════════════════════════════════════════════════╝

配置信息:
  ThingsKit地址: {base_url}
  用户名: {username}
  物模型目录: {model_dir}
  预览模式: {args.dry_run}
""")
    
    # 初始化客户端
    client = ThingsKitClient(base_url, username, password)
    
    # 登录
    if not client.login():
        print("[ERROR] 登录失败，请检查密码是否正确")
        sys.exit(1)
    
    print("[OK] 登录成功")
    
    # 初始化同步器
    sync = ModelSync(client, model_dir, args.dry_run)
    
    # 加载产品配置
    if not sync.load_products_config():
        print("[ERROR] 加载产品配置失败")
        sys.exit(1)
    
    # 执行同步
    if sync.sync_all():
        print("\n[SUCCESS] 所有产品同步成功！")
    else:
        print("\n[WARNING] 部分产品同步失败，请检查日志")
        sys.exit(1)


if __name__ == '__main__':
    main()
