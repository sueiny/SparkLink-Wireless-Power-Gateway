# -*- coding: utf-8 -*-
"""MQTT在线 + HTTP API下发服务"""
import sys, json, time, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
import paho.mqtt.client as mqtt
import requests, urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

TOKEN = "JVlmu4M2FM8CbAsjnr5N"
BASE = "https://thingskit.aiotcomm.com.cn"

def on_connect(c, ud, fl, rc, prop):
    if rc == 0:
        print('[MQTT] 在线')
        c.subscribe('#')
        print('[MQTT] 已订阅全部topic(#)，等待命令...')

def on_message(c, ud, msg):
    try:
        p = msg.payload.decode()
        if len(p) > 5:
            print(f'\n>>> [{msg.topic}] {p[:300]}')
    except:
        pass

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.username_pw_set(TOKEN)
client.on_connect = on_connect
client.on_message = on_message
client.connect("thingskit.aiotcomm.com.cn", 11883, 60)
client.loop_start()
time.sleep(2)

# HTTP登录
s = requests.Session()
s.verify = False
r = s.post(f'{BASE}/api/auth/login', json={"username":"1","password":"Sztu@123456"})
s.headers.update({'Authorization': f'Bearer {r.json().get("token")}'})

# 找ENV_001的设备ID
r = s.get(f'{BASE}/api/tenant/devices?pageSize=200&page=0')
env = next((d for d in r.json().get('data',[]) if d.get('name')=='ENV_001'), None)
gw = next((d for d in r.json().get('data',[]) if 'dtu网关' in d.get('name','').lower()), None)

if env:
    eid = env.get('id',{}).get('id','') if isinstance(env.get('id'),dict) else env.get('id','')
    print(f'\nENV_001 ID: {eid}')

    # 用HTTP API下发服务测试
    print('\n通过HTTP API下发"阈值设置"服务...')
    r = s.post(f'{BASE}/api/rpc/oneway/{eid}', json={
        "method": "set_threshold",
        "params": {"temp_high": 40.0, "temp_low": 0.0, "hum_high": 85.0, "hum_low": 0.0}
    })
    print(f'  HTTP RPC → {r.status_code} ({r.text[:200]})')
    time.sleep(2)

if gw:
    gid = gw.get('id',{}).get('id','') if isinstance(gw.get('id'),dict) else gw.get('id','')
    print(f'\n网关 ID: {gid}')
    print('通过HTTP API下发"重启网关"服务...')
    r = s.post(f'{BASE}/api/rpc/oneway/{gid}', json={"method": "reboot", "params": {}})
    print(f'  HTTP RPC → {r.status_code} ({r.text[:200]})')
    time.sleep(2)

print('\n等10秒看MQTT是否收到...')
time.sleep(10)
print('超时，如果没收到说明网关子设备RPC不走MQTT到网关')

client.loop_stop()
client.disconnect()
