# -*- coding: utf-8 -*-
"""物模型同步 - 使用 /api/yt/things_model 接口（逐条上传+发布）"""
import json
import requests
import urllib3
from pathlib import Path

urllib3.disable_warnings()

BASE = "https://thingskit.aiotcomm.com.cn"
USER = "1"
PASS = "Sztu@123456"
MODEL_DIR = Path("c:/Users/傅仁杰/Desktop/things_model")

# 产品配置
PRODUCTS = {
    "DTU网关v1":      {"file": "gateway_model.json",           "type": "GATEWAY"},
    "单相电表":        {"file": "single_phase_meter_model.json", "type": "SENSOR"},
    "温湿度变送器":    {"file": "env_sensor_model.json",         "type": "SENSOR"},
    "继电器":          {"file": "relay_device_model.json",       "type": "SENSOR"},
    "DTU节点":         {"file": "dtu_node_model.json",           "type": "SENSOR"},
}


def login(session):
    r = session.post(f"{BASE}/api/auth/login",
                     json={"username": USER, "password": PASS}, verify=False)
    if r.status_code == 200:
        token = r.json().get("token")
        session.headers.update({
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json"
        })
        return True
    return False


def get_profile(session, name):
    r = session.get(f"{BASE}/api/deviceProfiles?pageSize=100&page=0", verify=False)
    if r.status_code == 200:
        for p in r.json().get("data", []):
            if p.get("name") == name:
                return p
    return None


def get_existing_items(session, profile_id):
    """获取已有的物模型项"""
    r = session.get(f"{BASE}/api/yt/things_model/{profile_id}/items?pageSize=200&page=0", verify=False)
    if r.status_code == 200:
        return r.json().get("items", [])
    return []


def build_item(item_data, function_type, profile_id=None):
    """构建单个物模型条目"""
    base = {
        "functionType": function_type,
        "functionName": item_data.get("functionName", ""),
        "identifier": item_data.get("identifier", ""),
        "extensionDesc": item_data.get("extensionDesc"),
        "remark": item_data.get("remark"),
        "deviceProfileId": profile_id,
        "status": 1,
    }

    if function_type == "properties":
        base["accessMode"] = item_data.get("accessMode", "r")
        base["functionJson"] = {
            "dataType": item_data.get("specs", {}).get("dataType", {})
        }
    elif function_type == "events":
        base["eventType"] = item_data.get("eventType", "ALERT")
        base["functionJson"] = {
            "outputData": item_data.get("outputData", [])
        }
    elif function_type == "services":
        base["callType"] = item_data.get("callType", "ASYNC")
        base["functionJson"] = {
            "inputData": item_data.get("inputData", []),
            "outputData": item_data.get("outputData", [])
        }

    return base


def publish_item(session, item_id):
    """发布单个物模型项"""
    r = session.put(f"{BASE}/api/yt/things_model/{item_id}",
                    json={}, verify=False)
    return r.status_code == 200


def sync_product(session, name, info):
    print(f"\n{'='*50}")
    print(f"同步: {name}")
    print(f"{'='*50}")

    # 加载物模型文件
    filepath = MODEL_DIR / info["file"]
    data = json.load(open(filepath, "r", encoding="utf-8"))

    props = data.get("properties", [])
    events = data.get("events", [])
    services = data.get("services", [])
    print(f"  本地: {len(props)}属性, {len(events)}事件, {len(services)}服务")

    # 确保产品存在
    profile = get_profile(session, name)
    if not profile:
        # 创建产品
        r = session.post(f"{BASE}/api/deviceProfile", json={
            "name": name,
            "deviceType": info["type"],
            "type": "DEFAULT",
            "transportType": "DEFAULT",
            "provisionType": "DISABLED",
            "profileData": {
                "configuration": {"type": "DEFAULT"},
                "transportConfiguration": {"type": "DEFAULT"},
                "provisionConfiguration": {"type": "DISABLED"},
                "thingsModel": []
            }
        }, verify=False)
        if r.status_code in [200, 201]:
            profile = get_profile(session, name)
            print(f"  [创建] 新产品已创建")
        else:
            print(f"  [ERROR] 创建产品失败: {r.status_code}")
            return False

    profile_id = profile.get("id", {}).get("id", "")
    print(f"  profile_id: {profile_id[:20]}...")

    # 删除已有物模型项（避免重复）
    existing = get_existing_items(session, profile_id)
    print(f"  已有物模型项: {len(existing)} 个")
    for item in existing:
        item_id = item.get("id", {}).get("id", "")
        if item_id:
            session.delete(f"{BASE}/api/yt/things_model/{item_id}", verify=False)

    # 逐条上传
    total = len(props) + len(events) + len(services)
    uploaded = 0

    for p in props:
        body = build_item(p, "properties", profile_id)
        r = session.post(f"{BASE}/api/yt/things_model", json=body, verify=False)
        if r.status_code in [200, 201]:
            upload_item_id = r.json().get("id", {})
            if isinstance(upload_item_id, dict):
                upload_item_id = upload_item_id.get("id", "")
            publish_item(session, upload_item_id)
            uploaded += 1

    for e in events:
        body = build_item(e, "events", profile_id)
        r = session.post(f"{BASE}/api/yt/things_model", json=body, verify=False)
        if r.status_code in [200, 201]:
            upload_item_id = r.json().get("id", {})
            if isinstance(upload_item_id, dict):
                upload_item_id = upload_item_id.get("id", "")
            publish_item(session, upload_item_id)
            uploaded += 1

    for s in services:
        body = build_item(s, "services", profile_id)
        r = session.post(f"{BASE}/api/yt/things_model", json=body, verify=False)
        if r.status_code in [200, 201]:
            upload_item_id = r.json().get("id", {})
            if isinstance(upload_item_id, dict):
                upload_item_id = upload_item_id.get("id", "")
            publish_item(session, upload_item_id)
            uploaded += 1

    print(f"  [OK] 上传+发布: {uploaded}/{total}")
    return True


def main():
    session = requests.Session()
    session.verify = False

    if not login(session):
        print("[ERROR] 登录失败")
        return

    print("[OK] 登录成功")

    for name, info in PRODUCTS.items():
        sync_product(session, name, info)

    print(f"\n{'='*50}")
    print("全部同步完成！")
    print("注意: 设备未被删除重建，Token 不变")
    print(f"{'='*50}")


if __name__ == "__main__":
    main()
