# -*- coding: utf-8 -*-
"""温湿度变送器 ENV_001 事件上报 / 服务下发 测试"""
import json
import time
import paho.mqtt.client as mqtt

# ========== 配置 ==========
BROKER = "thingskit.aiotcomm.com.cn"
PORT = 11883
TOKEN = "JVlmu4M2FM8CbAsjnr5N"
DEVICE = "ENV_001"

# 事件示例数据
EVENT_DATA = {
    "high_temperature": {"temperature": 48.5, "threshold": 40.0},
    "high_humidity":    {"humidity": 93.2, "threshold": 80.0},
}

# 服务回复模板
SERVICE_REPLY = {
    "set_threshold":       {"result": True},
    "set_report_interval": {"result": True},
    "calibrate":           {"result": True},
}


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("[OK] 已连接 ThingsKit")
        client.subscribe("v1/devices/me/rpc/request/+")
        print("[订阅] v1/devices/me/rpc/request/+ (等待服务下发)")
    else:
        print(f"[FAIL] 连接失败, rc={rc}")


def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8")
    print(f"\n>>> 收到服务下发 <<<")
    print(f"    Topic: {msg.topic}")
    try:
        data = json.loads(payload)
        method = data.get("method", "N/A")
        params = data.get("params", {})
        request_id = data.get("id", "")
        print(f"    服务: {method}")
        print(f"    参数: {json.dumps(params, ensure_ascii=False)}")
        if request_id:
            reply = SERVICE_REPLY.get(method, {"result": True})
            reply_topic = f"v1/devices/me/rpc/response/{request_id}"
            client.publish(reply_topic, json.dumps(reply))
            print(f"    [回复] {json.dumps(reply)}")
    except json.JSONDecodeError:
        print(f"    Raw: {payload}")


def publish_event(client, event_id, data=None):
    topic = f"v1/devices/event/{DEVICE}/{event_id}"
    payload = json.dumps(data or EVENT_DATA.get(event_id, {}))
    client.publish(topic, payload)
    print(f"  [上报] {event_id} → {payload}")


def run():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(TOKEN)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_timeout = 5  # 5秒超时

    print(f"连接 {BROKER}:{PORT} ...")
    try:
        client.connect(BROKER, PORT, 60)
    except Exception as e:
        print(f"[FAIL] 连接失败: {e}")
        return

    client.loop_start()
    time.sleep(1.5)

    if not client.is_connected():
        print("[FAIL] 未连接成功，请检查端口是否正确")
        client.loop_stop()
        return

    print("\n" + "=" * 45)
    print("  ENV_001 温湿度变送器 测试")
    print("=" * 45)
    print("  1. 上报高温告警")
    print("  2. 上报高湿告警")
    print("  3. 同时上报两个")
    print("  q. 退出")
    print("=" * 45)

    while True:
        try:
            cmd = input("\n请选择 > ").strip()
            if cmd == "1":
                publish_event(client, "high_temperature")
            elif cmd == "2":
                publish_event(client, "high_humidity")
            elif cmd == "3":
                publish_event(client, "high_temperature")
                time.sleep(0.3)
                publish_event(client, "high_humidity")
            elif cmd.lower() == "q":
                break
            else:
                print("无效选项")
        except KeyboardInterrupt:
            print("\n退出")
            break

    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    run()
