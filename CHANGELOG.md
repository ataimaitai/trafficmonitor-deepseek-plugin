# Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.0.0] - 2026-10-27

### Added
- TrafficMonitor 插件（x64 DLL），提供两个显示项目：
  - `DS余额`：DeepSeek API 账户总余额（`GET /user/balance`，默认 10 分钟轮询，双击立即刷新）
  - `DS时段`：距下次峰/谷切换的倒计时，格式 `+HH:MM`（谷段）/ `-HH:MM`（峰段），逐分钟递减到 `00:00`
- 峰谷判定按北京时间（UTC+8）：工作日（不含法定节假日）9:00-12:00、14:00-18:00 为高峰；
  周末、法定节假日全天、调休上班的周末均为空闲（半价）
- 中国法定节假日数据三级来源：本地缓存 → holiday-cn 联网自动更新（jsdelivr/GitHub 双源）→ 内置 2026 官方表
- 网络策略：WinHTTP 直连 / 仅代理 / 自动回退（默认代理 `127.0.0.1:7897`）
- API Key 安全方案：从凭据文件（如 `%USERPROFILE%\.dsh\.credentials.yaml` 的 `refs: DEEPSEEK_API_KEY`）读取，
  插件配置仅保存文件路径；保存选项时自动清除 ini 中遗留的明文 Key，旧配置首次加载自动迁移
- 选项对话框：凭据文件路径（含浏览按钮与默认预填）、刷新间隔、代理服务器/方式、测试连接
- 插件命令：立即刷新余额、打开 DeepSeek 开放平台（用量）
- 悬浮提示仅显示余额 / 赠金 / 充值（失败时为简短状态与原因）
- 控制台测试器 `tester.exe`：峰谷计算 19 例、凭据解析 3 例、加载冒烟与 tooltip 断言，共 23 项断言
