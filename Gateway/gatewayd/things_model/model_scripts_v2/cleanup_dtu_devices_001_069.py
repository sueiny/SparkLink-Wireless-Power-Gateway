#!/usr/bin/env python3
"""
Keep only DTU_001..DTU_069 in ThingsKit.

The script only touches devices whose names start with "DTU_". It creates any
missing DTU_001..DTU_069 devices with the "DTU节点" profile and deletes other
DTU_ prefixed devices. Use --rebuild-range to delete and recreate an explicit
DTU sub-range when existing devices are bound to the wrong/default profile.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = os.getenv("THINGSKIT_BASE_URL", "https://thingskit.aiotcomm.com.cn")
USERNAME = os.getenv("THINGSKIT_USERNAME", "1")
PASSWORD = os.getenv("THINGSKIT_PASSWORD", "Sztu@123456")

DTU_PROFILE_NAME = "DTU节点"
GATEWAY_NAMES = ("dtu网关", "DTU网关", "DTU网关v1")
KEEP_NAMES = {f"DTU_{i:03d}" for i in range(1, 70)}
DTU_PREFIX_RE = re.compile(r"^DTU_")
DTU_NAME_RE = re.compile(r"^DTU_(\d{3})$")


def request_json(session, method, url, **kwargs):
    resp = session.request(method, url, verify=False, timeout=30, **kwargs)
    if resp.status_code >= 400:
        raise RuntimeError(f"{method} {url} failed: {resp.status_code} {resp.text[:200]}")
    if not resp.text:
        return {}
    return resp.json()


def login(session):
    data = request_json(
        session,
        "POST",
        f"{BASE_URL}/api/auth/login",
        json={"username": USERNAME, "password": PASSWORD},
    )
    token = data.get("token")
    if not token:
        raise RuntimeError("login succeeded but token is missing")
    session.headers.update({
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    })


def list_paged(session, path, page_size=100):
    items = []
    page = 0
    while True:
        data = request_json(
            session,
            "GET",
            f"{BASE_URL}{path}",
            params={"pageSize": page_size, "page": page},
        )
        batch = data.get("data", [])
        items.extend(batch)

        if "hasNext" in data:
            if not data.get("hasNext"):
                break
        elif "totalPages" in data:
            if page + 1 >= int(data.get("totalPages") or 0):
                break
        elif len(batch) < page_size:
            break
        if not batch:
            break

        page += 1
        if page > 1000:
            raise RuntimeError("pagination guard triggered")
    return items


def list_devices(session):
    return list_paged(session, "/api/tenant/devices", page_size=100)


def list_profiles(session):
    return list_paged(session, "/api/deviceProfiles", page_size=100)


def device_id(device):
    return (device.get("id") or {}).get("id", "")


def profile_id(profile):
    return (profile.get("id") or {}).get("id", "")


def device_profile_id(device):
    profile = device.get("deviceProfileId") or {}
    if isinstance(profile, dict):
        return profile.get("id", "")
    return ""


def device_profile_name(device, profile_names_by_id):
    name = device.get("deviceProfileName") or device.get("type") or ""
    pid = device_profile_id(device)
    if pid and pid in profile_names_by_id:
        name = profile_names_by_id[pid]
    return name or "(unknown)"


def parse_rebuild_range(text):
    if not text:
        return set()
    match = re.fullmatch(r"\s*(\d{1,3})\s*-\s*(\d{1,3})\s*", text)
    if not match:
        raise ValueError("range must look like 32-69")
    start = int(match.group(1))
    end = int(match.group(2))
    if start > end:
        raise ValueError("range start must be <= end")
    if start < 1 or end > 69:
        raise ValueError("range must be inside 1-69")
    return {f"DTU_{i:03d}" for i in range(start, end + 1)}


def find_gateway_id(devices):
    for name in GATEWAY_NAMES:
        for device in devices:
            if device.get("name") == name and device.get("deviceType") == "GATEWAY":
                return device_id(device)
    for name in GATEWAY_NAMES:
        for device in devices:
            if device.get("name") == name:
                return device_id(device)
    return ""


def find_org_id(devices):
    for device in devices:
        if device.get("name") in KEEP_NAMES and device.get("organizationId"):
            return device.get("organizationId")
    for device in devices:
        if DTU_PREFIX_RE.match(device.get("name", "")) and device.get("organizationId"):
            return device.get("organizationId")
    for device in devices:
        if device.get("organizationId"):
            return device.get("organizationId")
    return None


def compute_plan(devices, rebuild_names=None):
    rebuild_names = set(rebuild_names or set())
    by_name = defaultdict(list)
    for device in devices:
        name = device.get("name", "")
        if DTU_PREFIX_RE.match(name):
            by_name[name].append(device)

    keep_devices = []
    delete_devices = []
    for name in sorted(by_name):
        candidates = by_name[name]
        if name in rebuild_names:
            delete_devices.extend(candidates)
        elif name in KEEP_NAMES:
            keep_devices.append(candidates[0])
            delete_devices.extend(candidates[1:])
        else:
            delete_devices.extend(candidates)

    existing_keep_names = {device.get("name", "") for device in keep_devices}
    missing_names = sorted((KEEP_NAMES - existing_keep_names) | rebuild_names)
    return keep_devices, delete_devices, missing_names


def create_relation(session, gateway_id, child_id):
    if not gateway_id or not child_id:
        return
    relation = {
        "from": {"id": gateway_id, "entityType": "DEVICE"},
        "to": {"id": child_id, "entityType": "DEVICE"},
        "type": "Contains",
        "typeGroup": "COMMON",
    }
    request_json(session, "POST", f"{BASE_URL}/api/relation", json=relation)


def create_device(session, name, dtu_profile_id, org_id, gateway_id):
    payload = {
        "name": name,
        "type": DTU_PROFILE_NAME,
        "deviceType": "SENSOR",
        "deviceProfileId": {"entityType": "DEVICE_PROFILE", "id": dtu_profile_id},
    }
    if org_id:
        payload["organizationId"] = org_id
    if gateway_id:
        payload["gatewayId"] = {"entityType": "DEVICE", "id": gateway_id}

    created = request_json(session, "POST", f"{BASE_URL}/api/device", json=payload)
    create_relation(session, gateway_id, device_id(created))


def print_names(title, names):
    print(f"{title}: {len(names)}")
    for name in names:
        print(f"  - {name}")


def print_rebuild_details(devices, rebuild_names, profile_names_by_id):
    if not rebuild_names:
        return
    by_name = defaultdict(list)
    for device in devices:
        by_name[device.get("name", "")].append(device)

    print(f"重建范围: {len(rebuild_names)}")
    for name in sorted(rebuild_names):
        candidates = by_name.get(name, [])
        if not candidates:
            print(f"  - {name}: 当前不存在，将创建为 {DTU_PROFILE_NAME}")
            continue
        for device in candidates:
            print(
                f"  - {name}: 当前 profile={device_profile_name(device, profile_names_by_id)}, "
                f"id={device_id(device)} -> 删除后重建为 {DTU_PROFILE_NAME}"
            )


def main():
    parser = argparse.ArgumentParser(description="Keep only DTU_001..DTU_069 on ThingsKit")
    parser.add_argument("--apply", action="store_true", help="execute deletes and creates")
    parser.add_argument(
        "--rebuild-range",
        default="",
        help="delete and recreate an explicit DTU range, for example 32-69",
    )
    args = parser.parse_args()

    try:
        rebuild_names = parse_rebuild_range(args.rebuild_range)
    except ValueError as exc:
        raise SystemExit(f"invalid --rebuild-range: {exc}")

    session = requests.Session()
    session.verify = False

    print(f"ThingsKit: {BASE_URL}")
    print("登录中...")
    login(session)
    print("[OK] 登录成功")

    devices = list_devices(session)
    profiles = list_profiles(session)
    dtu_profile = next((p for p in profiles if p.get("name") == DTU_PROFILE_NAME), None)
    if not dtu_profile:
        raise RuntimeError(f"missing device profile: {DTU_PROFILE_NAME}")
    profile_names_by_id = {
        profile_id(profile): profile.get("name", "")
        for profile in profiles
        if profile_id(profile)
    }

    gateway_id = find_gateway_id(devices)
    org_id = find_org_id(devices)
    keep_devices, delete_devices, missing_names = compute_plan(devices, rebuild_names)

    dtu_devices = [d for d in devices if DTU_PREFIX_RE.match(d.get("name", ""))]
    print(f"云端设备总数: {len(devices)}")
    print(f"DTU_ 前缀设备数: {len(dtu_devices)}")
    print(f"将保留: {len(keep_devices)}")
    print(f"将创建: {len(missing_names)}")
    print(f"将删除: {len(delete_devices)}")
    print(f"DTU profile id: {profile_id(dtu_profile)}")
    print(f"gateway id: {gateway_id or '(none)'}")
    print(f"organization id: {org_id or '(none)'}")

    print_rebuild_details(devices, rebuild_names, profile_names_by_id)
    print_names("创建名单", missing_names)
    print_names("删除名单", [d.get("name", "") for d in delete_devices])

    if not args.apply:
        print("\nDRY-RUN: 未修改云端。确认名单后加 --apply 执行。")
        return 0

    failures = 0
    dtu_profile_id = profile_id(dtu_profile)

    print("\n执行删除...")
    for device in delete_devices:
        name = device.get("name", "")
        did = device_id(device)
        try:
            resp = session.delete(f"{BASE_URL}/api/device/{did}", verify=False, timeout=30)
            if resp.status_code in (200, 202, 204):
                print(f"  [OK] 删除 {name}")
            else:
                print(f"  [FAIL] 删除 {name}: {resp.status_code} {resp.text[:160]}")
                failures += 1
        except Exception as exc:
            print(f"  [FAIL] 删除 {name}: {exc}")
            failures += 1

    print("\n执行创建...")
    for name in missing_names:
        try:
            create_device(session, name, dtu_profile_id, org_id, gateway_id)
            print(f"  [OK] 创建 {name}")
        except Exception as exc:
            print(f"  [FAIL] 创建 {name}: {exc}")
            failures += 1

    final_devices = list_devices(session)
    final_keep, final_delete, final_missing = compute_plan(final_devices)
    print("\n最终校验:")
    print(f"  DTU_001..DTU_069: {len(final_keep)}")
    print(f"  额外 DTU_ 前缀设备: {len(final_delete)}")
    print(f"  缺失 DTU_001..DTU_069: {len(final_missing)}")
    if final_delete:
        print_names("仍需删除", [d.get("name", "") for d in final_delete])
    if final_missing:
        print_names("仍缺失", final_missing)

    if failures or final_delete or final_missing or len(final_keep) != 69:
        return 1
    print("[OK] 云平台 DTU_ 前缀设备已收敛到 DTU_001..DTU_069")
    return 0


if __name__ == "__main__":
    sys.exit(main())
