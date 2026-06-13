# -*- coding: utf-8 -*-
"""
族谱管理系统 - Web 服务器
Flask 静态文件服务 + API 反向代理，解决 CORS 问题
性能优化：
  - requests.Session 连接池复用 TCP 连接，避免每次请求都重新握手
  - 响应体直接转发（不反序列化再序列化），消除双重 JSON 处理开销
  - 请求耗时日志，方便定位慢接口
"""
import os
import json
import time
import requests
from requests.adapters import HTTPAdapter
from flask import (
    Flask, render_template, request, jsonify, Response,
    session, redirect, url_for, send_from_directory,
)

app = Flask(__name__)
app.secret_key = os.urandom(24).hex()

BASE_URL = "http://localhost:8088"
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")

# ---- 连接池 ----
# 复用 TCP 连接，避免每次代理请求都重新握手（节省 50-200ms/请求）
_session = requests.Session()
_adapter = HTTPAdapter(
    pool_connections=10,   # 连接池大小
    pool_maxsize=20,       # 每个 host 最大连接数
    max_retries=1,         # 失败重试 1 次
)
_session.mount("http://", _adapter)


# ==================== 静态文件 ====================
@app.route("/")
def index():
    """主页面"""
    return render_template("index.html")


@app.route("/static/<path:path>")
def serve_static(path):
    return send_from_directory(STATIC_DIR, path)


# ==================== API 代理 ====================
def _proxy(method, path):
    """通用 API 代理，转发请求到 Drogon 后端。
    直接转发原始响应体，避免 JSON 反序列化+再序列化的双重开销。
    使用连接池复用 TCP 连接。
    """
    url = f"{BASE_URL}{path}"
    headers = {"Content-Type": "application/json"}
    token = session.get("token")
    if token:
        headers["Authorization"] = f"Bearer {token}"

    try:
        start = time.time()
        if method == "GET":
            resp = _session.get(url, headers=headers, params=request.args, timeout=30)
        elif method == "POST":
            resp = _session.post(url, headers=headers, json=request.get_json(silent=True) or {}, timeout=30)
        elif method == "PUT":
            resp = _session.put(url, headers=headers, json=request.get_json(silent=True) or {}, timeout=30)
        elif method == "DELETE":
            resp = _session.delete(url, headers=headers, timeout=30)
        else:
            return jsonify({"code": 500, "message": "不支持的请求方法"}), 500

        elapsed = (time.time() - start) * 1000
        content_type = resp.headers.get("Content-Type", "application/json")

        # 超过 1 秒的请求输出警告日志
        if elapsed > 1000:
            app.logger.warning(f"SLOW PROXY {method} {path} -> {resp.status_code} ({len(resp.content)}B) in {elapsed:.0f}ms")
        else:
            app.logger.info(f"PROXY {method} {path} -> {resp.status_code} ({len(resp.content)}B) in {elapsed:.0f}ms")

        # 直接转发原始响应体，避免 json.loads + jsonify 双重序列化
        return Response(resp.content, status=resp.status_code, content_type=content_type)

    except requests.exceptions.ConnectionError:
        return jsonify({"code": 500, "message": "无法连接到后端服务，请确认 Drogon 服务已启动"}), 500
    except requests.exceptions.Timeout:
        return jsonify({"code": 500, "message": "后端服务响应超时"}), 500
    except Exception as e:
        return jsonify({"code": 500, "message": f"代理请求异常: {str(e)}"}), 500


# 代理所有 /api/* 请求
@app.route("/api/<path:subpath>", methods=["GET", "POST", "PUT", "DELETE"])
def proxy_api(subpath):
    return _proxy(request.method, f"/api/{subpath}")


# ==================== 会话管理 ====================
@app.route("/session/save", methods=["POST"])
def save_session():
    """保存登录信息到会话"""
    data = request.get_json() or {}
    session["token"] = data.get("token", "")
    session["user"] = data.get("user", {})
    return jsonify({"code": 200, "message": "ok"})


@app.route("/session/clear", methods=["POST"])
def clear_session():
    """清除会话"""
    session.clear()
    return jsonify({"code": 200, "message": "ok"})


@app.route("/session/info", methods=["GET"])
def session_info():
    """获取当前会话信息"""
    return jsonify({
        "code": 200,
        "data": {
            "token": session.get("token", ""),
            "user": session.get("user", {}),
        },
    })


if __name__ == "__main__":
    import sys
    debug_mode = "--debug" in sys.argv
    print("=" * 50)
    print("  族谱管理系统 Web 前端")
    print(f"  访问地址: http://localhost:5000")
    print(f"  后端代理: {BASE_URL}")
    print(f"  Debug 模式: {'开' if debug_mode else '关'}")
    print("=" * 50)
    # 预建立 TCP 连接池，避免第一次 API 调用时还需三次握手
    try:
        _session.get(BASE_URL, timeout=2)
        print("  [预连接] 后端连接已建立")
    except Exception:
        print("  [预连接] 后端暂未响应（稍后将自动连接）")
    app.run(host="0.0.0.0", port=5000, debug=debug_mode, threaded=True)