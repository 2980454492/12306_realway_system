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
      html += '<option value="🌆 ' + U.esc(cityNames[c]) + '">';
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
   * 🌆 前缀表示城市级别查询，匹配该城市所有车站。
   */
  resolveStationIds: function(input) {
    if (!input) return '';
    var isCity = (input.indexOf('🌆 ') === 0);
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
    var fromVal = fromEl.value;
    var toVal = toEl.value;
    toEl.innerHTML = '<option value="">不限</option>';
    if (fromVal === '') {
      // 不限：恢复全部小时
      for (var h = 0; h <= 23; h++) {
        var label = (h < 10 ? '0' : '') + h + ':00';
        toEl.innerHTML += '<option value="' + h + '">' + label + '</option>';
      }
    } else {
      var minH = parseInt(fromVal, 10) + 1;
      for (var h = minH; h <= 23; h++) {
        var label = (h < 10 ? '0' : '') + h + ':00';
        toEl.innerHTML += '<option value="' + h + '">' + label + '</option>';
      }
    }
    // 恢复之前的选中值（如果仍在范围内）
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
    // 计算每趟列车当前最低可购票价（有票的席位中最便宜的）
    function cheapestPrice(item) {
      var seats = item.available_seats || {};
      var prices = item.seat_prices || {};
      var map = [
        {sk: 'business_seats', pk: 'BUSINESS'},
        {sk: 'first_seats',    pk: 'FIRST'},
        {sk: 'second_seats',   pk: 'SECOND'},
        {sk: 'hard_sleeper',   pk: 'HARD_SLEEPER'},
        {sk: 'hard_seat',      pk: 'HARD_SEAT'},
        {sk: 'no_seat',        pk: 'NO_SEAT'}
      ];
      var best = Infinity;
      for (var m = 0; m < map.length; m++) {
        if ((seats[map[m].sk] || 0) > 0) {
          var p = prices[map[m].pk];
          if (p != null && p < best) best = p;
        }
      }
      return best === Infinity ? (item.price || 9999) : best;
    }

    list.sort(function(a, b) {
      switch (sortBy) {
        case 'arrival':   return (a.arrival_time   || 9999) - (b.arrival_time   || 9999);
        case 'duration':  return (a.duration_minutes || 9999) - (b.duration_minutes || 9999);
        case 'distance':  return (a.distance_km     || 9999) - (b.distance_km     || 9999);
        case 'price':     return cheapestPrice(a) - cheapestPrice(b);
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
      State._trainItems = State._trainItems || {};
      State._trainItems[itemKey] = item;

      // 席位列表（只显示有票的席位类型）
      var seatTypes = [
        {key: 'business_seats', label: '商务座', priceKey: 'BUSINESS'},
        {key: 'first_seats',    label: '一等座', priceKey: 'FIRST'},
        {key: 'second_seats',   label: '二等座', priceKey: 'SECOND'},
        {key: 'hard_sleeper',   label: '硬卧',  priceKey: 'HARD_SLEEPER'},
        {key: 'hard_seat',      label: '硬座',  priceKey: 'HARD_SEAT'},
        {key: 'no_seat',        label: '无座',  priceKey: 'NO_SEAT'}
      ];

      function buildSeatRow(avail, priceMap, itemKey, seatType) {
        var h = '';
        for (var s = 0; s < seatTypes.length; s++) {
          var st = seatTypes[s];
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

        cardHtml =
          '<div class="train-card transfer-card" onclick="UI.showDetail(\'' + itemKey + '\')">' +
          // 左侧主内容
          '<div class="transfer-main">' +
            // 第一行：第一程信息
            '<div class="transfer-leg-info">' +
              '<span class="leg-train-id">' + tid1 + '</span>' +
              '<span class="leg-route">' + U.esc(t1orig) + ' → ' + U.esc(t1term) + '</span>' +
              '<span class="leg-time">发 ' + U.fmtTime(item.departure_time) + ' · 到 ' + U.fmtTime(item.transfer_arrival_time) + '</span>' +
            '</div>' +
            // 第二行：第一程购票
            '<div class="transfer-leg-seats">' +
              buildSeatRow(item.first_leg_seats || {}, item.first_leg_seat_prices || {}, itemKey, 'first') +
            '</div>' +
            // 第三行：换乘间隔
            '<div class="transfer-gap">' +
              '换乘 ' + U.esc(item.transfer_station || '?') + ' · ' +
              (item.transfer_gap_minutes || 0) + '分钟' +
            '</div>' +
            // 第四行：第二程信息
            '<div class="transfer-leg-info">' +
              '<span class="leg-train-id">' + tid2 + '</span>' +
              '<span class="leg-route">' + U.esc(t2orig) + ' → ' + U.esc(t2term) + '</span>' +
              '<span class="leg-time">发 ' + U.fmtTime(item.transfer_departure_time) + ' · 到 ' + U.fmtTime(item.arrival_time) + '</span>' +
            '</div>' +
            // 第五行：第二程购票
            '<div class="transfer-leg-seats">' +
              buildSeatRow(item.second_leg_seats || {}, item.second_leg_seat_prices || {}, itemKey, 'second') +
            '</div>' +
          '</div>' +
          // 右侧总时间列
          '<div class="transfer-summary">' +
            '<span class="summary-dur">' + U.fmtDuration(item.duration_minutes) + '</span>' +
            '<div class="summary-row">' +
              '<span class="summary-time">' + U.fmtTime(item.departure_time) + '</span>' +
              '<span class="summary-dash">——</span>' +
              '<span class="summary-time">' + U.fmtTime(item.arrival_time) + '</span>' +
            '</div>' +
          '</div>' +
          '</div>';
      } else {
        // ── 直达卡片 ──
        cardHtml = '<div class="train-card" onclick="UI.showDetail(\'' + itemKey + '\')">' +
          '<div class="train-main">' +
            '<div class="train-info">' +
              '<div class="train-id">' + tid + '</div>' +
              '<div class="train-meta">' + orig + ' → ' + term + '</div>' +
              '<div class="train-meta">' + (item.distance_km || 0).toFixed(0) + 'km</div>' +
            '</div>' +
            '<div class="train-time">' +
              '<div class="time">' + U.fmtTime(item.departure_time) + ' – ' + U.fmtTime(item.arrival_time) + '</div>' +
              '<div class="duration">' + U.fmtDuration(item.duration_minutes) + '</div>' +
            '</div>' +
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
    var allEl = document.querySelector('.filter-type-all');
    var filterAll = allEl ? allEl.checked : true;

    // 1. 车型筛选
    if (!filterAll) {
      var checked = document.querySelectorAll('.filter-type-item:checked');
      var types = [];
      for (var t = 0; t < checked.length; t++) { types.push(checked[t].value); }
      list = list.filter(function(item) {
        var letter = (item.train_id || '')[0].toUpperCase();
        // G/D/C/Z/T/K/S 以外的归为 OTHER
        if ('GDCZTKS'.indexOf(letter) < 0) letter = 'OTHER';
        return types.indexOf(letter) >= 0;
      });
    }

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

    return list;
  },

  /** 车型"全部"切换：全选/取消全选各类型复选框 */
  toggleAllTypes: function(el) {
    var items = document.querySelectorAll('.filter-type-item');
    for (var i = 0; i < items.length; i++) {
      items[i].checked = el.checked;
      items[i].disabled = el.checked;
    }
    UI.applyFilters();
  },

  /** 单个车型变化时，同步"全部"复选框状态 */
  onTypeChange: function() {
    var allEl = document.querySelector('.filter-type-all');
    if (!allEl) return;
    var items = document.querySelectorAll('.filter-type-item');
    var allChecked = true;
    for (var i = 0; i < items.length; i++) {
      if (!items[i].checked) { allChecked = false; break; }
    }
    allEl.checked = allChecked;
    UI.applyFilters();
  },

  /** 筛选条件变更时重新渲染当前 tab */
  applyFilters: function() {
    UI.renderResults(State.currentTab);
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
        var arrTime = isFirst ? '始发' : U.fmtTime(s.arrival);
        var depTime = isLast ? '终到' : U.fmtTime(s.departure);
        var cls = (isFirst || isLast || (s.arrival >= 0 && s.departure >= 0)) ? 'stop' : 'pass';
        if (isGreen) cls += ' user-stop';
        if (isBlue)  cls += ' transfer-stop';
        h += '<div class="timeline-item ' + cls + '">' +
          '<div class="timeline-station">' + U.esc(s.station_name || ('站#' + s.station_id)) + '</div>' +
          '<div class="timeline-time">到 <span>' + arrTime + '</span> · 发 <span>' + depTime + '</span></div>' +
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
      State._trainItems = State._trainItems || {};
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
