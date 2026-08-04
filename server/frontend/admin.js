// admin.js — auto-split from app.js
'use strict';


  UI.loadUsers = async function() {
    var loadingEl = U.$('users-loading');
    if (loadingEl)
      loadingEl.style.display = 'block';
    var res = await API.get('/api/admin/users');
    if (loadingEl)
      loadingEl.style.display = 'none';
    if (!res.ok)
      return U.toast((res.data && res.data.error) || '加载失败', 'error');
    UI._allUsers = res.data.data || [];
    UI.renderUsers();
  };

  UI.renderUsers = function() {
    var users = UI._allUsers || [];
    var listEl = U.$('users-list');
    listEl.innerHTML = '';
    if (!users.length) {
      listEl.innerHTML = '<div class="loading">暂无用户</div>';
      return;
    }
    var tbl = document.createElement('table');
    tbl.className = 'users-table';
    tbl.innerHTML = '<thead><tr><th>用户名</th><th>角色</th><th>状态</th><th>操作</th></tr></thead>';
    var tbody = document.createElement('tbody');
    var tpl = U.$('tpl-user-row');
    for (let i = 0; i < users.length; i++) {
      var u = users[i];
      var row = tpl.content.cloneNode(true);
      row.querySelector('.user-username').textContent = u.username;
      // 角色标签
      var roleTag = row.querySelector('.user-role-tag');
      var roleInfo = {PASSENGER: ['旅客', 'tag-passenger'], STAFF: ['职工', 'tag-staff'],
        APPROVER: ['审核员', 'tag-approver'], INFRA_ADMIN: ['基础设施管理员', 'tag-infra'],
        SYS_ADMIN: ['系统管理员', 'tag-sys']};
      var ri = roleInfo[u.role] || [u.role, ''];
      roleTag.textContent = ri[0];
      roleTag.className = 'tag user-role-tag ' + ri[1];
      // 状态标签
      var stTag = row.querySelector('.user-status-tag');
      stTag.textContent = u.active ? '正常' : '已禁用';
      stTag.className = 'tag user-status-tag ' + (u.active ? 'tag-active' : 'tag-archived');
      // 操作按钮
      var btns = row.querySelectorAll('button');
      btns[0].onclick = (function(uid) { return function() { UI.editUserForm(uid); }; })(u.id);
      btns[1].onclick = (function(uid, uname) { return function() { UI.deleteUser(uid, uname); }; })(u.id, u.username);
      tbody.appendChild(row);
    }
    tbl.appendChild(tbody);
    listEl.appendChild(tbl);
  };

  UI.showUserForm = function(user) {
    UI._editingUserId = user ? user.id : null;
    U.$('user-form-title').textContent = user ? '编辑用户' : '新建用户';
    U.$('user-form-username').value = user ? user.username : '';
    U.$('user-form-username').disabled = !!user;  // 编辑时不可改用户名
    U.$('user-form-password').value = '';
    U.$('user-form-password').placeholder = user ? '留空则不修改密码' : '至少6位';
    U.$('user-form-role').value = user ? user.role : 'PASSENGER';
    U.$('user-form-error').textContent = '';
    U.$('user-form-submit-btn').textContent = user ? '保存' : '创建';
    U.$('user-form-overlay').style.display = 'flex';
  };

  UI.editUserForm = function(id) {
    for (let i = 0; i < UI._allUsers.length; i++)
      if (UI._allUsers[i].id === id) {
        UI.showUserForm(UI._allUsers[i]);
        return;
      }
  };

  UI.closeUserForm = function() {
    U.$('user-form-overlay').style.display = 'none';
    UI._editingUserId = null;
  };

  UI.submitUserForm = async function() {
    var username = U.$('user-form-username').value.trim();
    var password = U.$('user-form-password').value;
    var role = U.$('user-form-role').value;
    var errEl = U.$('user-form-error');

    if (!UI._editingUserId && (!username || username.length < 3)) {
      errEl.textContent = '用户名至少3位';
      return;
    }
    if (!UI._editingUserId && (!password || password.length < 6)) {
      errEl.textContent = '密码至少6位';
      return;
    }

    var res;
    if (UI._editingUserId) {
      var body = { role: role };
      if (password)
        body.password = password;
      res = await API.put('/api/admin/users/' + UI._editingUserId, body);
    } else {
      res = await API.post('/api/admin/users', { username: username, password: password, role: role });
    }

    if (res.ok) {
      U.toast(UI._editingUserId ? '已更新' : '已创建', 'success');
      UI.closeUserForm();
      UI.loadUsers();
    } else {
      errEl.textContent = (res.data && res.data.error) || '操作失败';
    }
  };

  UI.deleteUser = async function(id, username) {
    if (!confirm('确定删除用户 ' + username + '？此操作不可撤销。'))
      return;
    var res = await API.del('/api/admin/users/' + id);
    if (res.ok) {
      U.toast('已删除', 'success');
      UI.loadUsers();
    } else {
      U.toast((res.data && res.data.error) || '删除失败', 'error');
    }
  };

  UI.loadConfig = async function() {
    var loadingEl = U.$('config-loading');
    if (loadingEl) loadingEl.style.display = 'block';
    U.$('config-form').style.display = 'none';
    var res = await API.get('/api/admin/config');
    if (loadingEl) loadingEl.style.display = 'none';
    if (!res.ok) return U.toast((res.data && res.data.error) || '加载失败', 'error');
    var c = res.data.data || {};
    UI._cfgRates = c.rates || {};

    // 构建费率矩阵表格
    var pre = UI._ratePrefixes, seats = UI._rateSeats;
    var tbody = U.$('cfg-rate-table').querySelector('tbody');
    tbody.innerHTML = '';
    for (let pi = 0; pi < pre.length; pi++) {
      var p = pre[pi];
      var row = document.createElement('tr');
      var labelTd = document.createElement('td');
      labelTd.className = 'infra-name';
      labelTd.textContent = UI._ratePrefixLabels[p] || p;
      row.appendChild(labelTd);
      for (let si = 0; si < seats.length; si++) {
        var rate = (UI._cfgRates[p] && UI._cfgRates[p][seats[si]]) || 0;
        var inp = document.createElement('input');
        inp.id = 'cfg-r-' + p + '-' + seats[si];
        inp.type = 'number';
        inp.step = '0.01';
        inp.min = '0';
        inp.max = '10';
        inp.value = rate;
        inp.className = 'cfg-rate-input' + (rate > 0 ? '' : ' zero');
        var td = document.createElement('td');
        td.appendChild(inp);
        row.appendChild(td);
      }
      tbody.appendChild(row);
    }

    // 动态生成示例
    var gRate = (UI._cfgRates['G'] && UI._cfgRates['G']['SECOND']) || 0.46;
    var kRate = (UI._cfgRates['K'] && UI._cfgRates['K']['HARD_SEAT']) || 0.06;
    UI._renderConfigExample(gRate, kRate);

    U.$('cfg-refund-24h').value = c.refund_rate_24h || 0.95;
    U.$('cfg-refund-2-24h').value = c.refund_rate_2_24h || 0.90;
    U.$('cfg-refund-2h').value = c.refund_rate_2h || 0.80;
    U.$('config-form').style.display = '';
  };

  UI.saveConfig = async function() {
    var pre = UI._ratePrefixes, seats = UI._rateSeats;
    var rates = {};
    for (let pi = 0; pi < pre.length; pi++) {
      var p = pre[pi];
      rates[p] = {};
      for (let si = 0; si < seats.length; si++) {
        var el = document.getElementById('cfg-r-' + p + '-' + seats[si]);
        rates[p][seats[si]] = el ? (parseFloat(el.value) || 0) : 0;
      }
    }
    var body = {
      rates: rates,
      refund_rate_24h: parseFloat(U.$('cfg-refund-24h').value) || 0.95,
      refund_rate_2_24h: parseFloat(U.$('cfg-refund-2-24h').value) || 0.90,
      refund_rate_2h: parseFloat(U.$('cfg-refund-2h').value) || 0.80
    };
    var res = await API.put('/api/admin/config', body);
    if (res.ok) {
      U.toast('配置已保存，即时生效', 'success');
      UI._cfgRates = rates;
      var gRate = (rates['G'] && rates['G']['SECOND']) || 0.46;
      var kRate = (rates['K'] && rates['K']['HARD_SEAT']) || 0.06;
      UI._renderConfigExample(gRate, kRate);
    } else {
      U.toast((res.data && res.data.error) || '保存失败', 'error');
    }
  };

  UI._renderConfigExample = function(gRate, kRate) {
    U.$('cfg-example').innerHTML =
      '例：G2492 呼市→包头 170km 二等座<br>'
      + '&nbsp;&nbsp;&nbsp;&nbsp;= 170 × <span style="color:#00ff88">' + gRate.toFixed(2) + '</span> × 1'
      + ' = <span style="color:#e94560">¥' + (170 * gRate).toFixed(2) + '</span><br>'
      + '例：K7901 同程 硬座<br>'
      + '&nbsp;&nbsp;&nbsp;&nbsp;= 170 × <span style="color:#ffaa00">' + kRate.toFixed(2) + '</span> × 1'
      + ' = <span style="color:#e94560">¥' + (170 * kRate).toFixed(2) + '</span>';
  };

  UI.loadAudit = async function() {
    var loadingEl = U.$('audit-loading');
    if (loadingEl)
      loadingEl.style.display = 'block';
    var res = await API.get('/api/admin/audit?limit=500');
    if (loadingEl)
      loadingEl.style.display = 'none';
    if (!res.ok)
      return U.toast((res.data && res.data.error) || '加载失败', 'error');
    UI._auditRecords = res.data.data || [];
    // 显示链式校验状态
    var stEl = U.$('audit-chain-status');
    if (stEl) {
      stEl.textContent = res.data.verified ? '🔒 链式校验通过' : '⚠️ 链式校验失败！数据可能被篡改';
      stEl.style.color = res.data.verified ? '#00ff88' : '#ff4444';
    }
    UI.renderAudit();
  };

  UI.renderAudit = function() {
    var records = UI._auditRecords || [];
    var userFilter = (U.$('audit-filter-user') ? U.$('audit-filter-user').value.trim() : '').toLowerCase();
    var actionFilter = U.$('audit-filter-action') ? U.$('audit-filter-action').value : '';
    var fromFilter = U.$('audit-filter-from') ? U.$('audit-filter-from').value : '';
    var toFilter = U.$('audit-filter-to') ? U.$('audit-filter-to').value : '';

    var filtered = records.filter(function(r) {
      if (userFilter && (r.user_id || '').toLowerCase().indexOf(userFilter) < 0) return false;
      if (actionFilter && r.action !== actionFilter) return false;
      if (fromFilter && r.timestamp < fromFilter) return false;
      if (toFilter && r.timestamp > toFilter + 'T23:59:59') return false;
      return true;
    });

    var ACTION_LABEL = {LOGIN: '登录', USER_CREATE: '创建用户',
      USER_UPDATE: '更新用户', USER_DELETE: '删除用户'};
    var listEl = U.$('audit-list');
    listEl.innerHTML = '';

    if (!filtered.length) {
      listEl.innerHTML = '<div class="loading">暂无记录</div>';
      return;
    }

    var tbl = document.createElement('table');
    tbl.className = 'users-table';
    tbl.innerHTML = '<thead><tr><th>时间</th><th>用户</th><th>操作</th><th>目标</th><th>结果</th></tr></thead>';
    var tbody = document.createElement('tbody');
    for (let i = 0; i < filtered.length; i++) {
      var r = filtered[i];
      var tr = document.createElement('tr');
      var timeShort = (r.timestamp || '').replace('T', ' ').substring(0, 19);
      [timeShort, r.user_id || '—', ACTION_LABEL[r.action] || r.action, r.target || '—'].forEach(function(v) {
        var td = document.createElement('td');
        td.textContent = v;
        tr.appendChild(td);
      });
      var resultTd = document.createElement('td');
      var resultSpan = document.createElement('span');
      resultSpan.className = 'tag ' + (r.result === 'success' ? 'tag-active' : 'tag-error');
      resultSpan.textContent = r.result === 'success' ? '成功' : '失败';
      resultTd.appendChild(resultSpan);
      tr.appendChild(resultTd);
      tbody.appendChild(tr);
    }
    tbl.appendChild(tbody);
    listEl.appendChild(tbl);
  };
