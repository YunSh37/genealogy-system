# 族谱管理系统 - 后端性能优化需求文档

> **接收方：后端开发团队**  
> **日期：2026-06-14**  
> **前置说明：本文档基于前端实际调用链路和用户体验数据编写，所有瓶颈均有对应的前端调用栈可追溯。**

---

## 1. 系统架构概览

```
浏览器 (SPA) → Flask 代理 :5000 → Drogon 后端 :8088 → 数据库
                    ↑ 关键瓶颈层！
```

- **前端**：Canvas 2D 渲染族谱树，SPA 单页应用，所有数据通过 API 获取
- **Flask 代理层**：`server.py`，Werkzeug 开发服务器 + `requests` 库转发（**这是性能瓶颈的核心所在 — 见 2.1 节**）
- **后端**：C++ Drogon 框架，端口 8088，Bearer Token 认证
- **数据规模**：预期支持 10 万+ 条成员记录

---

## 2. 已识别的性能瓶颈（按严重程度排序）

### 🔴🔴 P0 - 最致命：Flask 代理层瓶颈（响应时间 Apifox 快、浏览器慢的根因）

**现象**：后端接口在 Apifox 中直接调用响应很快（<500ms），但通过前端网页访问时响应极慢（数秒甚至数十秒）。用户感知到"每个点击指令运行响应时间都很长"。

**根因分析**：

| 瓶颈点 | 详情 | 影响 |
|--------|------|------|
| **TCP 连接未复用** | `server.py` 每次 `_proxy()` 调用都使用 `requests.get(url, ...)` 创建新的 TCP 连接到 Drogon 后端。三次握手 + TLS（如有）增加 50-200ms/请求 | 每个 API 请求额外增加连接建立延迟 |
| **双重 JSON 序列化** | Drogon 返回 JSON 字符串 → `resp.json()` 反序列化为 Python dict → `jsonify(data)` 再次序列化为 JSON 字符串。对于 100k+ 记录（~50MB JSON），这一步消耗数秒 CPU | 大数据量请求的序列化耗时是纯传输的 2-3 倍 |
| **Flask/Werkzeug 开发服务器** | `app.run(threaded=True)` 使用 Werkzeug 开发服务器，线程受 Python GIL 限制，多个并发请求时性能急剧下降 | 多用户或多请求场景下响应时间线性增长 |
| **同步阻塞 I/O** | `requests.get/post` 是同步阻塞调用，阻塞整个线程直到 Drogon 响应完成 | 线程利用率极低 |
| **无响应流式传输** | 后端必须先完整生成 JSON → Flask 完整接收 → 才能开始向浏览器发送 | 浏览器等待时间 = 后端查询 + 后端序列化 + 网络传输 + Flask 反序列化 + Flask 再序列化 |

**典型请求时序对比**（以 `getAllMembers` 100k 记录为例）：

```
Apifox 直接调用：
  Drogon查询(200ms) → Drogon序列化(500ms) → 网络传输(100ms)
  总耗时：~800ms ✅

通过 Flask 代理：
  TCP连接建立(100ms) → Flask转发(50ms) → Drogon查询(200ms) → Drogon序列化(500ms)
  → Flask接收(200ms) → resp.json()反序列化(2s) → jsonify()再序列化(2s) → 浏览器接收(500ms)
  总耗时：~5.5s ❌ (6-7倍差距！)
```

**前端侧已实施的优化**（本迭代同步修改 `server.py`）：
1. ✅ 使用 `requests.Session()` + `HTTPAdapter` 连接池复用 TCP 连接
2. ✅ 添加请求耗时日志，便于定位慢接口
3. ✅ 提高连接池大小（pool_connections=10, pool_maxsize=20）

**后端需要配合的事项**：
- ✅ 启用 gzip 压缩（Drogon 配置），可减少 80-90% 传输体积
- ✅ 在响应头中设置 `Connection: keep-alive`

---

### 🔴 P0 - 致命：族谱树加载（getAllMembers）

**触发场景**：用户点击"族谱树"标签页

**前端调用**：
```javascript
// app.js 第473行
const data = await API.getAllMembers(this.currentGenealogyId);
// 实际发送: GET /api/genealogy/{gid}/member?page=1&page_size=99999
```

**问题分析**：

| 指标 | 当前值 | 预期值 |
|------|--------|--------|
| 单次返回记录数 | 全部（可达10万+） | ≤500条/次 |
| JSON 响应体大小 | ~50MB（10万条估算） | ≤500KB |
| 序列化耗时 | 数秒级别 | <100ms |
| 网络传输耗时 | 数十秒（HTTP 无压缩） | <1s |
| 前端解析耗时 | 阻塞主线程数秒 | <200ms |

**问题本质**：
- 前端需要完整成员列表来构建 Canvas 族谱树（`tree-renderer.js` 的 `buildTree()` 函数需要所有节点建立父子关系）
- 但一次性返回全量数据在任何数据规模下都不可行
- **已有但未使用的接口**：`GET /api/member/{mid}/family-tree?depth=N` — 这个接口支持按深度获取树结构，但前端当前未使用它

---

### 🔴 P0 - 致命：祖先/后代递归查询

**触发场景**：用户进行祖先查询或后代查询

**前端调用**：
```javascript
// GET /api/member/{mid}/ancestors?max_depth=10
// GET /api/member/{mid}/descendants?max_depth=10
```

**问题分析**：
- 递归查询数据库（每次递归 = 一次 SQL 查询）→ N+1 问题
- 深度 10 的祖先查询可能执行 10+ 次独立 SQL
- 无超时保护（已被 Flask 30s 代理超时截断）
- 无结果数量限制 — 某人的后代可能有数千人

---

### 🟡 P1 - 严重：统计分析全表扫描

**触发场景**：用户切换统计标签页

**前端调用**：
```javascript
// GET /api/genealogy/{gid}/stats/longest-lived-generation
// GET /api/genealogy/{gid}/stats/unmarried-men-over-50
// GET /api/genealogy/{gid}/stats/born-before-average
```

**问题分析**：
- 每次切换标签页都重新查询，无缓存
- 涉及全表聚合计算（AVG、MAX、年龄计算等）
- 10 万条记录的全表扫描 + 条件过滤 → 秒级延迟

---

### 🟡 P1 - 严重：亲缘关系查询路径爆炸

**触发场景**：用户查询两个成员之间的关系

**前端调用**：
```javascript
// GET /api/member/relationship?member_id1=X&member_id2=Y
```

**问题分析**：
- BFS/DFS 遍历整个家族图找到两个节点之间的路径
- 最坏情况：遍历全部 10 万+ 节点
- 无深度限制或超时保护

---

### 🟢 P2 - 中等：全局搜索

**触发场景**：顶部搜索框搜索成员

**前端调用**：
```javascript
// GET /api/member/search?keyword=XXX&page=1&page_size=20
```

**问题分析**：
- 如果 `name` 字段没有全文索引，LIKE 模糊匹配扫描全表
- 返回分页已做（page_size=20），这点正确

---

### 🟡 P1 - 严重：API 响应字段不一致（前后端协作问题）

**问题描述**：不同接口返回的成员信息字段不统一，导致前端部分表格的"辈分"列空白。

**具体表现**：

| 接口 | generation 字段 | 前端展示 |
|------|:---:|------|
| `GET /api/genealogy/{gid}/stats/longest-lived-generation` → `data.members[].generation` | ✅ 返回 | 正常显示 |
| `GET /api/genealogy/{gid}/stats/born-before-average` → `data.members[].generation` | ✅ 返回 | 正常显示 |
| `GET /api/genealogy/{gid}/member` → `data.members[].generation` | ❓ 可能缺失 | **空白** |
| `GET /api/member/{mid}/ancestors` → `data.ancestors[].generation` | ❓ 可能缺失 | **空白** |
| `GET /api/member/{mid}/descendants` → `data.descendants[].generation` | ❓ 可能缺失 | **空白** |

**后端修复要求**：
- **所有返回成员对象的接口必须统一包含以下基础字段**：
  - `member_id`, `name`, `gender`, `generation`, `birth_date`, `death_date`
  - `father_id`, `mother_id`, `spouse_id`（涉及关系查询时）
- 特别是 `/api/genealogy/{gid}/member`（成员列表分页查询）、`/api/member/{mid}/ancestors`、`/api/member/{mid}/descendants` 这三个接口必须补充 `generation` 字段

**前端侧已修复**：将 `m.generation || ''` 改为 `m.generation != null ? m.generation : ''`，防止 generation=0 时被 JavaScript 判为 falsy 而不显示（但即使修复，后端不返回 generation 仍然无法显示）。

---

### 🟡 P1 - 严重：新建族谱首个成员辈分错误（前后端协作问题）

**问题描述**：新建一个空族谱，添加第一个成员后，族谱树中该成员显示为"第0代"而非"第1代"。

**根因分析**：
- 前端 `tree-renderer.js`：`generation: m.generation || 0`，当后端不返回 generation 或返回 0 时，默认取 0
- 后端：新成员插入时 `generation` 字段可能默认值为 0，或未自动计算

**修复方案**：
- **前端**（本次修复）：改为 `generation: m.generation ?? 1`，当后端未返回 generation 时默认为第 1 代
- **后端**：建议在插入成员时，如果 generation 未指定，自动根据父亲 generation + 1 推算，或默认设为 1

---

### 🟡 P1 - 严重：仪表盘"辈分分布"内容错误

**问题描述**：仪表盘右侧"辈分分布"卡片显示的是男女人数和比例，而非实际的辈分分布数据。

**根因**：
- 前端 `_loadDashboard()` 中，`generation-dist` 区域被错误填充为性别统计数据
- 后端 `/api/genealogy/{gid}/stats` 接口可能未返回 `generation_distribution`（各辈分人数分布）

**后端修复要求**：
- `/api/genealogy/{gid}/stats` 接口需要增加 `generation_stats` 字段，返回各辈分的人数分布：
  ```json
  {
    "generation_stats": [
      {"generation": 1, "count": 5, "male": 3, "female": 2},
      {"generation": 2, "count": 12, "male": 7, "female": 5}
    ]
  }
  ```

**前端侧已修复**：优先从 `data.generation_stats` 渲染辈分分布，若后端未返回则提示"暂无辈分分布数据"。

---

### 🟡 P1 - 严重：祖先查询和后代查询展示格式错误

**问题描述**：
- **祖先查询**：应该显示从目标成员向上追溯到祖先的一条**路径**（链式结构），当前显示为平铺表格
- **后代查询**：应该显示以目标成员为根节点的一棵**家族树**（树状结构），当前显示为平铺表格

**前端改造需求**（本次迭代修改）：
- 祖先查询结果改为纵向路径图：祖先 → ... → 祖父 → 父亲 → 目标成员，箭头连接
- 后代查询结果改为 Canvas 树状渲染（复用 TreeRenderer）

**后端改造需求**：
- **祖先查询接口** `/api/member/{mid}/ancestors`：
  - `data.ancestors` 需要按辈分从远到近排序（最远祖先 → 父亲），relation 字段标明每层关系
  - 新增 `data.path_length` 字段表示路径深度
- **后代查询接口** `/api/member/{mid}/descendants`：
  - `data.descendants` 中每项必须包含 `father_id` 和 `mother_id`，前端需要这两个字段来构建树状结构
  - 建议同时返回 `target_member` 作为根节点信息

**理想的祖先查询响应示例**：
```json
{
  "target_member": {"member_id": 100, "name": "张三", "generation": 4},
  "path_length": 3,
  "ancestors": [
    {"member_id": 10, "name": "张一", "generation": 1, "gender": "male", 
     "birth_date": "1800-01-01", "death_date": "1870-01-01", "relation": "祖父"},
    {"member_id": 50, "name": "张二", "generation": 2, "gender": "male",
     "birth_date": "1830-01-01", "death_date": "1900-01-01", "relation": "父亲"}
  ]
}
```

---

### 🟢 P2 - 中等：通用问题

1. **无响应压缩**：后端未启用 gzip/brotli，10万条 JSON 原始传输数十 MB
2. **无缓存头**：所有响应缺少 `Cache-Control` / `ETag`，浏览器无法缓存
3. **30秒代理超时**：Flask 代理层 30s 超时，但大型查询本身就需要更长时间（治标不治本）
4. **无流式/分块传输**：后端必须先生成完整 JSON 再返回，无法边查边传

---

## 3. 具体优化方案

### 3.1 族谱树 - 分层按需加载（最高优先级）

**新增接口**：`GET /api/genealogy/{gid}/tree-layers`

**请求参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `root_id` | int | 否 | 以指定成员为根，不传则取最小辈分所有成员为根 |
| `max_depth` | int | 否 | 最大深度，默认 5 |
| `fields` | string | 否 | 返回字段，默认 `id,name,gender,father_id,mother_id,spouse_id,generation,birth_date,death_date` |

**响应格式**：
```json
{
  "code": 200,
  "data": {
    "root_ids": [1, 2, 3],
    "total_nodes": 15234,
    "returned_nodes": 487,
    "has_more": true,
    "max_depth_reached": 5,
    "layers": {
      "0": [  // 第0层 = 根节点
        {"member_id": 1, "name": "张三", "gender": "male", "father_id": null, "mother_id": null,
         "spouse_id": 2, "generation": 1, "birth_date": "1800-01-01", "death_date": "1875-06-15",
         "child_count": 5, "has_more_children": false}
      ],
      "1": [  // 第1层 = 根节点的子女
        ...
      ]
    }
  }
}
```

**关键设计点**：
- **按层返回**：每一层是一个数组，前端可以逐层渲染
- **`has_more` 字段**：告知前端是否还有更深层数据未加载
- **`child_count` / `has_more_children`**：节点上可显示"展开更多"按钮
- **精简字段**：不返回 `biography` 等大文本字段，减少传输量
- **分页支持**：当某一层节点数 > 500 时，支持 `layer_page` 和 `layer_page_size` 分页

**数据库实现建议**：
```sql
-- 单次 SQL 获取某一层的所有节点（以 father_id 为例）
-- 第1层：根节点的子女
SELECT id, name, gender, father_id, mother_id, spouse_id, generation,
       birth_date, death_date
FROM members
WHERE father_id IN (SELECT member_id FROM members WHERE generation = min_gen AND genealogy_id = ?)
  AND genealogy_id = ?
ORDER BY generation, birth_date;

-- 或采用递归 CTE（如果数据库支持）
WITH RECURSIVE tree AS (
  SELECT *, 0 as depth FROM members WHERE member_id IN (root_ids) AND genealogy_id = ?
  UNION ALL
  SELECT m.*, t.depth + 1
  FROM members m JOIN tree t ON (m.father_id = t.member_id OR m.mother_id = t.member_id)
  WHERE m.genealogy_id = ? AND t.depth < ?
)
SELECT * FROM tree ORDER BY depth, generation, birth_date;
```

**前端适配**（后续修改 app.js）：
- `_loadFamilyTree()` 改为首次加载 3-5 层
- 用户滚动画布到边缘时触发更深层数据加载
- 节点点击展开时加载该节点的子节点层

---

### 3.2 族谱树 - 备选方案（快速实现）

如果分层接口改动较大，可先优化现有 `getAllMembers` 调用方式：

**利用已有接口**：`GET /api/member/{mid}/family-tree?depth=N`

前端改为：
```javascript
// 不再调用 getAllMembers
// 改为：找到族谱中最小辈分的成员 ID，以他们为根获取 depth=5 的树
const roots = await API.getMembers(gid, 1, 10); // 只要前10个根成员
for (const root of roots.members) {
  const tree = await API.getFamilyTree(root.member_id, 5);
  // 合并多棵树
}
```

但这只是临时方案，**最终仍需分层加载接口**。

---

### 3.3 祖先/后代查询优化

**优化方向**：

1. **递归 CTE 替代 N+1 查询**：
   ```sql
   -- 祖先查询（从下往上）
   WITH RECURSIVE ancestors AS (
     SELECT *, 0 as depth FROM members WHERE member_id = ?
     UNION ALL
     SELECT m.*, a.depth + 1
     FROM members m JOIN ancestors a
       ON m.member_id IN (a.father_id, a.mother_id)
     WHERE a.depth < ?
   )
   SELECT * FROM ancestors WHERE member_id != ?;
   ```

2. **增加结果数量限制**：`max_results` 参数（默认 500）
3. **增加查询超时**：服务端 10s 超时，超时返回已查到的部分结果 + `partial: true` 标记
4. **缓存中间结果**：对热门成员（如根节点）的祖先树做 Redis 缓存

---

### 3.4 亲缘关系查询优化

**优化方向**：

1. **双向 BFS** 替代单向 BFS/DFS：从两个节点同时出发，在中间相遇，搜索空间从 O(N) 降到 O(√N)
2. **深度限制**：`max_depth` 参数（默认 20），超过则返回 `path_too_long: true`
3. **提前终止**：找到第一条路径后立即返回（不需要最短路径，因为家族关系中路径通常唯一）
4. **公共祖先索引**：预计算每个成员到根节点的路径（materialized path），关系查询变成路径对比

**Materialized Path 方案（推荐）**：
- 在 `members` 表增加 `ancestor_path` 字段（如 `1/15/234/567`）
- 关系查询变为：找两个 path 的最长公共前缀 → O(path_length)
- 新增成员时自动计算 path（取父节点 path + 自己的 ID）

---

### 3.5 统计查询优化

**优化方向**：

1. **预计算 + 缓存**：
   - 方案 A：定时任务（如每小时）计算统计结果，存入 `statistics_cache` 表
   - 方案 B：使用 Redis 缓存，TTL 30 分钟
   
2. **增量更新**：
   - 新增/修改/删除成员时，标记对应族谱的统计为 `dirty`
   - 下次查询时重新计算并缓存

3. **数据库层面**：
   ```sql
   -- 确保以下索引存在
   CREATE INDEX idx_member_genealogy_gen ON members(genealogy_id, generation);
   CREATE INDEX idx_member_genealogy_gender ON members(genealogy_id, gender);
   CREATE INDEX idx_member_genealogy_birth ON members(genealogy_id, birth_date);
   CREATE INDEX idx_member_genealogy_death ON members(genealogy_id, death_date);
   CREATE INDEX idx_member_spouse ON members(spouse_id);
   ```

---

### 3.6 全局搜索优化

**优化方向**：

1. **数据库全文索引**（MySQL）：
   ```sql
   ALTER TABLE members ADD FULLTEXT INDEX ft_member_name (name);
   -- 查询改为
   SELECT * FROM members WHERE MATCH(name) AGAINST(? IN BOOLEAN MODE);
   ```

2. **或使用 LIKE 优化**（如无法添加全文索引）：
   ```sql
   -- 前缀匹配优先（可以利用 B-Tree 索引）
   SELECT * FROM members WHERE name LIKE 'keyword%'  -- 快
   UNION
   SELECT * FROM members WHERE name LIKE '%keyword%' AND name NOT LIKE 'keyword%'  -- 慢但少
   LIMIT 20;
   ```

---

### 3.7 通用优化

| 优化项 | 说明 | 优先级 |
|--------|------|--------|
| **启用 gzip** | 在 Drogon 中启用 gzip 压缩，JSON 文本压缩率通常 80-90% | P0 |
| **添加数据库索引** | 见下方索引清单 | P0 |
| **连接池** | 确保数据库连接池 ≥ 20 | P1 |
| **响应缓存头** | 对 stats、genealogy list 等不频繁变化的数据加 `Cache-Control: max-age=300` | P1 |
| **分页默认值** | `/api/genealogy/{id}/member` 的 `page_size` 默认值从无限制改为 50，最大限制 500 | P1 |
| **批量查询接口** | 新增 `POST /api/member/batch` 一次查询多个 member_id（前端展示搜索结果时用） | P2 |
| **健康检查接口** | 新增 `GET /api/health`（不含数据库查询，仅确认服务存活） | P2 |

---

## 4. 数据库索引清单

```sql
-- ========== 必须添加 ==========

-- 1. 族谱隔离（几乎所有查询都带 genealogy_id）
CREATE INDEX idx_member_genealogy ON members(genealogy_id);

-- 2. 父子关系查询（族谱树核心查询）
CREATE INDEX idx_member_father ON members(father_id);
CREATE INDEX idx_member_mother ON members(mother_id);

-- 3. 辈分查询和排序
CREATE INDEX idx_member_generation ON members(generation);

-- 4. 搜索
CREATE INDEX idx_member_name ON members(name);

-- 5. 复合索引（覆盖最常见的查询模式）
CREATE INDEX idx_member_genealogy_gen ON members(genealogy_id, generation);
CREATE INDEX idx_member_genealogy_father ON members(genealogy_id, father_id);

-- ========== 建议添加 ==========

-- 6. 统计查询
CREATE INDEX idx_member_genealogy_gender ON members(genealogy_id, gender);
CREATE INDEX idx_member_genealogy_birth ON members(genealogy_id, birth_date);

-- 7. 配偶关系
CREATE INDEX idx_member_spouse ON members(spouse_id);

-- 8. 全文搜索（MySQL）
-- ALTER TABLE members ADD FULLTEXT INDEX ft_member_name (name);
```

---

## 5. 优先实施路线图

### 第一阶段（1-3天）：止血 — 解决用户可见的卡顿和字段缺失

| 任务 | 工作内容 | 效果 |
|------|----------|------|
| **添加数据库索引** | 执行第4节的"必须添加"索引 | 所有查询加速 10-100x |
| **启用 gzip 压缩** | Drogon 配置启用 gzip | 传输体积减少 80%+ |
| **限制 page_size 上限** | `page_size` 最大 500，默认 50 | 防止恶意/意外全量查询 |
| **修复 API 字段一致性** | `/api/genealogy/{gid}/member`、`/api/member/{mid}/ancestors`、`/api/member/{mid}/descendants` 补充 `generation` 字段 | 前端辈分列正常显示 |
| **修复新成员 generation 默认值** | `POST /api/genealogy/{gid}/member` 默认 generation=1 或根据父亲推算 | 新成员不再显示"第0代" |
| **补充 stats 接口 generation_stats** | `/api/genealogy/{gid}/stats` 增加 `generation_stats` 字段 | 仪表盘辈分分布正常显示 |

### 第二阶段（3-7天）：治本 — 族谱树分层加载 + 查询优化

| 任务 | 工作内容 | 效果 |
|------|----------|------|
| **实现分层树接口** | `GET /api/genealogy/{gid}/tree-layers` | 族谱树首次加载从 >30s → <1s |
| **递归查询改 CTE** | 祖先/后代查询用递归 CTE 替代 N+1 | 查询速度提升 10-50x |
| **祖先查询按辈分排序** | 返回结果从远到近有序排列，包含 relation 和 path_length | 前端可渲染为路径图 |
| **后代查询增加关系字段** | 返回结果包含 father_id/mother_id | 前端可构建树状结构 |
| **统计缓存** | stats 接口加 Redis 缓存 TTL 30min | 统计切换即时响应 |

### 第三阶段（1-2周）：深度优化

| 任务 | 工作内容 | 效果 |
|------|----------|------|
| **Materialized Path** | 新增 ancestor_path 字段 + 关系查询优化 | 关系查询 O(N) → O(log N) |
| **全文搜索** | 全文索引或 Elasticsearch | 模糊搜索毫秒级响应 |
| **统计预计算** | 定时任务 + 统计缓存表 | 统计接口零数据库查询 |

---

## 6. 接口改造对照表

| 当前接口 | 问题 | 改造方案 |
|----------|------|----------|
| `GET /api/genealogy/{gid}/member?page_size=99999` | 全量返回 | **新增** `GET /api/genealogy/{gid}/tree-layers`，按层按需加载；member 接口限制 page_size ≤ 500 |
| `GET /api/genealogy/{gid}/member` | 返回成员缺少 `generation` 字段 | 统一所有成员查询接口的返回字段，必须包含 `generation` |
| `GET /api/member/{mid}/ancestors` | 递归 N+1 查询；返回缺少 `generation`；前端需要路径排序 | 改用递归 CTE；增加 `generation` 字段；按辈分从远到近排序；增加 `path_length` |
| `GET /api/member/{mid}/descendants` | 递归 N+1 查询；返回缺少 `father_id`/`mother_id`/`generation` | 改用递归 CTE；增加完整关系字段；前端需要这些字段构建树状结构 |
| `GET /api/member/relationship` | 图遍历无限制 | 双向 BFS + 深度限制 + 超时保护；长期改为 Materialized Path |
| `GET /api/genealogy/{gid}/stats` | 缺少 `generation_stats` 辈分分布数据 | 增加 `generation_stats` 字段：各辈分人数、男女数 |
| `GET /api/genealogy/{gid}/stats/*` | 每次实时计算 | 加 Redis 缓存（TTL 30min）或定时预计算 |
| `GET /api/member/search` | LIKE 全表扫描 | 全文索引 + 前缀匹配优先策略 |
| `POST /api/genealogy/{gid}/member` | generation 默认值可能为 0 | 新成员 generation 默认值改为 1（或根据父亲 generation+1 推算） |

---

## 7. 附录：前端当前 API 调用量统计

以下统计基于前端代码实际调用路径，帮助后端评估各接口的调用频率和重要性：

| API 接口 | 调用场景 | 每次访问调用次数 | 性能敏感度 |
|----------|----------|:---:|:---:|
| `GET /api/genealogy/{gid}/member?page_size=99999` | 进入族谱树 | 1 次 | 🔴 极高 |
| `GET /api/genealogy/{gid}/stats` | 进入仪表盘 | 1 次 | 🟡 高 |
| `GET /api/genealogy/{gid}/member?page=&page_size=20` | 成员列表（分页） | 多次（翻页） | 🟢 中 |
| `GET /api/member/{mid}/ancestors` | 祖先查询 | 按需 | 🔴 高 |
| `GET /api/member/{mid}/descendants` | 后代查询 | 按需 | 🔴 高 |
| `GET /api/member/relationship` | 关系查询 | 按需 | 🔴 高 |
| `GET /api/member/search` | 全局搜索 | 按需 | 🟡 中 |
| `GET /api/genealogy/{gid}/stats/*` | 统计标签页切换 | 每次切换 1 次 | 🟡 中 |
| `GET /api/genealogy` | 进入首页 | 1 次 | 🟢 低 |
| `POST /api/genealogy/{gid}/member` | 添加成员 | 按需 | 🟢 低 |
| `PUT /api/member/{mid}` | 编辑成员 | 按需 | 🟢 低 |
| `DELETE /api/member/{mid}` | 删除成员 | 按需 | 🟢 低 |
| `GET /api/genealogy/{gid}/share` | 共享管理 | 按需 | 🟢 低 |

---

## 8. 沟通与反馈

如有任何疑问或需要前端侧配合修改（如适配新的分层树接口），请通过以下方式反馈：

- **接口格式**：所有新接口请保持统一响应格式 `{"code": 200, "message": "ok", "data": {...}}`
- **联调测试**：前端开发环境在 `localhost:5000`，后端在 `localhost:8088`
- **数据格式**：日期字段统一使用 ISO 8601 格式（`YYYY-MM-DD`）

---
*文档维护：前端开发团队 | 最后更新：2026-06-14*
