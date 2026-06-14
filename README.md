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

### 后端启动（WSL2 / Linux）

> 以下流程假定你使用 **WSL2 (Ubuntu)**。纯 Linux 环境操作完全相同，跳过 WSL 特定步骤即可。

**环境要求**：
- WSL2（推荐 Ubuntu 22.04）或原生 Linux
- GCC 9+ 或 Clang 10+（需支持 C++17）
- CMake 3.5+
- MySQL 8.0+
- Python 3.8+

---

#### 第 0 步：进入 WSL 并定位到项目目录

```bash
# 在 Windows 终端中进入 WSL
wsl

# 切换到项目后端目录（假设项目在 D:\shujuku\tmp\genealogy-system）
cd /mnt/d/shujuku/tmp/genealogy-system/backend
```

> 后续所有操作都在 **WSL 终端**中执行，工作目录为 `backend/`。
>
> **⚠ WSL 用户注意**：如果项目是通过 Windows 端 git 克隆的，`.sh` 脚本文件可能丢失执行权限或换行符异常。在继续之前，执行以下命令修复：
> ```bash
> chmod +x *.sh
> sed -i 's/\r$//' *.sh migrations/*.py migrations/*.sql
> ```
> 如果 `sed` 报错，安装 dos2unix：`sudo apt install -y dos2unix && dos2unix *.sh migrations/*.py migrations/*.sql`

---

#### 第 1 步：安装系统依赖（仅首次）

```bash
# 更新包列表
sudo apt update

# 安装编译工具链
sudo apt install -y build-essential cmake libjsoncpp-dev

# 安装 MySQL 客户端库
sudo apt install -y libmysqlclient-dev

# 安装 Drogon Web 框架（从源码编译）
cd /tmp
git clone https://github.com/drogonframework/drogon.git
cd drogon && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
cd /tmp

# 安装 Python 依赖（测试数据生成用，系统自带 Python3 即可，无需额外安装）
```

> 以上依赖仅需安装一次，后续重启 WSL 无需重复。

---

#### 第 2 步：启动 MySQL 并创建数据库

```bash
# 启动 MySQL 服务（WSL 中每次重启后都要执行）
sudo service mysql start
```

**接下来需要连接 MySQL。你的 MySQL root 可能有或没有密码，先快速测试一下：**

```bash
# 执行这条测试命令：
sudo mysql -u root -e "SELECT 1" 2>/dev/null && echo "→ 无密码，情况 A" || echo "→ 需要密码，情况 B"
```

根据输出选择对应的情况继续：

---

**情况 A — 输出"无密码"（WSL 默认安装，auth_socket 认证）：**

```bash
# 无需密码即可创建数据库
sudo mysql -u root -e "CREATE DATABASE IF NOT EXISTS genealogy_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
# 设置密码，使后端程序能通过 TCP 连接 MySQL
sudo mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456'; FLUSH PRIVILEGES;"
```

---

**情况 B — 输出"需要密码"（安装 MySQL 时设过 root 密码）：**

```bash
# 使用 -p 输入你的 MySQL root 密码来创建数据库
sudo mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS genealogy_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
# 统一密码为 123456，后续步骤不需要再区分
sudo mysql -u root -p -e "ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456'; FLUSH PRIVILEGES;"
```

---

> **无论哪种情况，完成后 MySQL root 密码统一为 `123456`。**
> Drogon 后端通过 TCP 连接 MySQL，必须使用密码认证 — 这是设置密码的原因。

---

#### 第 3 步：执行建表脚本

```bash
# 回到项目 backend 目录
cd /mnt/d/shujuku/tmp/genealogy-system/backend

# 执行完整建表脚本（6 张表 + 13 个索引 + CHECK 约束）
mysql -u root -p123456 genealogy_db < migrations/init_schema.sql

# 执行性能优化迁移（ancestor_path 物化路径 + 全文索引 + 统计缓存表）
mysql -u root -p123456 genealogy_db < migrations/migration_phase3.sql
```

---

#### 第 4 步：生成测试数据

```bash
# 进入 migrations 目录
cd migrations

# 运行数据生成脚本（生成 CSV 文件到 migrations/test_data/ 目录）
python3 generate_test_data.py

# 生成的数据量：10 个族谱、100,534 名成员、~50,000 条婚姻、最大深度 30 代

cd ..
```

---

#### 第 5 步：导入数据到 MySQL

```bash
# 导出密码环境变量（import_data.sh 自动读取，跳过交互提示）
export MYSQL_PASSWORD=123456

# 将生成的 CSV 文件导入数据库
./import_data.sh migrations/test_data
```

> **手动密码模式**：不设置 `MYSQL_PASSWORD` 环境变量，脚本会交互式询问密码。
> **自定义数据**：将 `migrations/test_data` 替换为你的 CSV 数据目录路径。

---

#### 第 6 步：修复辈分与物化路径 ★必须

LOAD DATA 导入时 `generation` 和 `ancestor_path` 列不在导入列表中，默认值不准确。此脚本通过迭代法逐层计算所有成员的正确辈分和祖先路径。

```bash
mysql -u root -p123456 genealogy_db < migrations/migration_fix_generations.sql
```

> 预计耗时 10~30 秒。输出末尾显示 `max_gen: 30` 即表示修复成功。

---

#### 第 7 步：编译后端程序

```bash
# 在 backend/ 目录下
mkdir -p build && cd build
cmake ..
cmake --build .
```

编译成功后会在 `build/` 目录下生成 `genealogy_system` 可执行文件。

---

#### 第 8 步：创建配置文件

```bash
# 回到 backend 目录
cd /mnt/d/shujuku/tmp/genealogy-system/backend

# 创建 config.json（密码 123456 与第 2 步中设置的一致）
cat > config.json << 'EOF'
{
    "listeners": [
        {
            "address": "0.0.0.0",
            "port": 8088
        }
    ],
    "db_clients": [
        {
            "name": "default",
            "rdbms": "mysql",
            "host": "127.0.0.1",
            "port": 3306,
            "dbname": "genealogy_db",
            "user": "root",
            "password": "123456",
            "client_encoding": "utf8mb4"
        }
    ],
    "app": {
        "number_of_threads": 4
    }
}
EOF
```

---

#### 第 9 步：启动后端服务

```bash
cd build
./genealogy_system
```

看到以下输出表示启动成功：

```
INFO  - Listening on 0.0.0.0:8088
```

> 服务运行在 `http://localhost:8088`。在 Windows 浏览器中访问此地址即可。

---

#### 验证服务

```bash
# 在另一个 WSL 终端中测试
curl http://localhost:8088/api/health
# 返回: {"status":"ok"}

curl http://localhost:8088/
# 返回服务基本信息
```

---

#### 完整流程速查

```
# ===== 后端完整部署流程（WSL2 / Linux）=====
# ★ 如果 sudo mysql 报 Access denied，把前两条 sudo mysql 末尾加上 -p 并输入密码
wsl                                                          # 进入 WSL
cd /mnt/d/shujuku/tmp/genealogy-system/backend               # 定位到后端目录
# --- 数据库：先测试连接方式 ---
sudo service mysql start                                      # 启动 MySQL
sudo mysql -u root -e "SELECT 1" 2>/dev/null && \
  (sudo mysql -u root -e "CREATE DATABASE IF NOT EXISTS genealogy_db CHARACTER SET utf8mb4;" && \
   sudo mysql -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456'; FLUSH PRIVILEGES;") || \
  (sudo mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS genealogy_db CHARACTER SET utf8mb4;" && \
   sudo mysql -u root -p -e "ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456'; FLUSH PRIVILEGES;")
mysql -u root -p123456 genealogy_db < migrations/init_schema.sql   # 建表
mysql -u root -p123456 genealogy_db < migrations/migration_phase3.sql  # 迁移
# --- 数据 ---
cd migrations && python3 generate_test_data.py && cd ..       # 生成数据
export MYSQL_PASSWORD=123456 && ./import_data.sh migrations/test_data  # 导入
mysql -u root -p123456 genealogy_db < migrations/migration_fix_generations.sql  # ★辈分修复
# --- 编译运行 ---
mkdir -p build && cd build && cmake .. && cmake --build .     # 编译
cd .. && cat > config.json << 'EOF'                           # 配置
{
    "listeners": [{"address": "0.0.0.0", "port": 8088}],
    "db_clients": [{"name": "default", "rdbms": "mysql",
        "host": "127.0.0.1", "port": 3306,
        "dbname": "genealogy_db", "user": "root",
        "password": "123456", "client_encoding": "utf8mb4"}],
    "app": {"number_of_threads": 4}
}
EOF
cd build && ./genealogy_system                                # 启动
```

---

### 常见问题排查

#### 1. `sudo mysql -u root` 报错 "Access denied"

```
ERROR 1045 (28000): Access denied for user 'root'@'localhost' (using password: NO)
```

**原因**：你的 MySQL root 用户已经设置了密码，不能免密登录。

**解决**：在所有 `sudo mysql` 命令后加 `-p`，输入你的 MySQL root 密码：

```bash
# 原命令（报错）:
sudo mysql -u root -e "CREATE DATABASE..."

# 改为（加 -p）:
sudo mysql -u root -p -e "CREATE DATABASE..."
# 提示 "Enter password:" 时输入你的 MySQL root 密码
```

#### 2. import_data.sh 报 "无法连接 MySQL"

```bash
# 检查 MySQL 是否启动
sudo service mysql status

# 未启动则启动
sudo service mysql start

# 确认密码环境变量是否正确
echo $MYSQL_PASSWORD

# 或使用交互模式（不设环境变量，手动输入密码）
unset MYSQL_PASSWORD
./import_data.sh migrations/test_data
```

#### 3. 运行 `.sh` 脚本报 "cannot execute: required file not found"

```
-bash: ./import_data.sh: cannot execute: required file not found
```

**原因**：脚本文件包含 Windows 换行符（CRLF），WSL 无法识别 `#!/bin/bash\r`。

**解决**：
```bash
# 方法 A：用 sed 清除回车符
sed -i 's/\r$//' *.sh migrations/*.py migrations/*.sql

# 方法 B：安装 dos2unix 工具
sudo apt install -y dos2unix
dos2unix *.sh migrations/*.py migrations/*.sql
```

#### 4. 运行 `.sh` 脚本报 "Permission denied"

```
-bash: ./import_data.sh: Permission denied
```

**原因**：通过 Windows git 克隆的项目，Linux 执行权限丢失。

**解决**：
```bash
chmod +x *.sh
```

#### 5. 编译报错 "drogon/drogon.h: No such file or directory"

```bash
# Drogon 未安装或库路径未更新
sudo ldconfig
# 确认 /usr/local/include/drogon 目录存在
ls /usr/local/include/drogon
```

#### 6. 后端启动后立即退出，日志显示 "Access denied"

```bash
# config.json 中的密码与 MySQL root 密码不一致
# 检查 config.json 的 password 字段是否为 "123456"
cat config.json | grep password
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
