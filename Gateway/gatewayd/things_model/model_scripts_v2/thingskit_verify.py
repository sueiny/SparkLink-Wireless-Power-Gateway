#!/usr/bin/env python3
"""
ThingsKit 物模型验证脚本

功能：
1. 验证产品物模型定义
2. 验证设备数据
3. 验证数据格式

使用方法：
python3 thingskit_verify.py
"""

import json
import requests
import urllib3
import time

urllib3.disable_warnings()


def main():
    base_url = "https://thingskit.aiotcomm.com.cn"
    
    # 登录
    r = requests.post(f"{base_url}/api/auth/login", 
                     json={"username": "1", "password": "Sztu@123456"}, 
                     verify=False)
    token = r.json()["token"]
    h = {"Authorization": f"Bearer {token}"}
    
    print("="*60)
    print("ThingsKit 物模型验证")
    print("="*60)
    
    # 1. 验证产品物模型
    print("\n[1] 产品物模型验证")
    print("-"*40)
    
    profiles = requests.get(f"{base_url}/api/deviceProfiles?pageSize=100&page=0", 
                           headers=h, verify=False).json()["data"]
    
    target_profiles = ["单相电表", "温湿度变送器", "继电器", "DTU节点", "DTU网关v1"]
    
    for p in profiles:
        name = p.get("name", "")
        if name in target_profiles:
            pid = p["id"]["id"]
            r = requests.get(f"{base_url}/api/deviceProfile/{pid}", headers=h, verify=False)
            profile = r.json()
            items = profile.get("profileData", {}).get("thingsModel", [])
            
            props = len([i for i in items if i.get("functionType") == "properties"])
            events = len([i for i in items if i.get("functionType") == "events"])
            services = len([i for i in items if i.get("functionType") == "services"])
            
            print(f"\n{name}:")
            print(f"  属性: {props}, 事件: {events}, 服务: {services}")
            
            # 检查关键字段
            for item in items:
                ident = item.get("identifier", "")
                if ident in ["voltage", "temperature", "relay_state", "online", "topology"]:
                    fj = item.get("functionJson", {})
                    dt = fj.get("dataType", {}) if fj else {}
                    print(f"  {ident}: type={dt.get('type', 'null')}")
    
    # 2. 验证设备数据
    print("\n[2] 设备数据验证")
    print("-"*40)
    
    devs = requests.get(f"{base_url}/api/tenant/devices?pageSize=100&page=0", 
                       headers=h, verify=False).json()["data"]
    
    target_devices = ["DTU_001", "METER_001", "ENV_001", "RELAY_001"]
    
    for d in devs:
        name = d.get("name", "")
        if name in target_devices:
            did = d["id"]["id"]
            r = requests.get(f"{base_url}/api/plugins/telemetry/DEVICE/{did}/values/timeseries", 
                           headers=h, verify=False)
            if r.status_code == 200:
                data = r.json()
                count = len([k for k in data.keys() if data[k]])
                print(f"\n{name}: {count} 个数据字段")
                for k, v in data.items():
                    if v and k in ["voltage", "temperature", "relay_state", "online"]:
                        print(f"  {k}: {v[0].get('value')}")
    
    # 3. 验证数据格式
    print("\n[3] 数据格式验证")
    print("-"*40)
    
    # 检查BOOL类型
    for d in devs:
        if d.get("name") == "METER_001":
            did = d["id"]["id"]
            r = requests.get(f"{base_url}/api/plugins/telemetry/DEVICE/{did}/values/timeseries", 
                           headers=h, verify=False)
            if r.status_code == 200:
                data = r.json()
                online = data.get("online", [])
                if online:
                    val = online[0].get("value")
                    print(f"METER_001 online: {val} (类型: {type(val).__name__})")
    
    # 检查STRUCT类型
    for d in devs:
        if d.get("name") == "DTU_001":
            did = d["id"]["id"]
            r = requests.get(f"{base_url}/api/plugins/telemetry/DEVICE/{did}/values/timeseries", 
                           headers=h, verify=False)
            if r.status_code == 200:
                data = r.json()
                topology = data.get("topology", [])
                if topology:
                    val = topology[0].get("value")
                    print(f"DTU_001 topology: {val}")
    
    print("\n" + "="*60)
    print("验证完成！")
    print("="*60)


if __name__ == "__main__":
    main()
