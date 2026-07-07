// app.js — 12306 铁路票务系统前端 SPA
// 零框架依赖，纯 fetch + DOM 操作
'use strict';

// ═══════════════════════════════════════════
// 状态管理
// ═══════════════════════════════════════════

const State = {
  token: localStorage.getItem('jwt_token') || '',
  user: (function(){ try { return JSON.parse(localStorage.getItem('jwt_user') || 'null'); } catch(e){ return null; } })(),
  stations: [],
  currentTab: 'direct',
  queryResult: null,
  selectedTrain: null,
  currentStatusFilter: '',
};

// ═══════════════════════════════════════════
// Auth 模块
// ═══════════════════════════════════════════

const Auth = {
  async login(e) {
    e.preventDefault();
    var nameEl = U.$('login-username'), passEl = U.$('login-password');
    var errEl = U.$('login-error');
    if (!nameEl || !passEl) return;
    var name = nameEl.value.trim(), pass = passEl.value.trim();
    if (errEl) errEl.textContent = '';

    var res = await API.post('/api/auth/login', { username: name, password: pass });
    if (!res.ok) {
      if (errEl) errEl.textContent = (res.data && res.data.error) || '用户名或密码错误';
      return;
    }
    State.token = res.data.token;
    State.user = { username: res.data.username, role: res.data.role };
    localStorage.setItem('jwt_token', State.token);
    localStorage.setItem('jwt_user', JSON.stringify(State.user));

    U.showNav();
    try { await U.loadStations(); } catch (_) {}
    UI.showPage('query');
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
  _fetch: async function(method, url, body) {
    var headers = { 'Content-Type': 'application/json' };
    if (State.token) headers['Authorization'] = 'Bearer ' + State.token;
    try {
      var opts = { method: method, headers: headers };
      if (body) opts.body = JSON.stringify(body);
      var resp = await fetch(url, opts);
      var data = await resp.json();
      if (resp.status === 401) { Auth.logout(); return { ok: false, data: data, status: 401 }; }
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
    var el = U.$('toast'); if (!el) return;
    el.textContent = msg; el.className = 'toast show ' + (type || 'success');
    setTimeout(function() { el.className = 'toast'; }, 3000);
  },
  fmtTime: function(hhmm) {
    if (hhmm < 0) return '--:--';
    var h = String(Math.floor(hhmm / 100)); if (h.length < 2) h = '0' + h;
    var m = String(hhmm % 100); if (m.length < 2) m = '0' + m;
    return h + ':' + m;
  },
  fmtDuration: function(min) {
    var h = Math.floor(min / 60), m = min % 60;
    return h > 0 ? h + 'h' + m + 'm' : m + '分钟';
  },
  seatLabel: function(type) {
    var m = { BUSINESS: '商务座', FIRST: '一等座', SECOND: '二等座', HARD_SLEEPER: '硬卧', HARD_SEAT: '硬座', NO_SEAT: '无座' };
    return m[type] || type;
  },
  esc: function(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); },
  showNav: function() {
    var u = U.$('nav-user'), b = U.$('btn-logout');
    if (u) u.textContent = State.user ? State.user.username + ' (' + State.user.role + ')' : '';
    if (b) b.style.display = State.user ? '' : 'none';
  },
  hideNav: function() {
    var u = U.$('nav-user'), b = U.$('btn-logout');
    if (u) u.textContent = '';
    if (b) b.style.display = 'none';
  },
  loadStations: async function() {
    if (State.stations.length) return;
    var res = await API.get('/api/debug/stations');
    if (res.ok && res.data && res.data.data) State.stations = res.data.data;
  },
};

// ═══════════════════════════════════════════
// UI 模块
// ═══════════════════════════════════════════

const UI = {
  showPage: function(name) {
    // 隐藏所有页面
    var pages = document.querySelectorAll('.page');
    for (var i = 0; i < pages.length; i++) { pages[i].classList.remove('active'); }
    // 显示目标页
    var page = U.$('page-' + name);
    if (page) page.classList.add('active');
    // 登录页不显示侧边栏
    var layout = U.$('app-layout');
    if (layout) layout.style.display = (name === 'login') ? 'none' : 'flex';
    // 更新侧边栏高亮
    var items = document.querySelectorAll('.sidebar-item');
    for (var j = 0; j < items.length; j++) {
      items[j].classList.toggle('active', items[j].getAttribute('data-page') === name);
    }
    if (name === 'query') UI.populateStationSelects();
    if (name === 'orders') UI.loadOrders();
  },

  /** 返回上一页（购票→查票） */
  goBack: function() { UI.showPage('query'); },

  navTo: function(name, data) {
    if (name === 'order-form' && data) State.selectedTrain = data;
    UI.showPage(name);
    if (name === 'order-form') UI.renderOrderForm();
  },

  // ── 站点下拉 ──
  populateStationSelects: function() {
    var from = U.$('query-from'), to = U.$('query-to');
    if (!from || !to) return;
    from.innerHTML = to.innerHTML = '<option value="">选择站点</option>';
    for (var i = 0; i < State.stations.length; i++) {
      var s = State.stations[i];
      from.innerHTML += '<option value="' + s.id + '">' + U.esc(s.name) + '</option>';
      to.innerHTML   += '<option value="' + s.id + '">' + U.esc(s.name) + '</option>';
    }
    var d = U.$('query-date'); if (d) d.value = new Date().toISOString().slice(0, 10);
  },

  // ── 查票 ──
  queryTrains: async function() {
    var fromEl = U.$('query-from'), toEl = U.$('query-to'), dateEl = U.$('query-date');
    if (!fromEl || !toEl) return;
    var from = fromEl.value, to = toEl.value, date = dateEl ? dateEl.value : '2026-07-07';
    if (!from || !to) return U.toast('请选择出发站和到达站', 'error');
    if (from === to) return U.toast('出发站和到达站不能相同', 'error');

    var loadingEl = U.$('query-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var errEl = U.$('query-error'); if (errEl) errEl.textContent = '';
    var listEl = U.$('query-results'); if (listEl) listEl.innerHTML = '';

    var res = await API.get('/api/trains/query?from=' + from + '&to=' + to + '&date=' + date);
    if (loadingEl) loadingEl.style.display = 'none';

    if (!res.ok) { if (errEl) errEl.textContent = (res.data && res.data.error) || '查询失败'; return; }
    State.queryResult = res.data;
    var dc = U.$('tab-direct-count'); if (dc) dc.textContent = '(' + res.data.direct_count + ')';
    var tc = U.$('tab-transfer-count'); if (tc) tc.textContent = '(' + res.data.transfer_count + ')';
    UI.renderResults('direct');
  },

  switchTab: function(tab) {
    State.currentTab = tab;
    var tabs = document.querySelectorAll('.tab');
    for (var i = 0; i < tabs.length; i++) {
      var attr = tabs[i].getAttribute('onclick') || '';
      tabs[i].classList.toggle('active', attr.indexOf("'" + tab + "'") >= 0);
    }
    UI.renderResults(tab);
  },

  renderResults: function(tab) {
    var el = U.$('query-results'); if (!el) return;
    var list = State.queryResult ? (tab === 'direct' ? State.queryResult.direct : State.queryResult.transfers) : [];
    var html = '';
    if (!list || !list.length) { html = '<div class="loading">暂无结果</div>'; }
    else for (var i = 0; i < list.length; i++) {
      var item = list[i];
      var seats = item.available_seats;
      var avail = seats ? (seats.second_seats || seats.hard_seat || seats.hard_sleeper || 0) : 0;
      var tid = U.esc(item.train_id);
      html += '<div class="train-card" onclick="UI.selectTrain(\'' + tid + '\',' +
        (item.from_station || 0) + ',' + (item.to_station || 0) + ',\'' +
        item.departure_time + '\',\'' + item.arrival_time + '\',' + (item.price || 0) + ')">' +
        '<div class="train-info"><div class="train-id">' + tid + '</div>' +
        '<div class="train-meta">' + (item.is_transfer ? '换乘: ' + U.esc(item.transfer_station || '') : '直达') + '</div></div>' +
        '<div class="train-time"><div class="time">' + U.fmtTime(item.departure_time) + ' – ' + U.fmtTime(item.arrival_time) + '</div>' +
        '<div class="duration">' + U.fmtDuration(item.duration_minutes) + '</div></div>' +
        '<div class="train-seats"><div class="price">¥' + (item.price || 0).toFixed(1) + '</div>' +
        '<div class="seats">' + avail + ' 张</div></div></div>';
    }
    el.innerHTML = html;
  },

  // ── 购票页 ──
  selectTrain: function(trainId, from, to, dep, arr, price) {
    var d = U.$('query-date');
    State.selectedTrain = { train_id: trainId, from_station: from, to_station: to,
      departure_time: dep, arrival_time: arr, price: price, date: d ? d.value : '2026-07-07' };
    UI.showPage('order-form');
    UI.renderOrderForm();
  },

  renderOrderForm: function() {
    var t = State.selectedTrain;
    var info = U.$('order-train-info');
    if (info) info.innerHTML = t ? '<strong>' + U.esc(t.train_id) + '</strong> &nbsp; ' +
      U.fmtTime(parseInt(t.departure_time)) + ' → ' + U.fmtTime(parseInt(t.arrival_time)) +
      ' &nbsp; ¥' + (t.price || 0).toFixed(1) + ' &nbsp; ' + t.date : '';
    var sel = U.$('order-seat-type');
    if (sel) sel.innerHTML = ['SECOND','FIRST','BUSINESS','HARD_SLEEPER','HARD_SEAT','NO_SEAT']
      .map(function(s) { return '<option value="' + s + '">' + U.seatLabel(s) + '</option>'; }).join('');
  },

  submitOrder: async function() {
    var t = State.selectedTrain;
    if (!t) return;
    var body = {
      train_id: t.train_id, date: t.date, from_station: t.from_station, to_station: t.to_station,
      seat_type: (U.$('order-seat-type') || {}).value || 'SECOND',
      count: parseInt((U.$('order-count') || {}).value) || 1,
      passenger_name: (U.$('order-passenger-name') || {}).value || '',
      passenger_id: (U.$('order-passenger-id') || {}).value || '',
    };
    if (!body.passenger_name || !body.passenger_id) return U.toast('请填写乘车人信息', 'error');
    var res = await API.post('/api/orders', body);
    if (res.ok) {
      U.toast('购票成功！订单号: ' + res.data.order_id, 'success');
      UI.showPage('orders');
    } else {
      U.toast((res.data && res.data.error) || '购票失败', 'error');
    }
  },

  // ── 订单页 ──
  filterOrders: function(status) {
    State.currentStatusFilter = status;
    var btns = document.querySelectorAll('.filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      var txt = btns[i].textContent.trim();
      btns[i].classList.toggle('active', txt === (status === '' ? '全部' : status === 'PAID' ? '已支付' : status === 'REFUNDED' ? '已退票' : '已取消'));
    }
    UI.loadOrders();
  },

  loadOrders: async function() {
    var loadingEl = U.$('orders-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var url = State.currentStatusFilter ? '/api/orders?status=' + State.currentStatusFilter : '/api/orders';
    var res = await API.get(url);
    if (loadingEl) loadingEl.style.display = 'none';

    if (!res.ok) { U.toast((res.data && res.data.error) || '加载失败', 'error'); return; }
    var html = '';
    var orders = (res.data && res.data.data) ? res.data.data : [];
    if (!orders.length) { html = '<div class="loading">暂无订单</div>'; }
    else for (var i = 0; i < orders.length; i++) {
      var o = orders[i];
      html += '<div class="order-card"><div class="order-header">' +
        '<span class="order-id">' + U.esc(o.id) + '</span>' +
        '<span class="order-status status-' + o.status + '">' + o.status + '</span></div>' +
        '<div class="order-body"><span><strong>' + U.esc(o.train_id) + '</strong></span>' +
        '<span>' + (o.date || '') + '</span><span>' + U.seatLabel(o.seat_type) + ' ' + (o.seat_number || 0) + '号</span>' +
        '<span style="color:#e94560">¥' + (o.price || 0).toFixed(1) + '</span></div>';
      if (o.status === 'PAID') html += '<div class="order-actions"><button class="btn btn-danger btn-sm" onclick="UI.refundOrder(\'' + o.id + '\')">退票</button></div>';
      html += '</div>';
    }
    var listEl = U.$('orders-list'); if (listEl) listEl.innerHTML = html;
  },

  refundOrder: async function(orderId) {
    if (!confirm('确定退票？退款金额按阶梯费率计算。')) return;
    var res = await API.post('/api/orders/' + orderId + '/refund', {});
    if (res.ok) {
      U.toast('退票成功，退款 ¥' + ((res.data && res.data.refund_amount) || 0).toFixed(2), 'success');
      UI.loadOrders();
    } else {
      U.toast((res.data && res.data.error) || '退票失败', 'error');
    }
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
