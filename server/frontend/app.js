// 零框架依赖，纯 fetch + DOM 操作
'use strict';

// ═══════════════════════════════════════════
// 状态管理
// ═══════════════════════════════════════════

const State = {
  token: localStorage.getItem('jwt_token') || '',
  user: (function(){ try { var u = JSON.parse(localStorage.getItem('jwt_user') || 'null'); if (!u || !u.id) { localStorage.removeItem('jwt_token'); localStorage.removeItem('jwt_user'); return null; } return u; } catch(e){ return null; } })(),
  stations: [],
  currentTab: 'direct',
  queryResult: null,
  selectedTrain: null,
  currentStatusFilter: '',
  _trainItems: {},
  stationResult: [],      // 车站查询原始结果
  stationIsCity: false,   // 是否为城市级查询
  stationCityStations: [], // 城市查询时的子站列表 [{id, name}]
  stationFilterSt: {},    // 车站筛选勾选状态 {stationName: true}
  _stationUnchecked: {},  // 未勾选的车站（重建 UI 时保留）
  _allTrains: [],          // 列车列表
  _trainStatusFilter: '',  // 列车状态筛选
  _allApprovals: [],       // 审批列表
  _approvalFilter: '',     // 审批状态筛选（默认待审批）
  _mySubmissions: [],      // 我的提交列表
  _mySubFilter: '',        // 我的提交状态筛选
  _neighborIndex: {},      // 车站-线路-邻居索引缓存
  _routePath: [],          // 当前正在构建的运行路径 [{station_id, station_name, line_id, line_name, arrival, departure, is_stop, distance_km, max_speed}]
  _pendingNeighbor: null,  // 待确认的邻居选择
  _pendingDeleteTrain: null,  // 待删除的列车 ID
  _editingTrainId: null,    // 正在编辑的列车 ID（null=新增模式）
  _lineNameIndex: null,     // line_id → line_name 反向索引（懒构建）
};

// ═══════════════════════════════════════════
// Auth 模块
// ═══════════════════════════════════════════

const Auth = {
  async login(e) {
    e.preventDefault();
    var nameEl = U.$('login-username'), passEl = U.$('login-password');
    var errEl = U.$('login-error');
    if (!nameEl || !passEl)
      return;
    var name = nameEl.value.trim(), pass = passEl.value.trim();
    if (errEl)
      errEl.textContent = '';

    var res = await API.post('/api/auth/login', { username: name, password: pass });
    if (!res.ok) {
      if (errEl)
        errEl.textContent = (res.data && res.data.error) || '用户名或密码错误';
      return;
    }
    State.token = res.data.token;
    State.user = { id: res.data.user_id, username: res.data.username, role: res.data.role };
    localStorage.setItem('jwt_token', State.token);
    localStorage.setItem('jwt_user', JSON.stringify({id: res.data.user_id || State.user.id, username: res.data.username, role: res.data.role}));

    U.showNav();
    try { await U.loadStations(); } catch (_) {}
    UI.showPage('query');
  },

  async register(e) {
    e.preventDefault();
    var nameEl = U.$('register-username'), passEl = U.$('register-password');
    var errEl = U.$('register-error');
    if (!nameEl || !passEl)
      return;
    var name = nameEl.value.trim(), pass = passEl.value.trim();
    if (errEl)
      errEl.textContent = '';
    if (name.length < 3) {
      if (errEl)
        errEl.textContent = '用户名至少3位';
      return;
    }
    if (pass.length < 6) {
      if (errEl)
        errEl.textContent = '密码至少6位';
      return;
    }

    var res = await API.post('/api/auth/register', { username: name, password: pass });
    if (!res.ok) {
      if (errEl)
        errEl.textContent = (res.data && res.data.error) || '注册失败';
      return;
    }
    State.token = res.data.token;
    State.user = { id: res.data.user_id, username: res.data.username, role: res.data.role };
    localStorage.setItem('jwt_token', State.token);
    localStorage.setItem('jwt_user', JSON.stringify({id: res.data.user_id, username: res.data.username, role: res.data.role}));

    U.showNav();
    try { await U.loadStations(); } catch (_) {}
    UI.showPage('query');
    U.toast('注册成功，欢迎 ' + name, 'success');
  },

  logout() {
    State.token = '';
    State.user = null;
    localStorage.removeItem('jwt_token');
    localStorage.removeItem('jwt_user');
    U.hideNav();
    UI.showPage('login');
    U.toast('已退出登录', 'success');
  },
};

// ═══════════════════════════════════════════
// API 模块
// ═══════════════════════════════════════════

const API = {
  get: function(url) { return this._fetch('GET', url); },
  post: function(url, body) { return this._fetch('POST', url, body); },
  put: function(url, body) { return this._fetch('PUT', url, body); },
  del: function(url) { return this._fetch('DELETE', url); },
  _fetch: async function(method, url, body) {
    var headers = { 'Content-Type': 'application/json' };
    if (State.token)
      headers['Authorization'] = 'Bearer ' + State.token;
    try {
      var opts = { method: method, headers: headers };
      if (body)
        opts.body = JSON.stringify(body);
      var resp = await fetch(url, opts);
      var data = await resp.json();
      if (resp.status === 401) {
        Auth.logout();
        return { ok: false, data: data, status: 401 };
      }
      return { ok: data.ok === true, data: data, status: resp.status };
    } catch (err) {
      return { ok: false, data: { error: '网络错误: ' + err.message }, status: 0 };
    }
  },
};

// ═══════════════════════════════════════════
// Utils 模块
// ═══════════════════════════════════════════

const U = {
  $: function(id) { return document.getElementById(id); },
  toast: function(msg, type) {
    var el = U.$('toast');
    if (!el)
      return;
    el.textContent = msg;
    el.className = 'toast show ' + (type || 'success');
    setTimeout(function() { el.className = 'toast'; }, 3000);
  },
  fmtTime: function(hhmm) {
    if (hhmm < 0)
      return '--:--';
    var h = String(Math.floor(hhmm / 100));
    if (h.length < 2)
      h = '0' + h;
    var m = String(hhmm % 100);
    if (m.length < 2)
      m = '0' + m;
    return h + ':' + m;
  },
  fmtDuration: function(min) {
    var h = Math.floor(min / 60), m = min % 60;
    return h > 0 ? h + 'h' + m + 'm' : m + 'm';
  },
  seatLabel: function(type) {
    var m = { BUSINESS: '商务座', FIRST: '一等座', SECOND: '二等座', HARD_SLEEPER: '硬卧', HARD_SEAT: '硬座', NO_SEAT: '无座' };
    return m[type] || type;
  },
  esc: function(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); },
  /** 安全获取表单元素的值，元素不存在时返回 fallback */
  val: function(id, fallback) { var el = U.$(id); return el ? el.value : (fallback || ''); },
  /** 在数组中按 id 查找元素 */
  findById: function(arr, id) { for (var i = 0; i < arr.length; i++) if (arr[i].id === id) return arr[i]; return null; },
  /** 计算两站之间的行程耗时（分钟），将 HHMM 整数差值转为分钟 */
  legDuration: function(dep, arr) { var dh = Math.floor(dep / 100) - Math.floor(arr / 100), dm = (dep % 100) - (arr % 100); return dh * 60 + dm; },
  showNav: function() {
    var u = U.$('nav-user'), b = U.$('btn-logout');
    if (u)
      u.textContent = State.user ? State.user.username + ' (' + State.user.role + ')' : '';
    if (b)
      b.style.display = State.user ? '' : 'none';
    // 按角色显示职工菜单
    var role = State.user ? State.user.role : '';
    var isStaff = (role === 'STAFF');                          // 列车管理
    var isApprover = (role === 'APPROVER');                    // 审批中心
    var isSysAdmin = (role === 'SYS_ADMIN');                   // 用户管理/审计/配置
    var isInfraAdmin = (role === 'INFRA_ADMIN');               // 站点/线路管理
    var items = document.querySelectorAll('.staff-only');
    for (var i = 0; i < items.length; i++) {
      var page = items[i].getAttribute('data-page');
      if (page === 'trains' || page === 'my-submissions')
        items[i].style.display = isStaff ? '' : 'none';
      else if (page === 'approvals')
        items[i].style.display = isApprover ? '' : 'none';
      else if (page === 'users' || page === 'audit' || page === 'config')
        items[i].style.display = isSysAdmin ? '' : 'none';
      else if (page === 'stations' || page === 'lines' || page === 'network' || page === 'infra-divider')
        items[i].style.display = isInfraAdmin ? '' : 'none';
      else
        items[i].style.display = (isStaff || isApprover || isSysAdmin || isInfraAdmin) ? '' : 'none';
    }
    var infraItems = document.querySelectorAll('.infra-only');
    for (var j = 0; j < infraItems.length; j++)
      infraItems[j].style.display = isInfraAdmin ? '' : 'none';
    var divider = document.querySelector('.sidebar-divider');
    if (divider)
      divider.style.display = (isStaff || isApprover || isSysAdmin || isInfraAdmin) ? '' : 'none';
  },
  hideNav: function() {
    var u = U.$('nav-user'), b = U.$('btn-logout');
    if (u)
      u.textContent = '';
    if (b)
      b.style.display = 'none';
  },

};

// ═══════════════════════════════════════════
// UI 模块
// ═══════════════════════════════════════════

const UI = {
  // ── 共享常量 ──

  /** 席位类型映射（key → 前端展示标签 + 后端价格字段） */
  SEAT_MAP: [
    {key: 'business_seats', label: '商务座', priceKey: 'BUSINESS'},
    {key: 'first_seats',    label: '一等座', priceKey: 'FIRST'},
    {key: 'second_seats',   label: '二等座', priceKey: 'SECOND'},
    {key: 'hard_sleeper',   label: '硬卧',   priceKey: 'HARD_SLEEPER'},
    {key: 'hard_seat',      label: '硬座',   priceKey: 'HARD_SEAT'},
    {key: 'no_seat',        label: '无座',   priceKey: 'NO_SEAT'}
  ],

  /** 审批类型/状态/样式映射（renderMySubmissions 和 renderApprovals 共用） */
  TYPE_LABEL: {0:'新增列车',1:'调整时刻',2:'新增线路',3:'新增站点',4:'删除列车'},
  STATUS_LABEL: {0:'待审批',1:'已通过',2:'已驳回',3:'已取消'},
  STATUS_CLS: {0:'submitted',1:'approved',2:'rejected',3:'withdrawn'},

  showPage: function(name) {
    // 隐藏所有页面
    var pages = document.querySelectorAll('.page');
    for (var i = 0; i < pages.length; i++) { pages[i].classList.remove('active'); }
    // 显示目标页
    var page = U.$('page-' + name);
    if (page)
      page.classList.add('active');
    // 登录/注册页不显示侧边栏
    var layout = U.$('app-layout');
    if (layout)
      layout.style.display = (name === 'login' || name === 'register') ? 'none' : 'flex';
    // 更新侧边栏高亮
    var items = document.querySelectorAll('.sidebar-item');
    for (var j = 0; j < items.length; j++) {
      items[j].classList.toggle('active', items[j].getAttribute('data-page') === name);
    }
    if (name === 'query')
      UI.populateStationSelects();
    if (name === 'orders')
      UI.loadOrders();
    if (name === 'add-train')
      UI.showAddTrainForm();
    else
      State._editingTrainId = null;
    if (name === 'trains')
      UI.loadTrains();
    if (name === 'my-submissions')
      UI.loadMySubmissions();
    if (name === 'approvals')
      UI.loadApprovals();
    if (name === 'users')
      UI.loadUsers();
    if (name === 'audit')
      UI.loadAudit();
    if (name === 'config')
      UI.loadConfig();
    if (name === 'stations')
      UI.loadStations();
    if (name === 'lines')
      UI.loadLines();
    if (name === 'network') {
      var c = U.$('network-container');
      if (c) {
        c.innerHTML = '';
        c._loaded = false;
      }
      UI.loadNetwork();
    }
  },

  /** 返回上一页（购票→查票） */
  goBack: function() { UI.showPage('query'); },

  navTo: function(name, data) {
    var role = State.user ? State.user.role : '';
    var isStaff = (role === 'STAFF');
    var isApprover = (role === 'APPROVER');
    var isSysAdmin = (role === 'SYS_ADMIN');
    var isInfraAdmin = (role === 'INFRA_ADMIN');
    if ((name === 'trains' || name === 'my-submissions' || name === 'add-train') && !isStaff)
      return;
    if (name === 'approvals' && !isApprover)
      return;
    if (name === 'users' && !isSysAdmin)
      return;
    if (name === 'audit' && !isSysAdmin)
      return;
    if (name === 'config' && !isSysAdmin)
      return;
    if ((name === 'stations' || name === 'lines' || name === 'network') && !isInfraAdmin)
      return;
    if (name === 'order-form' && data)
      State.selectedTrain = data;
    UI.showPage(name);
    if (name === 'order-form')
      UI.renderOrderForm();
  },

  /**
   * 列车最高时速限制（km/h）。
   * 只校验上限：列车运营速度由 min(线路限速, 列车最高设计时速) 决定。
   */
  _speedLimits: {
    G: 350, D: 300, C: 350, Z: 160, T: 140, K: 120, S: 999, OTHER: 120
  },

  _allUsers: [],
  _editingUserId: null,
  _allStations: [],
  _allLines: [],

  _showModal: function(html) {
    var overlay = U.$('detail-overlay');
    var card = overlay.querySelector('.detail-card');
    card.innerHTML = html;
    overlay.classList.add('show');
  },

  closeModal: function() {
    U.$('detail-overlay').classList.remove('show');
  },

  _cfgRates: {},
  _ratePrefixes: ['G','D','C','Z','T','K','*'],
  _rateSeats: ['BUSINESS','FIRST','SECOND','HARD_SLEEPER','HARD_SEAT','NO_SEAT'],
  _ratePrefixLabels: {G:'G 高铁',D:'D 动车',C:'C 城际',Z:'Z 直达',T:'T 特快',K:'K 快速','*':'其他'},

  _auditRecords: [],

  showRegister: function() {
    UI.showPage('register');
  },

  showLogin: function() {
    UI.showPage('login');
  },

};

// ═══════════════════════════════════════════
// 初始化
// ═══════════════════════════════════════════

(function init() {
  try {
    if (State.token && State.user) {
      U.showNav();
      U.loadStations().then(function() { UI.showPage('query'); }).catch(function() { UI.showPage('query'); });
    } else {
      U.hideNav();
      UI.showPage('login');
    }
  } catch (_) {
    try { UI.showPage('login'); } catch (__) {}
  }
})();
