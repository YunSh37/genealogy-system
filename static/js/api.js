/* ============================================================
   API 客户端 - 通过 Flask 代理调用后端接口
   ============================================================ */

const API = {
  async _request(method, path, body = null, params = null) {
    let url = path;
    if (params) {
      const qs = new URLSearchParams(params).toString();
      url += '?' + qs;
    }
    const opts = { method, headers: { 'Content-Type': 'application/json' } };
    if (body) opts.body = JSON.stringify(body);
    const resp = await fetch(url, opts);
    const data = await resp.json();
    if (data.code !== 200 && data.code !== 201) {
      throw new Error(data.message || '请求失败');
    }
    return data.data || data;
  },

  _get(path, params)    { return this._request('GET', path, null, params); },
  _post(path, body)     { return this._request('POST', path, body); },
  _put(path, body)      { return this._request('PUT', path, body); },
  _delete(path)         { return this._request('DELETE', path); },

  // ---- 会话 ----
  saveSession(token, user) { return this._post('/session/save', { token, user }); },
  clearSession()            { return this._post('/session/clear'); },
  getSession()              { return this._get('/session/info'); },

  // ---- 认证 ----
  login(username, password)    { return this._post('/api/auth/login', { username, password }); },
  register(username, password) { return this._post('/api/auth/register', { username, password }); },

  // ---- 族谱 ----
  getGenealogies()          { return this._get('/api/genealogy'); },
  createGenealogy(data)     { return this._post('/api/genealogy', data); },
  getGenealogyStats(gid)    { return this._get(`/api/genealogy/${gid}/stats`); },
  deleteGenealogy(gid)      { return this._delete(`/api/genealogy/${gid}`); },

  // ---- 成员 ----
  getMembers(gid, page = 1, size = 20) {
    return this._get(`/api/genealogy/${gid}/member`, { page, page_size: size });
  },
  getAllMembers(gid) {
    return this._get(`/api/genealogy/${gid}/member`, { page: 1, page_size: 99999 });
  },
  createMember(gid, data)   { return this._post(`/api/genealogy/${gid}/member`, data); },
  getMemberDetail(mid)      { return this._get(`/api/member/${mid}`); },
  updateMember(mid, data)   { return this._put(`/api/member/${mid}`, data); },
  deleteMember(mid)         { return this._delete(`/api/member/${mid}`); },
  searchMembers(keyword, page = 1, size = 20) {
    return this._get('/api/member/search', { keyword, page, page_size: size });
  },

  // ---- 血缘查询 ----
  getAncestors(mid, depth = 10) {
    return this._get(`/api/member/${mid}/ancestors`, { max_depth: depth });
  },
  getDescendants(mid, depth = 10) {
    return this._get(`/api/member/${mid}/descendants`, { max_depth: depth });
  },
  getFamilyTree(mid, depth = 3) {
    return this._get(`/api/member/${mid}/family-tree`, { depth });
  },
  getRelationship(id1, id2) {
    return this._get('/api/member/relationship', { member_id1: id1, member_id2: id2 });
  },

  // ---- 统计 ----
  getLongestLivedGen(gid)     { return this._get(`/api/genealogy/${gid}/stats/longest-lived-generation`); },
  getUnmarriedMenOver50(gid)  { return this._get(`/api/genealogy/${gid}/stats/unmarried-men-over-50`); },
  getBornBeforeAvg(gid)       { return this._get(`/api/genealogy/${gid}/stats/born-before-average`); },

  // ---- 共享 ----
  getShares(gid)              { return this._get(`/api/genealogy/${gid}/share`); },
  addShare(gid, userId, perm) { return this._post(`/api/genealogy/${gid}/share`, { user_id: userId, permission: perm }); },
  updateShare(gid, uid, perm) { return this._put(`/api/genealogy/${gid}/share/${uid}`, { permission: perm }); },
  deleteShare(gid, uid)       { return this._delete(`/api/genealogy/${gid}/share/${uid}`); },
};