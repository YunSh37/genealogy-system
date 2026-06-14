/* ============================================================
   Canvas 族谱树渲染引擎
   高性能：视口裁剪 + 分级渲染 + requestAnimationFrame
   ============================================================ */

const TreeRenderer = {
  // ---- 配置 ----
  NODE_W: 120,
  NODE_H: 40,
  H_GAP: 24,
  V_GAP: 60,
  NODE_RADIUS: 8,

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
  dragging: false,
  dragStartX: 0,
  dragStartY: 0,
  dragViewX: 0,
  dragViewY: 0,
  selectedNode: null,
  hoveredNode: null,
  onNodeClick: null,

  // 颜色
  colorMale: '#5B8DB8',
  colorMaleFill: '#EDF4FA',
  colorFemale: '#D4828A',
  colorFemaleFill: '#FDF0F2',
  colorSpouse: '#9B7FB8',
  colorLine: '#C4B8A8',
  colorSelected: '#D4A574',
  colorText: '#2C2C2C',
  colorTextLight: '#888',

  // ---- 初始化 ----
  init(canvasId, onNodeClick) {
    this.canvas = document.getElementById(canvasId);
    this.ctx = this.canvas.getContext('2d');
    this.onNodeClick = onNodeClick;
    this.dpr = window.devicePixelRatio || 1;
    this._bindEvents();
    this._resize();
    // 存储 resize 处理器引用以便 destroy 时移除
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
    if (this._eventsBound) return;  // 防止重复绑定（同一实例多次 init 时）
    this._eventsBound = true;

    this._onWheel = e => {
      e.preventDefault();
      const rect = this.canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const factor = e.deltaY < 0 ? 1.1 : 0.9;
      const newZoom = Math.max(this.minZoom, Math.min(this.maxZoom, this.zoom * factor));
      // 以鼠标为中心缩放
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
        // 悬停检测
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
          if (node && this.onNodeClick) {
            this.selectedNode = node;
            this._render();
            this.onNodeClick(node.member);
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
    // 从后往前检测（后绘制的在上层）
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
        generation: m.generation ?? 1,  // 默认第1代（不为0）
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

    // 建立父子关系
    Object.values(map).forEach(n => {
      const pid = n.fatherId || n.motherId;
      if (pid && map[pid]) {
        n.parent = map[pid];
        map[pid].children.push(n);
      }
    });

    // 建立配偶关系
    Object.values(map).forEach(n => {
      if (n.spouseId && map[n.spouseId] && n.gender === 'male') {
        n.spouse = map[n.spouseId];
        map[n.spouseId].spouse = n;
      }
    });

    // 孤儿节点回退：父亲/母亲不在集合中时，按辈分推测（解决后代查询中中间层缺失问题）
    const minGen = Math.min(...Object.values(map).map(n => n.generation));
    Object.values(map).forEach(n => {
      if (!n.parent && (n.fatherId || n.motherId) && n.generation > minGen) {
        const candidates = Object.values(map).filter(m =>
          m !== n && m.generation === n.generation - 1
        );
        if (candidates.length > 0) {
          // 选子树宽度最小的（尽量平衡）
          candidates.sort((a, b) => a.subtreeW - b.subtreeW);
          n.parent = candidates[0];
          candidates[0].children.push(n);
        }
      }
    });

    // 找根节点
    const roots = Object.values(map).filter(n => !n.parent);
    if (roots.length === 0 && Object.keys(map).length > 0) {
      // 取最小辈分
      const minGen = Math.min(...Object.values(map).map(n => n.generation));
      return { roots: Object.values(map).filter(n => n.generation === minGen), map };
    }

    this.nodeMap = map;
    this.roots = roots;
    return { roots, map };
  },

  // ---- 布局算法 ----
  layout(members) {
    const { roots, map } = this.buildTree(members);
    if (roots.length === 0) { this.nodes = []; return; }

    // 计算每个节点的深度（相对辈分）
    const minGen = Math.min(...Object.values(map).map(n => n.generation));
    Object.values(map).forEach(n => { n._depth = n.generation - minGen; });

    // 后序遍历计算子树宽度
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

    // 递归定位
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

    // 转换为像素坐标
    const nodes = [];
    Object.values(map).forEach(n => {
      n.x = n._x * (this.NODE_W + this.H_GAP);
      n.y = (n._depth || 0) * this.V_GAP;
      n.w = this.NODE_W;
      n.h = this.NODE_H;
      nodes.push(n);
    });

    // 去重配偶节点（它们有自己的坐标）
    this.nodes = nodes;
    this.roots = roots;
  },

  // ---- 渲染 ----
  _render() {
    const ctx = this.ctx;
    const w = this.canvas.width / this.dpr;
    const h = this.canvas.height / this.dpr;
    ctx.clearRect(0, 0, w, h);

    // 背景
    ctx.fillStyle = '#FFFEFB';
    ctx.fillRect(0, 0, w, h);

    ctx.save();
    ctx.translate(this.viewX, this.viewY);
    ctx.scale(this.zoom, this.zoom);

    // 计算视口范围（世界坐标）
    const vpLeft   = -this.viewX / this.zoom;
    const vpTop    = -this.viewY / this.zoom;
    const vpRight  = vpLeft + w / this.zoom;
    const vpBottom = vpTop + h / this.zoom;
    const margin = 200; // 视口外扩展

    // 渲染连线（不裁剪——父节点可能滚出视口但子节点在视口内）
    this.nodes.forEach(n => {
      this._drawConnections(n);
    });

    // 渲染节点
    this.nodes.forEach(n => {
      if (n.x + n.w < vpLeft - margin || n.x > vpRight + margin) return;
      if (n.y + n.h < vpTop - margin || n.y > vpBottom + margin) return;
      this._drawNode(n);
    });

    ctx.restore();
  },

  _drawNode(n) {
    const ctx = this.ctx;
    const { x, y, w, h } = n;
    const isMale = n.gender === 'male';
    const isSelected = n === this.selectedNode;
    const isHovered = n === this.hoveredNode;

    // 阴影
    if (isSelected) {
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
    ctx.strokeStyle = isSelected ? this.colorSelected : (isHovered ? '#999' : (isMale ? this.colorMale : this.colorFemale));
    ctx.lineWidth = isSelected ? 2.5 : (isHovered ? 2 : 1.5);
    ctx.stroke();

    ctx.shadowColor = 'transparent';
    ctx.shadowBlur = 0;

    // 辈分标签（左上角）
    ctx.fillStyle = isMale ? this.colorMale : this.colorFemale;
    ctx.font = '9px "PingFang SC","Microsoft YaHei",sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(`第${n.generation}代`, x + 4, y + 12);

    // 姓名（居中）
    ctx.fillStyle = this.colorText;
    ctx.font = 'bold 13px "PingFang SC","Microsoft YaHei",sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(n.name, x + w / 2, y + h - 12);
  },

  _drawConnections(n) {
    const ctx = this.ctx;
    ctx.strokeStyle = this.colorLine;
    ctx.lineWidth = 1;
    ctx.setLineDash([]);

    // 父子连线
    n.children.forEach(child => {
      if (child === n) return; // 防止自引用
      const px = n.x + n.w / 2;
      const py = n.y + n.h;
      const cx = child.x + child.w / 2;
      const cy = child.y;
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(px, py + 10);
      ctx.lineTo(cx, py + 10);
      ctx.lineTo(cx, cy);
      ctx.stroke();
    });

    // 配偶连线（双线）
    if (n.spouse && n.gender === 'male') {
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
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(sx, sy + 2);
      ctx.lineTo(ex, ey + 2);
      ctx.stroke();
      ctx.setLineDash([]);
    }
  },

  // ---- 公共方法 ----
  loadMembers(members) {
    this.layout(members);
    this._centerView();
    this._render();
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

  zoomIn()  { this.zoom = Math.min(this.maxZoom, this.zoom * 1.2); this._render(); },
  zoomOut() { this.zoom = Math.max(this.minZoom, this.zoom / 1.2); this._render(); },
  fitView() { this._centerView(); this._render(); },
  resetView() { this.zoom = 1.0; this.viewX = 0; this.viewY = 0; this._render(); },

  refresh() {
    this._render();
  },

  // ---- 销毁（移除所有事件监听器） ----
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
  },
};