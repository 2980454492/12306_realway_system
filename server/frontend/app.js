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
  del: function(url) { return this._fetch('DELETE', url); },
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
    return h > 0 ? h + 'h' + m + 'm' : m + 'm';
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
    // 按角色显示职工菜单
    var role = State.user ? State.user.role : '';
    var isStaff = (role === 'STAFF' || role === 'ADMIN');       // 列车管理
    var isApprover = (role === 'APPROVER' || role === 'ADMIN'); // 审批中心
    var items = document.querySelectorAll('.staff-only');
    for (var i = 0; i < items.length; i++) {
      var page = items[i].getAttribute('data-page');
      if (page === 'trains' || page === 'my-submissions') items[i].style.display = isStaff ? '' : 'none';
      else if (page === 'approvals') items[i].style.display = isApprover ? '' : 'none';
      else items[i].style.display = (isStaff || isApprover) ? '' : 'none';
    }
    var divider = document.querySelector('.sidebar-divider');
    if (divider) divider.style.display = (isStaff || isApprover) ? '' : 'none';
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

var SEAT_MAP = [
  {key: 'business_seats', label: '商务座', priceKey: 'BUSINESS'},
  {key: 'first_seats',    label: '一等座', priceKey: 'FIRST'},
  {key: 'second_seats',   label: '二等座', priceKey: 'SECOND'},
  {key: 'hard_sleeper',   label: '硬卧',   priceKey: 'HARD_SLEEPER'},
  {key: 'hard_seat',      label: '硬座',   priceKey: 'HARD_SEAT'},
  {key: 'no_seat',        label: '无座',   priceKey: 'NO_SEAT'}
];

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
    if (name === 'add-train') UI.showAddTrainForm();
    if (name === 'trains') UI.loadTrains();
    if (name === 'my-submissions') UI.loadMySubmissions();
    if (name === 'approvals') UI.loadApprovals();
  },

  /** 返回上一页（购票→查票） */
  goBack: function() { UI.showPage('query'); },

  navTo: function(name, data) {
    var role = State.user ? State.user.role : '';
    if ((name === 'trains' || name === 'my-submissions' || name === 'add-train') && role !== 'STAFF' && role !== 'ADMIN') return;
    if (name === 'approvals' && role !== 'APPROVER' && role !== 'ADMIN') return;
    if (name === 'order-form' && data) State.selectedTrain = data;
    UI.showPage(name);
    if (name === 'order-form') UI.renderOrderForm();
  },

  // ── 站点下拉 ──
  populateStationSelects: function() {
    var from = U.$('query-from'), to = U.$('query-to');
    if (!from || !to) return;
    // 构建 datalist：所有站名 + 城市名（去重，城市前缀 🌆）
    var cities = {};
    var html = '';
    for (var i = 0; i < State.stations.length; i++) {
      var s = State.stations[i];
      html += '<option value="' + U.esc(s.name) + '">';
      if (!cities[s.city]) { cities[s.city] = true; }
    }
    var cityNames = Object.keys(cities);
    for (var c = 0; c < cityNames.length; c++) {
      html += '<option value="🏠 ' + U.esc(cityNames[c]) + '">';
    }
    U.$('stations-datalist').innerHTML = html;
    // 绑定 datalist
    from.setAttribute('list', 'stations-datalist');
    to.setAttribute('list', 'stations-datalist');
    // 恢复上次的输入
    if (State._savedFrom) from.value = State._savedFrom;
    if (State._savedTo) to.value = State._savedTo;
    // 监听变更，保存值
    from.onchange = function() { State._savedFrom = from.value; };
    to.onchange = function() { State._savedTo = to.value; };
    from.onblur = function() { State._savedFrom = from.value; };
    to.onblur = function() { State._savedTo = to.value; };

    var d = U.$('query-date'); if (d) {
      var savedDate = d.value;
      if (!savedDate || savedDate < d.min || savedDate > d.max) {
        d.value = new Date().toISOString().slice(0, 10);
      }
      var today = new Date();
      var maxDay = new Date(today); maxDay.setDate(today.getDate() + 14);
      d.min = today.toISOString().slice(0, 10);
      d.max = maxDay.toISOString().slice(0, 10);
    }
    UI.initHourSelects();
  },

  /**
   * 将用户输入的站名/城市名解析为车站 ID 数组（逗号拼接给后端）。
   * 🏠 前缀表示城市级别查询，匹配该城市所有车站。
   */
  resolveStationIds: function(input) {
    if (!input) return '';
    var isCity = (input.indexOf('🏠 ') === 0);
    var name = isCity ? input.slice(3) : input;
    var ids = [];
    for (var i = 0; i < State.stations.length; i++) {
      var s = State.stations[i];
      if (isCity ? (s.city === name) : (s.name === name)) {
        ids.push(s.id);
      }
    }
    return ids.length ? ids.join(',') : '0';  // '0' 会导致后端校验失败
  },

  // ── 小时下拉初始化 ──

  /** 生成 0–23 小时 option，并初始化四个时间筛选下拉 */
  initHourSelects: function() {
    var hoursHtml = '<option value="">不限</option>';
    for (var h = 0; h <= 23; h++) {
      var label = (h < 10 ? '0' : '') + h + ':00';
      hoursHtml += '<option value="' + h + '">' + label + '</option>';
    }
    var ids = ['filter-dep-from', 'filter-dep-to', 'filter-arr-from', 'filter-arr-to'];
    for (var i = 0; i < ids.length; i++) {
      var el = U.$(ids[i]);
      if (el) { var val = el.value; el.innerHTML = hoursHtml; el.value = val; }
    }
  },

  /**
   * 将 to 下拉的选项限制为 > fromHour（级联约束第二个时间一定晚于第一个）
   * @param {string} fromId 出发"从"下拉的 ID
   * @param {string} toId   出发"到"下拉的 ID
   */
  cascadeHourTo: function(fromId, toId) {
    var fromEl = U.$(fromId), toEl = U.$(toId);
    if (!fromEl || !toEl) return;
    var fromVal = fromEl.value, toVal = toEl.value;
    var minH = fromVal === '' ? 0 : parseInt(fromVal, 10) + 1;
    var html = '<option value="">不限</option>';
    for (var h = minH; h <= 23; h++) {
      html += '<option value="' + h + '">' + (h < 10 ? '0' : '') + h + ':00</option>';
    }
    toEl.innerHTML = html;
    if (toVal && toEl.querySelector('option[value="' + toVal + '"]')) toEl.value = toVal;
    UI.applyFilters();
  },

  /** 出发"从"变更 → 级联更新出发"到" */
  onDepFromChange: function() { UI.cascadeHourTo('filter-dep-from', 'filter-dep-to'); },

  /** 到达"从"变更 → 级联更新到达"到" */
  onArrFromChange: function() { UI.cascadeHourTo('filter-arr-from', 'filter-arr-to'); },

  // ── 查票 ──
  queryTrains: async function() {
    var fromEl = U.$('query-from'), toEl = U.$('query-to'), dateEl = U.$('query-date');
    if (!fromEl || !toEl) return;
    var fromRaw = fromEl.value.trim(), toRaw = toEl.value.trim();
    var date = dateEl ? dateEl.value : '2026-07-07';
    if (!fromRaw || !toRaw) return U.toast('请选择出发站和到达站', 'error');

    var fromIds = UI.resolveStationIds(fromRaw);
    var toIds = UI.resolveStationIds(toRaw);
    if (!fromIds || fromIds === '0' || !toIds || toIds === '0') {
      return U.toast('未找到匹配的车站，请从下拉列表中选择', 'error');
    }
    if (fromIds === toIds) return U.toast('出发站和到达站不能相同', 'error');

    var loadingEl = U.$('query-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var errEl = U.$('query-error'); if (errEl) errEl.textContent = '';
    var listEl = U.$('query-results'); if (listEl) listEl.innerHTML = '';

    var res = await API.get('/api/trains/query?from=' + fromIds + '&to=' + toIds + '&date=' + date);
    if (loadingEl) loadingEl.style.display = 'none';

    if (!res.ok) { if (errEl) errEl.textContent = (res.data && res.data.error) || '查询失败'; return; }
    State.queryResult = res.data;
    // 仅新查询时构建站名筛选复选框
    var allResults = res.data.direct.concat(res.data.transfers || []);
    UI.populateStationFilters(allResults);
    UI.renderResults(State.currentTab);
    // 保持当前标签页高亮
    var tabs = document.querySelectorAll('.tab');
    for (var ti = 0; ti < tabs.length; ti++) {
      var attr = tabs[ti].getAttribute('onclick') || '';
      tabs[ti].classList.toggle('active', attr.indexOf("'" + State.currentTab + "'") >= 0);
    }
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
    var rawList = State.queryResult ? (tab === 'direct' ? State.queryResult.direct : State.queryResult.transfers) : [];
    // 排序：先拷贝再排，不改变原始数据
    var sortEl = U.$('query-sort');
    var sortBy = sortEl ? sortEl.value : 'departure';
    var list = rawList.slice();
    function cheapestPrice(item) {
      var seats = item.available_seats || {};
      var prices = item.seat_prices || {};
      var best = Infinity;
      for (var m = 0; m < SEAT_MAP.length; m++) {
        if ((seats[SEAT_MAP[m].key] || 0) > 0) {
          var p = prices[SEAT_MAP[m].priceKey];
          if (p != null && p < best) best = p;
        }
      }
      return best === Infinity ? (item.price || 9999) : best;
    }

    // 预计算最低票价，排序时 O(1) 复用
    for (var pi = 0; pi < list.length; pi++) {
      list[pi]._cp = cheapestPrice(list[pi]);
    }

    list.sort(function(a, b) {
      switch (sortBy) {
        case 'arrival':   return (a.arrival_time   || 9999) - (b.arrival_time   || 9999);
        case 'duration':  return (a.duration_minutes || 9999) - (b.duration_minutes || 9999);
        case 'distance':  return (a.distance_km     || 9999) - (b.distance_km     || 9999);
        case 'price':     return (a._cp || 9999) - (b._cp || 9999);
        default:          return (a.departure_time  || 9999) - (b.departure_time  || 9999);
      }
    });

    // ── 筛选 ──
    list = UI.filterList(list);
    // 更新标签页计数（筛选后）
    var countEl = U.$('tab-' + tab + '-count');
    if (countEl) countEl.textContent = '(' + list.length + ')';

    var html = '';
    if (!list || !list.length) { html = '<div class="loading">暂无结果</div>'; }
    else for (var i = 0; i < list.length; i++) {
      var item = list[i];
      var seats = item.available_seats || {};
      var prices = item.seat_prices || {};
      var tid = U.esc(item.train_id);
      var orig = item.origin_station || '?';
      var term = item.terminal_station || '?';
      // 存储供 detail 弹窗使用
      var itemKey = 'item_' + tab + '_' + i;
      State._trainItems[itemKey] = item;

      /** 时间条：历时在上，时间分列横线两端 */
      function timeBar(dep, dur, arr) {
        return '<div class="tb-wrap">' +
               '<span class="tb-dur">' + U.fmtDuration(dur) + '</span>' +
               '<div class="tb-line">' +
                 '<span class="tb-time">' + U.fmtTime(dep) + '</span>' +
                 '<span class="tb-dash">——</span>' +
                 '<span class="tb-time">' + U.fmtTime(arr) + '</span>' +
               '</div></div>';
      }

      function buildSeatRow(avail, priceMap, itemKey, seatType) {
        var h = '';
        for (var s = 0; s < SEAT_MAP.length; s++) {
          var st = SEAT_MAP[s];
          var cnt = avail[st.key] || 0;
          if (cnt > 0) {
            var p = (priceMap || {})[st.priceKey] || 0;
            h += '<span class="seat-tag" onclick="UI.buySeat(\'' + itemKey + '\',\'' + st.priceKey + '\',\'' + (seatType || '') + '\');event.stopPropagation()">' +
              st.label + ' <b>' + cnt + '</b>张 <span class="tag-price">¥' + p.toFixed(0) + '</span></span>';
          }
        }
        return h;
      }

      var cardHtml;
      if (item.is_transfer) {
        // ── 换乘卡片：五行布局 ──
        var tid1 = U.esc(item.train_id.split(' → ')[0] || '');
        var tid2 = U.esc(item.second_train_id || '');
        // T1／T2 的始发终到站
        var s1 = item.stops || [];
        var s2 = item.second_stops || [];
        var t1orig = (s1.length ? s1[0].station_name : '?');
        var t1term = (s1.length ? s1[s1.length-1].station_name : '?');
        var t2orig = (s2.length ? s2[0].station_name : '?');
        var t2term = (s2.length ? s2[s2.length-1].station_name : '?');

        // 通过站名反查中转站 ID
        var transferId = 0;
        for (var si = 0; si < State.stations.length; si++) {
          if (State.stations[si].name === item.transfer_station) { transferId = State.stations[si].id; break; }
        }
        // 第一程用户的上下车站
        var f1Name = '', t1Name = '';
        for (var si = 0; si < s1.length; si++) {
          if (s1[si].station_id === item.from_station) f1Name = s1[si].station_name;
          if (s1[si].station_id === transferId) t1Name = s1[si].station_name;
        }
        // 第二程用户的上下车站
        var f2Name = '', t2Name = '';
        for (var si = 0; si < s2.length; si++) {
          if (s2[si].station_id === transferId) f2Name = s2[si].station_name;
          if (s2[si].station_id === item.to_station) t2Name = s2[si].station_name;
        }
        // 各段时长计算
        function legDuration(dep, arr) {
          var dm = Math.floor(dep/100)*60 + dep%100;
          var am = Math.floor(arr/100)*60 + arr%100;
          if (am < dm) am += 1440;
          return am - dm;
        }

        // 每程时间行 HTML
        var leg1Dur = legDuration(item.departure_time, item.transfer_arrival_time);
        var leg2Dur = legDuration(item.transfer_departure_time, item.arrival_time);
        cardHtml =
          '<div class="train-card" onclick="UI.showDetail(\'' + itemKey + '\')">' +
          // 总时间右上角
          '<div class="transfer-summary tb-big">' +
            timeBar(item.departure_time, item.duration_minutes, item.arrival_time) +
          '</div>' +
          // 第一程
          '<div class="train-main">' +
            '<div class="train-info">' +
              '<div class="train-meta"><span class="train-id-inline">' + tid1 + '</span>　' + U.esc(f1Name || t1orig) + ' → ' + U.esc(t1Name || item.transfer_station) + '</div>' +
              '<div class="train-meta train-route">始发 · ' + U.esc(t1orig) + ' — 终到 · ' + U.esc(t1term) + '</div>' +
            '</div>' +
            '<div class="train-time">' + timeBar(item.departure_time, leg1Dur, item.transfer_arrival_time) + '</div>' +
          '</div>' +
          '<div class="train-seats-row">' + buildSeatRow(item.first_leg_seats || {}, item.first_leg_seat_prices || {}, itemKey, 'first') + '</div>' +
          // 换乘间隔
          '<div class="transfer-gap">换乘 ' + U.esc(item.transfer_station || '?') + ' · ' + U.fmtDuration(item.transfer_gap_minutes || 0) + '</div>' +
          // 第二程
          '<div class="train-main">' +
            '<div class="train-info">' +
              '<div class="train-meta"><span class="train-id-inline">' + tid2 + '</span>　' + U.esc(f2Name || item.transfer_station) + ' → ' + U.esc(t2Name || t2term) + '</div>' +
              '<div class="train-meta train-route">始发 · ' + U.esc(t2orig) + ' — 终到 · ' + U.esc(t2term) + '</div>' +
            '</div>' +
            '<div class="train-time">' + timeBar(item.transfer_departure_time, leg2Dur, item.arrival_time) + '</div>' +
          '</div>' +
          '<div class="train-seats-row">' + buildSeatRow(item.second_leg_seats || {}, item.second_leg_seat_prices || {}, itemKey, 'second') + '</div>' +
          '</div>';
      } else {
        // ── 直达卡片 ──
        // 查用户上车站名和下车站名
        var fromName = '', toName = '';
        for (var si = 0; si < (item.stops || []).length; si++) {
          if (item.stops[si].station_id === item.from_station) fromName = item.stops[si].station_name;
          if (item.stops[si].station_id === item.to_station) toName = item.stops[si].station_name;
        }

        cardHtml = '<div class="train-card" onclick="UI.showDetail(\'' + itemKey + '\')">' +
          '<div class="train-main">' +
            '<div class="train-info">' +
              '<div class="train-meta"><span class="train-id-inline">' + tid + '</span>　' + U.esc(fromName || orig) + ' → ' + U.esc(toName || term) + '</div>' +
              '<div class="train-meta train-route">始发 · ' + U.esc(orig) + ' — 终到 · ' + U.esc(term) + '</div>' +
            '</div>' +
            '<div class="train-time tb-big">' + timeBar(item.departure_time, item.duration_minutes, item.arrival_time) + '</div>' +
          '</div>' +
          '<div class="train-seats-row">' + buildSeatRow(seats, prices, itemKey) + '</div>' +
        '</div>';
      }

      html += cardHtml;
    }
    el.innerHTML = html;
  },

  // ── 筛选 ──

  /** 对已排序的列表应用全部筛选条件，返回筛选后的数组 */
  filterList: function(list) {
    // 1. 车型筛选
    var enabledTypes = {};
    var typeItems = document.querySelectorAll('.filter-type-item');
    for (var t = 0; t < typeItems.length; t++) {
      enabledTypes[typeItems[t].value] = typeItems[t].checked;
    }
    list = list.filter(function(item) {
      var letter = (item.train_id || '')[0].toUpperCase();
      if ('GDCZTKS'.indexOf(letter) < 0) letter = 'OTHER';
      return enabledTypes[letter] === true;
    });

    // 2. 只看有票
    var hasTicketEl = U.$('filter-has-ticket');
    if (hasTicketEl && hasTicketEl.checked) {
      list = list.filter(function(item) {
        var seats = item.available_seats || {};
        return (seats.business_seats || 0) + (seats.first_seats || 0) +
               (seats.second_seats || 0) + (seats.hard_sleeper || 0) +
               (seats.hard_seat || 0) + (seats.no_seat || 0) > 0;
      });
    }

    // 取小时：select 的 value 是 "0"~"23" 或 ""（不限）
    function hourFrom(el) {
      if (!el || !el.value) return null;
      return parseInt(el.value, 10);
    }

    // 3. 出发时间范围（只比较小时）
    var depFrom = hourFrom(U.$('filter-dep-from'));
    var depTo   = hourFrom(U.$('filter-dep-to'));
    if (depFrom != null || depTo != null) {
      list = list.filter(function(item) {
        var d = Math.floor(item.departure_time / 100);  // HHMM → 小时
        if (depFrom != null && d < depFrom) return false;
        if (depTo   != null && d > depTo)   return false;
        return true;
      });
    }

    // 4. 到达时间范围（只比较小时）
    var arrFrom = hourFrom(U.$('filter-arr-from'));
    var arrTo   = hourFrom(U.$('filter-arr-to'));
    if (arrFrom != null || arrTo != null) {
      list = list.filter(function(item) {
        var a = Math.floor(item.arrival_time / 100);
        if (arrFrom != null && a < arrFrom) return false;
        if (arrTo   != null && a > arrTo)   return false;
        return true;
      });
    }

    // 5. 出发站筛选
    var fromChecks = document.querySelectorAll('.filter-from-st:not(:checked)');
    if (fromChecks.length > 0) {
      var excludeFrom = [];
      for (var fi = 0; fi < fromChecks.length; fi++) { excludeFrom.push(fromChecks[fi].value); }
      list = list.filter(function(item) {
        var fname = item.from_station_name;
        if (!fname && item.stops) {
          for (var j = 0; j < item.stops.length; j++) {
            if (item.stops[j].station_id === item.from_station) { fname = item.stops[j].station_name; break; }
          }
        }
        return excludeFrom.indexOf(fname) < 0;
      });
    }

    // 6. 到达站筛选
    var toChecks = document.querySelectorAll('.filter-to-st:not(:checked)');
    if (toChecks.length > 0) {
      var excludeTo = [];
      for (var ti = 0; ti < toChecks.length; ti++) { excludeTo.push(toChecks[ti].value); }
      list = list.filter(function(item) {
        var tname = item.to_station_name;
        if (!tname && item.stops) {
          for (var j = 0; j < item.stops.length; j++) {
            if (item.stops[j].station_id === item.to_station) { tname = item.stops[j].station_name; break; }
          }
        }
        return excludeTo.indexOf(tname) < 0;
      });
    }

    return list;
  },

  /** 车型"全部"切换：全选/取消全选各类型复选框（购票页和车站查询共用） */
  toggleAllTypes: function(el) {
    var items = document.querySelectorAll('.filter-type-item, .station-filter-type-item');
    for (var i = 0; i < items.length; i++) {
      items[i].checked = el.checked;
      items[i].disabled = el.checked;
    }
    UI._refreshAfterFilter();
  },

  /** 单个车型变化时，同步"全部"复选框（购票页和车站查询共用） */
  onTypeChange: function() {
    var allEl = document.querySelector('.filter-type-all, .station-filter-type-all');
    if (!allEl) return;
    var items = document.querySelectorAll('.filter-type-item, .station-filter-type-item');
    var allChecked = true;
    for (var i = 0; i < items.length; i++) {
      if (!items[i].checked) { allChecked = false; break; }
    }
    allEl.checked = allChecked;
    UI._refreshAfterFilter();
  },

  /** 根据当前活跃页调用对应的刷新函数 */
  _refreshAfterFilter: function() {
    if (document.getElementById('page-station').classList.contains('active')) {
      UI.renderStationResults();
    } else {
      UI.applyFilters();
    }
  },

  /** 筛选条件变更时重新渲染当前 tab */
  applyFilters: function() {
    UI.renderResults(State.currentTab);
  },

  /** 从结果中提取出发/到达站名，动态生成多选复选框（保留已有勾选状态） */
  populateStationFilters: function(list) {
    // 收集当前结果中的站名
    var fromNames = {}, toNames = {};
    for (var i = 0; i < list.length; i++) {
      var item = list[i];
      var fname = item.from_station_name;
      var tname = item.to_station_name;
      if (!fname && item.stops) {
        for (var j = 0; j < item.stops.length; j++) {
          if (item.stops[j].station_id === item.from_station) fname = item.stops[j].station_name;
          if (item.stops[j].station_id === item.to_station) tname = item.stops[j].station_name;
        }
      }
      if (fname) fromNames[fname] = true;
      if (tname) toNames[tname] = true;
    }

    // 保存当前未勾选的站（只在新站名集合中存在的）
    function saveUnchecked(cls) {
      var unchecked = [];
      var els = document.querySelectorAll('.' + cls + ':not(:checked)');
      for (var u = 0; u < els.length; u++) { unchecked.push(els[u].value); }
      State._uncheckedStations = State._uncheckedStations || {};
      State._uncheckedStations[cls] = unchecked;
    }
    saveUnchecked('filter-from-st');
    saveUnchecked('filter-to-st');

    function buildChecks(containerId, names, cls) {
      var el = document.getElementById(containerId);
      if (!el) return;
      var keys = Object.keys(names).sort();
      var unchecked = (State._uncheckedStations || {})[cls] || [];
      var html = '<span class="filter-label">' + (containerId === 'filter-from-stations' ? '出发站' : '到达站') + '</span>';
      for (var k = 0; k < keys.length; k++) {
        var isChecked = (unchecked.indexOf(keys[k]) < 0) ? ' checked' : '';
        html += '<label class="filter-check"><input type="checkbox" class="filter-station ' + cls +
          '" value="' + U.esc(keys[k]) + '"' + isChecked + ' onchange="UI.applyFilters()"> ' +
          U.esc(keys[k]) + '</label>';
      }
      el.innerHTML = html;
    }

    buildChecks('filter-from-stations', fromNames, 'filter-from-st');
    buildChecks('filter-to-stations', toNames, 'filter-to-st');
  },

  // ── 点击席位标签 → 直接购票 ──
  buySeat: function(itemKey, seatType, leg) {
    var item = (State._trainItems || {})[itemKey];
    if (!item) return;
    // 通过站名查中转站 ID
    var transferId = 0;
    if (item.transfer_station) {
      for (var si = 0; si < State.stations.length; si++) {
        if (State.stations[si].name === item.transfer_station) {
          transferId = State.stations[si].id; break;
        }
      }
    }

    var prices, trainId, depTime, arrTime, fromSt, toSt;
    if (leg === 'first') {
      prices = item.first_leg_seat_prices || {};
      trainId = (item.train_id || '').split(' → ')[0] || '';
      depTime = item.departure_time;
      arrTime = item.transfer_arrival_time;
      fromSt = item.from_station;
      toSt = transferId;
    } else if (leg === 'second') {
      prices = item.second_leg_seat_prices || {};
      trainId = item.second_train_id || '';
      depTime = item.transfer_departure_time;
      arrTime = item.arrival_time;
      fromSt = transferId;
      toSt = item.to_station;
    } else {
      prices = item.seat_prices || {};
      trainId = item.train_id;
      depTime = item.departure_time;
      arrTime = item.arrival_time;
      fromSt = item.from_station;
      toSt = item.to_station;
    }
    State.selectedTrain = {
      train_id: trainId, from_station: fromSt,
      to_station: toSt, departure_time: depTime,
      arrival_time: arrTime, price: prices[seatType] || item.price,
      seat_type: seatType,
      date: (U.$('query-date') || {}).value || '2026-07-08'
    };
    UI.showPage('order-form');
    UI.renderOrderForm();
  },

  // ── 车次详情弹窗 ──
  showDetail: function(itemKey) {
    var item = (State._trainItems || {})[itemKey];
    if (!item) return;

    U.$('detail-train-id').textContent = item.train_id;
    var fromId = item.from_station, toId = item.to_station;
    var transferName = item.transfer_station || '';

    /**
     * 渲染时间线，highlight 是两个站 ID 的集合（通过 station_id 匹配绿色高亮）。
     * 换乘时额外传入 highlightName 用于中转站名匹配。
     */
    /**
     * 渲染时间线。
     * @param {Array} stops
     * @param {string} label 段落标签
     * @param {Array} greenIds 绿色高亮的站 ID 列表
     * @param {string|null} blueName 蓝色高亮的中转站名（仅换乘）
     */
    function renderTimeline(stops, label, greenIds, blueName) {
      if (!stops || !stops.length) return '';
      var h = (label ? '<div class="timeline-label">' + U.esc(label) + '</div>' : '');
      h += '<div class="timeline">';
      for (var i = 0; i < stops.length; i++) {
        var s = stops[i];
        var isFirst = i === 0, isLast = i === stops.length - 1;
        var isGreen = (greenIds.indexOf(s.station_id) >= 0);
        var isBlue  = (blueName && s.station_name === blueName);
        var arrTime = isFirst ? '---' : U.fmtTime(s.arrival);
        var depTime = isLast ? '---' : U.fmtTime(s.departure);
        var cls = (isFirst || isLast || (s.arrival >= 0 && s.departure >= 0)) ? 'stop' : 'pass';
        if (isGreen) cls += ' user-stop';
        if (isBlue)  cls += ' transfer-stop';
        h += '<div class="timeline-item ' + cls + '">' +
          '<div class="timeline-station">' + U.esc(s.station_name || ('站#' + s.station_id)) + '</div>' +
          '<div class="timeline-time"><span>' + arrTime + '</span> 到  <span>' + depTime + '</span> 发</div>' +
          '</div>';
      }
      h += '</div>';
      return h;
    }

    var html;
    if (item.second_stops && item.second_stops.length) {
      html  = renderTimeline(item.stops,
                '第一段 ' + U.esc(item.train_id.split(' → ')[0] || ''),
                [fromId], transferName);
      html += renderTimeline(item.second_stops,
                '第二段 ' + U.esc(item.second_train_id || ''),
                [toId], transferName);
    } else {
      html = renderTimeline(item.stops, '', [fromId, toId], null);
    }
    U.$('detail-stops').innerHTML = html;
    U.$('detail-overlay').classList.add('show');
  },

  closeDetail: function() {
    U.$('detail-overlay').classList.remove('show');
  },

  // ── 购票页 ──

  renderOrderForm: function() {
    var t = State.selectedTrain;
    var info = U.$('order-train-info');
    if (info) info.innerHTML = t ? '<strong>' + U.esc(t.train_id) + '</strong> &nbsp; ' +
      U.fmtTime(parseInt(t.departure_time)) + ' → ' + U.fmtTime(parseInt(t.arrival_time)) +
      ' &nbsp; <span style="color:#e94560">¥' + (t.price || 0).toFixed(0) + '</span> &nbsp; ' +
      U.seatLabel(t.seat_type || 'SECOND') + ' &nbsp; ' + t.date : '';
    // 席位已从查票页选定，隐藏选择器
    var sel = U.$('order-seat-type');
    if (sel) {
      sel.innerHTML = '<option value="' + (t.seat_type || 'SECOND') + '">' + U.seatLabel(t.seat_type || 'SECOND') + '</option>';
      sel.disabled = true;
    }
  },

  submitOrder: async function() {
    var t = State.selectedTrain;
    if (!t) return;
    var body = {
      train_id: t.train_id, date: t.date, from_station: t.from_station, to_station: t.to_station,
      seat_type: t.seat_type || 'SECOND',
      count: 1,
      passenger_name: (U.$('order-passenger-name') || {}).value || '',
      passenger_id: (U.$('order-passenger-id') || {}).value || '',
    };
    if (!body.passenger_name || !body.passenger_id) return U.toast('请填写乘车人信息', 'error');

    // 表单校验
    if (body.passenger_name.length < 2) return U.toast('乘车人姓名至少 2 个字符', 'error');
    if (!/^[一-鿿·]{2,20}$/.test(body.passenger_name)) return U.toast('乘车人姓名格式不正确', 'error');
    if (!/^\d{17}[\dXx]$/.test(body.passenger_id)) return U.toast('身份证号格式不正确（18 位）', 'error');
    if (!body.seat_type) return U.toast('请选择席位类型', 'error');
    if (body.count < 1 || body.count > 5) return U.toast('购票数量须为 1-5 张', 'error');
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
      btns[i].classList.toggle('active', txt === (status === '' ? '全部' : status === 'PAID' ? '已支付' : '已退票'));
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
      var oKey = 'order_' + i;
      State._trainItems[oKey] = o;
      var statusClass = o.status === 'PAID' ? 'status-paid' : 'status-refunded';
      html += '<div class="order-card" onclick="UI.showDetail(\'' + oKey + '\')">' +
        // 第一行：出发时间 | 车次 | 到达时间
        '<div class="order-row order-row-1">' +
          '<span class="order-time-dep">' + U.fmtTime(o.departure_time || 0) + '</span>' +
          '<span class="order-train-id">' + U.esc(o.train_id) + '</span>' +
          '<span class="order-time-arr">' + U.fmtTime(o.arrival_time || 0) + '</span>' +
        '</div>' +
        // 第二行：出发站 | 历时 | 到达站
        '<div class="order-row order-row-2">' +
          '<span class="order-station">' + U.esc(o.from_station_name || '?') + '</span>' +
          '<span class="order-duration">' + U.fmtDuration(o.duration_minutes || 0) + '</span>' +
          '<span class="order-station">' + U.esc(o.to_station_name || '?') + '</span>' +
        '</div>' +
        // 第三行：乘车人 | 座位号
        '<div class="order-row order-row-3">' +
          '<span class="order-passenger">' + U.esc(o.passenger_name || '?') + '</span>' +
          '<span class="order-seat">' + U.seatLabel(o.seat_type) + ' ' + (o.seat_number || 0) + '号</span>' +
        '</div>' +
        // 第四行：发车日期 | 售价
        '<div class="order-row order-row-4">' +
          '<span class="order-date">' + (o.date || '') + '</span>' +
          '<span class="order-price">¥' + (o.price || 0).toFixed(1) + '</span>' +
        '</div>' +
        // 第五行：退票按钮
        (o.status === 'PAID'
          ? '<div class="order-row order-row-5"><button class="btn btn-danger btn-sm" onclick="UI.refundOrder(\'' + o.id + '\');event.stopPropagation()">退票</button></div>'
          : '<div class="order-row order-row-5"><span class="order-status ' + statusClass + '">已退票</span></div>') +
      '</div>';
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

  // ── 车站查询 ──

  /** 输入时填充 datalist：站名 + 城市名 */
  onStationInput: function() {
    var el = U.$('station-query-input');
    var dl = U.$('station-query-datalist');
    if (!el || !dl || !State.stations.length) return;
    var cities = {};
    var html = '';
    for (var i = 0; i < State.stations.length; i++) {
      var s = State.stations[i];
      html += '<option value="' + U.esc(s.name) + '">';
      if (!cities[s.city]) { cities[s.city] = true; }
    }
    var cityNames = Object.keys(cities);
    for (var c = 0; c < cityNames.length; c++) {
      html += '<option value="🏠 ' + U.esc(cityNames[c]) + '">';
    }
    dl.innerHTML = html;
    el.setAttribute('list', 'station-query-datalist');
  },

  /** 执行车站查询 */
  searchStation: async function() {
    var input = (U.$('station-query-input') || {}).value || '';
    if (!input) return U.toast('请输入车站名或城市名', 'error');

    // 前端解析城市→站 ID（复用 resolveStationIds）
    var isCity = (input.indexOf('🏠 ') === 0);
    var ids = UI.resolveStationIds(input);
    if (ids === '0') return U.toast('未找到该车站或城市', 'error');

    var loadingEl = U.$('station-loading');
    if (loadingEl) loadingEl.style.display = 'block';
    U.$('station-results').innerHTML = '';

    try {
      var sort = (U.$('station-sort') || {}).value || 'departure';
      var url = '/api/trains/station?station=' + ids + '&sort=' + sort;
      var res = await API.get(url);
      if (loadingEl) loadingEl.style.display = 'none';
      if (!res.ok) {
        U.toast((res.data && res.data.error) || '查询失败', 'error');
        return;
      }
      State.stationResult = res.data.data || [];
      State.stationIsCity = isCity;
      // 保存子站列表（前端已知，无需后端返回）
      State.stationCityStations = [];
      if (isCity) {
        var searchName = input.slice(3);
        for (var si = 0; si < State.stations.length; si++) {
          if (State.stations[si].city === searchName) {
            State.stationCityStations.push(State.stations[si]);
          }
        }
      }

      // 初始化车站筛选勾选状态
      State.stationFilterSt = {};
      for (var k = 0; k < State.stationCityStations.length; k++) {
        var sn = State.stationCityStations[k].name;
        var unchecked = (State._stationUnchecked || {})[sn];
        State.stationFilterSt[sn] = (unchecked !== true);
      }

      // 初始化车型筛选（默认全选，重置静态 checkbox）
      var allEl = document.querySelector('.station-filter-type-all');
      if (allEl) {
        allEl.checked = true;
        var items = document.querySelectorAll('.station-filter-type-item');
        for (var t = 0; t < items.length; t++) {
          items[t].checked = true;
          items[t].disabled = false;
        }
      }

      UI.renderStationResults();
    } catch (e) {
      if (loadingEl) loadingEl.style.display = 'none';
      U.toast('查询出错', 'error');
    }
  },

  /** 渲染车站查询结果（含排序和筛选） */
  renderStationResults: function() {
    var list = State.stationResult.slice();
    if (!list.length) {
      U.$('station-results').innerHTML = '<div class="loading">暂无经停列车</div>';
      return;
    }

    // 读取车型筛选（从静态 checkbox 读取）
    var enabledTypes = {};
    var typeItems = document.querySelectorAll('.station-filter-type-item');
    for (var ti = 0; ti < typeItems.length; ti++) {
      enabledTypes[typeItems[ti].value] = typeItems[ti].checked;
    }
    list = list.filter(function(item) {
      var prefix = (item.train_id || '')[0];
      var typeKey = (['G','D','C','Z','T','K','S'].indexOf(prefix) >= 0) ? prefix : 'OTHER';
      return enabledTypes[typeKey] === true;
    });

    // 车站筛选：按条目自身的 station_name 匹配勾选的车站
    if (State.stationIsCity && State.stationCityStations.length > 1) {
      var enabledSt = {};
      var stChecks = document.querySelectorAll('.station-filter-st-check');
      var anyChecked = false;
      for (var si = 0; si < stChecks.length; si++) {
        enabledSt[stChecks[si].value] = stChecks[si].checked;
        if (stChecks[si].checked) anyChecked = true;
      }
      if (anyChecked) {
        list = list.filter(function(item) {
          return enabledSt[item.station_name] === true;
        });
      } else {
        list = [];
      }
    }

    // 渲染筛选栏：车站筛选按需填充
    if (State.stationIsCity && State.stationCityStations.length > 1) {
      var liveChecks = document.querySelectorAll('.station-filter-st-check');
      for (var l = 0; l < liveChecks.length; l++) {
        State.stationFilterSt[liveChecks[l].value] = liveChecks[l].checked;
      }
      var stHtml = '<span class="filter-label">车站</span>';
      for (var i = 0; i < State.stationCityStations.length; i++) {
        var sn = State.stationCityStations[i].name;
        var checked = State.stationFilterSt[sn] !== false ? ' checked' : '';
        stHtml += '<label class="filter-check"><input type="checkbox" class="station-filter-st-check" value="' +
          U.esc(sn) + '"' + checked + ' onchange="UI.renderStationResults()"> ' + U.esc(sn) + '</label>';
      }
      U.$('station-filter-st-group').innerHTML = stHtml;
      U.$('station-filter-st-row').style.display = '';
    } else {
      U.$('station-filter-st-row').style.display = 'none';
    }

    // 列车总数
    var countEl = U.$('station-count');
    if (countEl) {
      countEl.style.display = '';
      countEl.textContent = '共 ' + list.length + ' 趟列车';
    }

    // 渲染结果卡片
    var html = '';
    for (var j = 0; j < list.length; j++) {
      var item = list[j];
      var dir = U.esc(item.from_station_name || '始发') + ' → ' + U.esc(item.to_station_name || '终到');

      var arrStr = item.arrival_time > 0 ? U.fmtTime(item.arrival_time) : '---';
      var depStr = item.departure_time > 0 ? U.fmtTime(item.departure_time) : '---';

      var itemKey = 'st_' + j;
      State._trainItems[itemKey] = item;

      html += '<div class="train-card station-card" onclick="UI.showStationDetail(\'' + itemKey + '\')">' +
        '<div class="train-header">' +
          '<span class="train-id">' + U.esc(item.train_id) + '</span>' +
          '<span class="train-dir">' + dir + '</span>' +
        '</div>' +
        '<div class="station-times">' +
          '<span class="st-time"><span>' + arrStr + '</span> 到  </span>' +
          '<span class="st-time"><span>' + depStr + '</span> 发</span>' +
        '</div>' +
      '</div>';
    }
    U.$('station-results').innerHTML = html;
  },

  /** 车站查询 → 车次详情弹窗 */
  showStationDetail: function(itemKey) {
    var item = (State._trainItems || {})[itemKey];
    if (!item) return;

    U.$('detail-train-id').textContent = item.train_id + ' 经停时刻表';

    // 构建高亮站 ID 集合：城市查询时该城市所有车站都高亮，单站查询时仅该站
    var highlightIds = {};
    if (State.stationIsCity) {
      for (var ci = 0; ci < State.stationCityStations.length; ci++) {
        highlightIds[State.stationCityStations[ci].id] = true;
      }
    } else {
      highlightIds[item.station_id] = true;
    }

    function renderTimeline(stops) {
      if (!stops || !stops.length) return '';
      var h = '<div class="timeline">';
      for (var i = 0; i < stops.length; i++) {
        var s = stops[i];
        var isFirst = i === 0, isLast = i === stops.length - 1;
        var isQuery = highlightIds[s.station_id];
        var arrTime = isFirst ? '---' : U.fmtTime(s.arrival);
        var depTime = isLast ? '---' : U.fmtTime(s.departure);
        var cls = (isFirst || isLast || (s.arrival >= 0 && s.departure >= 0)) ? 'stop' : 'pass';
        if (isQuery) cls += ' user-stop';
        h += '<div class="timeline-item ' + cls + '">' +
          '<div class="timeline-station">' + U.esc(s.station_name || ('站#' + s.station_id)) + '</div>' +
          '<div class="timeline-time"><span>' + arrTime + '</span> 到  <span>' + depTime + '</span> 发</div>' +
          '</div>';
      }
      h += '</div>';
      return h;
    }

    U.$('detail-stops').innerHTML = renderTimeline(item.stops);
    U.$('detail-overlay').classList.add('show');
  },

  // ── 职工端：新增列车（线路感知逐步选线）──

  /**
   * 列车最高时速限制（km/h）。
   * 只校验上限：列车运营速度由 min(线路限速, 列车最高设计时速) 决定。
   */
  _speedLimits: {
    G: 350, D: 300, C: 350, Z: 160, T: 140, K: 120, S: 999, OTHER: 120
  },

  /** 前缀变更 → 显示最高限速 */
  onPrefixChange: function() {
    var p = (U.$('new-train-prefix') || {}).value;
    var hint = U.$('speed-limit-hint');
    if (!hint) return;
    var maxSpeed = UI._speedLimits[p];
    if (maxSpeed !== undefined) {
      hint.style.display = '';
      hint.textContent = '最高运营时速：' + (maxSpeed === 999 ? '不限' : maxSpeed + ' km/h');
    } else {
      hint.style.display = 'none';
    }
  },

  /** 初始化新增列车表单 */
  showAddTrainForm: function() {
    U.$('add-train-error').textContent = '';
    State._routePath = [];
    U.$('route-path-list').innerHTML = '';
    U.$('neighbor-panel').style.display = 'none';
    var html = '';
    for (var i = 0; i < State.stations.length; i++) {
      html += '<option value="' + U.esc(State.stations[i].name) + '">';
    }
    U.$('train-station-datalist').innerHTML = html;
    U.$('train-date-single').style.display = '';
    U.$('train-date-range').style.display = 'none';
    U.$('speed-limit-hint').style.display = 'none';
    if (Object.keys(State._neighborIndex).length === 0) {
      UI.loadNeighborIndex();
    }
  },

  /** 加载车站-线路-邻居索引并缓存 */
  loadNeighborIndex: async function() {
    var res = await API.get('/api/stations/neighbors');
    if (res.ok && res.data && res.data.data) {
      State._neighborIndex = res.data.data;
    }
  },

  /** 始发站+发车时间都填好 → 自动展示可选线路 */
  tryShowNeighbors: function() {
    var name = (U.$('first-station-input') || {}).value || '';
    var depTime = (U.$('first-depart-time') || {}).value || '';
    if (!name || !depTime) return;
    UI.onFirstStationChange();
  },

  /** 解析始发站并展示邻居线路 */
  onFirstStationChange: function() {
    var name = (U.$('first-station-input') || {}).value || '';
    var depTime = (U.$('first-depart-time') || {}).value || '';
    if (!name || !depTime) return;
    var sid = 0;
    for (var i = 0; i < State.stations.length; i++) {
      if (State.stations[i].name === name) { sid = State.stations[i].id; break; }
    }
    if (!sid) return;
    State._routePath = [{ station_id: sid, station_name: name, departure: UI._toHHMM(depTime), is_stop: true }];
    UI.renderRoutePath();
    UI.showNeighbors(sid);
  },

  /** 展示当前站出发的邻居线路 */
  showNeighbors: function(stationId) {
    var neighbors = (State._neighborIndex[String(stationId)] || []);
    if (!neighbors.length) {
      U.$('neighbor-panel').style.display = 'none';
      return;
    }
    U.$('neighbor-panel').style.display = '';
    var html = '';
    for (var i = 0; i < neighbors.length; i++) {
      var n = neighbors[i];
      html += '<div class="neighbor-card" onclick="UI.selectNeighbor(' + i + ',' + stationId + ')">' +
        '<span class="neighbor-line">' + U.esc(n.line_name) + '</span>' +
        '<span class="neighbor-arrow">→</span>' +
        '<span class="neighbor-station">' + U.esc(n.neighbor_name) + '</span>' +
        '<span class="neighbor-meta">限速 ' + (n.max_speed_kmh || '?') + ' km/h · ' + (n.distance_km ? n.distance_km.toFixed(0) : '?') + ' km</span>' +
      '</div>';
    }
    U.$('neighbor-options').innerHTML = html;
  },

  /** 选择邻居 → 弹出到发时间设置 */
  selectNeighbor: function(idx, fromStationId) {
    var neighbors = (State._neighborIndex[String(fromStationId)] || []);
    var n = neighbors[idx];
    if (!n) return;
    State._pendingNeighbor = n;
    U.$('stop-config-title').textContent = n.line_name + ' → ' + n.neighbor_name;
    U.$('stop-is-stop').checked = true;
    U.$('stop-time-fields').style.display = '';
    U.$('pass-time-field').style.display = 'none';
    U.$('stop-arrival-time').value = '';
    U.$('stop-depart-time').value = '';
    U.$('stop-pass-time').value = '';
    U.$('stop-speed-info').textContent = '';
    U.$('stop-config').style.display = '';
  },

  /** 停靠/通过切换 */
  onStopToggle: function() {
    var isStop = (U.$('stop-is-stop') || {}).checked;
    U.$('stop-time-fields').style.display = isStop ? '' : 'none';
    U.$('pass-time-field').style.display = isStop ? 'none' : '';
    U.$('stop-speed-info').textContent = '';
  },

  /** 计算当前段时速 + 校验 */
  computeSpeed: function() {
    var n = State._pendingNeighbor;
    if (!n) return;
    var prev = State._routePath[State._routePath.length - 1];
    if (!prev || !prev.departure) return;
    var isStop = (U.$('stop-is-stop') || {}).checked;
    var arrTime;
    if (isStop) {
      arrTime = (U.$('stop-arrival-time') || {}).value || '';
    } else {
      arrTime = (U.$('stop-pass-time') || {}).value || '';
    }
    if (!arrTime) { U.$('stop-speed-info').textContent = ''; return; }
    var curArr = UI._toHHMM(arrTime);
    if (curArr <= prev.departure) { U.$('stop-speed-info').textContent = '到达须晚于上一站发车'; return; }
    var prevMin = Math.floor(prev.departure/100)*60 + prev.departure%100;
    var curMin = Math.floor(curArr/100)*60 + curArr%100;
    var mins = curMin - prevMin;
    if (mins <= 0) return;
    var speed = (n.distance_km / mins) * 60;
    var prefix = (U.$('new-train-prefix') || {}).value || '';
    var trainMax = UI._speedLimits[prefix] || 999;
    var lineMax = n.max_speed_kmh || 999;
    var limit = Math.min(trainMax, lineMax);
    var ok = speed <= limit;
    U.$('stop-speed-info').textContent = '时速 ' + Math.round(speed) + ' km/h（限速 ' + limit + ' km/h）';
    U.$('stop-speed-info').style.color = ok ? '#00ff88' : '#ff4444';
  },

  /** 确认 → 加入路径 */
  confirmStop: function() {
    var n = State._pendingNeighbor;
    if (!n) return;
    var isStop = (U.$('stop-is-stop') || {}).checked;
    var arrTime, depTime;
    if (isStop) {
      arrTime = (U.$('stop-arrival-time') || {}).value || '';
      depTime = (U.$('stop-depart-time') || {}).value || '';
      if (!arrTime || !depTime) { U.toast('请填写到站和发车时间', 'error'); return; }
    } else {
      arrTime = (U.$('stop-pass-time') || {}).value || '';
      depTime = arrTime;
      if (!arrTime) { U.toast('请填写通过时间', 'error'); return; }
    }
    var prev = State._routePath[State._routePath.length - 1];
    var curArr = UI._toHHMM(arrTime);
    if (curArr <= (prev.departure || 0)) { U.toast('到达须晚于上一站发车', 'error'); return; }
    if (isStop && UI._toHHMM(depTime) <= curArr) { U.toast('发车须晚于到站', 'error'); return; }

    State._routePath.push({
      station_id: n.neighbor_station_id,
      station_name: n.neighbor_name,
      line_id: n.line_id,
      line_name: n.line_name,
      arrival: curArr,
      departure: isStop ? UI._toHHMM(depTime) : -1,
      is_stop: isStop,
      distance_km: n.distance_km,
      max_speed_kmh: n.max_speed_kmh
    });
    U.$('stop-config').style.display = 'none';
    State._pendingNeighbor = null;
    UI.renderRoutePath();
    UI.showNeighbors(n.neighbor_station_id);
  },

  /** 取消时间设置 */
  cancelStop: function() {
    U.$('stop-config').style.display = 'none';
    State._pendingNeighbor = null;
  },

  /** 设为终点站 */
  finishRoute: function() {
    U.$('neighbor-panel').style.display = 'none';
    var path = State._routePath;
    if (path.length > 0) path[path.length - 1].departure = -1;
    UI.renderRoutePath();
    U.toast('已设终点站', 'success');
  },

  /** 渲染运行路径 */
  renderRoutePath: function() {
    var path = State._routePath;
    if (!path.length) { U.$('route-path-list').innerHTML = ''; return; }
    var html = '';
    for (var i = 0; i < path.length; i++) {
      var s = path[i];
      var isFirst = (i === 0), isLast = (s.departure === -1);
      html += '<div class="route-row">' +
        '<span class="route-idx">' + (i + 1) + '</span>' +
        '<span class="route-station">' + U.esc(s.station_name) + '</span>';
      if (s.line_name && i > 0) {
        html += '<span class="route-line">（' + U.esc(s.line_name) + '）</span>';
      }
      html += '<span class="route-tag">' + (isFirst ? '始发' : isLast ? '终到' : s.is_stop ? '停靠' : '通过') + '</span>';
      if (!isFirst) html += '<span class="route-time">' + U.fmtTime(s.arrival) + ' 到</span>';
      if (!isLast) html += '<span class="route-time">' + U.fmtTime(s.departure) + ' 发</span>';
      if (s.distance_km && i > 0) {
        html += '<span class="route-dist">' + s.distance_km.toFixed(0) + ' km</span>';
      }
      if (i > 0 && !isLast) {
        html += '<button class="btn btn-sm btn-danger" style="margin-left:auto" onclick="UI.removeRouteStop(' + i + ')">✕</button>';
      }
      html += '</div>';
    }
    U.$('route-path-list').innerHTML = html;
  },

  /** 移除路径中的一站及之后所有站，回退选线 */
  removeRouteStop: function(idx) {
    State._routePath.splice(idx);
    var last = State._routePath[State._routePath.length - 1];
    if (last.departure === -1) last.departure = 0;
    U.$('neighbor-panel').style.display = '';
    UI.renderRoutePath();
    UI.showNeighbors(last.station_id);
  },

  /** HH:MM → HHMM 整数 */
  _toHHMM: function(t) {
    if (!t) return 0;
    var p = t.split(':');
    return parseInt(p[0]) * 100 + parseInt(p[1]);
  },

  /** 提交新增列车 */
  submitNewTrain: async function() {
    var prefix = (U.$('new-train-prefix') || {}).value || '';
    var number = (U.$('new-train-number') || {}).value || '';
    if (!prefix || !number) return U.toast('请选择列车种类并输入车次号', 'error');
    var tid = prefix + number;
    var trainType = parseInt((U.$('new-train-type') || {}).value || 0);
    var path = State._routePath;
    if (path.length < 2) return U.toast('至少需要始发站和终点站', 'error');

    var stops = [], routeStations = [];
    for (var i = 0; i < path.length; i++) {
      var s = path[i];
      var isFirst = (i === 0), isLast = (i === path.length - 1);
      var av = isFirst ? -1 : s.arrival;
      var dv = isLast ? -1 : s.departure;
      if (isFirst) dv = s.departure;
      stops.push({
        station_id: s.station_id, line_id: s.line_id || 0,
        arrival: av, departure: dv, platform: 0
      });
      routeStations.push(s.station_id);
    }

    var validFrom = '', validUntil = '';
    if (trainType === 0) {
      validFrom = (U.$('new-train-valid-from') || {}).value || '';
      if (!validFrom) return U.toast('请选择生效日期', 'error');
    } else {
      validFrom = (U.$('new-train-range-from') || {}).value || '';
      validUntil = (U.$('new-train-range-to') || {}).value || '';
      if (!validFrom || !validUntil) return U.toast('请选择运行区间', 'error');
    }

    var body = {
      id: tid, type: trainType, stops: stops,
      route_stations: routeStations, status: 0,
      valid_from: validFrom, valid_until: validUntil,
      seat_config: {
        business_seats: parseInt((U.$('sc-business') || {}).value || 0),
        first_seats: parseInt((U.$('sc-first') || {}).value || 0),
        second_seats: parseInt((U.$('sc-second') || {}).value || 0),
        hard_sleeper: parseInt((U.$('sc-sleeper') || {}).value || 0),
        hard_seat: parseInt((U.$('sc-hseat') || {}).value || 0),
        no_seat: parseInt((U.$('sc-noseat') || {}).value || 0)
      }
    };

    var res = await API.post('/api/admin/trains', body);
    if (res.ok) {
      U.toast('已提交审批：' + (res.data.approval_id || ''), 'success');
      UI.navTo('trains');
    } else {
      var errMsg = (res.data && res.data.error) || '提交失败';
      var conflicts = (res.data && res.data.conflicts) || [];
      if (conflicts.length > 0) {
        for (var c = 0; c < conflicts.length; c++) {
          var cf = conflicts[c];
          var sa = '', sb = '';
          for (var si = 0; si < State.stations.length; si++) {
            if (State.stations[si].id === cf.station_a) sa = State.stations[si].name;
            if (State.stations[si].id === cf.station_b) sb = State.stations[si].name;
          }
          errMsg += '\n• ' + cf.train_id + ' 占用了 ' + (sa || cf.station_a) + ' → ' + (sb || cf.station_b) +
            ' 区间 ' + U.fmtTime(cf.conflicting_enter) + '–' + U.fmtTime(cf.conflicting_leave);
        }
      }
      U.$('add-train-error').textContent = errMsg;
    }
  },

  // ── 职工端：列车管理（列表）──

  /** 加载列车列表 */
  loadTrains: async function() {
    var loadingEl = U.$('trains-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var res = await API.get('/api/admin/trains');
    if (loadingEl) loadingEl.style.display = 'none';
    if (!res.ok) return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._allTrains = res.data.data || [];
    UI.renderTrains();
  },

  /** 按状态筛选列车 */
  filterTrains: function(status) {
    State._trainStatusFilter = status;
    var btns = document.querySelectorAll('#page-trains .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      var txt = btns[i].textContent.trim();
      var match = (status === '' ? '全部' : status === 'ACTIVE' ? '运行中' : status === 'SUSPENDED' ? '已停运' : '已归档');
      btns[i].classList.toggle('active', txt === match);
    }
    UI.renderTrains();
  },

  /** 渲染列车列表 */
  renderTrains: function() {
    var trains = State._allTrains || [];
    var status = State._trainStatusFilter;
    if (status) trains = trains.filter(function(t) { return t.status == (status === 'ACTIVE' ? 0 : status === 'SUSPENDED' ? 1 : 2); });

    var tpl = U.$('tpl-train-mgmt-card');
    var listEl = U.$('trains-list');
    listEl.innerHTML = '';
    if (!trains.length) { listEl.innerHTML = '<div class="loading">暂无列车数据</div>'; return; }
    for (var i = 0; i < trains.length; i++) {
      var t = trains[i];
      var card = tpl.content.cloneNode(true);
      card.querySelector('.train-mgmt-id').textContent = t.id;
      card.querySelector('.train-tag-type').textContent = t.type === 0 ? '图定' : '临客';
      var tagEl = card.querySelector('.train-tag-status');
      tagEl.textContent = t.status === 0 ? '运行中' : t.status === 1 ? '已停运' : '已归档';
      tagEl.className = 'tag train-tag-status tag-' + (t.status === 0 ? 'active' : 'archived');
      card.querySelector('.train-mgmt-stops').textContent = (t.stops_count || 0) + ' 站';
      if (t.status === 0) {
        var btn = document.createElement('button');
        btn.className = 'btn btn-sm btn-danger';
        btn.textContent = '删除';
        btn.onclick = function() { UI.deleteTrain(t.id); };
        card.querySelector('.train-mgmt-actions').appendChild(btn);
      }
      listEl.appendChild(card);
    }
  },

  /** 删除列车 */
  deleteTrain: async function(trainId) {
    if (!confirm('确定删除列车 ' + trainId + '？')) return;
    var res = await API.del('/api/admin/trains/' + trainId);
    if (res.ok) { U.toast('已提交审批', 'success'); UI.loadTrains(); }
    else U.toast((res.data && res.data.error) || '删除失败', 'error');
  },

  // ── 职工端：我的提交 ──

  /** 加载我的提交（STAFF 查看自己提交的审批） */
  loadMySubmissions: async function() {
    var loadingEl = U.$('my-submissions-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var status = State._mySubFilter || '';
    var userId = State.user ? State.user.username : '';
    var url = '/api/admin/approvals?submitter_id=' + encodeURIComponent(userId);
    if (status) url += '&status=' + status;
    var res = await API.get(url);
    if (loadingEl) loadingEl.style.display = 'none';
    if (!res.ok) return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._mySubmissions = res.data.data || [];
    UI.renderMySubmissions();
  },

  /** 按状态筛选我的提交 */
  filterMySubmissions: function(status) {
    State._mySubFilter = status;
    var labels = {'': '全部', 'SUBMITTED': '待审批', 'APPROVED': '已通过', 'REJECTED': '已驳回'};
    var btns = document.querySelectorAll('#page-my-submissions .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      btns[i].classList.toggle('active', btns[i].textContent.trim() === (labels[status] || '全部'));
    }
    UI.loadMySubmissions();
  },

  /** 渲染我的提交列表 */
  renderMySubmissions: function() {
    var items = State._mySubmissions || [];
    var typeLabel = {0:'新增列车',1:'调整时刻',2:'新增线路',3:'新增站点',4:'删除列车'};
    var statusLabel = {0:'待审批',1:'已通过',2:'已驳回',3:'已过期'};
    var statusCls = {0:'submitted',1:'approved',2:'rejected',3:'expired'};
    var tpl = U.$('tpl-submission-card');
    var listEl = U.$('my-submissions-list');
    listEl.innerHTML = '';
    if (!items.length) { listEl.innerHTML = '<div class="loading">暂无提交记录</div>'; return; }
    for (var i = 0; i < items.length; i++) {
      var a = items[i];
      var card = tpl.content.cloneNode(true);
      card.querySelector('.approval-type').textContent = typeLabel[a.type] || '未知';
      var stEl = card.querySelector('.approval-status');
      stEl.textContent = statusLabel[a.status] || '未知';
      stEl.className = 'approval-status ' + (statusCls[a.status] || 'submitted');
      // 车次 + 提交时间
      var trainName = '';
      try { trainName = U.esc((typeof a.payload === 'string' ? JSON.parse(a.payload) : a.payload).id || '?'); } catch (e) {}
      card.querySelector('.approval-meta-submitter').textContent = '车次: ' + trainName + ' | 提交时间: ' + (a.submitted_at || '');
      // 审批人 + 审批时间（已决定的才有）
      var deciderEl = card.querySelector('.approval-meta-decider');
      if (a.approver_id) {
        deciderEl.textContent = '审批人: ' + a.approver_id + ' | 审批时间: ' + (a.decided_at || '');
      } else {
        deciderEl.style.display = 'none';
      }
      // 驳回意见
      var cmtEl = card.querySelector('.approval-comment');
      if (a.comment) { cmtEl.textContent = '驳回意见: ' + a.comment; }
      else { cmtEl.style.display = 'none'; }
      listEl.appendChild(card);
    }
  },

  // ── 职工端：审批中心（审核员）──

  /** 加载审批列表（默认待审批，历史记录只看自己审批过的） */
  loadApprovals: async function() {
    var loadingEl = U.$('approvals-loading'); if (loadingEl) loadingEl.style.display = 'block';
    var status = State._approvalFilter || 'SUBMITTED';  // 默认待审批
    var userId = State.user ? State.user.username : '';
    var url = '/api/admin/approvals?status=' + status;
    // 查看已通过/已驳回时只看自己的审批记录
    if (status === 'APPROVED' || status === 'REJECTED') {
      url += '&approver_id=' + encodeURIComponent(userId);
    }
    var res = await API.get(url);
    if (loadingEl) loadingEl.style.display = 'none';
    if (!res.ok) return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._allApprovals = res.data.data || [];
    UI.renderApprovals();
  },

  /** 按状态筛选审批 */
  filterApprovals: function(status) {
    State._approvalFilter = status;
    var labels = {'': '全部', 'SUBMITTED': '待审批', 'APPROVED': '已通过', 'REJECTED': '已驳回'};
    var btns = document.querySelectorAll('#page-approvals .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      btns[i].classList.toggle('active', btns[i].textContent.trim() === (labels[status] || '待审批'));
    }
    UI.loadApprovals();
  },

  /** 渲染审批列表 */
  renderApprovals: function() {
    var approvals = State._allApprovals || [];
    var typeLabel = {0:'新增列车',1:'调整时刻',2:'新增线路',3:'新增站点',4:'删除列车'};
    var statusLabel = {0:'待审批',1:'已通过',2:'已驳回',3:'已过期'};
    var statusCls = {0:'submitted',1:'approved',2:'rejected',3:'expired'};
    var tpl = U.$('tpl-approval-card');
    var listEl = U.$('approvals-list');
    listEl.innerHTML = '';
    if (!approvals.length) { listEl.innerHTML = '<div class="loading">暂无审批</div>'; return; }
    for (var i = 0; i < approvals.length; i++) {
      var a = approvals[i];
      var card = tpl.content.cloneNode(true);
      card.querySelector('.approval-type').textContent = typeLabel[a.type] || '未知';
      var stEl = card.querySelector('.approval-status');
      stEl.textContent = statusLabel[a.status] || '未知';
      stEl.className = 'approval-status ' + (statusCls[a.status] || 'submitted');
      // 提交人
      card.querySelector('.approval-meta-submitter').textContent = '提交人: ' + (a.submitter_id || '?') + ' | ' + (a.submitted_at || '');
      // 车次
      try {
        var p = (typeof a.payload === 'string') ? JSON.parse(a.payload) : a.payload;
        card.querySelector('.approval-payload').textContent = '车次: ' + (p.id || '?');
      } catch (e) { card.querySelector('.approval-payload').style.display = 'none'; }
      // 审批操作按钮（仅待审批状态）
      var actionsEl = card.querySelector('.approval-actions');
      if (a.status === 0) {
        actionsEl.innerHTML = '<button class="btn btn-sm btn-primary">通过</button><button class="btn btn-sm btn-danger">驳回</button>';
        var btns = actionsEl.querySelectorAll('button');
        btns[0].onclick = function() { UI.approveOne(a.id); };
        btns[1].onclick = function() { UI.rejectOne(a.id); };
      } else {
        actionsEl.style.display = 'none';
      }
      // 审批人（已决定才有）
      var deciderEl = card.querySelector('.approval-meta-decider');
      if (a.approver_id) {
        deciderEl.textContent = '审批人: ' + a.approver_id + ' | ' + (a.decided_at || '');
      } else {
        deciderEl.style.display = 'none';
      }
      // 驳回意见
      var cmtEl = card.querySelector('.approval-comment');
      if (a.comment) { cmtEl.textContent = '驳回意见: ' + a.comment; }
      else { cmtEl.style.display = 'none'; }
      listEl.appendChild(card);
    }
  },

  /** 审批通过 */
  approveOne: async function(id) {
    if (!confirm('确认通过该审批？')) return;
    var res = await API.post('/api/admin/approvals/' + id + '/approve');
    if (res.ok) { U.toast('审批通过', 'success'); UI.loadApprovals(); UI.loadTrains(); }
    else U.toast((res.data && res.data.error) || '审批失败', 'error');
  },

  /** 审批驳回 */
  rejectOne: async function(id) {
    var comment = prompt('驳回意见：');
    if (!comment) return;
    var res = await API.post('/api/admin/approvals/' + id + '/reject', { comment: comment });
    if (res.ok) { U.toast('已驳回', 'success'); UI.loadApprovals(); }
    else U.toast((res.data && res.data.error) || '驳回失败', 'error');
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
