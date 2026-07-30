// staff.js — auto-split from app.js
'use strict';


  UI.showAddTrainForm = function() {
    // 先加载邻居索引（新增和编辑都需要）
    var needIndex = (Object.keys(State._neighborIndex).length === 0);
    if (needIndex) {
      UI.loadNeighborIndex().then(function() { UI._showAddTrainFormImpl(); });
    } else {
      UI._showAddTrainFormImpl();
    }
  };

  UI._showAddTrainFormImpl = function() {
    U.$('add-train-error').textContent = '';
    // 填充站名 datalist
    var html = '';
    for (var i = 0; i < State.stations.length; i++) {
      html += '<option value="' + U.esc(State.stations[i].name) + '">';
    }
    U.$('train-station-datalist').innerHTML = html;
    var minDate = new Date();
    minDate.setDate(minDate.getDate() + 3);  // 新增列车须 ≥3 天
    var minStr = minDate.toISOString().slice(0,10);
    U.$('new-train-valid-from').setAttribute('min', minStr);
    U.$('new-train-range-from').setAttribute('min', minStr);

    // ── 统一重置为新增模式（编辑模式随后填充）──
    State._routePath = [];
    U.$('route-path-list').innerHTML = '';
    U.$('neighbor-panel').style.display = 'none';
    U.$('stop-config').style.display = 'none';
    U.$('speed-limit-hint').style.display = 'none';
    U.$('first-station-row').style.display = '';
    U.$('train-date-single').style.display = '';
    U.$('train-date-range').style.display = 'none';
    // 清空表单
    U.$('new-train-prefix').value = '';
    U.$('new-train-prefix').disabled = false;
    U.$('new-train-number').value = '';
    U.$('new-train-number').disabled = false;
    U.$('new-train-type').value = '0';
    U.$('first-station-input').value = '';
    U.$('first-depart-time').value = '';
    U.$('new-train-valid-from').value = '';
    U.$('new-train-range-from').value = '';
    U.$('new-train-range-to').value = '';
    U.$('sc-business').value = 0;
    U.$('sc-first').value = 50;
    U.$('sc-second').value = 100;
    U.$('sc-sleeper').value = 0;
    U.$('sc-hseat').value = 0;
    U.$('sc-noseat').value = 50;
    U.$('page-add-train').querySelector('h2').textContent = '新增列车';

    // 编辑模式：在统一界面上填入已有数据
    if (State._editingTrainId)
      UI._populateEditForm();
  };

  UI.loadNeighborIndex = async function() {
    var res = await API.get('/api/stations/neighbors');
    if (res.ok && res.data && res.data.data) {
      State._neighborIndex = res.data.data;
    }
  };

  UI._buildRoutePathFromTrain = function(train) {
    var segments = train.segments || [];
    var stops = train.stops || [];
    if (!segments.length)
      return;

    // 构建办客站 ID 集合（用于判断 is_stop）
    var stopIds = {};
    for (var i = 0; i < stops.length; i++) {
      stopIds[stops[i].station_id] = true;
    }

    var path = [];
    for (var i = 0; i < segments.length; i++) {
      var seg = segments[i];
      if (i === 0) {
        path.push({
          station_id: seg.from_station,
          station_name: UI._stationName(seg.from_station, ''),
          line_id: 0, line_name: '',
          arrival: -1,
          departure: seg.enter_time,
          is_stop: true,
          is_terminal: false,
          distance_km: 0,
          speed_kmh: 0
        });
      }
      var isLastSeg = (i === segments.length - 1);
      path.push({
        station_id: seg.to_station,
        station_name: UI._stationName(seg.to_station, ''),
        line_id: seg.line_id || 0,
        line_name: UI._lineName(seg.line_id),
        arrival: seg.leave_time,
        departure: isLastSeg ? -1 : 0,  // 非末站由外层填充
        is_stop: !!stopIds[seg.to_station],
        is_terminal: false,  // 不标终点，允许继续编辑
        distance_km: seg.distance_km || 0,
        speed_kmh: seg.speed_kmh || 0
      });
    }
    // 填充中间站的发车时间（= 下一段的 enter_time）
    for (var i = 0; i < path.length - 1; i++) {
      if (path[i].departure <= 0)
        path[i].departure = segments[i].enter_time;
    }
    State._routePath = path;
  };

  UI._populateEditForm = function() {
    var trainId = State._editingTrainId;
    var train = null;
    for (var i = 0; i < State._allTrains.length; i++) {
      if (State._allTrains[i].id === trainId) {
        train = State._allTrains[i];
        break;
      }
    }
    if (!train) {
      U.toast('列车数据未找到', 'error');
      return;
    }

    // 车次（编辑模式不可改）
    U.$('new-train-prefix').value = train.id[0] || '';
    U.$('new-train-prefix').disabled = true;
    UI.onPrefixChange();
    U.$('new-train-number').value = train.id.substring(1);
    U.$('new-train-number').disabled = true;

    // 类型 + 日期（修改须 ≥15 天，覆盖新增的 +3 天）
    U.$('new-train-type').value = String(train.type || 0);
    UI.onTrainTypeChange();
    if (train.type === 1) {
      U.$('new-train-range-from').value = train.valid_from || '';
      U.$('new-train-range-to').value = train.valid_until || '';
    } else {
      U.$('new-train-valid-from').value = train.valid_from || '';
    }
    var min15 = new Date();
    min15.setDate(min15.getDate() + 15);
    var min15Str = min15.toISOString().slice(0,10);
    U.$('new-train-valid-from').setAttribute('min', min15Str);
    U.$('new-train-range-from').setAttribute('min', min15Str);

    // 席位
    var sc = train.seat_config || {};
    U.$('sc-business').value = sc.business_seats || 0;
    U.$('sc-first').value = sc.first_seats || 0;
    U.$('sc-second').value = sc.second_seats || 0;
    U.$('sc-sleeper').value = sc.hard_sleeper || 0;
    U.$('sc-hseat').value = sc.hard_seat || 0;
    U.$('sc-noseat').value = sc.no_seat || 0;

    // 重建路径 + 预填始发站
    UI._buildRoutePathFromTrain(train);
    UI.renderRoutePath();
    if (State._routePath.length > 0) {
      var origin = State._routePath[0];
      U.$('first-station-input').value = origin.station_name || '';
      var dep = origin.departure;
      if (dep > 0) {
        var hh = String(Math.floor(dep / 100));
        if (hh.length < 2) hh = '0' + hh;
        var mm = String(dep % 100);
        if (mm.length < 2) mm = '0' + mm;
        U.$('first-depart-time').value = hh + ':' + mm;
      }
      var last = State._routePath[State._routePath.length - 1];
      UI.showNeighbors(last.station_id);
    }

    U.$('page-add-train').querySelector('h2').textContent = '修改列车 ' + trainId;
  };

  UI.editTrain = function(trainId) {
    State._editingTrainId = trainId;
    UI.navTo('add-train');
  };

  UI.tryShowNeighbors = function() {
    var name = (U.$('first-station-input') || {}).value || '';
    var depTime = (U.$('first-depart-time') || {}).value || '';
    if (!name || !depTime)
      return;
    UI.onFirstStationChange();
  };

  UI.onFirstStationChange = function() {
    var name = (U.$('first-station-input') || {}).value || '';
    var depTime = (U.$('first-depart-time') || {}).value || '';
    if (!name || !depTime)
      return;
    var sid = 0;
    for (var i = 0; i < State.stations.length; i++) {
      if (State.stations[i].name === name) {
        sid = State.stations[i].id;
        break;
      }
    }
    if (!sid)
      return;
    State._routePath = [{ station_id: sid, station_name: name, departure: UI._toHHMM(depTime), is_stop: true, is_terminal: false }];
    UI.renderRoutePath();
    UI.showNeighbors(sid);
  };

  UI.showNeighbors = function(stationId) {
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
  };

  UI.selectNeighbor = function(idx, fromStationId) {
    var neighbors = (State._neighborIndex[String(fromStationId)] || []);
    var n = neighbors[idx];
    if (!n)
      return;
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
  };

  UI.onStopToggle = function() {
    var isStop = (U.$('stop-is-stop') || {}).checked;
    U.$('stop-time-fields').style.display = isStop ? '' : 'none';
    U.$('pass-time-field').style.display = isStop ? 'none' : '';
    U.$('stop-speed-info').textContent = '';
  };

  UI.computeSpeed = function() {
    var n = State._pendingNeighbor;
    if (!n)
      return;
    var prev = State._routePath[State._routePath.length - 1];
    if (!prev || !prev.departure)
      return;
    var isStop = (U.$('stop-is-stop') || {}).checked;
    var arrTime;
    if (isStop) {
      arrTime = (U.$('stop-arrival-time') || {}).value || '';
    } else {
      arrTime = (U.$('stop-pass-time') || {}).value || '';
    }
    if (!arrTime) {
      U.$('stop-speed-info').textContent = '';
      return;
    }
    var curArr = UI._toHHMM(arrTime);
    if (curArr <= prev.departure) {
      U.$('stop-speed-info').textContent = '到达须晚于上一站发车';
      return;
    }
    var prevMin = Math.floor(prev.departure/100)*60 + prev.departure%100;
    var curMin = Math.floor(curArr/100)*60 + curArr%100;
    var mins = curMin - prevMin;
    if (mins <= 0)
      return;
    var speed = (n.distance_km / mins) * 60;
    var prefix = (U.$('new-train-prefix') || {}).value || '';
    var trainMax = UI._speedLimits[prefix] || 999;
    var lineMax = n.max_speed_kmh || 999;
    var limit = Math.min(trainMax, lineMax);
    var ok = speed <= limit;
    U.$('stop-speed-info').textContent = '时速 ' + Math.round(speed) + ' km/h（限速 ' + limit + ' km/h）';
    U.$('stop-speed-info').style.color = ok ? '#00ff88' : '#ff4444';
  };

  UI.confirmStop = function() {
    var n = State._pendingNeighbor;
    if (!n)
      return;
    var isStop = (U.$('stop-is-stop') || {}).checked;
    var arrTime, depTime;
    if (isStop) {
      arrTime = (U.$('stop-arrival-time') || {}).value || '';
      depTime = (U.$('stop-depart-time') || {}).value || '';
      if (!arrTime || !depTime) {
        U.toast('请填写到站和发车时间', 'error');
        return;
      }
    } else {
      arrTime = (U.$('stop-pass-time') || {}).value || '';
      depTime = arrTime;
      if (!arrTime) {
        U.toast('请填写通过时间', 'error');
        return;
      }
    }
    var prev = State._routePath[State._routePath.length - 1];
    var curArr = UI._toHHMM(arrTime);
    if (curArr <= (prev.departure || 0)) {
      U.toast('到达须晚于上一站发车', 'error');
      return;
    }
    if (isStop && UI._toHHMM(depTime)
      <= curArr) {
      U.toast('发车须晚于到站', 'error');
      return;
    }

    // 预计算时速（和 segments 的 speed_kmh 同构）
    var segSpeed = 0;
    if (prev.departure > 0 && curArr > 0 && n.distance_km > 0) {
      var dm = Math.floor(prev.departure/100)*60 + (prev.departure%100);
      var am = Math.floor(curArr/100)*60 + (curArr%100);
      if (am > dm)
        segSpeed = Math.round(n.distance_km / ((am - dm) / 60));
    }
    State._routePath.push({
      station_id: n.neighbor_station_id,
      station_name: n.neighbor_name,
      line_id: n.line_id,
      line_name: n.line_name,
      arrival: curArr,
      departure: isStop ? UI._toHHMM(depTime) : curArr,
      is_stop: isStop,
      is_terminal: false,
      distance_km: n.distance_km,
      speed_kmh: segSpeed
    });
    U.$('stop-config').style.display = 'none';
    State._pendingNeighbor = null;
    UI.renderRoutePath();
    UI.showNeighbors(n.neighbor_station_id);
  };

  UI.cancelStop = function() {
    U.$('stop-config').style.display = 'none';
    State._pendingNeighbor = null;
  };

  UI.finishRoute = function() {
    U.$('neighbor-panel').style.display = 'none';
    var path = State._routePath;
    if (path.length > 0) {
      path[path.length - 1].is_terminal = true;
      path[path.length - 1].departure = -1;
    }
    UI.renderRoutePath();
    U.toast('已设终点站', 'success');
  };

  UI.renderRoutePath = function() {
    var path = State._routePath;
    if (!path.length) {
      U.$('route-path-list').innerHTML = '';
      return;
    }
    var html = '<table class="route-table">' +
      '<tr><th>#</th><th>站点</th><th>线路</th><th>类型</th><th>到</th><th>发</th><th>里程</th><th>时速</th><th></th></tr>';
    for (var i = 0; i < path.length; i++) {
      var s = path[i];
      var isFirst = (i === 0), isLast = s.is_terminal === true;
      var tag = isFirst ? '始发' : isLast ? '终到' : s.is_stop ? '停靠' : '通过';
      var arrTime = isFirst ? '---' : U.fmtTime(s.arrival);
      var depTime = isLast ? '---' : (s.departure > 0 ? U.fmtTime(s.departure) : U.fmtTime(s.arrival));
      // 里程：neighbor API 提供；时速：本地根据时间差计算
      var dist = (i > 0 && s.distance_km) ? Number(s.distance_km).toFixed(0) + ' km' : '';
      var speedStr = '';
      if (i > 0 && s.distance_km && s.arrival > 0 && path[i-1].departure > 0) {
        var prevMin = Math.floor(path[i-1].departure/100)*60 + (path[i-1].departure%100);
        var curMin = Math.floor(s.arrival/100)*60 + (s.arrival%100);
        if (curMin > prevMin)
          speedStr = Math.round(Number(s.distance_km) / ((curMin - prevMin) / 60)) + ' km/h';
      }
      html += '<tr>' +
        '<td class="route-idx">' + (i + 1) + '</td>' +
        '<td class="route-sta">' + U.esc(s.station_name) + '</td>' +
        '<td class="route-ln">' + (i > 0 ? U.esc(s.line_name || '') : '') + '</td>' +
        '<td><span class="route-tag">' + tag + '</span></td>' +
        '<td class="route-time">' + arrTime + '</td>' +
        '<td class="route-time">' + depTime + '</td>' +
        '<td class="route-dist">' + dist + '</td>' +
        '<td class="route-speed">' + speedStr + '</td>' +
        '<td class="route-del">' + (i > 0 && !isLast ? '<button class="btn btn-sm btn-danger" onclick="UI.removeRouteStop(' + i + ')">✕</button>' : '') + '</td>' +
      '</tr>';
    }
    html += '</table>';
    U.$('route-path-list').innerHTML = html;
  };

  UI.removeRouteStop = function(idx) {
    State._routePath.splice(idx);
    var last = State._routePath[State._routePath.length - 1];
    last.is_terminal = false;
    if (last.departure === -1)
      last.departure = 0;
    U.$('neighbor-panel').style.display = '';
    UI.renderRoutePath();
    UI.showNeighbors(last.station_id);
  };

  UI.submitNewTrain = async function() {
    var prefix = (U.$('new-train-prefix') || {}).value || '';
    var number = (U.$('new-train-number') || {}).value || '';
    if (!prefix || !number)
      return U.toast('请选择列车种类并输入车次号', 'error');
    var tid = prefix + number;
    var trainType = parseInt((U.$('new-train-type') || {}).value || 0);
    var path = State._routePath;
    if (path.length < 2)
      return U.toast('至少需要始发站和终点站', 'error');

    // stops: 全部站（含通过），始发+停靠+通过+终到（segments 由后端从 stops 推导）
    var stops = [];
    for (var i = 0; i < path.length; i++) {
      var s = path[i];
      var isFirst = (i === 0), isLast = (i === path.length - 1);
      stops.push({
        station_id: s.station_id, line_id: s.line_id || 0,
        arrival: isFirst ? -1 : s.arrival,
        departure: isLast ? -1 : s.departure,
        platform: 0
      });
    }

    var validFrom = '', validUntil = '';
    if (trainType === 0) {
      validFrom = (U.$('new-train-valid-from') || {}).value || '';
      if (!validFrom)
        return U.toast('请选择生效日期', 'error');
    } else {
      validFrom = (U.$('new-train-range-from') || {}).value || '';
      validUntil = (U.$('new-train-range-to') || {}).value || '';
      if (!validFrom || !validUntil)
        return U.toast('请选择运行区间', 'error');
    }

    var body = {
      id: tid, type: trainType, stops: stops, status: 0,
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

    var isEdit = !!State._editingTrainId;
    var url = isEdit ? '/api/admin/trains/' + encodeURIComponent(State._editingTrainId) : '/api/admin/trains';
    var res = isEdit ? await API.put(url, body) : await API.post(url, body);
    if (res.ok) {
      U.toast('已提交审批：' + (res.data.approval_id || ''), 'success');
      State._editingTrainId = null;
      U.$('new-train-number').disabled = false;
      UI.navTo('trains');
    } else {
      var errMsg = (res.data && res.data.error) || '提交失败';
      var conflicts = (res.data && res.data.conflicts) || [];
      if (conflicts.length > 0) {
        for (var c = 0; c < conflicts.length; c++) {
          var cf = conflicts[c];
          var sa = '', sb = '';
          for (var si = 0; si < State.stations.length; si++) {
            if (State.stations[si].id === cf.station_a)
              sa = State.stations[si].name;
            if (State.stations[si].id === cf.station_b)
              sb = State.stations[si].name;
          }
          errMsg += '\n• ' + cf.train_id + ' 占用了 ' + (sa || cf.station_a) + ' → ' + (sb || cf.station_b) +
            ' 区间 ' + U.fmtTime(cf.conflicting_enter) + '–' + U.fmtTime(cf.conflicting_leave);
        }
      }
      U.$('add-train-error').textContent = errMsg;
    }
  };

  UI.loadTrains = async function() {
    // 立即清空旧列表，防止显示上次访问的残留数据
    U.$('trains-list').innerHTML = '<div class="loading">加载中…</div>';
    var loadingEl = U.$('trains-loading');
    if (loadingEl)
      loadingEl.style.display = 'block';
    var res = await API.get('/api/admin/trains');
    if (loadingEl)
      loadingEl.style.display = 'none';
    if (!res.ok)
      return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._allTrains = res.data.data || [];
    // 构建 id→train 索引，供审批列表 O(1) 查找 stops
    State._trainMap = {};
    for (var ti = 0; ti < State._allTrains.length; ti++)
      State._trainMap[State._allTrains[ti].id] = State._allTrains[ti];
    // 清空搜索框和排序（不重置复选框，HTML checked 属性自然生效）
    var inp = U.$('train-search-input');
    if (inp) {
      inp.value = '';
      inp.setAttribute('list', 'train-search-datalist');
    }
    var sortEl = U.$('trains-sort');
    if (sortEl)
      sortEl.value = 'train_id';
    U.$('trains-list').innerHTML = '<div class="loading">点击"查询"查看列车列表</div>';
  };

  UI.filterTrains = function(status) {
    State._trainStatusFilter = status;
    var btns = document.querySelectorAll('#page-trains .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      var txt = btns[i].textContent.trim();
      var match = (status === '' ? '全部' : status === 'ACTIVE' ? '运行中' : status === 'PENDING' ? '待审批' : '已归档');
      btns[i].classList.toggle('active', txt === match);
    }
    UI.renderTrains();
  };

  UI.onTrainSearchInput = function() {
    var inp = U.$('train-search-input');
    if (!inp)
      return;
    var trains = State._allTrains || [];
    U.$('train-search-datalist').innerHTML = trains.map(function(t) {
      return '<option value="' + U.esc(t.id) + '">';
    }).join('');
    inp.setAttribute('list', 'train-search-datalist');
  };

  UI.renderTrains = function() {
    var trains = State._allTrains || [];

    // 1. 状态筛选
    var status = State._trainStatusFilter;
    if (status)
      trains = trains.filter(function(t) { return t.status == (status === 'ACTIVE' ? 0 : status === 'PENDING' ? 1 : 2); });

    // 2. 车型筛选（读取当前页的复选框，无复选框时不过滤）
    var enabledTypes = {};
    var scope = document.querySelector('.page.active') || document;
    var typeItems = scope.querySelectorAll('.filter-type-item');
    if (typeItems.length > 0) {
      for (var ti = 0; ti < typeItems.length; ti++)
        enabledTypes[typeItems[ti].value] = typeItems[ti].checked;
      trains = trains.filter(function(t) {
        var prefix = (t.id || '')[0].toUpperCase();
        if ('GDCZTKS'.indexOf(prefix) < 0)
          prefix = 'OTHER';
        return enabledTypes[prefix] !== false;
      });
    }

    // 3. 车次搜索
    var searchVal = ((U.$('train-search-input') || {}).value || '').trim().toUpperCase();
    if (searchVal)
      trains = trains.filter(function(t) { return (t.id || '').toUpperCase().indexOf(searchVal) >= 0; });

    // 4. 排序
    var sortBy = (U.$('trains-sort') || {}).value || 'train_id';
    trains.sort(function(a, b) {
      if (sortBy === 'departure') {
        var da = (a.stops && a.stops[0] && a.stops[0].departure > 0) ? a.stops[0].departure : 9999;
        var db = (b.stops && b.stops[0] && b.stops[0].departure > 0) ? b.stops[0].departure : 9999;
        return da - db;
      }
      return (a.id || '').localeCompare(b.id || '');
    });

    // 构建车次 datalist
    var dl = U.$('train-search-datalist');
    if (dl)
      dl.innerHTML = trains.map(function(t) { return '<option value="' + U.esc(t.id) + '">'; }).join('');

    var tpl = U.$('tpl-train-mgmt-card');
    var listEl = U.$('trains-list');
    listEl.innerHTML = '';
    if (!trains.length) {
      listEl.innerHTML = '<div class="loading">暂无列车数据</div>';
      return;
    }
    for (var i = 0; i < trains.length; i++) {
      var t = trains[i];
      var card = tpl.content.cloneNode(true);
      var key = 'train_' + i;
      State._trainItems[key] = t;
      var root = card.querySelector('.train-mgmt-card');
      root.onclick = function(k) { return function() { UI.showTrainDetail(k); }; }(key);
      root.style.cursor = 'pointer';
      card.querySelector('.train-mgmt-id').textContent = t.id;
      card.querySelector('.train-tag-type').textContent = t.type === 0 ? '图定' : '临客';
      var tagEl = card.querySelector('.train-tag-status');
      tagEl.textContent = t.status === 0 ? '运行中' : t.status === 1 ? '待审批' : '已归档';
      tagEl.className = 'tag train-tag-status tag-' + (t.status === 0 ? 'active' : t.status === 1 ? 'pending' : 'archived');
      var stops = t.stops || [];
      var origin = stops.length ? (stops[0].station_name || '?') : '?';
      var terminal = stops.length ? (stops[stops.length - 1].station_name || '?') : '?';
      card.querySelector('.train-mgmt-route').textContent = origin + ' → ' + terminal;
      if (t.status !== 2) {  // 已归档的不显示编辑/删除
        var editBtn = document.createElement('button');
        editBtn.className = 'btn btn-sm btn-primary';
        editBtn.textContent = '管理';
        editBtn.onclick = (function(id) { return function(e) { e.stopPropagation(); UI.editTrain(id); }; })(t.id);
        card.querySelector('.train-mgmt-actions').appendChild(editBtn);
        var btn = document.createElement('button');
        btn.className = 'btn btn-sm btn-danger';
        btn.textContent = '删除';
        btn.onclick = (function(id) { return function(e) { e.stopPropagation(); UI.deleteTrain(id); }; })(t.id);
        card.querySelector('.train-mgmt-actions').appendChild(btn);
      }
      listEl.appendChild(card);
    }
  };

  UI._stationName = function(id, fallback) {
    if (fallback)
      return fallback;
    for (var i = 0; i < State.stations.length; i++) {
      if (State.stations[i].id === id)
        return State.stations[i].name;
    }
    return '站#' + id;
  };

  UI._lineName = function(lineId) {
    if (!lineId)
      return '';
    if (!State._lineNameIndex) {
      State._lineNameIndex = {};
      var idx = State._neighborIndex || {};
      for (var sid in idx) {
        var nb = idx[sid];
        for (var n = 0; n < nb.length; n++) {
          if (nb[n].line_id && !State._lineNameIndex[nb[n].line_id])
            State._lineNameIndex[nb[n].line_id] = nb[n].line_name;
        }
      }
    }
    return State._lineNameIndex[lineId] || '';
  };

  UI._buildFullStops = function(stops, segments) {
    if (!segments || !segments.length)
      return stops;
    // 从 segments 提取第一个站 + 每个 segment 的 to 站
    var result = [];
    for (var i = 0; i < segments.length; i++) {
      var seg = segments[i];
      if (i === 0) {
        result.push({ station_id: seg.from_station, line_id: 0, arrival: -1, departure: seg.enter_time });
      }
      var isLast = (i === segments.length - 1);
      // 判断通过：该站是否在 stops 中（有 departure != arrival）
      var stopMatch = null;
      for (var j = 0; j < stops.length; j++) {
        if (stops[j].station_id === seg.to_station) {
          stopMatch = stops[j];
          break;
        }
      }
      if (stopMatch && !isLast) {
        result.push(stopMatch);
      } else if (stopMatch && isLast) {
        result.push({ station_id: seg.to_station, line_id: stopMatch.line_id, arrival: seg.leave_time, departure: -1 });
      } else {
        // 通过站：到达=离开=leave_time
        result.push({ station_id: seg.to_station, line_id: seg.line_id, arrival: seg.leave_time, departure: seg.leave_time });
      }
    }
    return result;
  };

  UI._showStopTable = function(title, stops, segments) {
    U.$('detail-train-id').textContent = title;
    var fullStops = UI._buildFullStops(stops || [], segments || []);
    if (!fullStops.length) {
      U.$('detail-stops').innerHTML = '';
      return;
    }
    var html = '<table class="route-table">' +
      '<tr><th>#</th><th>站点</th><th>线路</th><th>类型</th><th>到</th><th>发</th><th>里程</th><th>时速</th></tr>';
    for (var i = 0; i < fullStops.length; i++) {
      var s = fullStops[i];
      var isFirst = (i === 0), isLast = (i === fullStops.length - 1);
      var isPass = !isFirst && !isLast && s.arrival > 0 && s.departure > 0 && s.arrival === s.departure;
      var tag = isFirst ? '始发' : isLast ? '终到' : isPass ? '通过' : '停靠';
      var name = UI._stationName(s.station_id, s.station_name);
      var lineName = UI._lineName(s.line_id);
      var arrTime = isFirst ? '---' : U.fmtTime(s.arrival);
      var depTime = (isLast || isPass) ? '---' : U.fmtTime(s.departure);

      // 里程和时速：优先从 segments 取预计算值，否则现场算
      var distStr = '', speedStr = '';
      if (i > 0 && segments && segments[i - 1] && segments[i - 1].distance_km) {
        var seg = segments[i - 1];
        distStr = seg.distance_km.toFixed(0) + ' km';
        speedStr = seg.speed_kmh ? seg.speed_kmh + ' km/h' : '';
      }

      html += '<tr>' +
        '<td class="route-idx">' + (i + 1) + '</td>' +
        '<td class="route-sta">' + U.esc(name) + '</td>' +
        '<td class="route-ln">' + U.esc(lineName) + '</td>' +
        '<td><span class="route-tag">' + tag + '</span></td>' +
        '<td class="route-time">' + arrTime + '</td>' +
        '<td class="route-time">' + depTime + '</td>' +
        '<td class="route-dist">' + distStr + '</td>' +
        '<td class="route-speed">' + speedStr + '</td>' +
      '</tr>';
    }
    html += '</table>';
    U.$('detail-stops').innerHTML = html;
    U.$('detail-overlay').classList.add('show');
  };

  UI.showTrainDetail = async function(itemKey) {
    var t = (State._trainItems || {})[itemKey];
    if (!t)
      return;
    if (Object.keys(State._neighborIndex).length === 0) await UI.loadNeighborIndex();
    UI._showStopTable(t.id + ' 停站时刻表', t.stops || [], t.segments || []);
  };

  UI.deleteTrain = function(trainId) {
    State._pendingDeleteTrain = trainId;
    var minDate = new Date();
    minDate.setDate(minDate.getDate() + 15);  // 第14天已放票，须 ≥15 天
    var minStr = minDate.toISOString().slice(0,10);
    var inp = U.$('delete-date-input');
    inp.setAttribute('min', minStr);
    inp.value = minStr;
    U.$('delete-date-error').textContent = '';
    U.$('delete-date-modal').style.display = 'flex';
  };

  UI.confirmDelete = async function() {
    var trainId = State._pendingDeleteTrain;
    var delDate = (U.$('delete-date-input') || {}).value || '';
    if (!delDate) {
      U.toast('请选择删除日期', 'error');
      return;
    }
    U.$('delete-date-modal').style.display = 'none';
    if (!confirm('确定删除 ' + trainId + '？删除日期: ' + delDate)) return;
    var res = await API.del('/api/admin/trains/' + trainId + '?date=' + encodeURIComponent(delDate));
    if (res.ok) {
      U.toast('已提交审批', 'success');
      UI.loadTrains();
    }
    else U.toast((res.data && res.data.error) || '删除失败', 'error');
  };

  UI.cancelDelete = function() {
    U.$('delete-date-modal').style.display = 'none';
    State._pendingDeleteTrain = null;
  };

  UI.loadMySubmissions = async function() {
    // 静默加载列车列表（删除列车审批需要查找 stops 数据）
    await UI._ensureTrainsLoaded();
    var loadingEl = U.$('my-submissions-loading');
    if (loadingEl)
      loadingEl.style.display = 'block';
    var status = State._mySubFilter || '';
    var userId = State.user ? State.user.id : '';
    var url = '/api/admin/approvals?submitter_id=' + encodeURIComponent(userId);
    if (status)
      url += '&status=' + status;
    var res = await API.get(url);
    if (loadingEl)
      loadingEl.style.display = 'none';
    if (!res.ok)
      return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._mySubmissions = res.data.data || [];
    UI.renderMySubmissions();
  };

  UI.filterMySubmissions = function(status) {
    State._mySubFilter = status;
    var labels = {'': '全部', 'SUBMITTED': '待审批', 'APPROVED': '已通过', 'REJECTED': '已驳回', 'WITHDRAWN': '已取消'};
    var btns = document.querySelectorAll('#page-my-submissions .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      btns[i].classList.toggle('active', btns[i].textContent.trim() === (labels[status] || '全部'));
    }
    UI.loadMySubmissions();
  };

  UI.renderMySubmissions = function() {
    var items = State._mySubmissions || [];
    var tpl = U.$('tpl-submission-card');
    var listEl = U.$('my-submissions-list');
    listEl.innerHTML = '';
    if (!items.length) {
      listEl.innerHTML = '<div class="loading">暂无提交记录</div>';
      return;
    }
    for (var i = 0; i < items.length; i++) {
      var a = items[i];
      var card = tpl.content.cloneNode(true);
      // 解析 payload 存起来供详情使用
      var info = UI._resolveApprovalPayload(a, i);
      // 卡片可点击查看详情（仅新增/删除列车有时刻表可展示）
      var hasStops = (a.type === 0 /* CREATE_TRAIN */ || a.type === 4 /* DELETE_TRAIN */) || (info.tstops && info.tstops.length > 0);
      if (hasStops) {
        card.querySelector('.approval-card').onclick = (function(k) { return function() { UI.showSubmissionDetail(k); }; })(info.key);
        card.querySelector('.approval-card').style.cursor = 'pointer';
      }
      // 填充数据
      var trainName = info.train ? U.esc(info.train.id || '?') : '?';
      card.querySelector('.submission-train-id').textContent = trainName;
      card.querySelector('.submission-time').textContent = (a.submitted_at || '');
      card.querySelector('.submission-type-tag').textContent = UI.TYPE_LABEL[a.type] || '未知';
      var stEl = card.querySelector('.submission-status-tag');
      stEl.textContent = UI.STATUS_LABEL[a.status] || '未知';
      stEl.className = 'submission-status-tag ' + (UI.STATUS_CLS[a.status] || 'submitted');
      var deciderEl = card.querySelector('.approval-meta-decider');
      if (a.approver_id) {
        deciderEl.textContent = '审批人: ' + a.approver_id + ' | 审批时间: ' + (a.decided_at || '');
      } else {
        deciderEl.style.display = 'none';
      }
      var cmtEl = card.querySelector('.approval-comment');
      if (a.status === 2 && a.comment)
        cmtEl.textContent = '驳回意见: ' + a.comment;
      else { cmtEl.style.display = 'none'; }
      // 撤回按钮（仅待审批）
      if (a.status === 0) {
        var btn = document.createElement('button');
        btn.className = 'btn btn-sm btn-danger';
        btn.textContent = '撤回';
        btn.onclick = (function(id) { return function(e) { e.stopPropagation(); UI.withdrawSubmission(id); }; })(a.id);
        card.querySelector('.submission-actions').appendChild(btn);
      }
      listEl.appendChild(card);
    }
  };

  UI.withdrawSubmission = async function(id) {
    if (!confirm('确定撤回该提交？')) return;
    var res = await API.post('/api/admin/approvals/' + id + '/withdraw');
    if (res.ok) {
      U.toast('已撤回', 'success');
      UI.loadMySubmissions();
    }
    else U.toast((res.data && res.data.error) || '撤回失败', 'error');
  };

  UI.showSubmissionDetail = async function(key) {
    var item = (State._trainItems || {})[key];
    if (!item)
      return;
    if (Object.keys(State._neighborIndex).length === 0) await UI.loadNeighborIndex();
    var title = item.train_id + ' 停站时刻表';
    // 调整时刻/新增线路/新增站点等类型无 stops 数据时，显示基本信息
    if (!item.stops || !item.stops.length) {
      title = item.train_id + ' — ' + UI.TYPE_LABEL[item.approval_type];
      U.$('detail-train-id').textContent = title;
      var div = document.createElement('div');
      div.className = 'detail-empty-message';
      div.textContent = '暂无停站数据（审批类型：' + UI.TYPE_LABEL[item.approval_type] + '）';
      U.$('detail-stops').innerHTML = '';
      U.$('detail-stops').appendChild(div);
      U.$('detail-overlay').classList.add('show');
      return;
    }
    UI._showStopTable(title, item.stops, item.segments || []);
  };

  UI._ensureTrainsLoaded = async function() {
    if (State._allTrains.length)
      return;
    try {
      var tr = await API.get('/api/admin/trains');
      if (tr.ok) {
        State._allTrains = tr.data.data || [];
        State._trainMap = {};
        for (var ti = 0; ti < State._allTrains.length; ti++)
          State._trainMap[State._allTrains[ti].id] = State._allTrains[ti];
      }
    } catch (_) {}
  };

  UI._resolveApprovalPayload = function(a, idx) {
    var key = (idx < 0 ? 'apr_' : 'sub_') + Math.abs(idx);
    var train = null;
    try { train = (typeof a.payload === 'string' ? JSON.parse(a.payload) : a.payload); } catch (e) {}
    var tid = train ? train.id : '?';
    var tstops = train ? (train.stops || []) : [];
    var tsegs = train ? (train.segments || []) : [];
    // 删除列车：payload 无 stops，从列车索引查找
    if (a.type === 4 /* DELETE_TRAIN */ && !tstops.length) {
      var cached = State._trainMap && State._trainMap[tid];
      if (cached) {
        tstops = cached.stops || [];
        tsegs = cached.segments || [];
      }
    }
    State._trainItems[key] = { train_id: tid, stops: tstops, segments: tsegs, approval_type: a.type, payload: train };
    return { key: key, tid: tid, tstops: tstops, tsegs: tsegs, train: train };
  };

  UI.onPrefixChange = function() {
    var p = (U.$('new-train-prefix') || {}).value;
    var hint = U.$('speed-limit-hint');
    if (!hint)
      return;
    var maxSpeed = UI._speedLimits[p];
    if (maxSpeed !== undefined) {
      hint.style.display = '';
      hint.textContent = '最高运营时速：' + (maxSpeed === 999 ? '不限' : maxSpeed + ' km/h');
    } else {
      hint.style.display = 'none';
    }
  };

  UI.onTrainTypeChange = function() {
    var type = parseInt((U.$('new-train-type') || {}).value || 0);
    U.$('train-date-single').style.display = (type === 0) ? '' : 'none';
    U.$('train-date-range').style.display = (type === 1) ? '' : 'none';
  };

  UI._toHHMM = function(t) {
    if (!t)
      return 0;
    var p = t.split(':');
    return parseInt(p[0]) * 100 + parseInt(p[1]);
  };
