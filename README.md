# 族谱管理系统

基于 Flask + Drogon(C++) 的族谱管理系统，支持 10 万+ 成员记录的高效管理。

## 项目结构

```
genealogy_frontend/          # ← 当前目录
├── server.py                # Flask 代理服务器（反向代理 → Drogon 后端）
├── requirements.txt         # Python 依赖
├── templates/
│   └── index.html           # SPA 主页面
├── static/
│   ├── css/style.css        # 全局样式（中国传统配色）
│   └── js/
│       ├── api.js           # API 客户端（封装所有后端接口）
│       ├── app.js           # 主应用逻辑（SPA 路由、状态管理、UI 交互）
│       └── tree-renderer.js # Canvas 2D 族谱树渲染引擎
└── PERFORMANCE_ANALYSIS.md  # 后端性能优化需求文档

backend/                     # [待补充] Drogon C++ 后端代码
└── ...
```

## 技术架构

```
浏览器 (SPA) → Flask 代理 :5000 → Drogon 后端 :8088 → 数据库
```

- **前端**：纯 HTML/CSS/JS 单页应用，Canvas 2D 渲染族谱树
- **代理层**：Flask 反向代理，解决 CORS，管理用户 Session
- **后端**：C++ Drogon 框架，高性能异步 Web 框架
- **认证**：Bearer Token（JWT）

## 快速开始

### 环境要求
- Python 3.8+
- Drogon 后端服务运行在 `localhost:8088`

### 启动前端

```bash
# 安装依赖
pip install -r requirements.txt

# 启动（生产模式）
python server.py

# 启动（调试模式）
python server.py --debug
```

访问 http://localhost:5000

## 功能列表

| 功能 | 说明 |
|------|------|
| 用户注册/登录 | 用户名+密码，Bearer Token 认证 |
| 族谱管理 | 创建、删除、多族谱切换 |
| 族谱树 | Canvas 2D 渲染，支持缩放/平移/点击查看详情 |
| 成员管理 | 增删改查，分页列表，姓名搜索 |
| 祖先查询 | 纵向路径图，从远祖到目标成员 |
| 后代查询 | 树状结构展示，父子关系可视化 |
| 亲缘关系 | 两成员间的关系路径查询 |
| 统计分析 | 平均寿命最长一代 / 50+未婚男性 / 早于平均出生年份 |
| 族谱共享 | 创建者管理共享权限（编辑/查看） |

## 后端性能优化

详见 [PERFORMANCE_ANALYSIS.md](PERFORMANCE_ANALYSIS.md) — 包含已识别的性能瓶颈分析、接口改造需求、数据库索引建议及实施路线图。

## License

Private
