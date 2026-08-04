# 种子用户账号密码

密码通过 argon2id 哈希存储，无法逆向。以下为已知密码的种子账号：

## 管理角色（密码规则：角色名缩写+123）

| 用户名 | 密码 | 角色 | 权限 |
|--------|------|------|------|
| `sys_admin` | `sys123` | SYS_ADMIN | 用户管理、审计日志、系统配置 |
| `infra_admin` | `infra123` | INFRA_ADMIN | 站点管理、线路管理、查看路网 |
| `approver` | `approver123` | APPROVER | 审批中心（通过/驳回） |
| `staff` | `staff123` | STAFF | 列车管理、我的提交 |
| `passenger` | `pass123` | PASSENGER |


> 可通过注册页面自助创建新旅客账号。`ut_*` 前缀账号由测试套件自动创建。
