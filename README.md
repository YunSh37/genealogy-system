# 族谱管理系统

一个基于 Web 的族谱管理平台，支持多族谱切换、成员管理、血缘关系查询与可视化展示。

## 项目结构

```
./
├── server.py                # Flask 代理服务器（反向代理 → 后端 API）
├── requirements.txt         # Python 依赖清单
├── templates/
│   └── index.html           # SPA 单页应用主页面
├── static/
│   ├── css/
│   │   └── style.css        # 全局样式表（中国传统配色）
│   └── js/
│       ├── api.js           # API 客户端（封装所有后端接口）
│       ├── app.js           # 主应用逻辑（SPA 路由 / 状态管理 / 交互控制）
│       └── tree-renderer.js # Canvas 2D 族谱树渲染引擎
├── 前端实现介绍.md           # 前端实现详细说明
├── backend/                 # Drogon C++ 后端源代码
│   ├── CMakeLists.txt       # CMake 构建配置
│   ├── main.cc              # 服务入口
│   ├── .gitignore           # 忽略构建产物和配置文件
│   ├── import_data.sh       # 数据导入脚本（CSV → MySQL）
│   ├── export_data.sh       # 数据导出脚本（MySQL → CSV）
│   ├── export_backup.py     # Python 数据导出工具
│   ├── controllers/         # 控制器（HTTP 请求处理）
│   ├── models/              # 数据模型
│   ├── migrations/          # 数据库迁移脚本
│   │   ├── init_schema.sql           # 完整建表语句
│   │   ├── migration_phase3.sql      # Phase 3 性能优化迁移
│   │   ├── migration_fix_generations.sql  # 辈分修复
│   │   ├── generate_test_data.py     # 测试数据生成（10万+成员）
│   │   └── phase3_performance.sql.py # Python 版迁移脚本
│   ├── test/                # 单元测试
│   └── 后端代码实现简介.md   # 后端实现详细介绍
└── README.md
```

## 技术架构

```
浏览器 (SPA 单页应用)
    │
    ▼
Flask 代理服务器 :5000           ← 前端代码（当前仓库）
    │  · 反向代理后端 API
    │  · 管理用户登录 Session
    │  · 解决跨域（CORS）
    ▼
Drogon C++ 后端 :8088            ← backend/ 目录
    │  · 业务逻辑处理
    │  · 数据库读写
    │  · Bearer Token 认证
    ▼
MySQL 8.0 数据库
```

- **前端**：纯 HTML + CSS + JavaScript，不依赖任何框架，Canvas 2D 绘制族谱树
- **代理层**：Flask（Python），轻量反向代理
- **后端**：Drogon（C++17），高性能异步非阻塞 Web 框架
- **数据库**：MySQL 8.0，支持全文搜索、递归 CTE、Materialized Path
- **认证**：Bearer Token 内存管理

## 功能概览

| 模块 | 说明 |
|------|------|
| 用户注册/登录 | 用户名 + 密码注册，Bearer Token 认证 |
| 族谱管理 | 创建、删除族谱，支持多族谱切换 |
| 族谱树 | Canvas 2D 渲染后代树，支持缩放/平移/点击详情，智能选根 |
| 成员管理 | 增删改查、分页列表、姓名搜索（全文搜索） |
| 祖先查询 | 纵向时间线，从远祖到目标成员，支持无限深度追溯到根 |
| 后代查询 | 树状结构展示后代，支持折叠/全屏，支持无限深度 |
| 亲缘关系 | 查询两个成员之间的血缘关系路径 |
| 统计分析 | 平均寿命最长一代 / 50+未婚男性 / 早于平均出生年份 |
| 族谱共享 | 创建者可为其他用户分配查看/编辑权限 |
| 数据导入导出 | CSV 格式，支持全量/分支导出 |

## 快速开始

### 前端启动

**环境要求**：Python 3.8+

```bash
# 1. 安装 Python 依赖
pip install -r requirements.txt

# 2. 启动代理服务器
python server.py

# 3. 打开浏览器访问
# http://localhost:5000
```

> **调试模式**：`python server.py --debug` 可开启 Flask 调试模式（代码修改后自动重启）。

### 后端启动

**环境要求**：
- Linux（推荐 Ubuntu 20.04+）/ WSL2
- GCC 9+ 或 Clang 10+（需支持 C++17）
- CMake 3.5+
- [Drogon](https://github.com/drogonframework/drogon) Web 框架
- jsoncpp
- MySQL 8.0+
- libmysqlclient-dev

#### 1. 安装系统依赖

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libjsoncpp-dev

# 安装 Drogon（从源码编译）
git clone https://github.com/drogonframework/drogon.git
cd drogon && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install

# 安装 MySQL 客户端库
sudo apt install libmysqlclient-dev
```

#### 2. 初始化数据库

```bash
# 创建数据库
mysql -u root -e "CREATE DATABASE genealogy_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"

# 执行完整建表脚本
mysql -u root genealogy_db < backend/migrations/init_schema.sql

# 执行性能优化迁移
mysql -u root genealogy_db < backend/migrations/migration_phase3.sql

# 修复辈分数据（从 CSV 导入后需要）
mysql -u root genealogy_db < backend/migrations/migration_fix_generations.sql
```

#### 3. 生成测试数据（可选）

```bash
cd backend/migrations
python generate_test_data.py
```

默认生成 10 个族谱、10 万+ 成员、最大深度 30 代。可通过环境变量调整参数。

#### 4. 构建后端

```bash
cd backend
mkdir build && cd build
cmake ..
cmake --build .
```

#### 5. 配置

在 `backend/` 目录下创建 `config.json`（参考结构见 [后端代码实现简介.md](backend/后端代码实现简介.md)）。

#### 6. 启动

```bash
cd backend/build
./genealogy_system
# 服务启动在 http://localhost:8088
```

#### 7. 导入实际数据（可选）

```bash
cd backend

# 先设置环境变量
export MYSQL_HOST=127.0.0.1
export MYSQL_PORT=3306
export MYSQL_USER=root
export MYSQL_PASSWORD=your_password
export MYSQL_DATABASE=genealogy_db

# 导入 CSV 数据
./import_data.sh /path/to/your/csv/files
```

## 性能基准

测试环境：WSL2 Ubuntu, MySQL 8.0, 10 个族谱, 100,534 名成员, 最大深度 30 代。

| 场景 | 数据量 | 响应时间 | 说明 |
|------|--------|----------|------|
| 根节点后代 depth=5 | ~8,000 | **0.47s** | 含 JSON 序列化 |
| 根节点后代 depth=30 | ~48,000 | **1.88s** | SQL ~50ms，其余为序列化 |
| 非根节点后代 depth=10 | ~1,700 | **0.12s** | 迭代法批量查询 |
| 祖先追溯（到根） | 21 代 | **<10ms** | ancestor_path 解析 |
| 家族树 depth=30 | ~48,000 后代 + 祖先链 | **3.56s** | 含全部后代配偶信息 |
| 家族树 depth=10 | ~1,200 | **0.10s** | 含祖先+后代+配偶 |
| 亲缘关系查询 | — | **<50ms** | 批量替代 N+1 |
| 成员搜索（全文） | 100K+ | **<100ms** | FULLTEXT 索引 |

## 详细文档

- **前端实现**：详见 [前端实现介绍.md](./前端实现介绍.md) — 包含完整的模块说明、核心算法和数据流图
- **后端实现**：详见 [backend/后端代码实现简介.md](backend/后端代码实现简介.md) — 包含完整的 API 接口列表、核心性能优化策略、数据库设计及安全设计说明

## License

Private
