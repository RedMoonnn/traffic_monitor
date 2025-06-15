from flask import Flask, jsonify, request
from flask_cors import CORS
from flask_socketio import SocketIO
import json
import time
from datetime import datetime

app = Flask(__name__)
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*")

# 存储最新的流量数据
latest_stats = {
    'total_bytes': 0,
    'peak_rate': 0,
    'avg_2s': 0,
    'avg_10s': 0,
    'avg_40s': 0,
    'timestamp': None
}

# 存储历史数据（最近1小时）
history_data = []
# 新增：分IP分方向历史（每个元素为一组list）
ip_stats_history = []

@app.route('/api/stats', methods=['GET'])
def get_stats():
    """获取最新的流量统计数据"""
    return jsonify(latest_stats)

@app.route('/api/history', methods=['GET'])
def get_history():
    """获取历史流量数据"""
    return jsonify(history_data)

@app.route('/api/ip_history', methods=['GET'])
def get_ip_history():
    """获取分IP分方向历史流量数据"""
    return jsonify(ip_stats_history)

@app.route('/api/update', methods=['POST'])
def update_stats():
    """更新流量统计数据"""
    global latest_stats, history_data, ip_stats_history
    
    data = request.json
    if not data:
        return jsonify({'error': 'No data provided'}), 400
    
    # 如果数据是数组，说明是分IP分方向统计
    if isinstance(data, list):
        # 存历史，允许无限增长（如需限制可改为更大值如3600）
        ip_stats_history.append(data)
        # 通过WebSocket广播分IP分方向统计
        socketio.emit('stats_update', data)
        return jsonify({'status': 'success'})
    
    # 否则是全局统计
    latest_stats = {
        'total_bytes': data.get('total', 0),
        'peak_rate': data.get('peak', 0),
        'avg_2s': data.get('avg2', 0),
        'avg_10s': data.get('avg10', 0),
        'avg_40s': data.get('avg40', 0),
        'timestamp': datetime.now().isoformat()
    }
    
    # 添加到历史数据
    history_data.append(latest_stats)
    
    # 只保留最近1小时的数据（假设每秒一个数据点）
    if len(history_data) > 3600:
        history_data.pop(0)
    
    # 通过WebSocket广播更新
    socketio.emit('stats_update', latest_stats)
    
    return jsonify({'status': 'success'})

@app.route('/api/packets', methods=['POST'])
def update_packets():
    """更新数据包信息"""
    data = request.json
    if not data:
        return jsonify({'error': 'No data provided'}), 400
    
    # 通过WebSocket广播数据包信息
    socketio.emit('packet_update', data)
    
    return jsonify({'status': 'success'})

if __name__ == '__main__':
    import eventlet
    import eventlet.wsgi
    socketio.run(app, host='0.0.0.0', port=5000, debug=True) 