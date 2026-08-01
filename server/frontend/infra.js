// infra.js — auto-split from app.js
'use strict';


  UI.loadStations = async function() {
    var res = await API.get('/api/admin/stations');
    if (!res.ok) return U.toast('加载失败', 'error');
    UI._allStations = res.data.data || [];
    State.stations = UI._allStations;  // 同步给依赖 State.stations 的旧代码使用
    UI.renderStations();
  };

  UI.renderStations = function() {
    var listEl = U.$('stations-list');
    listEl.innerHTML = '';
    var stations = UI._allStations;
    // 筛选
    var search = (U.$('station-search') ? U.$('station-search').value : '').trim().toLowerCase();
    var typeFilter = U.$('station-type-filter') ? U.$('station-type-filter').value : '';
    if (search || typeFilter) {
      stations = stations.filter(function(s) {
        if (search && s.name.toLowerCase().indexOf(search) < 0 && s.city.toLowerCase().indexOf(search) < 0)
          return false;
        if (typeFilter && s.type !== typeFilter)
          return false;
        return true;
      });
    }
    if (!stations.length) {
      listEl.innerHTML = '<div class="loading">暂无站点</div>';
      return;
    }
    var tbl = document.createElement('table');
    tbl.className = 'users-table';
    tbl.innerHTML = '<thead><tr><th>ID</th><th>站名</th><th>城市</th><th>类型</th><th>操作</th></tr></thead>';
    var tbody = document.createElement('tbody');
    var typeLabels = {HIGH_SPEED:'高铁站',NORMAL:'普速站',HUB:'枢纽站'};
    for (var i = 0; i < stations.length; i++) {
      var s = stations[i];
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>' + s.id + '</td>'
        + '<td style="font-weight:600;color:#e0e0e0">' + U.esc(s.name) + '</td>'
        + '<td>' + U.esc(s.city) + '</td>'
        + '<td>' + (typeLabels[s.type] || s.type) + '</td>'
        + '<td class="user-actions"><button class="btn btn-sm btn-primary">编辑</button>'
        + '<button class="btn btn-sm btn-danger">删除</button></td>';
      var btns = tr.querySelectorAll('button');
      btns[0].onclick = (function(id){return function(){UI.editStation(id);};})(s.id);
      btns[1].onclick = (function(id,name){return function(){UI.deleteStation(id,name);};})(s.id, s.name);
      tbody.appendChild(tr);
    }
    tbl.appendChild(tbody);
    listEl.appendChild(tbl);
  };

  UI.showStationForm = function(station) {
    var h = '<h3>' + (station ? '编辑站点' : '新增站点') + '</h3>'
      + '<div class="form-group"><label>站名</label><input id="sf-name" class="input" value="' + (station ? U.esc(station.name) : '') + '"></div>'
      + '<div class="form-group"><label>城市</label><input id="sf-city" class="input" value="' + (station ? U.esc(station.city) : '') + '"></div>'
      + '<div class="form-group"><label>类型</label><select id="sf-type" class="input">'
      + '<option value="HIGH_SPEED"' + (station && station.type==='HIGH_SPEED'?' selected':'') + '>高铁站</option>'
      + '<option value="NORMAL"' + (station && station.type==='NORMAL'?' selected':'') + '>普速站</option>'
      + '<option value="HUB"' + (station && station.type==='HUB'?' selected':'') + '>枢纽站</option></select></div>'
      + '<div class="form-group"><label>纬度</label><input id="sf-lat" class="input" type="number" step="0.0001" value="' + (station ? station.latitude : '') + '"></div>'
      + '<div class="form-group"><label>经度</label><input id="sf-lng" class="input" type="number" step="0.0001" value="' + (station ? station.longitude : '') + '"></div>'
      + '<div style="display:flex;gap:8px;margin-top:16px"><button class="btn btn-primary" onclick="UI.saveStation(' + (station ? station.id : 0) + ')">保存</button>'
      + '<button class="btn" onclick="UI.closeModal()">取消</button></div>';
    UI._showModal(h);
  };

  UI.saveStation = async function(id) {
    var body = {
      id: 0,
      name: U.$('sf-name').value.trim(),
      city: U.$('sf-city').value.trim(),
      type: U.$('sf-type').value,
      latitude: parseFloat(U.$('sf-lat').value) || 0,
      longitude: parseFloat(U.$('sf-lng').value) || 0
    };
    if (!body.name) return U.toast('站名不能为空', 'error');
    var res = id ? await API.put('/api/admin/stations/' + id, body) : await API.post('/api/admin/stations', body);
    if (res.ok) {
      U.toast(id ? '已更新' : '已创建', 'success');
      UI.closeModal();
      UI.loadStations();
    } else {
      U.toast((res.data && res.data.error) || '操作失败', 'error');
    }
  };

  UI.editStation = function(id) {
    for (var i = 0; i < UI._allStations.length; i++)
      if (UI._allStations[i].id === id) {
        UI.showStationForm(UI._allStations[i]);
        return;
      }
  };

  UI.deleteStation = async function(id, name) {
    if (!confirm('确定删除站点 ' + name + '？')) return;
    var res = await API.del('/api/admin/stations/' + id);
    if (res.ok) {
      U.toast('已删除', 'success');
      UI.loadStations();
    } else {
      U.toast((res.data && res.data.error) || '删除失败', 'error');
    }
  };

  UI.loadLines = async function() {
    var res = await API.get('/api/admin/lines');
    if (!res.ok) return U.toast('加载失败', 'error');
    UI._allLines = res.data.data || [];
    UI.renderLines();
  };

  UI.renderLines = function() {
    var listEl = U.$('lines-list');
    listEl.innerHTML = '';
    var lines = UI._allLines;
    // 筛选
    var search = (U.$('line-search') ? U.$('line-search').value : '').trim().toLowerCase();
    var typeFilter = U.$('line-type-filter') ? U.$('line-type-filter').value : '';
    if (search || typeFilter) {
      lines = lines.filter(function(l) {
        if (search && l.name.toLowerCase().indexOf(search) < 0)
          return false;
        if (typeFilter && l.type !== typeFilter)
          return false;
        return true;
      });
    }
    if (!lines.length) {
      listEl.innerHTML = '<div class="loading">暂无线路</div>';
      return;
    }
    var tbl = document.createElement('table');
    tbl.className = 'users-table';
    tbl.innerHTML = '<thead><tr><th>ID</th><th>名称</th><th>类型</th><th>时速(km/h)</th><th>车站数</th><th>操作</th></tr></thead>';
    var tbody = document.createElement('tbody');
    var typeLabels = {HIGH_SPEED:'高铁',EXPRESS:'快铁',NORMAL:'普铁',INTERCITY:'城际'};
    for (var i = 0; i < lines.length; i++) {
      var l = lines[i];
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>' + l.id + '</td>'
        + '<td style="font-weight:600;color:#e0e0e0">' + U.esc(l.name) + '</td>'
        + '<td>' + (typeLabels[l.type] || l.type) + '</td>'
        + '<td>' + (l.max_speed_kmh || 0) + '</td>'
        + '<td>' + (l.stations ? l.stations.length : 0) + '</td>'
        + '<td class="user-actions"><button class="btn btn-sm btn-primary">编辑</button>'
        + '<button class="btn btn-sm btn-danger">删除</button></td>';
      var btns = tr.querySelectorAll('button');
      btns[0].onclick = (function(id){return function(){UI.editLine(id);};})(l.id);
      btns[1].onclick = (function(id,name){return function(){UI.deleteLine(id,name);};})(l.id, l.name);
      tbody.appendChild(tr);
    }
    tbl.appendChild(tbody);
    listEl.appendChild(tbl);
  };

  UI.showLineForm = async function(line) {
    // 确保站点列表已加载（用于城市名校验）
    if (!UI._allStations || !UI._allStations.length) {
      var sr = await API.get('/api/admin/stations');
      if (sr.ok)
        UI._allStations = sr.data.data || [];
    }
    // 城市名 → 站名（取每个城市第一个站）
    var cityToName = {};
    for (var si = 0; si < (UI._allStations || []).length; si++) {
      var st = UI._allStations[si];
      if (!cityToName[st.city])
        cityToName[st.city] = st.name;
    }
    var cities = line ? (line.stations || []).slice() : [''];
    for (var ci = 0; ci < cities.length; ci++)
      cities[ci] = cityToName[cities[ci]] || cities[ci];  // 城市名→站名
    var title = line ? '编辑线路' : '新增线路';
    var editingId = line ? line.id : 0;

    var h = '<h3>' + title + '</h3>'
      + '<div class="form-group"><label>线路名称</label><input id="lf-name" class="input" value="' + (line ? U.esc(line.name) : '') + '"></div>'
      + '<div class="form-group"><label>类型</label><select id="lf-type" class="input">'
      + '<option value="HIGH_SPEED"' + (line && line.type === 'HIGH_SPEED' ? ' selected' : '') + '>高铁</option>'
      + '<option value="EXPRESS"' + (line && line.type === 'EXPRESS' ? ' selected' : '') + '>快铁</option>'
      + '<option value="NORMAL"' + (line && line.type === 'NORMAL' ? ' selected' : '') + '>普铁</option>'
      + '<option value="INTERCITY"' + (line && line.type === 'INTERCITY' ? ' selected' : '') + '>城际</option></select></div>'
      + '<div class="form-group"><label>设计时速（km/h）</label><input id="lf-speed" class="input" type="number" value="' + (line ? line.max_speed_kmh : '') + '"></div>'
      + '<div class="form-group"><label>途经车站</label>'
      + '<div id="lf-city-list" style="display:flex;flex-direction:column;gap:6px"></div>'
      + '</div>'
      + '<div id="lf-error" class="error-msg"></div>'
      + '<div style="display:flex;gap:8px;margin-top:16px">'
      + '<button class="btn btn-primary" onclick="UI._lfSave(' + editingId + ')">保存</button>'
      + '<button class="btn" onclick="UI.closeModal()">取消</button></div>';
    // 建立站名 datalist
    var dl = document.createElement('datalist');
    dl.id = 'station-suggest-list';
    var namesSet = {};
    for (var ci = 0; ci < (UI._allStations || []).length; ci++)
      namesSet[UI._allStations[ci].name] = true;
    for (var sn in namesSet)
      dl.innerHTML += '<option value="' + U.esc(sn) + '">';
    UI._showModal(h);
    U.$('lf-city-list').parentNode.insertBefore(dl, U.$('lf-city-list'));

    // 初始化已有车站
    for (var i = 0; i < cities.length; i++) {
      var row = UI._lfAddRow(i);
      row.querySelector('input').value = cities[i];
      if (cities[i])
        UI._lfCheckStation(row.querySelector('input'));
    }
    UI._lfUpdateRoles();
  };

  UI._lfAddRow = function(idx) {
    var list = U.$('lf-city-list');
    var div = document.createElement('div');
    div.style.cssText = 'display:flex;gap:6px;align-items:center';
    var role = document.createElement('span');
    role.className = 'lf-role';
    role.style.cssText = 'font-size:11px;color:#707090;min-width:40px';
    role.textContent = idx === 0 ? '起点' : '途经';
    var inp = document.createElement('input');
    inp.className = 'input';
    inp.placeholder = '车站名';
    inp.setAttribute('autocomplete', 'off');
    inp.setAttribute('list', 'station-suggest-list');
    inp.style.cssText = 'flex:1';
    inp.oninput = function() { UI._lfCheckStation(inp); };
    var addBtn = document.createElement('button');
    addBtn.className = 'btn btn-sm';
    addBtn.style.cssText = 'background:#005530;color:#00ff88;border-color:#00ff88';
    addBtn.textContent = '+';
    addBtn.title = '在下方插入一站';
    addBtn.onclick = function() {
      var next = div.nextSibling;
      var newRow = UI._lfAddRow(-1);
      if (next)
        list.insertBefore(newRow, next);
      else
        list.appendChild(newRow);
      UI._lfUpdateRoles();
    };
    var delBtn = document.createElement('button');
    delBtn.className = 'btn btn-sm btn-danger';
    delBtn.textContent = '✕';
    delBtn.title = '删除此站';
    delBtn.onclick = function() {
      div.remove();
      UI._lfUpdateRoles();
    };
    div.appendChild(role);
    div.appendChild(inp);
    div.appendChild(addBtn);
    div.appendChild(delBtn);
    list.appendChild(div);
    return div;
  };

  UI._lfCheckStation = function(inp) {
    var name = inp.value.trim();
    if (!name) {
      inp.style.borderColor = '#0f3460';
      return;
    }
    var found = false;
    var stations = UI._allStations || [];
    for (var i = 0; i < stations.length; i++) {
      if (stations[i].name === name) {
        found = true;
        break;
      }
    }
    inp.style.borderColor = found ? '#00ff88' : '#ff4444';
  };

  UI._lfUpdateRoles = function() {
    var rows = U.$('lf-city-list').querySelectorAll('div');
    if (rows.length === 0)
      return;
    rows[0].querySelector('.lf-role').textContent = '起点';
    for (var i = 1; i < rows.length; i++)
      rows[i].querySelector('.lf-role').textContent = i === rows.length - 1 ? '终点' : '途经';
  };

  UI._lfSave = async function(id) {
    var name = (U.$('lf-name').value || '').trim();
    if (!name)
      return U.toast('线路名称不能为空', 'error');
    var type = U.$('lf-type').value;
    var speed = parseInt(U.$('lf-speed').value) || 0;
    var rows = U.$('lf-city-list').querySelectorAll('div');
    var cities = [];
    // 站名 → 城市名 映射
    var nameToCity = {};
    for (var si = 0; si < (UI._allStations || []).length; si++)
      nameToCity[UI._allStations[si].name] = UI._allStations[si].city;
    for (var i = 0; i < rows.length; i++) {
      var val = (rows[i].querySelector('input').value || '').trim();
      if (!val)
        return U.toast('站点名不能为空', 'error');
      cities.push(nameToCity[val] || val);  // 有映射用城市名，否则保持原值
    }
    if (cities.length < 2)
      return U.toast('至少需要起点和终点两个站点', 'error');

    var body = {
      id: 0,
      name: name,
      type: type,
      stations: cities,
      max_speed_kmh: speed
    };
    var res = id
      ? await API.put('/api/admin/lines/' + id, body)
      : await API.post('/api/admin/lines', body);
    if (res.ok) {
      U.toast(id ? '已更新' : '已创建', 'success');
      UI.closeModal();
      UI.loadLines();
    } else {
      U.toast((res.data && res.data.error) || '操作失败', 'error');
    }
  };

  UI.editLine = function(id) {
    for (var i = 0; i < UI._allLines.length; i++)
      if (UI._allLines[i].id === id) {
        UI.showLineForm(UI._allLines[i]);
        return;
      }
  };

  UI.deleteLine = async function(id, name) {
    if (!confirm('确定删除线路 ' + name + '？')) return;
    var res = await API.del('/api/admin/lines/' + id);
    if (res.ok) {
      U.toast('已删除', 'success');
      UI.loadLines();
    } else {
      U.toast((res.data && res.data.error) || '删除失败', 'error');
    }
  };

  UI.loadNetwork = async function() {
    var container = U.$('network-container');
    if (!container || container._loaded) return;
    container._loaded = true;

    // 图例
    var legEl = U.$('network-legend');
    if (legEl) 
      legEl.innerHTML = '<div style="display:flex;gap:16px">'
      + '<span style="color:#44aaff">⬤ 枢纽站</span>'
      + '<span style="color:#ff4444">⬤ 高铁站</span>'
      + '<span style="color:#44ff44">⬤ 普速站</span></div>'
      + '<div style="display:flex;gap:16px"><span style="color:#ff6666">━ 高铁线路</span>'
      + '<span style="color:#ffaa00">━ 快铁线路</span>'
      + '<span style="color:#66ff66">━ 普铁线路</span>'
      + '<span style="color:#66bbff">━ 城际线路</span></div>';

    try {
      var sr = await API.get('/api/admin/stations');
      var lr = await API.get('/api/admin/lines');
      if (!sr.ok || !lr.ok) {
        container.innerHTML = '<div class="loading">加载失败</div>';
        return;
      }
      var stations = sr.data.data || [];
      var lines = lr.data.data || [];
    } catch(e) {
      container.innerHTML = '<div class="loading">加载失败</div>';
      return;
    }

    // 经纬度投影
    var minLat=90,maxLat=-90,minLng=180,maxLng=-180;
    stations.forEach(function(s){
      minLat=Math.min(minLat,s.latitude);
      maxLat=Math.max(maxLat,s.latitude);
      minLng=Math.min(minLng,s.longitude);
      maxLng=Math.max(maxLng,s.longitude)
    });
    var pad=40, lngR=maxLng-minLng||0.5, latR=maxLat-minLat||0.3;
    var W=container.clientWidth, H=container.clientHeight;
    var sc=Math.min((W-2*pad)/lngR,(H-2*pad)/latR)||1;
    var ox=(W-lngR*sc)/2, oy=(H-latR*sc)/2;
    function toX(lng){return ox+(lng-minLng)*sc}
    function toY(lat){return oy+(maxLat-lat)*sc}

    var NODES = stations.map(function(s) {
      return {
        id: s.id, name: s.name, city: s.city,
        type: s.type === 'HIGH_SPEED' ? 0 : s.type === 'HUB' ? 2 : 1,
        x: toX(s.longitude), y: toY(s.latitude),
        lng: s.longitude, lat: s.latitude
      };
    });
    var EDGES = [];
    lines.forEach(function(l) {
      for (var i = 0; i + 1 < l.stations.length; i++)
        EDGES.push({
          from: l.stations[i],
          to: l.stations[i + 1],
          line_name: l.name,
          type: l.type === 'HIGH_SPEED' ? 0 : l.type === 'INTERCITY' ? 2 : l.type === 'EXPRESS' ? 3 : 1
        });
    });
    var nodeById = {};
    NODES.forEach(function(n) {
      nodeById[n.id] = n;
      if (n.city)
        nodeById[n.city] = n;
    });

    var tc = ['#ff4444', '#44ff44', '#44aaff'];
    var ec = ['#ff6666', '#66ff66', '#66bbff', '#ffaa00'];
    var tp = [1, 2, 0];

    var svg=document.createElementNS('http://www.w3.org/2000/svg','svg');
    svg.setAttribute('viewBox','0 0 '+W+' '+H);
    svg.style.cssText='display:block;width:100%;height:100%;pointer-events:auto;user-select:none;-webkit-user-select:none';

    var info=document.createElement('div');
    info.style.cssText='position:absolute;top:8px;right:8px;background:rgba(22,33,62,0.95);'
    + 'border:1px solid #0f3460;border-radius:8px;padding:12px 16px;'
    + 'display:none;font-size:13px;z-index:10;pointer-events:none';
    container.appendChild(info);

    // 缩放层（站点/线路随缩放变化）+ 文字层（固定大小）
    var zG=document.createElementNS('http://www.w3.org/2000/svg','g');
    var eG=document.createElementNS('http://www.w3.org/2000/svg','g');
    var nG=document.createElementNS('http://www.w3.org/2000/svg','g');
    var tG=document.createElementNS('http://www.w3.org/2000/svg','g');
    zG.appendChild(eG);
    zG.appendChild(nG);
    svg.appendChild(zG);
    svg.appendChild(tG);
    container.appendChild(svg);
    var scl=1,tx=0,ty=0;

    // 平移时只需更新文字层位置，无需重建边/节点，缓存上次完整渲染结果
    var _panOnly = false;
    var _cachedHidden = new Set();
    var _cachedFEdges = [];
    var _cachedEdgeCount = {};

    function getFilteredEdges() {
      var checks = document.querySelectorAll('.net-filter-line');
      var mask = {};
      for (var ci = 0; ci < checks.length; ci++)
        mask[parseInt(checks[ci].value)] = checks[ci].checked;
      return EDGES.filter(function(e) { return mask[e.type]; });
    }

    function update(){
      zG.setAttribute('transform','translate('+tx+','+ty+') scale('+scl+')');
      if (_panOnly) {
        // 仅平移：跳过边/节点重建，只更新文字层位置
        renderT(_cachedHidden, _cachedFEdges, _cachedEdgeCount);
        _panOnly = false;
      } else {
        renderN();
      }
    }

    var rafId=null;
    function scheduleUpdate() {
      if (!rafId)
        rafId = requestAnimationFrame(function() {
          rafId = null;
          update();
        });
    }

    var drag=false,dx=0,dy=0;
    svg.addEventListener('mousedown', function(e) {
      drag = true;
      dx = e.clientX - tx;
      dy = e.clientY - ty;
      svg.style.cursor = 'grabbing';
    });
    window.addEventListener('mousemove', function(e) {
      if (!drag)
        return;
      tx = e.clientX - dx;
      ty = e.clientY - dy;
      _panOnly = true;
      scheduleUpdate();
    });
    window.addEventListener('mouseup', function() {
      drag = false;
      svg.style.cursor = '';
    });

    svg.addEventListener('wheel', function(e) {
      e.preventDefault();
      var mx = e.clientX - svg.getBoundingClientRect().left;
      var my = e.clientY - svg.getBoundingClientRect().top;
      var os = scl;
      scl = Math.max(0.1, Math.min(30, scl * (e.deltaY < 0 ? 1.15 : 0.87)));
      var r = scl / os;
      tx = mx - r * (mx - tx);
      ty = my - r * (my - ty);
      scheduleUpdate();
    });

    function ek(e){return e.from<e.to?e.from+'-'+e.to:e.to+'-'+e.from}
    function isClose(a,b,th){var dx=(a.x-b.x)*scl,dy=(a.y-b.y)*scl;return Math.sqrt(dx*dx+dy*dy)<th}

    /** 计算边的方向法向量，edge 和 label 渲染共用，避免两处独立计算不同步 */
    function edgeNormal(k) {
      var canonical = k.split('-');
      var a = nodeById[canonical[0]];
      var b = nodeById[canonical[1]];
      var ddx = b.x - a.x;
      var ddy = b.y - a.y;
      var len = Math.sqrt(ddx * ddx + ddy * ddy) || 1;
      return { nx: -ddy / len, ny: ddx / len, len: len };
    }

    /** 计算平行边曲线偏移量：与边长成正比，短边弯度小，避免近站点间弧线过尖锐 */
    function curveOffset(len, t2, idx) {
      var off = Math.min(len * 0.3, 18);
      if (t2 > 1 && off < 3)
        off = 3;  // 多线路时保留最小间距，避免曲线重合
      return (idx - (t2 + 1) / 2) * off;
    }

    function renderN() {
      nG.innerHTML = '';
      eG.innerHTML = '';
      var fEdges = getFilteredEdges();
      var edgeCount = {};
      fEdges.forEach(function(e) { var k = ek(e); edgeCount[k] = (edgeCount[k] || 0) + 1; });
      var th = 30 / scl;
      var hidden = new Set();
      var r = 5 / scl;
      var sw = 1.5 / scl;

      // 网格索引加速聚类
      var cell = {};
      var cSize = Math.max(th, 10);
      for (var i = 0; i < NODES.length; i++) {
        var cx = Math.floor(NODES[i].x / cSize);
        var cy = Math.floor(NODES[i].y / cSize);
        var ck = cx + ',' + cy;
        if (!cell[ck])
          cell[ck] = [];
        cell[ck].push(i);
      }
      for (var ck in cell) {
        var parts = ck.split(',');
        var cx = +parts[0];
        var cy = +parts[1];
        for (var dx = -1; dx <= 1; dx++) {
          for (var dy = -1; dy <= 1; dy++) {
            var nk = (cx + dx) + ',' + (cy + dy);
            if (!cell[nk])
              continue;
            for (var ai = 0; ai < cell[ck].length; ai++) {
              var i = cell[ck][ai];
              if (hidden.has(i))
                continue;
              for (var bi = 0; bi < cell[nk].length; bi++) {
                var j = cell[nk][bi];
                if (j <= i || hidden.has(j))
                  continue;
                if (isClose(NODES[i], NODES[j], th)) {
                  if (tp[NODES[i].type] <= tp[NODES[j].type])
                    hidden.add(j);
                  else
                    hidden.add(i);
                }
              }
            }
          }
        }
      }

      // 边
      var eOff = {};
      fEdges.forEach(function(e, i) {
        var k = ek(e);
        var t2 = edgeCount[k] || 1;
        var idx = eOff[k] = (eOff[k] || 0) + 1;
        var a = nodeById[e.from];
        var b = nodeById[e.to];
        if (!a || !b)
          return;
        var mx = (a.x + b.x) / 2;
        var my = (a.y + b.y) / 2;
        var geo = edgeNormal(k);
        var off = curveOffset(geo.len, t2, idx);
        var cx = mx + geo.nx * off;
        var cy = my + geo.ny * off;
        var p = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        p.setAttribute('d', 'M' + a.x + ',' + a.y + ' Q' + cx + ',' + cy + ' ' + b.x + ',' + b.y);
        p.setAttribute('fill', 'none');
        p.setAttribute('stroke', ec[e.type] || '#66ff66');
        p.setAttribute('stroke-width', sw.toFixed(2));
        p.setAttribute('opacity', '0.6');
        eG.appendChild(p);
      });

      // 站点圆点
      for (var i = 0; i < NODES.length; i++) {
        if (hidden.has(i))
          continue;
        (function(n) {
          var c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
          c.setAttribute('cx', n.x);
          c.setAttribute('cy', n.y);
          c.setAttribute('r', r.toFixed(2));
          c.setAttribute('fill', tc[n.type]);
          c.setAttribute('stroke', tc[n.type]);
          c.style.cursor = 'pointer';
          c.onmouseenter = function() {
            info.innerHTML = '<h3 style="margin:0 0 8px;color:#53a8ff">' + n.name + '</h3><p style="margin:4px 0;color:#9090b0;font-size:12px">城市: ' + n.city + '</p>';
            info.style.display = 'block';
          };
          c.onmouseleave = function() { info.style.display = 'none'; };
          nG.appendChild(c);
        })(NODES[i]);
      }

      // 缓存本次渲染结果，供后续平移时复用（避免重建边/节点）
      _cachedHidden = hidden;
      _cachedFEdges = fEdges;
      _cachedEdgeCount = edgeCount;
      renderT(hidden, fEdges, edgeCount);
    }

    function renderT(hidden, fEdges, edgeCount) {
      tG.innerHTML = '';
      // 站名
      for (var i = 0; i < NODES.length; i++) {
        if (hidden.has(i))
          continue;
        var n = NODES[i];
        var t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        t.setAttribute('x', n.x * scl + tx);
        t.setAttribute('y', n.y * scl + ty + 14);
        t.setAttribute('fill', '#e0e0e0');
        t.setAttribute('font-size', '11');
        t.setAttribute('text-anchor', 'middle');
        t.textContent = n.name;
        tG.appendChild(t);
      }

      // 线路标签重叠检测
      var eh = new Set();
      for (var i = 0; i < fEdges.length; i++) {
        if (eh.has(i))
          continue;
        for (var j = i + 1; j < fEdges.length; j++) {
          if (eh.has(j))
            continue;
          var a = nodeById[fEdges[i].from];
          var b = nodeById[fEdges[i].to];
          var c2 = nodeById[fEdges[j].from];
          var d = nodeById[fEdges[j].to];
          if (!a || !b || !c2 || !d)
            continue;
          var k1 = ek(fEdges[i]);
          var k2 = ek(fEdges[j]);
          if (k1 === k2)
            continue;
          var mx1 = (a.x + b.x) / 2 * scl + tx;
          var my1 = (a.y + b.y) / 2 * scl + ty;
          var mx2 = (c2.x + d.x) / 2 * scl + tx;
          var my2 = (c2.y + d.y) / 2 * scl + ty;
          if (Math.sqrt((mx1 - mx2) * (mx1 - mx2) + (my1 - my2) * (my1 - my2)) < 80) {
            eh.add(j);
          }
        }
      }

      // 线路标签
      var elOff = {};
      fEdges.forEach(function(e, i) {
        if (eh.has(i))
          return;
        var k = ek(e);
        var t2 = edgeCount[k] || 1;
        var idx = elOff[k] = (elOff[k] || 0) + 1;
        var a = nodeById[e.from];
        var b = nodeById[e.to];
        if (!a || !b)
          return;
        var mx = (a.x + b.x) / 2;
        var my = (a.y + b.y) / 2;
        var geo = edgeNormal(k);
        var off2 = curveOffset(geo.len, t2, idx);
        var t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        t.setAttribute('x', (mx + geo.nx * off2) * scl + tx);
        t.setAttribute('y', (my + geo.ny * off2) * scl + ty - 6);
        t.setAttribute('fill', '#9090b0');
        t.setAttribute('font-size', '12');
        t.setAttribute('text-anchor', 'middle');
        t.textContent = e.line_name;
        tG.appendChild(t);
      });
    }
    UI._renderNetworkMap = renderN;
    renderN();

  };
