# DeepSeekPlugin — TrafficMonitor 插件

在 TrafficMonitor 的主窗口 / 任务栏中显示：

| 显示项目 | ID | 说明 | 示例 |
| --- | --- | --- | --- |
| **DeepSeek 余额** | `DeepSeekBalance` | DeepSeek API 账户总余额（含赠金+充值） | `DS余额: ￥95.01` |
| **DeepSeek 峰谷倒计时** | `DeepSeekPeriod` | 距下次时段切换的倒计时（`HH:MM`，逐分钟递减到 `00:00`） | `DS时段: +61:47` |

- `+61:47` = 当前处于**空闲时段**（谷段，半价），61 小时 47 分后切换为高峰时段
- `-04:24` = 当前处于**高峰时段**（峰段），4 小时 24 分后切换为空闲时段
- 符号规则：**`+` 谷段 / `-` 峰段**；倒计时到 `00:00` 后时段切换、符号翻转并重新开始倒计时
- 长假期间小时数可超过 24（如春节 `+191:00`），示例宽度按 3 位小时预留
- **周末/法定节假日**全天空闲、不存在切换点，倒计时跨到下一个工作日的高峰开始：
  如周六 06:00 显示 `+51:00`、周六 10:00 显示 `+47:00`（均指向周一 09:00），这与官方计费规则一致；
  工作日则严格按窗口显示（如 06:00 → `+03:00`、10:00 → `-02:00`）

鼠标悬停提示（tooltip）**仅显示余额、赠金、充值**三项；获取失败时只显示简短状态与原因
（峰谷信息请看任务栏的 `DS时段` 项目）。

其他交互：
- **双击**余额项目 → 立即刷新余额
- TrafficMonitor 托盘右键菜单 → 插件命令：`立即刷新 DeepSeek 余额`、`打开 DeepSeek 开放平台(用量)`

---

## 峰谷时段规则（官方）

来源：[DeepSeek API 文档 — 模型 & 价格](https://api-docs.deepseek.com/zh-cn/quick_start/pricing)

> 北京时间**周一至周五（不含中国法定节假日）9:00–12:00、14:00–18:00** 为**高峰时段**；
> 其余时段，**包括周末及中国法定节假日全天**均为**空闲时段**，空闲时段价格为高峰时段的一半。
> 注意：**调休上班的周六/周日仍按周末计**，全天为空闲时段。

插件按北京时间（UTC+8）计算，与系统时区无关。法定节假日判定需要节假日日历，见下文。

## 节假日数据

优先级从高到低：

1. **本地缓存**：`<配置目录>\holidays_<年份>.txt`（每行一个日期 `YYYY-MM-DD`，可手工编辑）
2. **自动联网获取**：[NateScarlet/holiday-cn](https://github.com/NateScarlet/holiday-cn)
   （依据国务院公告自动更新，jsdelivr CDN → GitHub raw 双源，支持代理回退）。
   启动时及每 12 小时检查一次；缓存超过 30 天自动刷新；次年数据公布后（通常每年 11 月）自动获取。
3. **内置 2026 年官方节假日表**（元旦/春节/清明/五一/端午/中秋/国庆，共 33 天）——完全离线也可用。

若某年数据全部缺失（如断网且超出内置年份），该年退化为“仅按周末判断”，tooltip 中会明确标注。

## 安装

1. 确认 TrafficMonitor 为 **64 位**版本（本仓库产物为 x64；32 位 TM 需按下文自行编译 x86）。
2. 将 `build\DeepSeekPlugin.dll` 复制到 TrafficMonitor 主程序目录下的 `plugins` 文件夹。
3. 重启 TrafficMonitor → 右键菜单「其他功能」→「插件管理」，确认列表中出现
   “DeepSeek 余额与峰谷时段”。
4. 在插件管理中点击「**选项**」，确认**凭据文件**路径
   （默认预填 `%USERPROFILE%\.dsh\.credentials.yaml`，也可点「浏览…」选择其他文件），
   可顺手点「测试连接」验证。插件从该文件的 `refs:` 段读取 `DEEPSEEK_API_KEY` 条目，
   **不在插件配置中保存 Key 明文**。
5. 在「选项」→「主窗口设置 / 任务栏窗口设置」→「显示设置」中勾选
   **DeepSeek 余额**、**DeepSeek 峰谷倒计时** 两个项目即可显示。

## 选项说明

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| 凭据文件 | `%USERPROFILE%\.dsh\.credentials.yaml`（存在时预填） | 从中读取 `refs:` 段的 `DEEPSEEK_API_KEY` 作为 API Key；插件配置只保存此路径 |
| 刷新间隔(分钟) | 10 | 余额轮询间隔（1~1440） |
| 代理服务器 | `127.0.0.1:7897` | HTTP 代理地址（Clash 默认端口） |
| 代理方式 | 自动回退 | `仅直连` / `仅代理` / `自动回退`（先直连，失败自动改走代理） |

凭据文件格式（DSH 凭据文件即为此格式）：

```yaml
version: 1
records:
  ...
refs:
  DEEPSEEK_API_KEY: sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

配置保存于 `<TrafficMonitor配置目录>\plugins\DeepSeekPlugin.ini`（首次运行自动生成；
若插件目录不可写则退回 DLL 所在目录）：

```ini
[config]
CredentialFile=C:\Users\<用户名>\.dsh\.credentials.yaml
ProxyServer=127.0.0.1:7897
ProxyMode=2        ; 0=仅直连 1=仅代理 2=自动回退
RefreshMinutes=10
```

> 🔒 **安全说明**：插件配置中不保存 Key 明文；在选项对话框中保存凭据文件路径时，
> 会自动清除 ini 中遗留的旧版明文 `ApiKey`（该键仅作为未配置凭据文件时的兼容兜底）。
> 从旧版升级时：若 ini 中遗留明文 `ApiKey` 且默认凭据文件能读出 `DEEPSEEK_API_KEY`，
> 插件会在首次加载时**自动迁移**为凭据文件模式并清除明文，无需手动操作。
> 凭据文件读取失败时任务栏显示 `凭据错误`，tooltip 中给出具体原因。

## 余额接口

`GET https://api.deepseek.com/user/balance`（Bearer 认证），文档：
[查询余额](https://api-docs.deepseek.com/zh-cn/api/get-user-balance/)。
返回 `is_available`、`total_balance`（总余额）、`granted_balance`（赠金）、`topped_up_balance`（充值）。
状态说明：`未配置`=未设置凭据文件；`凭据错误`=凭据文件无法打开或缺少 DEEPSEEK_API_KEY 条目（tooltip 有详情）；
`Key无效`=HTTP 401；`网络错误`=直连与代理均失败（详见 tooltip）；
`无可用余额`=账户余额耗尽（`is_available=false`）。

## 构建

**MinGW-w64 (g++)**（本机路径已写入脚本，缺失时回退到 PATH 中的 g++）：

```bat
build.bat
```

产物：`build\DeepSeekPlugin.dll`、`build\tester.exe`。

**MSVC**（x64 本机工具命令提示符）：

```bat
cl /LD /utf-8 /EHsc /O2 /DUNICODE /D_UNICODE /Iinclude src\*.cpp /Fe:DeepSeekPlugin.dll /link winhttp.lib shell32.lib gdi32.lib user32.lib
```

32 位 TrafficMonitor 请用对应 x86 工具链执行同样命令。

## 测试

`tester.exe`（与 DLL 同目录运行）：

1. **峰谷计算单元测试**：16 组固定时刻（高峰内/边界整点/周末/调休周六/国庆/春节/清明/中秋/跨年等），
   校验时段判定、切换时刻、剩余秒数与任务栏显示文本；
2. **凭据文件解析测试**：写入样例 yaml（含干扰条目/行尾注释/引号变体），校验 `DEEPSEEK_API_KEY`
   解析、缺条目与文件缺失时的报错；
3. **冒烟测试**：模拟 TrafficMonitor 加载流程（`TMPluginGetInstance` → `OnExtenedInfo` → `DataRequired` 循环），
   配置指向样例凭据文件，打印两个项目的实时值与 tooltip，并校验 tooltip 中的 Key 来源。

如需验证真实余额：把 `testcfg\DeepSeekPlugin.ini` 的 `CredentialFile` 指向你的真实凭据文件后重跑 tester.exe。

## 文件结构

```
DeepSeekPlugin/
├── include/PluginInterface.h     # TrafficMonitor 官方插件接口 (API v8)
├── src/
│   ├── DeepSeekPlugin.h/.cpp     # 插件主体、显示项目、峰谷计算、后台线程
│   ├── HttpClient.cpp            # WinHTTP 封装(直连/代理/自动回退) + JSON 解析
│   ├── Holidays.cpp              # 节假日数据(内置表/联网获取/本地缓存)
│   ├── Credentials.cpp           # 凭据文件读取(refs: DEEPSEEK_API_KEY)
│   └── OptionsDialog.cpp         # 选项对话框(纯 Win32, 无资源文件)
├── tester/tester.cpp             # 控制台测试器
├── build.bat                     # MinGW 构建脚本
└── README.md
```

## 已知限制

- 余额为轮询快照，非实时扣费流水；用量明细请点插件命令「打开 DeepSeek 开放平台(用量)」。
- 若 DeepSeek 未来调整峰谷时段规则，需更新代码中的时段常量（`IsPeakBJTime`）后重新编译。
- 无自绘界面（使用主程序标准文本绘制），跟随 TrafficMonitor 皮肤/字体设置。
