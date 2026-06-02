#!/usr/bin/env python3
"""
ThingsKit 物模型格式转换工具

功能：
1. 将本地格式转换为ThingsKit平台格式
2. specs -> functionJson
3. 添加缺失字段

使用方法：
python3 thingskit_model_convert.py
"""

import json
import sys
from pathlib import Path


def normalize_struct_child(child: dict) -> dict:
    """规范化 STRUCT 子字段，兼容 name/dataType 字符串和平台导出格式。"""
    data_type = child.get("dataType", {})
    if isinstance(data_type, str):
        data_type = {
            "type": data_type,
            "specs": child.get("specs", {})
        }

    return {
        "functionName": child.get("functionName") or child.get("name") or child.get("identifier", ""),
        "identifier": child.get("identifier", ""),
        "remark": child.get("remark"),
        "dataType": data_type
    }


def sample_value_for_type(data_type: dict):
    """为 STRUCT 的 json 信息生成一个示例值。"""
    type_name = (data_type or {}).get("type")
    if type_name in ("DOUBLE", "FLOAT"):
        return 1.23
    if type_name in ("INT", "LONG"):
        return 1
    if type_name == "BOOL":
        return True
    if type_name == "ENUM":
        specs_list = data_type.get("specsList") or []
        if specs_list:
            return specs_list[0].get("value", 0)
        specs = data_type.get("specs") or {}
        if specs:
            first_key = next(iter(specs.keys()))
            try:
                return int(first_key)
            except ValueError:
                return first_key
        return 0
    return ""


def normalize_function_json(item: dict, function_type: str) -> dict:
    """生成平台 functionJson。

    当前模型文件优先使用已有 functionJson；旧格式才从 specs/inputData
    转换。STRUCT 会额外补 json 示例对象，方便 ThingsKit 展示结构体。
    """
    function_json = dict(item.get("functionJson") or {})

    if not function_json:
        if function_type == "properties":
            function_json = {
                "dataType": item.get("specs", {}).get("dataType", {})
            }
        elif function_type == "events":
            function_json = {
                "outputData": item.get("outputData", [])
            }
        elif function_type == "services":
            function_json = {
                "inputData": item.get("inputData", []),
                "outputData": item.get("outputData", [])
            }

    data_type = function_json.get("dataType")
    if isinstance(data_type, dict) and data_type.get("type") == "STRUCT":
        children = data_type.get("specs") or data_type.get("specsList") or []
        normalized_children = [normalize_struct_child(child) for child in children]
        data_type["specs"] = normalized_children
        data_type["specsList"] = normalized_children
        function_json["dataType"] = data_type
        function_json["json"] = {
            child["identifier"]: sample_value_for_type(child.get("dataType", {}))
            for child in normalized_children
            if child.get("identifier")
        }

    return function_json


def convert_item(item: dict, function_type: str) -> dict:
    """转换单个物模型条目为 ThingsKit profileData.thingsModel 格式。"""
    return {
        "functionType": function_type,
        "functionName": item.get("functionName", ""),
        "identifier": item.get("identifier", ""),
        "callType": item.get("callType") if function_type == "services" else None,
        "accessMode": item.get("accessMode") if function_type == "properties" else None,
        "eventType": item.get("eventType") if function_type == "events" else None,
        "functionJson": normalize_function_json({**item, "functionType": function_type}, function_type),
        "extensionDesc": item.get("extensionDesc"),
        "status": item.get("status", 1) or 1,
        "deviceProfileId": None,
        "remark": item.get("remark")
    }


def convert_property(prop: dict) -> dict:
    """转换属性格式"""
    return convert_item(prop, "properties")


def convert_event(event: dict) -> dict:
    """转换事件格式"""
    if "eventType" not in event:
        event = {**event, "eventType": "ALERT"}
    return convert_item(event, "events")


def convert_service(service: dict) -> dict:
    """转换服务格式"""
    if "callType" not in service:
        service = {**service, "callType": "ASYNC"}
    return convert_item(service, "services")


def convert_model_file(input_path: str, output_path: str):
    """转换物模型文件"""
    with open(input_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    result = {
        "properties": [convert_property(p) for p in data.get("properties", [])],
        "events": [convert_event(e) for e in data.get("events", [])],
        "services": [convert_service(s) for s in data.get("services", [])]
    }
    
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2, ensure_ascii=False)
    
    print(f"[OK] 转换完成: {output_path}")
    print(f"  属性: {len(result['properties'])} 个")
    print(f"  事件: {len(result['events'])} 个")
    print(f"  服务: {len(result['services'])} 个")


def main():
    """主函数"""
    model_dir = Path("/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model")
    output_dir = model_dir / "converted"
    output_dir.mkdir(exist_ok=True)
    
    print("="*60)
    print("ThingsKit 物模型格式转换工具")
    print("="*60)
    
    # 转换所有物模型文件
    model_files = list(model_dir.glob("*_model.json"))
    
    for filepath in model_files:
        output_path = output_dir / filepath.name
        convert_model_file(filepath, output_path)
    
    print("\n" + "="*60)
    print(f"转换完成！文件保存在: {output_dir}")
    print("="*60)
    
    # 显示转换后的样例
    print("\n转换后的格式样例:")
    with open(output_dir / "single_phase_meter_model.json", 'r', encoding='utf-8') as f:
        data = json.load(f)
        print(json.dumps(data['properties'][0], indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
