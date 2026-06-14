/* ============================================================
   Canvas 族谱树渲染引擎
   竖式卡片 + 可折叠分支 + 全屏支持
   ============================================================ */

const TreeRenderer = {
  // ---- 配置（竖式窄卡片，水平更紧凑）----
  NODE_W: 56,
  NODE_H: 82,
  H_GAP: 12,
  V_GAP: 110,    // 必须 > NODE_H，否则上下代节点重叠
  NODE_RADIUS: 6,
  AUTO_COLLAPSE_DEPTH: 8,  // 自动折叠深度：加载后仅显示前 8 代

  // ---- 状态 ----
  canvas: null,
  ctx: null,
  dpr: 1,
  nodes: [],         // 所有渲染节点（展平后）
  nodeMap: {},       // member_id -> node
  roots: [],
  viewX: 0,
  viewY: 0,
  zoom: 1.0,
  minZoom: 0.02,
  maxZoom: 3.0,
  _renderScheduled: false,  // rAF 合并标记，防高频事件堆积
  dragging: false,
  dragStartX: 0,
  dragStartY: 0,
  dragViewX: 0,
  dragViewY: 0,
  selectedNode: null,
  hoveredNode: null,
  highlightedIds: null,  // Set<number>：需要高亮的成员 ID 集合
  _visibleSet: null,     // Set<number>：当前可见节点 ID，替换 O(n) includes 为 O(1) has
  _treeBounds: null,     // { minX, maxX, minY, maxY, treeW, treeH }：缓存套，布局时计算一次
  onNodeClick: null,
  onNodeDblClick: null,  // 双击回调（用于折叠/展开）

  // 颜色
  colorMale: '#5B8DB8',
  colorMaleFill: '#EDF4FA',
  colorFemale: '#D4828A',
  colorFemaleFill: '#FDF0F2',
  colorSpouse: '#9B7FB8',
  colorLine: '#C4B8A8',
  colorSelected: '#D4A574',
  colorHighlight: '#E6A817',  // 查询目标高亮色（金色）
  colorText: '#2C2C2C',
  colorTextLight: '#888',
  colorCollapse: '#FF9800',   // 折叠标记颜色

  // ---- 初始化 ----
  init(canvasId, onNodeClick, onNodeDblClick) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.onNodeClick = onNodeClick;
    this.onNodeDblClick = onNodeDblClick || null;
    this.dpr = window.devicePixelRatio || 1;
    this._bindEvents();
    this._resize();
    if (this._onResize) window.removeEventListener('resize', this._onResize);
    this._onResize = () => this._resize();
    window.addEventListener('resize', this._onResize);
  },

  _resize() {
    const rect = this.canvas.parentElement.getBoundingClientRect();
    this.canvas.width = rect.width * this.dpr;
    this.canvas.height = rect.height * this.dpr;
    this.canvas.style.width = rect.width + 'px';
    this.canvas.style.height = rect.height + 'px';
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this._render();
  },

  // ---- 事件绑定 ----
  _bindEvents() {
    if (this._eventsBound) return;
    this._eventsBound = true;

    // 用于区分单击和双击
    this._lastClickTime = 0;
    this._lastClickNode = null;

    this._onWheel = e => {
      e.preventDefault();
      const rect = this.canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const factor = e.deltaY < 0 ? 1.1 : 0.9;
      const newZoom = Math.max(this.minZoom, Math.min(this.maxZoom, this.zoom * factor));
      this.viewX = mx - (mx - this.viewX) * (newZoom / this.zoom);
      this.viewY = my - (my - this.viewY) * (newZoom / this.zoom);
      this.zoom = newZoom;
      this._render();
    };
    this.canvas.addEventListener('wheel', this._onWheel, { passive: false });

    this._onMouseDown = e => {
      this.dragging = true;
      this.dragStartX = e.clientX;
      this.dragStartY = e.clientY;
      this.dragViewX = this.viewX;
      this.dragViewY = this.viewY;
      this.canvas.style.cursor = 'grabbing';
    };
    this.canvas.addEventListener('mousedown', this._onMouseDown);

    this._onMouseMove = e => {
      if (this.dragging) {
        this.viewX = this.dragViewX + (e.clientX - this.dragStartX);
        this.viewY = this.dragViewY + (e.clientY - this.dragStartY);
        this._render();
      } else {
        const rect = this.canvas.getBoundingClientRect();
        const mx = (e.clientX - rect.left - this.viewX) / this.zoom;
        const my = (e.clientY - rect.top - this.viewY) / this.zoom;
        const old = this.hoveredNode;
        this.hoveredNode = this._hitTest(mx, my);
        if (old !== this.hoveredNode) {
          this._render();
          this.canvas.style.cursor = this.hoveredNode ? 'pointer' : 'grab';
        }
      }
    };
    window.addEventListener('mousemove', this._onMouseMove);

    this._onMouseUp = e => {
      if (this.dragging) {
        const dx = Math.abs(e.clientX - this.dragStartX);
        const dy = Math.abs(e.clientY - this.dragStartY);
        if (dx < 3 && dy < 3) {
          // 点击事件
          const rect = this.canvas.getBoundingClientRect();
          const mx = (e.clientX - rect.left - this.viewX) / this.zoom;
          const my = (e.clientY - rect.top - this.viewY) / this.zoom;
          const node = this._hitTest(mx, my);
          if (node) {
            const now = Date.now();
            // 检测双击（300ms 内连续两次点击同一节点）
            if (
              this._lastClickNode === node &&
              now - this._lastClickTime < 300 &&
              this.onNodeDblClick
            ) {
              // 双击：折叠/展开
              this.onNodeDblClick(node.member);
              // 不触发单击
              this._lastClickTime = 0;
              this._lastClickNode = null;
            } else if (this.onNodeClick) {
              // 单击：延迟执行，等待可能到来的双击
              const clickNode = node;
              this._lastClickTime = now;
              this._lastClickNode = node;
              // 300ms 后如果没等到双击，执行单击
              setTimeout(() => {
                if (this._lastClickNode === clickNode && this._lastClickTime === now) {
                  this.selectedNode = clickNode;
                  this._render();
                  this.onNodeClick(clickNode.member);
                }
              }, 310);
            }
          }
        }
        this.dragging = false;
        this.canvas.style.cursor = this.hoveredNode ? 'pointer' : 'grab';
      }
    };
    window.addEventListener('mouseup', this._onMouseUp);

    // 触摸事件
    this._onTouchStart = e => {
      if (e.touches.length === 1) {
        this.dragging = true;
        this.dragStartX = e.touches[0].clientX;
        this.dragStartY = e.touches[0].clientY;
        this.dragViewX = this.viewX;
        this.dragViewY = this.viewY;
      }
    };
    this.canvas.addEventListener('touchstart', this._onTouchStart);

    this._onTouchMove = e => {
      e.preventDefault();
      if (this.dragging && e.touches.length === 1) {
        this.viewX = this.dragViewX + (e.touches[0].clientX - this.dragStartX);
        this.viewY = this.dragViewY + (e.touches[0].clientY - this.dragStartY);
        this._render();
      }
    };
    this.canvas.addEventListener('touchmove', this._onTouchMove, { passive: false });

    this._onTouchEnd = () => { this.dragging = false; };
    this.canvas.addEventListener('touchend', this._onTouchEnd);
  },

  // ---- 碰撞检测 ----
  _hitTest(mx, my) {
    for (let i = this.nodes.length - 1; i >= 0; i--) {
      const n = this.nodes[i];
      if (mx >= n.x && mx <= n.x + n.w && my >= n.y && my <= n.y + n.h) {
        return n;
      }
    }
    return null;
  },

  // ---- 构建树 ----
  buildTree(members) {
    const map = {};
    members.forEach(m => {
      map[m.member_id] = {
        member: m,
        id: m.member_id,
        name: m.name || '',
        gender: m.gender || 'male',
        birth: (m.birth_date || '').substring(0, 10),
        death: (m.death_date || '').substring(0, 10),
        generation: m.generation ?? 1,
        fatherId: m.father_id,
        motherId: m.mother_id,
        spouseId: m.spouse_id,
        spouse: null,
        children: [],
        parent: null,
        x: 0, y: 0, w: this.NODE_W, h: this.NODE_H,
        subtreeW: 1,
        collapsed: false,
      };
    });

    // 建立父子关系（父亲优先，父亲不在时退回母亲）
    Object.values(map).forEach(n => {
      let pid = null;
      // 父亲在数据中 → 优先用父亲
      if (n.fatherId && map[n.fatherId]) {
        pid = n.fatherId;
      } else if (n.motherId && map[n.motherId]) {
        // 父亲不在数据中，尝试母亲
        pid = n.motherId;
      }
      if (pid) {
        n.parent = map[pid];
        map[pid].children.push(n);
      }
    });

    // 建立配偶关系（双向：丈夫 ↔ 妻子）
    Object.values(map).forEach(n => {
      if (n.spouseId && map[n.spouseId]) {
        const spouse = map[n.spouseId];
        if (!n.spouse && !spouse.spouse) {
          n.spouse = spouse;
          spouse.spouse = n;
        }
      }
    });

    const roots = Object.values(map).filter(n => !n.parent);
    if (roots.length === 0 && Object.keys(map).length > 0) {
      const minGen = Math.min(...Object.values(map).map(n => n.generation));
      return { roots: Object.values(map).filter(n => n.generation === minGen), map };
    }

    this.nodeMap = map;
    this.roots = roots;
    return { roots, map };
  },

  // ---- 折叠/展开（逐代展开，不一次性展开到底）----
  toggleCollapse(memberId) {
    const node = this.nodeMap[memberId];
    if (!node || node.children.length === 0) return;
    if (node.collapsed) {
      // 展开：只展开一层，子节点默认再折叠以保证逐代展开
      node.collapsed = false;
      node.children.forEach(child => {
        if (child.children.length > 0) {
          child.collapsed = true;
        }
      });
    } else {
      // 折叠：收起到只剩本节点
      node.collapsed = true;
    }
    this._relayout();
    this._render();
  },

  // 展开全部后代（跳过逐代限制，一次性展示到底）
  expandAll(memberId) {
    const node = this.nodeMap[memberId];
    if (!node) return;
    node.collapsed = false;
    node.children.forEach(child => {
      child.collapsed = false;
      this._uncollapseChildren(child);
    });
    this._relayout();
    this._render();
  },

  _uncollapseChildren(node) {
    node.collapsed = false;
    node.children.forEach(child => this._uncollapseChildren(child));
  },

  // 一键展开全部（从所有根节点递归展开到底）
  expandAllNodes() {
    const visited = new Set();
    const uncollapse = (node) => {
      if (visited.has(node.id)) return;
      visited.add(node.id);
      node.collapsed = false;
      node.children.forEach(c => uncollapse(c));
    };
    this.roots.forEach(r => uncollapse(r));
    // 也需要处理非根但有子节点的（配偶等）
    Object.values(this.nodeMap).forEach(n => {
      if (n.children.length > 0) n.collapsed = false;
    });
    this._relayout();
    this._render();
  },

  // 一键折叠：先展开全部，再执行默认折叠（深度 >= AUTO_COLLAPSE_DEPTH 折叠）
  collapseAllNodes() {
    // 先全部展开
    const visited = new Set();
    const uncollapse = (node) => {
      if (visited.has(node.id)) return;
      visited.add(node.id);
      node.collapsed = false;
      node.children.forEach(c => uncollapse(c));
    };
    this.roots.forEach(r => uncollapse(r));
    Object.values(this.nodeMap).forEach(n => {
      if (n.children.length > 0) n.collapsed = false;
    });
    // 再执行默认折叠（与首次加载一致）
    this._autoCollapse();
    this._render();
  },

  // ---- 计算某节点及所有后代 ID（用于折叠统计）----
  _countDescendants(node, visited = new Set()) {
    if (visited.has(node.id)) return 0;
    visited.add(node.id);
    let count = node.children.length;
    node.children.forEach(c => { count += this._countDescendants(c, visited); });
    return count;
  },

  // ---- 重新布局（不重建树，保留 collapsed 状态）----
  _relayout() {
    const map = this.nodeMap;
    const roots = this.roots;
    if (!roots || roots.length === 0) return;

    const minGen = Math.min(...Object.values(map).map(n => n.generation));
    Object.values(map).forEach(n => { n._depth = n.generation - minGen; });

    // 后序遍历计算子树宽度（折叠时视为叶子）
    const calcWidth = (node, visited = new Set()) => {
      if (visited.has(node.id)) return 1;
      visited.add(node.id);
      if (node.collapsed || node.children.length === 0) {
        node.subtreeW = 1;
      } else {
        let w = 0;
        node.children.forEach(c => { w += calcWidth(c, visited); });
        node.subtreeW = Math.max(w, 1);
      }
      return node.subtreeW;
    };
    roots.forEach(r => calcWidth(r));

    let nextX = 0;
    const position = (node, depth) => {
      if (node.children.length === 0 || node.collapsed) {
        node._x = nextX;
        nextX += 1;
      } else {
        const startX = nextX;
        node.children.forEach(c => position(c, depth + 1));
        const endX = nextX;
        node._x = (startX + endX - 1) / 2;
      }
    };
    roots.forEach(r => position(r, 0));

    // 过滤掉因折叠而不可见的后代节点
    const visibleNodes = [];
    const collectVisible = (node, visited = new Set()) => {
      if (visited.has(node.id)) return;
      visited.add(node.id);
      node.x = node._x * (this.NODE_W + this.H_GAP);
      node.y = (node._depth || 0) * this.V_GAP;
      node.w = this.NODE_W;
      node.h = this.NODE_H;
      visibleNodes.push(node);
      if (!node.collapsed) {
        node.children.forEach(c => collectVisible(c, visited));
      }
    };
    roots.forEach(r => collectVisible(r));
    this.nodes = visibleNodes;

    // 构建可见节点 ID 集合（替换 O(n) includes 为 O(1) has）
    this._visibleSet = new Set();
    visibleNodes.forEach(n => this._visibleSet.add(n.id));

    // 缓存树边界（避免 render / 滑轨每次 O(n) 重新遍历）
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    visibleNodes.forEach(n => {
      if (n.x < minX) minX = n.x;
      if (n.x + n.w > maxX) maxX = n.x + n.w;
      if (n.y < minY) minY = n.y;
      if (n.y + n.h > maxY) maxY = n.y + n.h;
    });
    this._treeBounds = { minX, maxX, minY, maxY, treeW: maxX - minX + 40, treeH: maxY - minY + 40 };
  },

  // ---- 布局算法 ----
  layout(members) {
    const { roots, map } = this.buildTree(members);
    if (roots.length === 0) { this.nodes = []; return; }

    this._relayout();
  },

  // ---- 渲染（requestAnimationFrame 合并，避免高频事件堆积）----
  _render() {
    if (this._renderScheduled) return;
    this._renderScheduled = true;
    requestAnimationFrame(() => {
      this._renderScheduled = false;
      if (!this.ctx || !this.canvas) return;
      this._doRender();
    });
  },

  _doRender() {
    const ctx = this.ctx;
    const w = this.canvas.width / this.dpr;
    const h = this.canvas.height / this.dpr;
    ctx.clearRect(0, 0, w, h);

    ctx.fillStyle = '#FFFEFB';
    ctx.fillRect(0, 0, w, h);

    ctx.save();
    ctx.translate(this.viewX, this.viewY);
    ctx.scale(this.zoom, this.zoom);

    const vpLeft   = -this.viewX / this.zoom;
    const vpTop    = -this.viewY / this.zoom;
    const vpRight  = vpLeft + w / this.zoom;
    const vpBottom = vpTop + h / this.zoom;
    const margin = 200;

    // 连线（不裁剪——父节点可能滚出视口但子节点在视口内，连线绘制很轻量）
    for (let i = 0; i < this.nodes.length; i++) {
      this._drawConnections(this.nodes[i]);
    }

    // 节点
    for (let i = 0; i < this.nodes.length; i++) {
      const n = this.nodes[i];
      if (n.x + n.w < vpLeft - margin || n.x > vpRight + margin) continue;
      if (n.y + n.h < vpTop - margin || n.y > vpBottom + margin) continue;
      this._drawNode(n);
    }

    ctx.restore();
  },

  // ---- 绘制竖式卡片 ----
  _drawNode(n) {
    const ctx = this.ctx;
    const { x, y, w, h } = n;
    const isMale = n.gender === 'male';
    const isSelected = n === this.selectedNode;
    const isHovered = n === this.hoveredNode;
    const isHighlighted = this.highlightedIds && this.highlightedIds.has(n.id);

    // 阴影
    if (isHighlighted) {
      ctx.shadowColor = this.colorHighlight;
      ctx.shadowBlur = 10;
    } else if (isSelected) {
      ctx.shadowColor = this.colorSelected;
      ctx.shadowBlur = 12;
    } else if (isHovered) {
      ctx.shadowColor = 'rgba(0,0,0,0.15)';
      ctx.shadowBlur = 6;
    }

    // 圆角矩形
    const r = this.NODE_RADIUS;
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.quadraticCurveTo(x + w, y, x + w, y + r);
    ctx.lineTo(x + w, y + h - r);
    ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    ctx.lineTo(x + r, y + h);
    ctx.quadraticCurveTo(x, y + h, x, y + h - r);
    ctx.lineTo(x, y + r);
    ctx.quadraticCurveTo(x, y, x + r, y);
    ctx.closePath();

    // 填充
    ctx.fillStyle = isMale ? this.colorMaleFill : this.colorFemaleFill;
    ctx.fill();

    // 边框
    if (isHighlighted) {
      ctx.strokeStyle = this.colorHighlight;
      ctx.lineWidth = 2.5;
    } else if (isSelected) {
      ctx.strokeStyle = this.colorSelected;
      ctx.lineWidth = 2;
    } else if (isHovered) {
      ctx.strokeStyle = '#999';
      ctx.lineWidth = 1.5;
    } else {
      ctx.strokeStyle = isMale ? this.colorMale : this.colorFemale;
      ctx.lineWidth = 1;
    }
    ctx.stroke();

    ctx.shadowColor = 'transparent';
    ctx.shadowBlur = 0;

    // ---- 绘制竖向文字 ----
    // 顶部：辈分标签
    ctx.fillStyle = isMale ? this.colorMale : this.colorFemale;
    ctx.font = '8px "PingFang SC","Microsoft YaHei",sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(`第${n.generation}代`, x + w / 2, y + 12);

    // 中部：姓名竖排（逐字从上到下）
    ctx.fillStyle = this.colorText;
    ctx.font = 'bold 12px "PingFang SC","Microsoft YaHei",sans-serif';
    ctx.textAlign = 'center';
    const chars = [...n.name];
    const charGap = 14;
    const nameH = chars.length * charGap;
    const nameStartY = y + (h - nameH) / 2 + charGap * 0.6;
    chars.forEach((ch, i) => {
      ctx.fillText(ch, x + w / 2, nameStartY + i * charGap);
    });

    // 底部：出生年份
    ctx.fillStyle = this.colorTextLight;
    ctx.font = '8px "PingFang SC","Microsoft YaHei",sans-serif';
    ctx.textAlign = 'center';
    const birthYear = n.birth ? n.birth.substring(0, 4) : '';
    ctx.fillText(birthYear || '?', x + w / 2, y + h - 8);

    // ---- 折叠标记 ----
    if (n.children.length > 0 && n.collapsed) {
      const cx = x + w / 2;
      const cy = y + h + 10;
      // 小圆形背景
      ctx.fillStyle = this.colorCollapse;
      ctx.beginPath();
      ctx.arc(cx, cy, 8, 0, Math.PI * 2);
      ctx.fill();
      // 数字
      const total = this._countDescendants(n);
      ctx.fillStyle = '#fff';
      ctx.font = 'bold 9px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(Math.min(total, 99) + '+', cx, cy + 3);
    }
  },

  // ---- 绘制连线 ----
  _drawConnections(n) {
    const ctx = this.ctx;
    ctx.strokeStyle = this.colorLine;
    ctx.lineWidth = 1;
    ctx.setLineDash([]);

    // 父子连线（只连可见的孩子）
    n.children.forEach(child => {
      if (child === n) return;
      // 跳过因折叠不可见的节点（O(1) Set 查找）
      if (!this._visibleSet || !this._visibleSet.has(child.id)) return;
      const px = n.x + n.w / 2;
      const py = n.y + n.h;
      const cx = child.x + child.w / 2;
      const cy = child.y;

      // 从父节点底部中心垂直向下 → 水平转折 → 连接到子节点顶部
      const midY = py + (cy - py) / 2;
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(px, midY);
      ctx.lineTo(cx, midY);
      ctx.lineTo(cx, cy);
      ctx.stroke();
    });

    // 配偶连线（每个配偶对只画一次，取 id 较小的一方，O(1) Set 查找）
    if (n.spouse && n.id < n.spouse.id && this._visibleSet && this._visibleSet.has(n.spouse.id)) {
      const sx = n.x + n.w;
      const sy = n.y + n.h / 2;
      const ex = n.spouse.x;
      const ey = n.spouse.y + n.spouse.h / 2;
      ctx.strokeStyle = this.colorSpouse;
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(sx, sy - 2);
      ctx.lineTo(ex, ey - 2);
      ctx.setLineDash([]);
      ctx.stroke();
    }
  },

  // ---- 折叠/展开回调（供外部调用）----
  handleDblClick(member) {
    this.toggleCollapse(member.member_id);
  },

  // ---- 公共方法 ----
  loadMembers(members) {
    this.layout(members);
    this._autoCollapse();
    this._alignRootTop();
    this._render();
  },

  // 初次加载时自动折叠超出 AUTO_COLLAPSE_DEPTH 的深度
  _autoCollapse() {
    // 计算每个节点从根开始的结构深度
    const calcDepth = (node, depth, visited = new Set()) => {
      if (visited.has(node.id)) return;
      visited.add(node.id);
      node._treeDepth = depth;
      node.children.forEach(c => calcDepth(c, depth + 1, visited));
    };
    this.roots.forEach(r => calcDepth(r, 0));
    // 折叠深度 >= AUTO_COLLAPSE_DEPTH-1 的有子节点（这样刚好显示 AUTO_COLLAPSE_DEPTH 代）
    const threshold = this.AUTO_COLLAPSE_DEPTH - 1;
    Object.values(this.nodeMap).forEach(n => {
      if (n._treeDepth >= threshold && n.children.length > 0) {
        n.collapsed = true;
      }
    });
    if (threshold <= 0) return; // 阈值为 0 意味着全部折叠，那不需要重新布局
    this._relayout();
  },

  _centerView() {
    if (this.nodes.length === 0) return;
    const rect = this.canvas.getBoundingClientRect();
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    this.nodes.forEach(n => {
      minX = Math.min(minX, n.x);
      maxX = Math.max(maxX, n.x + n.w);
      minY = Math.min(minY, n.y);
      maxY = Math.max(maxY, n.y + n.h);
    });
    const treeW = maxX - minX;
    const treeH = maxY - minY;
    this.zoom = Math.min(1.0, (rect.width - 60) / treeW, (rect.height - 60) / treeH);
    this.viewX = (rect.width - treeW * this.zoom) / 2 - minX * this.zoom;
    this.viewY = (rect.height - treeH * this.zoom) / 2 - minY * this.zoom;
  },

  // 根节点顶部对齐（首次加载 / 重置时使用）
  _alignRootTop() {
    if (this.nodes.length === 0) return;
    const rect = this.canvas.getBoundingClientRect();
    // 水平居中：计算可见节点的水平范围
    let minX = Infinity, maxX = -Infinity;
    this.nodes.forEach(n => {
      minX = Math.min(minX, n.x);
      maxX = Math.max(maxX, n.x + n.w);
    });
    const treeW = maxX - minX;
    const treeH = (this.nodes.length > 0 ? Math.max(...this.nodes.map(n => n.y + n.h)) - Math.min(...this.nodes.map(n => n.y)) : 0);
    this.zoom = Math.min(1.0, (rect.width - 60) / treeW, (rect.height - 60) / treeH);
    this.viewX = (rect.width - treeW * this.zoom) / 2 - minX * this.zoom;
    this.viewY = 20; // 顶部留 20px，根节点对齐顶部
  },

  zoomIn()  { this.zoom = Math.min(this.maxZoom, this.zoom * 1.2); this._render(); },
  zoomOut() { this.zoom = Math.max(this.minZoom, this.zoom / 1.2); this._render(); },
  fitView() { this._centerView(); this._render(); },

  // 重置视图：优先定位到高亮节点，无高亮节点时根节点顶部对齐
  resetView() {
    if (this.highlightedIds && this.highlightedIds.size > 0) {
      const targetId = [...this.highlightedIds][0];
      const targetNode = this.nodeMap[targetId];
      if (targetNode && this.nodes.includes(targetNode)) {
        this._centerOnNode(targetNode);
        this._render();
        return;
      }
    }
    this._alignRootTop();
    this._render();
  },

  // 将视口居中到指定节点，并缩放到合适大小
  _centerOnNode(node) {
    const rect = this.canvas.getBoundingClientRect();
    const nodeCenterX = node.x + node.w / 2;
    const nodeCenterY = node.y + node.h / 2;
    // 让节点在画面中约占 1/4，便于观看，同时限制缩放范围
    const targetZoom = Math.min(
      1.5,
      (rect.width - 80) / (node.w * 4),
      (rect.height - 80) / (node.h * 4)
    );
    this.zoom = Math.max(0.3, Math.min(1.8, targetZoom));
    this.viewX = rect.width / 2 - nodeCenterX * this.zoom;
    this.viewY = rect.height / 2 - nodeCenterY * this.zoom;
  },

  refresh() {
    this._render();
  },

  // ---- 销毁 ----
  destroy() {
    if (this._onWheel)      { this.canvas?.removeEventListener('wheel', this._onWheel); this._onWheel = null; }
    if (this._onMouseDown)  { this.canvas?.removeEventListener('mousedown', this._onMouseDown); this._onMouseDown = null; }
    if (this._onMouseMove)  { window.removeEventListener('mousemove', this._onMouseMove); this._onMouseMove = null; }
    if (this._onMouseUp)    { window.removeEventListener('mouseup', this._onMouseUp); this._onMouseUp = null; }
    if (this._onTouchStart) { this.canvas?.removeEventListener('touchstart', this._onTouchStart); this._onTouchStart = null; }
    if (this._onTouchMove)  { this.canvas?.removeEventListener('touchmove', this._onTouchMove); this._onTouchMove = null; }
    if (this._onTouchEnd)   { this.canvas?.removeEventListener('touchend', this._onTouchEnd); this._onTouchEnd = null; }
    if (this._onResize)     { window.removeEventListener('resize', this._onResize); this._onResize = null; }
    this._eventsBound = false;
    this._renderScheduled = false;  // 清除可能残留的 rAF 标记
  },

  // ---- 工厂方法 ----
  createInstance() {
    const inst = {};
    for (const key in TreeRenderer) {
      if (typeof TreeRenderer[key] === 'function') {
        inst[key] = TreeRenderer[key];
      }
    }
    inst.NODE_W = 56;
    inst.NODE_H = 82;
    inst.H_GAP = 12;
    inst.V_GAP = 110;
    inst.NODE_RADIUS = 6;
    inst.AUTO_COLLAPSE_DEPTH = 8;
    inst.canvas = null;
    inst.ctx = null;
    inst.dpr = 1;
    inst.nodes = [];
    inst.nodeMap = {};
    inst.roots = [];
    inst.viewX = 0;
    inst.viewY = 0;
    inst.zoom = 1.0;
    inst.minZoom = 0.02;
    inst.maxZoom = 3.0;
    inst._renderScheduled = false;
    inst.dragging = false;
    inst.dragStartX = 0;
    inst.dragStartY = 0;
    inst.dragViewX = 0;
    inst.dragViewY = 0;
    inst.selectedNode = null;
    inst.hoveredNode = null;
    inst.highlightedIds = null;
    inst._visibleSet = null;
    inst._treeBounds = null;
    inst.onNodeClick = null;
    inst.onNodeDblClick = null;
    inst._eventsBound = false;
    inst._lastClickTime = 0;
    inst._lastClickNode = null;
    inst._onWheel = null;
    inst._onMouseDown = null;
    inst._onMouseMove = null;
    inst._onMouseUp = null;
    inst._onTouchStart = null;
    inst._onTouchMove = null;
    inst._onTouchEnd = null;
    inst._onResize = null;

    inst.colorMale = '#5B8DB8';
    inst.colorMaleFill = '#EDF4FA';
    inst.colorFemale = '#D4828A';
    inst.colorFemaleFill = '#FDF0F2';
    inst.colorSpouse = '#9B7FB8';
    inst.colorLine = '#C4B8A8';
    inst.colorSelected = '#D4A574';
    inst.colorHighlight = '#E6A817';
    inst.colorText = '#2C2C2C';
    inst.colorTextLight = '#888';
    inst.colorCollapse = '#FF9800';

    return inst;
  },
};