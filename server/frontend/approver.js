// approver.js — auto-split from app.js
'use strict';


  UI.loadApprovals = async function() {
    var loadingEl = U.$('approvals-loading');
    if (loadingEl)
      loadingEl.style.display = 'block';
    var status = State._approvalFilter || 'SUBMITTED';  // 默认待审批
    var userId = State.user ? State.user.id : '';
    var url = '/api/admin/approvals?status=' + status;
    // 查看已通过/已驳回时只看自己的审批记录
    if (status === 'APPROVED' || status === 'REJECTED') {
      url += '&approver_id=' + encodeURIComponent(userId);
    }
    var res = await API.get(url);
    if (loadingEl)
      loadingEl.style.display = 'none';
    if (!res.ok)
      return U.toast((res.data && res.data.error) || '加载失败', 'error');

    State._allApprovals = res.data.data || [];
    await UI._ensureTrainsLoaded();
    UI.renderApprovals();
  };

  UI.filterApprovals = function(status) {
    State._approvalFilter = status;
    var labels = {'SUBMITTED': '待审批', 'APPROVED': '已通过', 'REJECTED': '已驳回'};
    var btns = document.querySelectorAll('#page-approvals .filter-bar .btn');
    for (var i = 0; i < btns.length; i++) {
      btns[i].classList.toggle('active', btns[i].textContent.trim() === (labels[status] || '待审批'));
    }
    UI.loadApprovals();
  };

  UI.renderApprovals = function() {
    var approvals = State._allApprovals || [];
    var tpl = U.$('tpl-approval-card');
    var listEl = U.$('approvals-list');
    listEl.innerHTML = '';
    if (!approvals.length) {
      listEl.innerHTML = '<div class="loading">暂无审批</div>';
      return;
    }
    for (var i = 0; i < approvals.length; i++) {
      var a = approvals[i];
      var card = tpl.content.cloneNode(true);

      // 解析 payload，提取列车信息供详情展示（与我的提交共用 showSubmissionDetail）
      var info = UI._resolveApprovalPayload(a, -i - 1);  // 负索引 → 'apr_' 前缀
      // 卡片可点击查看详情
      card.querySelector('.approval-card').onclick = (function(k) { return function() { UI.showSubmissionDetail(k); }; })(info.key);
      card.querySelector('.approval-card').style.cursor = 'pointer';

      card.querySelector('.approval-type').textContent = UI.TYPE_LABEL[a.type] || '未知';
      var stEl = card.querySelector('.approval-status');
      stEl.textContent = UI.STATUS_LABEL[a.status] || '未知';
      stEl.className = 'approval-status ' + (UI.STATUS_CLS[a.status] || 'submitted');
      // 提交人
      card.querySelector('.approval-meta-submitter').textContent = '提交人: ' + (a.submitter_id || '?') + ' | ' + (a.submitted_at || '');
      // 车次
      card.querySelector('.approval-payload').textContent = '车次: ' + (info.tid || '?');
      // 审批操作按钮（仅待审批状态）
      var actionsEl = card.querySelector('.approval-actions');
      if (a.status === 0) {
        actionsEl.innerHTML = '<button class="btn btn-sm btn-primary">通过</button><button class="btn btn-sm btn-danger">驳回</button>';
        var btns = actionsEl.querySelectorAll('button');
        btns[0].onclick = function(e) { e.stopPropagation(); UI.approveOne(a.id); };
        btns[1].onclick = function(e) { e.stopPropagation(); UI.rejectOne(a.id); };
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
      if (a.status === 2 && a.comment)
        cmtEl.textContent = '驳回意见: ' + a.comment;
      else { cmtEl.style.display = 'none'; }
      listEl.appendChild(card);
    }
  };

  UI.approveOne = async function(id) {
    if (!confirm('确认通过该审批？')) return;
    var res = await API.post('/api/admin/approvals/' + id + '/approve');
    if (res.ok) {
      U.toast('审批通过', 'success');
      UI.loadApprovals();
    }
    else U.toast((res.data && res.data.error) || '审批失败', 'error');
  };

  UI.rejectOne = async function(id) {
    var comment = prompt('驳回意见：');
    if (!comment)
      return;
    var res = await API.post('/api/admin/approvals/' + id + '/reject', { comment: comment });
    if (res.ok) {
      U.toast('已驳回', 'success');
      UI.loadApprovals();
    }
    else U.toast((res.data && res.data.error) || '驳回失败', 'error');
  };
