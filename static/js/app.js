/* ============================================================
   Canvas 横向滑轨模块
   为 Canvas 树谱树添加底部滚动条，控制左右平移
   ============================================================ */
const HScrollbar = {
  attach(renderer, containerSelector) {
    if (!renderer || !renderer.canvas) return;

    const canvas = renderer.canvas;
    const parent = canvas.parentElement;
    if (!parent) return;

    // 创建滑轨（若已存在则复用）
    let track = parent.querySelector('.canvas-hscroll');
    if (!track) {
      track = document.createElement('div');
      track.className = 'canvas-hscroll';
      track.innerHTML = '<div class="canvas-hscroll-thumb"></div>';
      parent.appendChild(track);
    }
    const thumb = track.querySelector('.canvas-hscroll-thumb');

    // 拖拽滑轨
    let dragging = false, startX = 0, startLeft = 0;
    thumb.onmousedown = (e) => {
      e.preventDefault();
      dragging = true;
      startX = e.clientX;
      startLeft = thumb.offsetLeft;
      document.body.style.userSelect = 'none';
    };
    const onMove = (e) => {
      if (!dragging) return;
      const trackW = track.clientWidth;
      const thumbW = thumb.offsetWidth;
      const maxLeft = trackW - thumbW;
      if (maxLeft <= 0) return;
      const newLeft = Math.max(0, Math.min(maxLeft, startLeft + (e.clientX - startX)));
      const ratio = newLeft / maxLeft;
      const nodes = renderer.nodes || [];
      if (nodes.length === 0) return;
      let minX = Infinity, maxX = -Infinity;
      nodes.forEach(n => {
        if (n.x < minX) minX = n.x;
        if (n.x + n.w > maxX) maxX = n.x + n.w;
      });
      const treeW = maxX - minX + 40;
      const vpW = canvas.clientWidth / renderer.zoom;
      if (treeW <= vpW) return;
      const viewRange = treeW - vpW;
      renderer.viewX = -(minX - 20 + ratio * viewRange) * renderer.zoom;
      renderer._render();
    };
    const onUp = () => { dragging = false; document.body.style.userSelect = ''; };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);

    // 点击轨道跳转
    track.onclick = (e) => {
      if (e.target === thumb) return;
      const rect = track.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const thumbW = thumb.offsetWidth;
      const maxLeft = track.clientWidth - thumbW;
      if (maxLeft <= 0) return;
      const newLeft = Math.max(0, Math.min(maxLeft, clickX - thumbW / 2));
      const ratio = newLeft / maxLeft;
      const nodes = renderer.nodes || [];
      if (nodes.length === 0) return;
      let minX = Infinity, maxX = -Infinity;
      nodes.forEach(n => {
        if (n.x < minX) minX = n.x;
        if (n.x + n.w > maxX) maxX = n.x + n.w;
      });
      const treeW = maxX - minX + 40;
      const vpW = canvas.clientWidth / renderer.zoom;
      if (treeW <= vpW) return;
      renderer.viewX = -(minX - 20 + ratio * (treeW - vpW)) * renderer.zoom;
      renderer._render();
    };

    // Hook _render 同步滑轨位置（canvas 平移/缩放时自动更新滑轨）
    const origRender = renderer._render.bind(renderer);
    renderer._render = function() {
      origRender();
      const nodes = this.nodes || [];
      if (nodes.length === 0) { track.style.display = 'none'; return; }
      let minX = Infinity, maxX = -Infinity;
      nodes.forEach(n => {
        if (n.x < minX) minX = n.x;
        if (n.x + n.w > maxX) maxX = n.x + n.w;
      });
      const treeW = maxX - minX + 40;
      const vpW = canvas.clientWidth / this.zoom;
      if (treeW <= vpW) { track.style.display = 'none'; return; }
      track.style.display = '';
      const viewStart = -this.viewX / this.zoom;
      const viewEnd = viewStart + vpW;
      const trackW = track.clientWidth;
      const thumbW = Math.max(40, Math.round((vpW / treeW) * trackW));
      const maxLeft = trackW - thumbW;
      const scrollRange = treeW - vpW;
      const scrolled = viewStart - (minX - 20);
      const ratio = Math.max(0, Math.min(1, scrolled / scrollRange));
      thumb.style.width = thumbW + 'px';
      thumb.style.left = Math.round(ratio * maxLeft) + 'px';
    };
  },
};

/* ============================================================
   亲缘关系树 缩放/平移 模块
   为关系树 HTML 结构添加鼠标滚轮缩放 + 拖拽平移
   ============================================================ */
const RelZoom = {
  container: null,
  tree: null,
  zoom: 1,
  panX: 0,
  panY: 0,
  dragging: false,
  startX: 0,
  startY: 0,
  startPanX: 0,
  startPanY: 0,
  minZoom: 0.2,
  maxZoom: 3,
  _wheelHandler: null,
  _mmHandler: null,
  _muHandler: null,

  init(containerId) {
    this.container = document.getElementById(containerId);
    if (!this.container) return;
    this.tree = this.container.querySelector('.rel-zoom-tree');
    if (!this.tree) return;
    this.zoom = 1; this.panX = 0; this.panY = 0;
    this._apply();

    // 移除旧的监听器（若重复初始化）
    if (this._wheelHandler) this.container.removeEventListener('wheel', this._wheelHandler);
    if (this._mmHandler) window.removeEventListener('mousemove', this._mmHandler);
    if (this._muHandler) window.removeEventListener('mouseup', this._muHandler);

    this._wheelHandler = (e) => {
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.1 : 0.9;
      const newZoom = Math.max(this.minZoom, Math.min(this.maxZoom, this.zoom * factor));
      const rect = this.container.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      this.panX = mx - (mx - this.panX) * (newZoom / this.zoom);
      this.panY = my - (my - this.panY) * (newZoom / this.zoom);
      this.zoom = newZoom;
      this._apply();
    };

    this._mmHandler = (e) => {
      if (!this.dragging) return;
      this.panX = this.startPanX + (e.clientX - this.startX);
      this.panY = this.startPanY + (e.clientY - this.startY);
      this._apply();
    };

    this._muHandler = () => { this.dragging = false; if (this.container) this.container.style.cursor = 'grab'; };

    this.container.addEventListener('wheel', this._wheelHandler, { passive: false });
    window.addEventListener('mousemove', this._mmHandler);
    window.addEventListener('mouseup', this._muHandler);

    this.container.addEventListener('mousedown', (e) => {
      this.dragging = true;
      this.startX = e.clientX; this.startY = e.clientY;
      this.startPanX = this.panX; this.startPanY = this.panY;
      this.container.style.cursor = 'grabbing';
    });
  },

  _apply() {
    if (this.tree) this.tree.style.transform = `translate(${this.panX}px, ${this.panY}px) scale(${this.zoom})`;
  },

  zoomIn()  { this.zoom = Math.min(this.maxZoom, this.zoom * 1.2); this._apply(); },
  zoomOut() { this.zoom = Math.max(this.minZoom, this.zoom / 1.2); this._apply(); },
  fit()     { this.zoom = 1; this.panX = 0; this.panY = 0; this._apply(); },

  addScrollbar() {
    if (!this.container) return;
    // 防止重复添加
    if (this.container.querySelector('.rel-hscroll')) return;
    const track = document.createElement('div');
    track.className = 'rel-hscroll';
    track.innerHTML = '<div class="rel-hscroll-thumb"></div>';
    this.container.appendChild(track);
    const thumb = track.querySelector('.rel-hscroll-thumb');

    let dragging = false, startX = 0, startLeft = 0;
    thumb.onmousedown = (e) => {
      e.preventDefault();
      dragging = true;
      startX = e.clientX;
      startLeft = thumb.offsetLeft;
      document.body.style.userSelect = 'none';
    };
    const onMove = (e) => {
      if (!dragging || !this.tree) return;
      const maxLeft = track.clientWidth - thumb.offsetWidth;
      if (maxLeft <= 0) return;
      const newLeft = Math.max(0, Math.min(maxLeft, startLeft + (e.clientX - startX)));
      const treeW = this.tree.scrollWidth;
      const containerW = this.container.clientWidth;
      const scale = this.zoom;
      const scaledW = treeW * scale;
      if (scaledW <= containerW) return;
      const maxPan = scaledW - containerW;
      const ratio = newLeft / maxLeft;
      this.panX = -ratio * maxPan;
      this._apply();
      thumb.style.left = newLeft + 'px';
    };
    const onUp = () => { dragging = false; document.body.style.userSelect = ''; };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);

    track.onclick = (e) => {
      if (e.target === thumb || !this.tree) return;
      const rect = track.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const thumbW = thumb.offsetWidth;
      const maxLeft = track.clientWidth - thumbW;
      if (maxLeft <= 0) return;
      const newLeft = Math.max(0, Math.min(maxLeft, clickX - thumbW / 2));
      const treeW = this.tree.scrollWidth;
      const containerW = this.container.clientWidth;
      const scaledW = treeW * this.zoom;
      if (scaledW <= containerW) return;
      const maxPan = scaledW - containerW;
      this.panX = -(newLeft / maxLeft) * maxPan;
      this._apply();
      thumb.style.left = newLeft + 'px';
    };

    // 每次 _apply 时同步滑轨位置
    const origApply = this._apply.bind(this);
    this._apply = () => {
      origApply();
      if (!this.tree) return;
      const treeW = this.tree.scrollWidth;
      const containerW = this.container.clientWidth;
      const scale = this.zoom;
      const scaledW = treeW * scale;
      if (scaledW <= containerW) { track.style.display = 'none'; return; }
      track.style.display = '';
      const thumbW = Math.max(40, Math.round((containerW / scaledW) * track.clientWidth));
      thumb.style.width = thumbW + 'px';
      const maxLeft = track.clientWidth - thumbW;
      const maxPan = scaledW - containerW;
      const ratio = Math.max(0, Math.min(1, -this.panX / maxPan));
      thumb.style.left = Math.round(ratio * maxLeft) + 'px';
    };
  },

  destroy() {
    if (this._wheelHandler && this.container) this.container.removeEventListener('wheel', this._wheelHandler);
    if (this._mmHandler) window.removeEventListener('mousemove', this._mmHandler);
    if (this._muHandler) window.removeEventListener('mouseup', this._muHandler);
    this.container = null; this.tree = null;
  },
};

/* ============================================================
   族谱管理系统 - 主应用逻辑
   SPA 路由 / 视图切换 / 数据加载 / 事件处理
   ============================================================ */

const App = {
  // ---- 状态 ----
  currentGenealogyId: null,
  currentGenealogyName: '',
  currentRole: 'view',
  currentPage: 'dashboard',
  memberPage: 1,
  memberPageSize: 20,
  memberTotal: 0,
  memberSearching: false,
  memberKeyword: '',
  _loading: false,

  // ---- 加载状态 ----
  _setLoading(el, loading) {
    if (typeof el === 'string') el = document.getElementById(el);
    if (!el) return;
    if (loading) {
      // 保存原始父容器引用以便恢复
      if (!el.dataset) return;
      el.dataset.prevHtml = el.innerHTML;
      el.style.opacity = '0.5';
      el.style.pointerEvents = 'none';
    } else {
      el.style.opacity = '';
      el.style.pointerEvents = '';
    }
  },

  // 按钮加载状态
  _btnLoading(btn, loading) {
    if (typeof btn === 'string') btn = document.getElementById(btn);
    if (!btn) return;
    btn.disabled = loading;
    if (loading) {
      btn.dataset.prevText = btn.textContent;
      btn.innerHTML = '<span class="spinner" style="width:14px;height:14px;border-width:2px;vertical-align:middle;margin-right:4px"></span> 处理中...';
    } else if (btn.dataset.prevText) {
      btn.innerHTML = btn.dataset.prevText;
      delete btn.dataset.prevText;
    }
  },

  // ---- 前端全状态重置（解决多用户切换时数据残留问题） ----
  _resetState() {
    // 清空所有 App 状态
    this.currentGenealogyId = null;
    this.currentGenealogyName = '';
    this.currentRole = 'view';
    this.currentPage = 'dashboard';
    this.memberPage = 1;
    this.memberTotal = 0;
    this.memberSearching = false;
    this.memberKeyword = '';
    this._loading = false;

    // 清空侧边栏信息
    document.getElementById('sidebar-genealogy-name').textContent = '';
    document.getElementById('sidebar-genealogy-role').textContent = '';

    // 清空所有查询结果区域（不留历史数据的可见残留）
    const resultAreas = [
      'anc-result', 'desc-result', 'rel-result',
      'stats-content', 'stats-cards', 'generation-dist',
    ];
    resultAreas.forEach(id => {
      const el = document.getElementById(id);
      if (el) el.innerHTML = '';
    });
    document.querySelectorAll('.data-table tbody, #genealogy-grid').forEach(el => el.innerHTML = '');

    // 清空 TreeRenderer 状态
    if (typeof TreeRenderer !== 'undefined') {
      TreeRenderer.nodes = [];
      TreeRenderer.nodeMap = {};
      TreeRenderer.roots = [];
      if (TreeRenderer.canvas && TreeRenderer.ctx) {
        const w = TreeRenderer.canvas.width / (TreeRenderer.dpr || 1);
        const h = TreeRenderer.canvas.height / (TreeRenderer.dpr || 1);
        TreeRenderer.ctx.clearRect(0, 0, w, h);
      }
    }
    // 清理 RelZoom 模块的 window 事件监听器
    if (typeof RelZoom !== 'undefined') RelZoom.destroy();
  },

  // ---- 初始化 ----
  init() {
    this._bindGlobalEvents();
    this._checkSession();
  },

  _checkSession() {
    API.getSession().then(data => {
      if (data.token) {
        this._showMainView(data.user);
      } else {
        this._showLoginView();
      }
    }).catch(() => this._showLoginView());
  },

  // ==================== 视图切换 ====================
  _showLoginView() {
    document.getElementById('view-login').classList.add('active');
    document.getElementById('view-main').classList.remove('active');
    // 预连接：趁用户还没输入时提前建立代理 TCP 连接，让登录更快
    setTimeout(() => { fetch('/api/genealogy', {method: 'GET'}).catch(() => {}); }, 800);
  },

  _showMainView(user) {
    this._resetState();  // 清理上一个用户的残留数据
    document.getElementById('view-login').classList.remove('active');
    document.getElementById('view-main').classList.add('active');
    if (user && user.username) {
      document.getElementById('topbar-user').textContent =
        `${user.username} (ID:${user.user_id})`;
    }
    this._showPage('genealogy-list');
    this._loadGenealogyList();
  },

  _showPage(name) {
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    const page = document.getElementById('page-' + name);
    if (page) page.classList.add('active');
  },

  _showSubPage(name) {
    this.currentPage = name;
    document.querySelectorAll('.sub-page').forEach(p => p.classList.remove('active'));
    const sub = document.getElementById('sub-' + name);
    if (sub) sub.classList.add('active');
    // 更新侧边栏
    document.querySelectorAll('.nav-item').forEach(a => a.classList.remove('active'));
    const nav = document.querySelector(`[data-page="${name}"]`);
    if (nav) nav.classList.add('active');
  },

  _enterGenealogy(gid, name, role) {
    this.currentGenealogyId = gid;
    this.currentGenealogyName = name;
    this.currentRole = role;
    document.getElementById('sidebar-genealogy-name').textContent = name;
    const roleText = { owner: '创建者', edit: '编辑者', view: '查看者' };
    const badge = document.getElementById('sidebar-genealogy-role');
    badge.textContent = roleText[role] || role;
    badge.className = 'badge role-' + role;

    this._showPage('genealogy-workspace');
    this._showSubPage('dashboard');
    this._loadDashboard();
  },

  // ==================== 全局事件 ====================
  _bindGlobalEvents() {
    // 登录表单
    document.getElementById('form-login').addEventListener('submit', e => {
      e.preventDefault();
      this._doLogin();
    });
    document.getElementById('form-register').addEventListener('submit', e => {
      e.preventDefault();
      this._doRegister();
    });

    // 登录/注册 Tab 切换
    document.querySelectorAll('.login-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        const target = tab.dataset.tab;
        document.querySelectorAll('.login-tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.login-form').forEach(f => f.classList.remove('active'));
        tab.classList.add('active');
        document.getElementById('form-' + target).classList.add('active');
      });
    });

    // 退出登录
    document.getElementById('btn-logout').addEventListener('click', () => this._logout());

    // 返回首页
    document.getElementById('btn-home').addEventListener('click', () => {
      this._showPage('genealogy-list');
      this.currentGenealogyId = null;
    });

    // 全局搜索
    document.getElementById('btn-search').addEventListener('click', () => this._globalSearch());
    document.getElementById('global-search').addEventListener('keydown', e => {
      if (e.key === 'Enter') this._globalSearch();
    });

    // 创建族谱
    document.getElementById('btn-create-genealogy').addEventListener('click', () => this._showCreateGenealogyModal());

    // 返回列表
    document.getElementById('btn-back-to-list').addEventListener('click', () => {
      this._showPage('genealogy-list');
      this.currentGenealogyId = null;
      this._loadGenealogyList();
    });

    // 侧边栏导航
    document.querySelectorAll('.nav-item').forEach(item => {
      item.addEventListener('click', e => {
        e.preventDefault();
        const page = item.dataset.page;
        this._showSubPage(page);
        this._loadSubPageData(page);
      });
    });

    // 族谱树工具栏
    document.getElementById('tree-zoom-in').addEventListener('click', () => TreeRenderer.zoomIn());
    document.getElementById('tree-zoom-out').addEventListener('click', () => TreeRenderer.zoomOut());
    document.getElementById('tree-fit').addEventListener('click', () => TreeRenderer.fitView());
    document.getElementById('tree-reset').addEventListener('click', () => TreeRenderer.resetView());
    document.getElementById('tree-load').addEventListener('click', () => this._loadFamilyTree());
    document.getElementById('tree-depth').addEventListener('keydown', e => {
      if (e.key === 'Enter') this._loadFamilyTree();
    });

    // 成员管理
    document.getElementById('btn-member-search').addEventListener('click', () => this._searchMembers());
    document.getElementById('member-search').addEventListener('keydown', e => {
      if (e.key === 'Enter') this._searchMembers();
    });
    document.getElementById('btn-member-clear').addEventListener('click', () => this._clearMemberSearch());
    document.getElementById('btn-add-member').addEventListener('click', () => this._showMemberForm());

    // 祖先查询
    document.getElementById('btn-query-ancestors').addEventListener('click', () => this._queryAncestors());

    // 后代查询
    document.getElementById('btn-query-descendants').addEventListener('click', () => this._queryDescendants());

    // 亲缘关系查询
    document.getElementById('btn-query-relationship').addEventListener('click', () => this._queryRelationship());

    // 统计选项卡
    document.querySelectorAll('.stats-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        document.querySelectorAll('.stats-tab').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        this._loadStat(tab.dataset.stat);
      });
    });

    // 共享管理
    document.getElementById('btn-add-share').addEventListener('click', () => this._showAddShareModal());
  },

  // ==================== 登录/注册 ====================
  async _doLogin() {
    const username = document.getElementById('login-username').value.trim();
    const password = document.getElementById('login-password').value.trim();
    if (username.length < 3) { this._showLoginError('用户名至少3个字符'); return; }
    if (password.length < 6) { this._showLoginError('密码至少6个字符'); return; }
    const btn = document.querySelector('#form-login button[type="submit"]');
    this._btnLoading(btn, true);
    try {
      const data = await API.login(username, password);
      await API.saveSession(data.token, data.user);
      this._showMainView(data.user);
    } catch (e) {
      this._showLoginError(e.message);
    } finally {
      this._btnLoading(btn, false);
    }
  },

  _showLoginError(msg) {
    const el = document.getElementById('login-error');
    el.textContent = msg;
    setTimeout(() => { el.textContent = ''; }, 3000);
  },

  async _doRegister() {
    const username = document.getElementById('reg-username').value.trim();
    const password = document.getElementById('reg-password').value.trim();
    const confirm = document.getElementById('reg-confirm').value.trim();
    const errEl = document.getElementById('reg-error');
    if (username.length < 3) { errEl.textContent = '用户名至少3个字符'; return; }
    if (password.length < 6) { errEl.textContent = '密码至少6个字符'; return; }
    if (password !== confirm) { errEl.textContent = '两次密码不一致'; return; }
    const btn = document.querySelector('#form-register button[type="submit"]');
    this._btnLoading(btn, true);
    try {
      await API.register(username, password);
      this._toast('注册成功！请切换到登录标签页登录', 'success');
      // 切换到登录
      document.querySelector('.login-tab[data-tab="login"]').click();
      document.getElementById('login-username').value = username;
      document.getElementById('login-password').focus();
    } catch (e) {
      errEl.textContent = e.message;
      setTimeout(() => { errEl.textContent = ''; }, 3000);
    } finally {
      this._btnLoading(btn, false);
    }
  },

  async _logout() {
    if (!confirm('确定要退出登录吗？')) return;
    this._resetState();                        // 清空全部前端状态
    document.querySelector('#view-main .nav-item.active')?.classList.remove('active'); // 重置导航高亮
    await API.clearSession();
    this._showLoginView();
  },

  // ==================== Toast ====================
  _toast(msg, type = 'info') {
    const el = document.getElementById('toast');
    el.textContent = msg;
    el.className = 'toast ' + type + ' show';
    setTimeout(() => { el.classList.remove('show'); }, 2500);
  },

  // ==================== 模态对话框 ====================
  _showModal(title, contentHtml, onSave) {
    const overlay = document.getElementById('modal-overlay');
    const box = document.getElementById('modal-box');
    box.innerHTML = `<h3>${title}</h3>${contentHtml}
      <div class="modal-actions">
        <button class="btn" id="modal-cancel">取消</button>
        <button class="btn btn-primary" id="modal-save">确定</button>
      </div>`;
    overlay.style.display = 'flex';
    document.getElementById('modal-cancel').addEventListener('click', () => {
      overlay.style.display = 'none';
    });
    document.getElementById('modal-save').addEventListener('click', async () => {
      try {
        await onSave();
        overlay.style.display = 'none';
      } catch (e) {
        this._toast(e.message, 'error');
      }
    });
    overlay.addEventListener('click', e => {
      if (e.target === overlay) overlay.style.display = 'none';
    });
  },

  _getModalValue(selector) {
    const el = document.querySelector(selector);
    return el ? el.value : '';
  },

  // ==================== 族谱列表 ====================
  async _loadGenealogyList() {
    const grid = document.getElementById('genealogy-grid');
    const empty = document.getElementById('genealogy-empty');
    this._setLoading(grid, true);
    try {
      const data = await API.getGenealogies();
      const list = data.genealogies || [];
      if (list.length === 0) {
        grid.innerHTML = '';
        empty.style.display = 'block';
        return;
      }
      empty.style.display = 'none';
      grid.innerHTML = list.map(g => {
        const roleText = { owner: '创建者', edit: '编辑者', view: '查看者' };
        return `
        <div class="genealogy-card" data-id="${g.genealogy_id}" data-name="${g.name}" data-role="${g.role}">
          <div class="card-surname">${g.surname || ''}</div>
          <div class="card-name">${g.name || ''}</div>
          <div class="card-meta">
            <div>创始人：${g.founder_name || '未知'}</div>
            <div>创建时间：${(g.create_time || '').substring(0, 10)}</div>
            <div>${g.description || ''}</div>
          </div>
          <span class="card-role role-${g.role}">${roleText[g.role] || g.role}</span>
        </div>`;
      }).join('');

      // 点击卡片进入
      grid.querySelectorAll('.genealogy-card').forEach(card => {
        card.addEventListener('click', () => {
          const gid = parseInt(card.dataset.id);
          const name = card.dataset.name;
          const role = card.dataset.role;
          this._enterGenealogy(gid, name, role);
        });
      });
    } catch (e) {
      grid.innerHTML = '';
      this._toast('获取族谱列表失败：' + e.message, 'error');
    } finally {
      this._setLoading(grid, false);
    }
  },

  _showCreateGenealogyModal() {
    const html = `
      <div class="form-group"><label>族谱名称 *</label><input id="modal-name" placeholder="例：张氏族谱"></div>
      <div class="form-group"><label>姓氏 *</label><input id="modal-surname" placeholder="例：张"></div>
      <div class="form-group"><label>始祖姓名</label><input id="modal-founder" placeholder="选填"></div>
      <div class="form-group"><label>简介</label><textarea id="modal-desc" placeholder="选填"></textarea></div>`;
    this._showModal('创建族谱', html, async () => {
      const name = document.getElementById('modal-name').value.trim();
      const surname = document.getElementById('modal-surname').value.trim();
      if (!name) throw new Error('族谱名称不能为空');
      if (!surname) throw new Error('姓氏不能为空');
      await API.createGenealogy({
        name,
        surname,
        founder_name: document.getElementById('modal-founder').value.trim(),
        description: document.getElementById('modal-desc').value.trim(),
      });
      this._toast('族谱创建成功！', 'success');
      this._loadGenealogyList();
    });
  },

  // ==================== 全局搜索 ====================
  async _globalSearch() {
    const keyword = document.getElementById('global-search').value.trim();
    if (!keyword) return;
    try {
      const data = await API.searchMembers(keyword, 1, 20);
      const results = data.results || [];
      if (results.length === 0) {
        this._toast('未找到匹配的成员', 'info');
        return;
      }
      // 构建结果 HTML
      const html = results.map(m => `
        <tr>
          <td>${m.member_id}</td>
          <td>${m.name}</td>
          <td>${m.gender === 'male' ? '男' : '女'}</td>
          <td>${(m.birth_date || '').substring(0, 10)}</td>
          <td>${m.generation ?? ''}</td>
        </tr>`).join('');

      this._showModal('搜索结果', `
        <div class="table-wrapper">
          <table class="data-table">
            <thead><tr><th>ID</th><th>姓名</th><th>性别</th><th>出生</th><th>辈分</th></tr></thead>
            <tbody>${html}</tbody>
          </table>
        </div>`, () => {});
    } catch (e) {
      this._toast('搜索失败：' + e.message, 'error');
    }
  },

  // ==================== 子页面数据加载 ====================
  _loadSubPageData(page) {
    switch (page) {
      case 'dashboard': this._loadDashboard(); break;
      case 'family-tree': this._loadFamilyTree(); break;
      case 'members': this._loadMembers(); break;
      case 'share': this._loadShares(); break;
      case 'statistics': this._loadStat('longest-lived'); break;
    }
  },

  // ==================== 仪表盘 ====================
  async _loadDashboard() {
    if (!this.currentGenealogyId) return;
    const cards = document.getElementById('stats-cards');
    this._setLoading(cards, true);
    try {
      const data = await API.getGenealogyStats(this.currentGenealogyId);
      const gs = data.gender_stats || {};
      const malePct = gs.male_percentage || 0;
      const femalePct = gs.female_percentage || 0;
      document.getElementById('stats-cards').innerHTML = `
        <div class="stat-card"><div class="stat-value">${data.total_members || 0}</div><div class="stat-label">总成员数</div></div>
        <div class="stat-card"><div class="stat-value" style="color:#5B8DB8">${gs.male || 0}</div><div class="stat-label">男性</div></div>
        <div class="stat-card"><div class="stat-value" style="color:#D4828A">${gs.female || 0}</div><div class="stat-label">女性</div></div>
        <div class="stat-card"><div class="stat-value">${gs.male_percentage || 0}%</div><div class="stat-label">男女比例</div></div>`;

      // 饼图
      const canvas = document.getElementById('chart-gender');
      const ctx = canvas.getContext('2d');
      const cw = canvas.parentElement.clientWidth - 40;
      canvas.width = cw;
      canvas.height = 200;
      const cx = 100, cy = 100, r = 80;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      if (malePct + femalePct > 0) {
        const maleAngle = (malePct / 100) * Math.PI * 2;
        ctx.beginPath(); ctx.moveTo(cx, cy);
        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + maleAngle);
        ctx.fillStyle = '#5B8DB8'; ctx.fill();
        ctx.beginPath(); ctx.moveTo(cx, cy);
        ctx.arc(cx, cy, r, -Math.PI / 2 + maleAngle, -Math.PI / 2 + Math.PI * 2);
        ctx.fillStyle = '#D4828A'; ctx.fill();
      }
      // 图例
      ctx.font = '12px "PingFang SC","Microsoft YaHei",sans-serif';
      ctx.fillStyle = '#5B8DB8'; ctx.fillRect(200, 70, 12, 12);
      ctx.fillStyle = '#333'; ctx.fillText(`男性 ${malePct}%`, 216, 81);
      ctx.fillStyle = '#D4828A'; ctx.fillRect(200, 95, 12, 12);
      ctx.fillText(`女性 ${femalePct}%`, 216, 106);

      // 辈分分布
      const genStats = data.generation_stats || [];
      let genHtml = '';
      if (genStats.length > 0) {
        genHtml = genStats.map(g =>
          `<div class="gen-item"><span>第${g.generation}代</span><span>${g.count}人（男${g.male || 0} 女${g.female || 0}）</span></div>`
        ).join('');
      } else {
        genHtml = '<div class="gen-item" style="justify-content:center;color:#999">暂无辈分分布数据</div>';
      }
      document.getElementById('generation-dist').innerHTML = genHtml;
    } catch (e) {
      this._toast('获取统计数据失败：' + e.message, 'error');
    } finally {
      this._setLoading(cards, false);
    }
  },

  // ==================== 族谱树 ====================
  async _loadFamilyTree() {
    if (!this.currentGenealogyId) return;
    const container = document.querySelector('.tree-container');
    const depth = parseInt(document.getElementById('tree-depth')?.value) || 5;
    try {
      // 第一步：获取少量成员，找到辈分最小的男性作为先祖
      const membersData = await API.getMembers(this.currentGenealogyId, 1, 200);
      const members = membersData.members || [];
      if (members.length === 0) {
        container.innerHTML = '<p style="text-align:center;color:#999;padding:60px">暂无成员数据，请先添加成员</p>';
        return;
      }
      // 找辈分最小的男性（generation 最小），找不到男性则取任意最小辈分成员
      let ancestor = null;
      let minGen = Infinity;
      members.forEach(m => {
        const gen = m.generation ?? 1;
        if (gen < minGen) {
          minGen = gen;
          ancestor = m;
        } else if (gen === minGen && ancestor && m.gender === 'male' && ancestor.gender !== 'male') {
          ancestor = m;
        }
      });

      // 第二步：以先祖为根，按指定深度加载族谱树
      const treeData = await API.getFamilyTree(ancestor.member_id, depth);

      // 拼接所有成员：兼容新旧两种响应格式
      // 新格式: { target_member, ancestors[], descendants[] }
      // 旧格式: { members[] } 或 { descendants[] }
      const seenIds = new Set();
      const allMembers = [];

      // 添加祖先路径（新格式）
      (treeData.ancestors || []).forEach(a => {
        if (a.member_id && !seenIds.has(a.member_id)) {
          allMembers.push(a);
          seenIds.add(a.member_id);
        }
      });

      // 添加目标成员（新格式）
      const target = treeData.target_member;
      if (target && target.member_id && !seenIds.has(target.member_id)) {
        allMembers.push(target);
        seenIds.add(target.member_id);
      }

      // 添加后代树
      const descendants = treeData.descendants || treeData.members || [];
      descendants.forEach(d => {
        if (d.member_id && !seenIds.has(d.member_id)) {
          allMembers.push(d);
          seenIds.add(d.member_id);
        }
      });

      // 兜底：如果新格式字段都没有，用祖先对象本身
      if (allMembers.length === 0 && ancestor.member_id) {
        allMembers.push(ancestor);
      }

      if (allMembers.length === 0) {
        container.innerHTML = '<p style="text-align:center;color:#999;padding:60px">暂无族谱树数据</p>';
        return;
      }

      // 复用已有 canvas，不重复创建（避免事件监听器累积）
      if (!container.querySelector('#tree-canvas')) {
        container.innerHTML = '<canvas id="tree-canvas"></canvas>';
      }
      // 等一帧确保 canvas 布局完成后再初始化和渲染
      requestAnimationFrame(() => {
        TreeRenderer.init('tree-canvas', (member) => {
          this._showMemberDetailModal(member.member_id);
        });
        TreeRenderer.loadMembers(allMembers);

        // 绑定横向滑轨
        HScrollbar.attach(TreeRenderer, '.tree-container');
      });
    } catch (e) {
      container.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:60px">加载族谱树失败：${e.message}</p>`;
    }
  },

  // ==================== 成员管理 ====================
  async _loadMembers() {
    if (!this.currentGenealogyId) return;
    const tbody = document.querySelector('#member-table tbody');
    this._setLoading(tbody, true);
    try {
      let data;
      if (this.memberSearching && this.memberKeyword) {
        data = await API.searchMembers(this.memberKeyword, this.memberPage, this.memberPageSize);
        data.members = data.results || [];
      } else {
        data = await API.getMembers(this.currentGenealogyId, this.memberPage, this.memberPageSize);
      }
      this.memberTotal = data.total || 0;
      this._renderMemberTable(data.members || []);
      this._renderMemberPagination();
    } catch (e) {
      this._toast('获取成员列表失败：' + e.message, 'error');
    } finally {
      this._setLoading(tbody, false);
    }
  },

  _renderMemberTable(members) {
    const tbody = document.querySelector('#member-table tbody');
    tbody.innerHTML = members.map(m => {
      const birth = (m.birth_date || '').substring(0, 10);
      const death = (m.death_date || '').substring(0, 10);
      const age = this._calcAge(m.birth_date, m.death_date);
      return `<tr>
        <td>${m.member_id}</td>
        <td><strong>${m.name || ''}</strong></td>
        <td style="color:${m.gender === 'male' ? '#5B8DB8' : '#D4828A'}">${m.gender === 'male' ? '男' : '女'}</td>
        <td>${birth}</td><td>${death}</td><td>${age !== null ? age : ''}</td>
        <td>${m.generation ?? ''}</td>
        <td>
          <button class="btn btn-sm" data-edit="${m.member_id}">编辑</button>
          <button class="btn btn-sm btn-danger" data-del="${m.member_id}">删除</button>
        </td>
      </tr>`;
    }).join('');

    // 绑定事件
    tbody.querySelectorAll('[data-edit]').forEach(btn => {
      btn.addEventListener('click', () => this._showMemberForm(parseInt(btn.dataset.edit)));
    });
    tbody.querySelectorAll('[data-del]').forEach(btn => {
      btn.addEventListener('click', () => this._deleteMember(parseInt(btn.dataset.del)));
    });
  },

  _renderMemberPagination() {
    const totalPages = Math.max(1, Math.ceil(this.memberTotal / this.memberPageSize));
    const el = document.getElementById('member-pagination');
    el.innerHTML = `
      <button ${this.memberPage <= 1 ? 'disabled' : ''} id="member-prev">上一页</button>
      <span class="page-current">第 ${this.memberPage} / ${totalPages} 页</span>
      <button ${this.memberPage >= totalPages ? 'disabled' : ''} id="member-next">下一页</button>
      <span style="margin-left:12px">共 ${this.memberTotal} 条</span>`;
    document.getElementById('member-prev').addEventListener('click', () => {
      if (this.memberPage > 1) { this.memberPage--; this._loadMembers(); }
    });
    document.getElementById('member-next').addEventListener('click', () => {
      if (this.memberPage < totalPages) { this.memberPage++; this._loadMembers(); }
    });
  },

  _searchMembers() {
    const keyword = document.getElementById('member-search').value.trim();
    if (!keyword) { this._clearMemberSearch(); return; }
    this.memberSearching = true;
    this.memberKeyword = keyword;
    this.memberPage = 1;
    this._loadMembers();
  },

  _clearMemberSearch() {
    this.memberSearching = false;
    this.memberKeyword = '';
    document.getElementById('member-search').value = '';
    this.memberPage = 1;
    this._loadMembers();
  },

  _showMemberForm(memberId = null) {
    const isEdit = memberId !== null;
    const title = isEdit ? '编辑成员' : '添加成员';
    const html = `
      <div class="form-group"><label>姓名 *</label><input id="mf-name" value="${isEdit ? '' : ''}" placeholder="必填"></div>
      <div class="form-group"><label>性别 *</label><select id="mf-gender"><option value="male">男</option><option value="female">女</option></select></div>
      <div class="form-group"><label>出生日期</label><input type="date" id="mf-birth"></div>
      <div class="form-group"><label>逝世日期</label><input type="date" id="mf-death"></div>
      <div class="form-group"><label>生平简介</label><textarea id="mf-bio" placeholder="选填"></textarea></div>
      <div class="form-group"><label>父亲 ID</label><input type="number" id="mf-father" placeholder="选填"></div>
      <div class="form-group"><label>母亲 ID</label><input type="number" id="mf-mother" placeholder="选填"></div>
      <div class="form-group"><label>配偶 ID</label><input type="number" id="mf-spouse" placeholder="选填"></div>`;

    this._showModal(title, html, async () => {
      const body = {
        name: document.getElementById('mf-name').value.trim(),
        gender: document.getElementById('mf-gender').value,
      };
      if (!body.name) throw new Error('姓名不能为空');
      const birth = document.getElementById('mf-birth').value;
      const death = document.getElementById('mf-death').value;
      const bio = document.getElementById('mf-bio').value.trim();
      const father = parseInt(document.getElementById('mf-father').value) || 0;
      const mother = parseInt(document.getElementById('mf-mother').value) || 0;
      const spouse = parseInt(document.getElementById('mf-spouse').value) || 0;
      if (birth) body.birth_date = birth;
      if (death) body.death_date = death;
      if (bio) body.biography = bio;
      if (father) body.father_id = father;
      if (mother) body.mother_id = mother;
      if (spouse) body.spouse_id = spouse;

      if (isEdit) {
        await API.updateMember(memberId, body);
        this._toast('成员更新成功！', 'success');
      } else {
        await API.createMember(this.currentGenealogyId, body);
        this._toast('成员添加成功！', 'success');
      }
      this._loadMembers();
      // 如果正在显示族谱树，也刷新
      if (this.currentPage === 'family-tree') this._loadFamilyTree();
    });

    // 如果是编辑，预填数据
    if (isEdit) {
      API.getMemberDetail(memberId).then(m => {
        document.getElementById('mf-name').value = m.name || '';
        document.getElementById('mf-gender').value = m.gender || 'male';
        if (m.birth_date) document.getElementById('mf-birth').value = m.birth_date.substring(0, 10);
        if (m.death_date) document.getElementById('mf-death').value = m.death_date.substring(0, 10);
        if (m.biography) document.getElementById('mf-bio').value = m.biography;
        if (m.father_id) document.getElementById('mf-father').value = m.father_id;
        if (m.mother_id) document.getElementById('mf-mother').value = m.mother_id;
        if (m.spouse_id) document.getElementById('mf-spouse').value = m.spouse_id;
      }).catch(() => {});
    }
  },

  async _deleteMember(memberId) {
    if (!confirm(`确定要删除成员 ID=${memberId} 吗？此操作不可撤销。`)) return;
    try {
      await API.deleteMember(memberId);
      this._toast('删除成功！', 'success');
      this._loadMembers();
      if (this.currentPage === 'family-tree') this._loadFamilyTree();
    } catch (e) {
      this._toast('删除失败：' + e.message, 'error');
    }
  },

  async _showMemberDetailModal(memberId) {
    try {
      const m = await API.getMemberDetail(memberId);
      const age = this._calcAge(m.birth_date, m.death_date);
      const html = `
        <div style="line-height:2;font-size:14px">
          <p><strong>ID：</strong>${m.member_id || ''}</p>
          <p><strong>姓名：</strong>${m.name || ''}</p>
          <p><strong>性别：</strong>${m.gender === 'male' ? '男' : '女'}</p>
          <p><strong>出生：</strong>${(m.birth_date || '').substring(0, 10)}</p>
          <p><strong>逝世：</strong>${(m.death_date || '').substring(0, 10)}</p>
          <p><strong>年龄：</strong>${age !== null ? age : ''}</p>
          <p><strong>辈分：</strong>第${m.generation ?? ''}代</p>
          <p><strong>父亲ID：</strong>${m.father_id || '无'}</p>
          <p><strong>母亲ID：</strong>${m.mother_id || '无'}</p>
          <p><strong>配偶ID：</strong>${m.spouse_id || '无'}</p>
          <p><strong>生平：</strong>${m.biography || '无'}</p>
        </div>`;
      this._showModal('成员详情', html, () => {});
    } catch (e) {
      this._toast('获取详情失败：' + e.message, 'error');
    }
  },

  // ==================== 祖先查询 ====================
  async _queryAncestors() {
    const memberId = parseInt(document.getElementById('anc-member-id').value);
    const depth = parseInt(document.getElementById('anc-depth').value) || 10;
    if (!memberId) { this._toast('请输入成员 ID', 'error'); return; }
    const btn = document.getElementById('btn-query-ancestors');
    const el = document.getElementById('anc-result');
    this._btnLoading(btn, true);
    el.innerHTML = '<p style="text-align:center;color:#999;padding:40px"><span class="spinner"></span> 查询中...</p>';
    try {
      const data = await API.getAncestors(memberId, depth);
      const target = data.target_member || {};
      const raw = data.ancestors || [];

      if (raw.length === 0 && !target.member_id) {
        el.innerHTML = '<p style="text-align:center;color:#999;padding:40px">未查询到祖先信息</p>';
        return;
      }

      // 过滤：同辈分存在两人时只保留男性
      const byGen = {};
      raw.forEach(a => {
        const gen = a.generation ?? 0;
        if (!byGen[gen]) byGen[gen] = [];
        byGen[gen].push(a);
      });
      const filtered = [];
      Object.keys(byGen).sort((a, b) => +a - +b).forEach(gen => {
        const group = byGen[gen];
        const male = group.find(a => a.gender === 'male');
        filtered.push(male || group[0]);
      });

      // 从远到近排序（generation 小的先出现）
      filtered.sort((a, b) => (a.generation || 0) - (b.generation || 0));

      // 把目标人物追加到路径末端
      if (target.member_id) {
        filtered.push(target);
      }

      // 计算 S 形布局
      const containerW = el.clientWidth - 32;
      const cardW = 150;
      const cardGap = 16;
      const perRow = Math.max(1, Math.floor((containerW + cardGap) / (cardW + cardGap)));

      // 拆分为行（蛇形：奇数行 RTL，偶数行 LTR）
      const rows = [];
      for (let i = 0; i < filtered.length; i += perRow) {
        rows.push(filtered.slice(i, i + perRow));
      }

      // 渲染每行
      const rowHtml = rows.map((row, ri) => {
        const isRTL = ri % 2 === 1;
        const dir = isRTL ? 'rtl' : 'ltr';
        const items = isRTL ? [...row].reverse() : row;
        return `
          <div class="anc-snake-row anc-snake-${dir}">
            ${items.map((a, i) => {
              const genColor = a.gender === 'female' ? 'var(--color-female)' : 'var(--color-male)';
              const arrow = (i < items.length - 1)
                ? `<div class="anc-snake-arrow">${isRTL ? '←' : '→'}</div>`
                : '';
              // 性别决定父亲/母亲标签
              const relLabel = a.member_id === target.member_id
                ? '本人'
                : (a.gender === 'female' ? '母亲' : '父亲');
              return `
                <div class="anc-snake-card" data-member-id="${a.member_id || ''}" style="border-top:3px solid ${genColor};cursor:pointer">
                  <div class="anc-snake-name">${a.name || ''}</div>
                  <div class="anc-snake-meta">第${a.generation ?? '?'}代 · ${relLabel}</div>
                  <div class="anc-snake-life">${(a.birth_date || '').substring(0, 10)}</div>
                </div>
                ${arrow}`;
            }).join('')}
          </div>
          ${ri < rows.length - 1 ? '<div class="anc-snake-connector">↧</div>' : ''}`;
      }).join('');

      el.innerHTML = `
        <div style="margin-bottom:12px;font-size:13px;color:var(--color-text-light)">
          <strong>查询目标：</strong>${target.name || ''} (ID:${memberId}) · ${filtered.length} 人 · 深度 ${depth}
        </div>
        <div class="anc-snake-container">${rowHtml}</div>`;

      // 绑定祖先卡片点击事件
      el.querySelectorAll('.anc-snake-card[data-member-id]').forEach(card => {
        card.addEventListener('click', () => {
          const mid = parseInt(card.dataset.memberId);
          if (mid) this._showMemberDetailModal(mid);
        });
      });
    } catch (e) {
      el.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:40px">查询失败：${e.message}</p>`;
    } finally {
      this._btnLoading(btn, false);
    }
  },

  // ==================== 后代查询 ====================
  async _queryDescendants() {
    const memberId = parseInt(document.getElementById('desc-member-id').value);
    const depth = parseInt(document.getElementById('desc-depth').value) || 10;
    if (!memberId) { this._toast('请输入成员 ID', 'error'); return; }
    const btn = document.getElementById('btn-query-descendants');
    const el = document.getElementById('desc-result');
    this._btnLoading(btn, true);
    // 不用 _setLoading（它会保存/恢复 innerHTML 从而破坏 canvas）
    el.innerHTML = '<p style="text-align:center;color:#999;padding:40px"><span class="spinner"></span> 查询中...</p>';
    try {
      const data = await API.getDescendants(memberId, depth);
      const target = data.target_member || {};
      const items = data.descendants || [];

      if (items.length === 0 && !target.member_id) {
        el.innerHTML = '<p style="text-align:center;color:#999;padding:40px">未查询到后代信息</p>';
        this._btnLoading(btn, false);
        return;
      }

      el.innerHTML = `
        <div style="margin-bottom:8px;font-size:13px;color:var(--color-text-light);display:flex;align-items:center;gap:8px">
          <strong>根节点：</strong>${target.name || ''} (ID:${memberId}) · 共 ${items.length} 位后代 · 深度 ${depth}
          <span class="flex-spacer"></span>
          <button class="btn btn-sm desc-zoom-in">🔍+</button>
          <button class="btn btn-sm desc-zoom-out">🔍-</button>
          <button class="btn btn-sm desc-fit">适应</button>
        </div>
        <div class="desc-canvas-container">
          <canvas id="desc-canvas"></canvas>
        </div>`;

      // 销毁旧的渲染器实例（移除其 window 事件监听器，防止泄漏和冲突）
      if (this._descRenderer) {
        this._descRenderer.destroy();
      }

      // 每次创建新的渲染器实例（旧 canvas 已被 innerHTML 销毁，需要全新绑定）
      this._descRenderer = Object.create(TreeRenderer);
      this._descRenderer.canvas = null;
      this._descRenderer.ctx = null;
      this._descRenderer.nodes = [];
      this._descRenderer.nodeMap = {};
      this._descRenderer.roots = [];
      this._descRenderer.viewX = 0;
      this._descRenderer.viewY = 0;
      this._descRenderer.zoom = 1.0;
      this._descRenderer.dragging = false;
      this._descRenderer.selectedNode = null;
      this._descRenderer.hoveredNode = null;
      this._descRenderer._eventsBound = false;  // 重置事件绑定标记（原型链上为 true）

      // 等一帧确保 canvas 在 DOM 中完成布局
      requestAnimationFrame(() => {
        try {
          this._descRenderer.init('desc-canvas', (member) => {
            this._showMemberDetailModal(member.member_id);
          });
          // 目标成员 + 所有后代一起传入，让树以目标为根
          const allMembers = target.member_id ? [target, ...items] : items;
          this._descRenderer.loadMembers(allMembers);

          // 绑定缩放按钮
          el.querySelector('.desc-zoom-in')?.addEventListener('click', () => this._descRenderer.zoomIn());
          el.querySelector('.desc-zoom-out')?.addEventListener('click', () => this._descRenderer.zoomOut());
          el.querySelector('.desc-fit')?.addEventListener('click', () => this._descRenderer.fitView());

          // 绑定横向滑轨
          HScrollbar.attach(this._descRenderer, '.desc-canvas-container');
        } catch (err) {
          console.error('后代树渲染失败:', err);
          el.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:40px">渲染后代树失败：${err.message}</p>`;
        }
      });
    } catch (e) {
      el.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:40px">查询失败：${e.message}</p>`;
    } finally {
      this._btnLoading(btn, false);
    }
  },

  // ==================== 亲缘关系查询 ====================
  async _queryRelationship() {
    const id1 = parseInt(document.getElementById('rel-id1').value);
    const id2 = parseInt(document.getElementById('rel-id2').value);
    if (!id1 || !id2) { this._toast('请输入两个成员 ID', 'error'); return; }
    const btn = document.getElementById('btn-query-relationship');
    const el = document.getElementById('rel-result');
    this._btnLoading(btn, true);
    el.innerHTML = '<p style="text-align:center;color:#999;padding:40px"><span class="spinner"></span> 查询中...</p>';
    try {
      const data = await API.getRelationship(id1, id2);
      const typeMap = { blood: '血缘关系', spouse: '配偶关系', family: '家族关系' };

      if (!data.has_relationship) {
        el.innerHTML = '<p style="text-align:center;color:#999;padding:40px">两人暂无亲缘关系</p>';
        this._btnLoading(btn, false);
        return;
      }

      // 使用新格式（后端已返回 path_to_member1 / path_to_member2）
      const path1 = data.path_to_member1 || data.path1 || [];
      const path2 = data.path_to_member2 || data.path2 || [];
      const ancestor = data.common_ancestor || data.common_relative || {};

      // 过滤：同辈只保留男性
      const filterByGen = (path) => {
        const byGen = {};
        path.forEach(n => {
          const gen = n.generation ?? 0;
          if (!byGen[gen]) byGen[gen] = [];
          byGen[gen].push(n);
        });
        const result = [];
        Object.keys(byGen).sort((a, b) => +a - +b).forEach(gen => {
          const group = byGen[gen];
          const male = group.find(n => n.gender === 'male');
          result.push(male || group[0]);
        });
        return result;
      };

      const fp1 = filterByGen(path1);
      const fp2 = filterByGen(path2);

      // 兼容旧格式：如果 path 是从 leaf 到 root 的顺序，需要反转
      const p1Ordered = fp1.length >= 2 && (fp1[0].generation ?? 0) > (fp1[fp1.length - 1].generation ?? 0)
        ? [...fp1].reverse() : fp1;
      const p2Ordered = fp2.length >= 2 && (fp2[0].generation ?? 0) > (fp2[fp2.length - 1].generation ?? 0)
        ? [...fp2].reverse() : fp2;

      // 渲染路径节点
      const renderPath = (path) => {
        if (path.length === 0) return '';
        return path.map((n, i) => {
          const isLast = i === path.length - 1;
          const color = n.gender === 'female' ? 'var(--color-female)' : 'var(--color-male)';
          return `
            <div class="rel-tree-node-wrap">
              <div class="rel-tree-node ${isLast ? 'rel-tree-leaf' : ''}" data-member-id="${n.member_id || ''}" style="border-left:3px solid ${color};cursor:pointer">
                <div class="rel-tree-name">${n.name || '?'}</div>
                <div class="rel-tree-meta">第${n.generation ?? '?'}代${n.relation ? ' · ' + n.relation : ''}</div>
              </div>
              ${i < path.length - 1 ? '<div class="rel-tree-line"></div>' : ''}
            </div>`;
        }).join('');
      };

      el.innerHTML = `
        <div style="margin-bottom:8px;font-size:13px;color:var(--color-text-light);display:flex;align-items:center;gap:8px;flex-wrap:wrap">
          <strong>关系类型：</strong>${typeMap[data.relationship_type] || data.relationship_type}
          <span class="flex-spacer"></span>
          <button class="btn btn-sm rel-zoom-in">🔍+</button>
          <button class="btn btn-sm rel-zoom-out">🔍-</button>
          <button class="btn btn-sm rel-fit">适应</button>
          <span class="tree-hint">🖱️ 滚轮缩放 | 拖拽平移</span>
        </div>
        <div class="rel-canvas-container" id="rel-zoom-wrap">
          <div class="rel-zoom-tree">
            <div class="rel-tree-container">
              <div class="rel-tree-ancestor">
                <div class="rel-tree-node rel-tree-root" data-member-id="${ancestor.member_id || ''}" style="cursor:pointer">
                  <div class="rel-tree-name">${ancestor.name || '公共祖先'}</div>
                  <div class="rel-tree-meta">第${ancestor.generation ?? '?'}代 · 公共祖先</div>
                </div>
              </div>
              <div class="rel-tree-split">
                <div class="rel-tree-split-line"></div>
              </div>
              <div class="rel-tree-branches">
                <div class="rel-tree-branch">
                  <div class="rel-tree-branch-line"></div>
                  ${renderPath(p1Ordered.slice(1))}
                </div>
                <div class="rel-tree-branch">
                  <div class="rel-tree-branch-line"></div>
                  ${renderPath(p2Ordered.slice(1))}
                </div>
              </div>
            </div>
          </div>
        </div>`;

      // 初始化缩放/平移
      RelZoom.init('rel-zoom-wrap');
      RelZoom.addScrollbar();
      el.querySelector('.rel-zoom-in')?.addEventListener('click', () => RelZoom.zoomIn());
      el.querySelector('.rel-zoom-out')?.addEventListener('click', () => RelZoom.zoomOut());
      el.querySelector('.rel-fit')?.addEventListener('click', () => RelZoom.fit());

      // 绑定关系节点点击查看详情
      el.querySelectorAll('.rel-tree-node[data-member-id]').forEach(node => {
        node.addEventListener('click', () => {
          const mid = parseInt(node.dataset.memberId);
          if (mid) this._showMemberDetailModal(mid);
        });
      });
    } catch (e) {
      el.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:40px">查询失败：${e.message}</p>`;
    } finally {
      this._btnLoading(btn, false);
    }
  },

  // ==================== 统计分析 ====================
  async _loadStat(type) {
    if (!this.currentGenealogyId) return;
    const el = document.getElementById('stats-content');
    el.innerHTML = '<p style="text-align:center;color:#999;padding:30px"><span class="spinner"></span> 加载中...</p>';
    try {
      let data, columns, rows;
      switch (type) {
        case 'longest-lived':
          data = await API.getLongestLivedGen(this.currentGenealogyId);
          columns = ['ID', '姓名', '性别', '出生', '逝世', '寿命(岁)'];
          el.innerHTML = `<p style="margin-bottom:12px">第${data.generation}代 | 平均寿命：${data.avg_lifespan?.toFixed(1)} 岁 | ${data.member_count} 人</p>`;
          rows = (data.members || []).map(m => [
            m.member_id, m.name, m.gender === 'male' ? '男' : '女',
            (m.birth_date || '').substring(0, 10), (m.death_date || '').substring(0, 10), m.lifespan
          ]);
          break;
        case 'unmarried-men':
          data = await API.getUnmarriedMenOver50(this.currentGenealogyId);
          columns = ['ID', '姓名', '出生', '年龄', '配偶ID'];
          el.innerHTML = `<p style="margin-bottom:12px">${data.query_condition || ''} | 共 ${data.total || 0} 人</p>`;
          rows = (data.members || []).map(m => [
            m.member_id, m.name, (m.birth_date || '').substring(0, 10), m.age, m.spouse_id || '无'
          ]);
          break;
        case 'born-before-avg':
          data = await API.getBornBeforeAvg(this.currentGenealogyId);
          columns = ['ID', '姓名', '出生', '出生年份', '辈分', '平均出生年份', '差值(年)'];
          el.innerHTML = `<p style="margin-bottom:12px">${data.query_condition || ''} | 共 ${data.total || 0} 人</p>`;
          rows = (data.members || []).map(m => [
            m.member_id, m.name, (m.birth_date || '').substring(0, 10),
            m.birth_year, m.generation, m.avg_birth_year?.toFixed(1), m.years_earlier
          ]);
          break;
      }
      el.innerHTML += `<div class="table-wrapper"><table class="data-table">
        <thead><tr>${columns.map(c => `<th>${c}</th>`).join('')}</tr></thead>
        <tbody>${rows.map(r => `<tr>${r.map(v => `<td>${v !== null && v !== undefined ? v : ''}</td>`).join('')}</tr>`).join('')}</tbody>
      </table></div>`;
    } catch (e) {
      el.innerHTML = `<p style="text-align:center;color:#C44D4D;padding:30px">加载失败：${e.message}</p>`;
    }
  },

  // ==================== 共享管理 ====================
  async _loadShares() {
    if (!this.currentGenealogyId) return;
    const isOwner = this.currentRole === 'owner';
    document.getElementById('btn-add-share').style.display = isOwner ? '' : 'none';
    document.getElementById('share-role-hint').textContent = isOwner ? '' : '仅创建者可管理共享';
    try {
      const data = await API.getShares(this.currentGenealogyId);
      const shares = data.shares || [];
      const tbody = document.querySelector('#share-table tbody');
      tbody.innerHTML = shares.map(s => `
        <tr>
          <td>${s.share_id}</td><td>${s.user_id}</td><td>${s.username || ''}</td>
          <td>${s.permission === 'edit' ? '编辑' : '查看'}</td>
          <td>${(s.created_at || '').substring(0, 10)}</td>
          <td>
            ${isOwner ? `
              <button class="btn btn-sm" data-chg="${s.user_id}" data-perm="${s.permission}">改权限</button>
              <button class="btn btn-sm btn-danger" data-share-del="${s.user_id}">删除</button>
            ` : '<span style="color:#999">—</span>'}
          </td>
        </tr>`).join('');

      if (isOwner) {
        tbody.querySelectorAll('[data-chg]').forEach(btn => {
          btn.addEventListener('click', () => {
            const uid = parseInt(btn.dataset.chg);
            const cur = btn.dataset.perm;
            const newPerm = cur === 'edit' ? 'view' : 'edit';
            if (!confirm(`将用户 ${uid} 的权限改为「${newPerm === 'edit' ? '编辑' : '查看'}」？`)) return;
            API.updateShare(this.currentGenealogyId, uid, newPerm)
              .then(() => { this._toast('权限更新成功', 'success'); this._loadShares(); })
              .catch(e => this._toast('更新失败：' + e.message, 'error'));
          });
        });
        tbody.querySelectorAll('[data-share-del]').forEach(btn => {
          btn.addEventListener('click', () => {
            const uid = parseInt(btn.dataset.shareDel);
            if (!confirm(`确定删除用户 ${uid} 的共享权限？`)) return;
            API.deleteShare(this.currentGenealogyId, uid)
              .then(() => { this._toast('删除成功', 'success'); this._loadShares(); })
              .catch(e => this._toast('删除失败：' + e.message, 'error'));
          });
        });
      }
    } catch (e) {
      this._toast('获取共享列表失败：' + e.message, 'error');
    }
  },

  _showAddShareModal() {
    const html = `
      <div class="form-group"><label>用户 ID *</label><input type="number" id="share-uid" placeholder="输入要共享的用户 ID"></div>
      <div class="form-group"><label>权限</label><select id="share-perm"><option value="edit">编辑</option><option value="view">查看</option></select></div>`;
    this._showModal('添加共享用户', html, async () => {
      const uid = parseInt(document.getElementById('share-uid').value);
      if (!uid) throw new Error('请输入用户 ID');
      const perm = document.getElementById('share-perm').value;
      await API.addShare(this.currentGenealogyId, uid, perm);
      this._toast('添加共享成功！', 'success');
      this._loadShares();
    });
  },

  // ==================== 工具方法 ====================
  _calcAge(birth, death) {
    if (!birth) return null;
    try {
      const b = new Date(birth.substring(0, 10));
      const e = death ? new Date(death.substring(0, 10)) : new Date();
      let age = e.getFullYear() - b.getFullYear();
      if (e.getMonth() < b.getMonth() || (e.getMonth() === b.getMonth() && e.getDate() < b.getDate())) {
        age--;
      }
      return Math.max(0, age);
    } catch (e) {
      return null;
    }
  },
};

// ---- 启动 ----
document.addEventListener('DOMContentLoaded', () => App.init());