#!/usr/bin/env python3
"""
批量清理旧设备并创建新DTU设备
"""

import json
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# 配置
BASE_URL = "https://thingskit.aiotcomm.com.cn"
USERNAME = "1"
PASSWORD = "Sztu@123456"

# 需要删除的旧设备名称模式
OLD_DEVICE_PREFIXES = ["NODE_"]

# 需要创建的新DTU设备
NEW_DTU_DEVICES = [f"DTU_{i:03d}" for i in range(12, 32)]  # DTU_012 ~ DTU_031

# 设备类型映射
DEVICE_TYPE_MAP = {
    'rk3506_gateway': 'GATEWAY',
    'single_phase_meter': 'SENSOR',
    'env_sensor': 'SENSOR',
    'relay_device': 'SENSOR',
    'dtu_node': 'SENSOR'
}

# 网关设备ID（dtu网关）
GATEWAY_DEVICE_ID = None

def get_existing_device_info(session):
    """获取现有设备的组织ID和网关ID"""
    global GATEWAY_DEVICE_ID
    
    resp = session.get(f"{BASE_URL}/api/tenant/devices?pageSize=100&page=0")
    if resp.status_code != 200:
        return None, None
    
    devices = resp.json().get('data', [])
    org_id = None
    
    for d in devices:
        name = d.get('name', '')
        # 获取网关设备ID
        if name == 'dtu网关':
            GATEWAY_DEVICE_ID = d.get('id', {}).get('id', '')
        # 优先从DTU/METER等业务设备获取组织ID
        if name.startswith(('DTU_', 'METER_', 'ENV_', 'RELAY_')) and d.get('organizationId'):
            org_id = d.get('organizationId')
            break
    
    # 如果业务设备没有，从任意设备获取
    if not org_id:
        for d in devices:
            if d.get('organizationId'):
                org_id = d.get('organizationId')
                break
    
    return org_id, GATEWAY_DEVICE_ID


def main():
    global GATEWAY_DEVICE_ID
    
    session = requests.Session()
    session.verify = False
    
    # 登录
    print("登录中...")
    resp = session.post(f"{BASE_URL}/api/auth/login", json={"username": USERNAME, "password": PASSWORD})
    if resp.status_code != 200:
        print(f"[ERROR] 登录失败: {resp.status_code}")
        return
    
    token = resp.json().get('token')
    session.headers.update({
        'Authorization': f'Bearer {token}',
        'Content-Type': 'application/json'
    })
    print("[OK] 登录成功")
    
    # 获取现有设备信息（组织ID和网关ID）
    print("\n获取现有设备信息...")
    org_id, gateway_id = get_existing_device_info(session)
    print(f"  组织ID: {org_id}")
    print(f"  网关ID: {gateway_id}")
    
    # 获取所有设备
    print("\n获取设备列表...")
    resp = session.get(f"{BASE_URL}/api/tenant/devices?pageSize=100&page=0")
    if resp.status_code != 200:
        print(f"[ERROR] 获取设备失败: {resp.status_code}")
        return
    
    devices = resp.json().get('data', [])
    print(f"共 {len(devices)} 个设备")
    
    # 删除旧的 NODE_XX 设备
    print("\n" + "="*50)
    print("删除旧的 NODE_XX 设备")
    print("="*50)
    
    deleted_count = 0
    for device in devices:
        name = device.get('name', '')
        if any(name.startswith(prefix) for prefix in OLD_DEVICE_PREFIXES):
            device_id = device.get('id', {}).get('id', '')
            print(f"  删除: {name} ({device_id[:20]}...)")
            resp = session.delete(f"{BASE_URL}/api/device/{device_id}")
            if resp.status_code in [200, 204]:
                print(f"    [OK] 删除成功")
                deleted_count += 1
            else:
                print(f"    [ERROR] 删除失败: {resp.status_code}")
    
    print(f"\n共删除 {deleted_count} 个旧设备")
    
    # 获取产品配置文件列表
    print("\n" + "="*50)
    print("获取产品配置文件")
    print("="*50)
    
    resp = session.get(f"{BASE_URL}/api/deviceProfiles?pageSize=100&page=0")
    profiles = {}
    if resp.status_code == 200:
        for p in resp.json().get('data', []):
            profiles[p.get('name')] = p
            print(f"  - {p.get('name')}")
    
    # 获取 DTU节点 产品的配置文件ID
    dtu_profile = profiles.get('DTU节点')
    if not dtu_profile:
        print("[ERROR] 未找到 'DTU节点' 产品配置文件")
        return
    
    dtu_profile_id = dtu_profile.get('id', {}).get('id', '')
    print(f"\nDTU节点配置文件ID: {dtu_profile_id[:20]}...")
    
    # 创建新的 DTU 设备
    print("\n" + "="*50)
    print("创建新的 DTU 设备")
    print("="*50)
    
    # 重新获取设备列表（删除后）
    resp = session.get(f"{BASE_URL}/api/tenant/devices?pageSize=100&page=0")
    existing_devices = set()
    if resp.status_code == 200:
        for d in resp.json().get('data', []):
            existing_devices.add(d.get('name', ''))
    
    created_count = 0
    for device_name in NEW_DTU_DEVICES:
        if device_name in existing_devices:
            print(f"  跳过: {device_name} (已存在)")
            continue
        
        print(f"  创建: {device_name}")
        data = {
            'name': device_name,
            'type': 'DTU节点',
            'deviceProfileId': {'entityType': 'DEVICE_PROFILE', 'id': dtu_profile_id},
            'deviceType': 'SENSOR',
            'organizationId': org_id,
            'gatewayId': {'entityType': 'DEVICE', 'id': gateway_id} if gateway_id else None
        }
        resp = session.post(f"{BASE_URL}/api/device", json=data)
        if resp.status_code in [200, 201]:
            print(f"    [OK] 创建成功")
            created_count += 1
        else:
            print(f"    [ERROR] 创建失败: {resp.status_code} {resp.text[:100]}")
    
    print(f"\n共创建 {created_count} 个新设备")
    
    # 最终设备列表
    print("\n" + "="*50)
    print("最终设备列表")
    print("="*50)
    
    resp = session.get(f"{BASE_URL}/api/tenant/devices?pageSize=100&page=0")
    if resp.status_code == 200:
        devices = resp.json().get('data', [])
        dtu_devices = [d for d in devices if d.get('name', '').startswith('DTU_')]
        meter_devices = [d for d in devices if d.get('name', '').startswith('METER_')]
        env_devices = [d for d in devices if d.get('name', '').startswith('ENV_')]
        relay_devices = [d for d in devices if d.get('name', '').startswith('RELAY_')]
        
        print(f"\nDTU设备 ({len(dtu_devices)}个):")
        for d in sorted(dtu_devices, key=lambda x: x.get('name', '')):
            print(f"  - {d.get('name')} (类型: {d.get('deviceType')}, 组织: {d.get('organizationId', 'N/A')[:8]}...)")
        
        print(f"\n电表设备 ({len(meter_devices)}个):")
        for d in sorted(meter_devices, key=lambda x: x.get('name', '')):
            print(f"  - {d.get('name')} (类型: {d.get('deviceType')}, 组织: {d.get('organizationId', 'N/A')[:8]}...)")
        
        print(f"\n温湿度设备 ({len(env_devices)}个):")
        for d in sorted(env_devices, key=lambda x: x.get('name', '')):
            print(f"  - {d.get('name')} (类型: {d.get('deviceType')}, 组织: {d.get('organizationId', 'N/A')[:8]}...)")
        
        print(f"\n继电器设备 ({len(relay_devices)}个):")
        for d in sorted(relay_devices, key=lambda x: x.get('name', '')):
            print(f"  - {d.get('name')} (类型: {d.get('deviceType')}, 组织: {d.get('organizationId', 'N/A')[:8]}...)")
    
    print("\n" + "="*50)
    print("完成！")
    print("="*50)

if __name__ == '__main__':
    main()
