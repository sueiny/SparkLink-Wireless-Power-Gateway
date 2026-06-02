#!/usr/bin/env python3
"""
ThingsKit 物模型格式校验工具

功能：
1. 校验本地物模型JSON文件格式
2. 对比平台实际格式
3. 输出格式差异

使用方法：
python3 thingskit_model_validate.py
"""

import json
import sys
from pathlib import Path
from typing import Dict, List, Tuple


class ModelValidator:
    """物模型格式校验器"""
    
    # ThingsKit平台实际格式要求
    REQUIRED_FIELDS = {
        'properties': ['functionType', 'functionName', 'identifier', 'accessMode'],
        'events': ['functionType', 'functionName', 'identifier', 'eventType'],
        'services': ['functionType', 'functionName', 'identifier', 'callType']
    }
    
    # 数据类型定义
    VALID_DATA_TYPES = ['DOUBLE', 'INT', 'LONG', 'BOOL', 'TEXT', 'ENUM', 'STRUCT', 'ARRAY']
    
    # 访问模式
    VALID_ACCESS_MODES = ['r', 'rw']
    
    # 事件类型
    VALID_EVENT_TYPES = ['ALERT', 'INFO', 'ERROR']
    
    # 调用类型
    VALID_CALL_TYPES = ['SYNC', 'ASYNC']
    
    def __init__(self, model_dir: str):
        self.model_dir = Path(model_dir)
        self.errors = []
        self.warnings = []
    
    def validate_file(self, filename: str) -> Tuple[List[str], List[str]]:
        """校验单个文件"""
        self.errors = []
        self.warnings = []
        
        filepath = self.model_dir / filename
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except Exception as e:
            self.errors.append(f"文件读取失败: {e}")
            return self.errors, self.warnings
        
        # 校验properties
        for prop in data.get('properties', []):
            self._validate_property(prop, filename)
        
        # 校验events
        for event in data.get('events', []):
            self._validate_event(event, filename)
        
        # 校验services
        for service in data.get('services', []):
            self._validate_service(service, filename)
        
        return self.errors, self.warnings
    
    def _validate_property(self, prop: Dict, filename: str):
        """校验属性"""
        # 检查必需字段
        for field in self.REQUIRED_FIELDS['properties']:
            if field not in prop:
                self.errors.append(f"[{filename}] 属性缺少必需字段: {field} (属性: {prop.get('functionName', 'unknown')})")
        
        # 检查functionType
        if prop.get('functionType') != 'properties':
            self.errors.append(f"[{filename}] functionType应为'properties'，实际为: {prop.get('functionType')}")
        
        # 检查accessMode
        if prop.get('accessMode') not in self.VALID_ACCESS_MODES:
            self.errors.append(f"[{filename}] accessMode无效: {prop.get('accessMode')}，应为: {self.VALID_ACCESS_MODES}")
        
        # 检查数据类型（在specs或functionJson中）
        specs = prop.get('specs', {})
        data_type = specs.get('dataType', {})
        if not data_type:
            self.warnings.append(f"[{filename}] 属性缺少dataType定义: {prop.get('functionName')}")
        elif data_type.get('type') not in self.VALID_DATA_TYPES:
            self.errors.append(f"[{filename}] 数据类型无效: {data_type.get('type')}，应为: {self.VALID_DATA_TYPES}")
    
    def _validate_event(self, event: Dict, filename: str):
        """校验事件"""
        # 检查必需字段
        for field in self.REQUIRED_FIELDS['events']:
            if field not in event:
                self.errors.append(f"[{filename}] 事件缺少必需字段: {field} (事件: {event.get('functionName', 'unknown')})")
        
        # 检查functionType
        if event.get('functionType') != 'events':
            self.errors.append(f"[{filename}] functionType应为'events'，实际为: {event.get('functionType')}")
        
        # 检查eventType
        if event.get('eventType') not in self.VALID_EVENT_TYPES:
            self.errors.append(f"[{filename}] eventType无效: {event.get('eventType')}，应为: {self.VALID_EVENT_TYPES}")
        
        # 检查outputData
        if 'outputData' not in event:
            self.warnings.append(f"[{filename}] 事件缺少outputData: {event.get('functionName')}")
    
    def _validate_service(self, service: Dict, filename: str):
        """校验服务"""
        # 检查必需字段
        for field in self.REQUIRED_FIELDS['services']:
            if field not in service:
                self.errors.append(f"[{filename}] 服务缺少必需字段: {field} (服务: {service.get('functionName', 'unknown')})")
        
        # 检查functionType
        if service.get('functionType') != 'services':
            self.errors.append(f"[{filename}] functionType应为'services'，实际为: {service.get('functionType')}")
        
        # 检查callType
        if service.get('callType') not in self.VALID_CALL_TYPES:
            self.errors.append(f"[{filename}] callType无效: {service.get('callType')}，应为: {self.VALID_CALL_TYPES}")
        
        # 检查inputData和outputData
        if 'inputData' not in service:
            self.warnings.append(f"[{filename}] 服务缺少inputData: {service.get('functionName')}")
        if 'outputData' not in service:
            self.warnings.append(f"[{filename}] 服务缺少outputData: {service.get('functionName')}")
    
    def validate_all(self) -> Dict:
        """校验所有文件"""
        results = {}
        
        # 查找所有物模型文件
        model_files = list(self.model_dir.glob("*_model.json"))
        
        for filepath in model_files:
            filename = filepath.name
            errors, warnings = self.validate_file(filename)
            results[filename] = {
                'errors': errors,
                'warnings': warnings,
                'valid': len(errors) == 0
            }
        
        return results
    
    def print_report(self, results: Dict):
        """打印校验报告"""
        print("\n" + "="*60)
        print("ThingsKit 物模型格式校验报告")
        print("="*60)
        
        total_files = len(results)
        valid_files = sum(1 for r in results.values() if r['valid'])
        total_errors = sum(len(r['errors']) for r in results.values())
        total_warnings = sum(len(r['warnings']) for r in results.values())
        
        for filename, result in results.items():
            status = "✅ 通过" if result['valid'] else "❌ 失败"
            print(f"\n[{status}] {filename}")
            
            if result['errors']:
                print("  错误:")
                for error in result['errors']:
                    print(f"    ❌ {error}")
            
            if result['warnings']:
                print("  警告:")
                for warning in result['warnings']:
                    print(f"    ⚠️  {warning}")
        
        print("\n" + "="*60)
        print(f"校验总结:")
        print(f"  文件总数: {total_files}")
        print(f"  通过文件: {valid_files}")
        print(f"  失败文件: {total_files - valid_files}")
        print(f"  错误总数: {total_errors}")
        print(f"  警告总数: {total_warnings}")
        print("="*60)
        
        return total_errors == 0


def main():
    """主函数"""
    model_dir = "/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model"
    
    print("ThingsKit 物模型格式校验工具")
    print(f"校验目录: {model_dir}")
    
    validator = ModelValidator(model_dir)
    results = validator.validate_all()
    valid = validator.print_report(results)
    
    sys.exit(0 if valid else 1)


if __name__ == '__main__':
    main()
