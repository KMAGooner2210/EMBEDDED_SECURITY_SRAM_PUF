import sqlite3
import json
import paho.mqtt.client as mqtt
from threading import Thread
from flask import Flask, jsonify, render_template
from flask_cors import CORS

# --- CAU HINH HE THONG ---
DB_FILE = "puf_database.db"
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
THRESHOLD = 15.0

# Khởi tạo Flask Web Server, chỉ đường dẫn template sang thư mục 4_Dashboard
app = Flask(__name__, template_folder="../4_Dashboard")
CORS(app)

# 1. KHOI TAO DATABASE (Them bang auth_logs de luu nhat ky Web)
def init_db():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    # Bảng lưu vân tay gốc
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS devices (
            mac_address TEXT PRIMARY KEY,
            golden_puf TEXT NOT NULL
        )
    ''')
    # Bảng lưu nhật ký xác thực để hiển thị lên Web
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS auth_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            mac_address TEXT NOT NULL,
            diff_bits INTEGER NOT NULL,
            ber REAL NOT NULL,
            status TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()
    print("[DATABASE] Da khoi tao / kiem tra database.")

# 2. DANG KY THIET BI MAU
def register_sample_device():
    sample_puf = "61018163fe27f4af84012011ebb878dc03528291f7b0ddae31064b03f9e3fcff4c20946471dcde57e80c8c00b16da44d52f502627ade8fbdc885338199141f9f237203032b5bc92f080e6409d7f931e1a16b51337bfa71ff1e71249eb7b76e8a1e3d2014efc9ef5e8131122a766975cfe0e8699efd7d16bfc08140087bf377f6"
    sample_mac = "80:b5:4e:c7:7f:f8" 
    
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute("INSERT OR REPLACE INTO devices VALUES (?, ?)", (sample_mac, sample_puf))
    conn.commit()
    conn.close()
    print(f"[DATABASE] Da nap thiet bi mau MAC: {sample_mac} vao DB.")

# 3. GHI NHAT KY XAC THUC VAO DATABASE
def log_authentication(mac, diff_bits, ber, status):
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO auth_logs (mac_address, diff_bits, ber, status)
        VALUES (?, ?, ?, ?)
    ''', (mac, diff_bits, ber, status))
    conn.commit()
    conn.close()

# 4. HAM TRICH XUAT BIT
def get_bit_from_hex(hex_str, bit_index):
    byte_idx = bit_index // 8
    bit_pos = bit_index % 8
    byte_val = int(hex_str[byte_idx*2 : byte_idx*2+2], 16)
    return (byte_val >> bit_pos) & 1

# 5. SU KIEN KET NOI MQTT
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Da ket noi den Broker vao cong {MQTT_PORT}")
    client.subscribe("esp32/puf/response")

# 6. SU KIEN NHAN TIN NHAN MQTT (Xử lý và lưu DB)
def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    try:
        data = json.loads(payload)
    except Exception:
        return

    mac = data.get("mac")
    actual_puf_list = data.get("response")
    
    if not mac or not actual_puf_list:
        return

    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute("SELECT golden_puf FROM devices WHERE mac_address=?", (mac,))
    row = cursor.fetchone()
    conn.close()
    
    if not row:
        print(f"[XAC THUC] Tu choi! MAC {mac} chua dang ky.")
        log_authentication(mac, 0, 100.0, "REJECTED_NOT_ENROLLED")
        return
        
    golden_puf_hex = row[0]
    golden_puf_bytes = list(bytes.fromhex(golden_puf_hex))
    
    different_bits = 0
    for b1, b2 in zip(golden_puf_bytes, actual_puf_list):
        xor_result = b1 ^ b2
        different_bits += bin(xor_result).count('1')

    ber = (different_bits / 1024.0) * 100
    status = "ACCEPTED" if ber <= THRESHOLD else "REJECTED"
    
    # Ghi log xác thực vào DB để hiển thị lên giao diện Web
    log_authentication(mac, different_bits, ber, status)
    print(f"[XAC THUC] MAC: {mac} | BER: {ber:.2f}% | Trang thai: {status}")

# --- KHỞI CHẠY MQTT BROKER TRÊN MỘT LUỒNG RIÊNG ---
def start_mqtt():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

# --- CÁC ROUTE CỦA WEB SERVER (FLASK) ---
@app.route('/')
def home():
    return render_template("index.html")

@app.route('/api/logs')
def get_logs():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    
    # Lấy 10 nhật ký xác thực mới nhất
    cursor.execute('''
        SELECT datetime(timestamp, 'localtime'), mac_address, diff_bits, ber, status 
        FROM auth_logs ORDER BY id DESC LIMIT 10
    ''')
    rows = cursor.fetchall()
    
    # Tính toán thống kê nhanh
    cursor.execute("SELECT COUNT(*) FROM auth_logs")
    total_attempts = cursor.fetchone()[0]
    
    cursor.execute("SELECT COUNT(*) FROM auth_logs WHERE status='ACCEPTED'")
    accepted_attempts = cursor.fetchone()[0]
    
    cursor.execute("SELECT AVG(ber) FROM auth_logs")
    avg_ber_row = cursor.fetchone()[0]
    avg_ber = avg_ber_row if avg_ber_row is not None else 0.0
    
    conn.close()
    
    logs = []
    for row in rows:
        logs.append({
            "timestamp": row[0],
            "mac": row[1],
            "diff_bits": row[2],
            "ber": row[3],
            "status": row[4]
        })
        
    return jsonify({
        "logs": logs,
        "stats": {
            "total": total_attempts,
            "accepted": accepted_attempts,
            "avg_ber": avg_ber
        }
    })

if __name__ == "__main__":
    init_db()
    register_sample_device()
    
    # Chạy MQTT Client ở luồng phụ (Background Thread)
    mqtt_thread = Thread(target=start_mqtt)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    
    # Chạy Flask Web Server ở luồng chính (Main Thread) tại cổng 5000
    print("[SERVER] Flask Web Server dang chay tai: http://localhost:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)
