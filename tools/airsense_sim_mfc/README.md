# AirSense 10 串口模拟设备（MFC 图形版）

无真机时替代 ResMed AirSense 10，作为 AirBridge 固件 UART 协议栈的对端：接收主机帧、按协议应答、推送实时流，并把每一帧逐字段解读出来。

与 `tools/airsense_sim/`（Python 版）行为一致，本版为 Windows 单文件 exe，无需 Python 环境，串口从下拉框直接选。

## 构建

- Visual Studio（MFC 组件）、Release x64。
- 工程已设为 MFC **静态链接**，产物不依赖 MFC 运行时 DLL。
- `vcxproj` 中 `PlatformToolset` 为 `v145`；工具集不同时改这一处即可。

```
msbuild AirSenseSim.vcxproj /p:Configuration=Release /p:Platform=x64
```

输出：`x64/Release/AirSenseSim.exe`（约 6 MB）。

## 接线

PC 的 USB-UART 接 ESP32-S3：

| USB-UART | ESP32-S3 |
|---|---|
| TX | GPIO44（主机 RX） |
| RX | GPIO43（主机 TX） |
| GND | GND |

串口参数 **57600 8N1**，无流控。

## 使用

1. 选择串口 → **打开串口**。
2. 点 **自检演示**：不开串口也会跑一遍完整帧序列，用来确认界面与协议解析正常。
3. 与固件联调：打开串口后由固件主动轮询，日志区实时显示 TX/RX 帧。
4. 点击日志任意一行，右侧 **帧结构与指令解读** 显示该帧的时间、方向、类型、长度、载荷、0x55 转义、CRC 校验结果、HEX 与载荷文本，以及命令语义（例如 `G C &CSG` 的 V4 记录逐字段含义）。

### 设备状态与故障注入

| 控件 | 作用 |
|---|---|
| `ROP` 下拉 | 设置 `G S #ROP` 返回值：`0000 待机` / `0001 治疗中` / `0004 过渡状态` |
| `CSG 返回 6009` | 自定义设置注册表读取一律回错误码 6009 |
| `ROP 不响应` | 收到 `G S #ROP` 不回帧，用于测试主机超时重试 |
| `应答坏 CRC` | 每个应答 CRC 故意写坏，用于测试主机校验分支 |
| `推送实时流` | 按订阅周期发 `L` 帧（TCE 通道），模拟治疗数据流 |
| `显示 HEX` / `暂停滚动` | 日志显示与滚动控制 |
| `保存日志` | 整份日志另存为文本 |

### 手动注入

在输入框按 `类型 内容` 填写，点 **注入帧** 即向主机发一帧：

```
R G S #ROP = 0001       应答型
E G S #XXX = 6006       错误应答（6006 = 未知变量或通道）
L TCE00001002           实时流
```

## 自检

```
AirSenseSim.exe --selftest
```

结果写入 `x64/Release/selftest_result.txt`，当前 19 项全部通过（CRC 标准向量、帧转义、重同步、坏 CRC、ROP 读写、CSG V4 记录、枚举选项、未知变量、订阅推送、三类故障注入）。

## 变量目录

`VarCatalog.cpp` 是从固件 `src/settings_defs.h` 生成的变量快照，使 exe 可独立运行。固件变量表变更后重新生成：

```
python gen_catalog.py
```

## 文件

| 文件 | 说明 |
|---|---|
| `QFrame.*` | qframe 编解码：0x55 同步、3 位 hex 长度、0x55 双写转义、CRC-16/CCITT-FALSE |
| `VarCatalog.*` | 变量目录（由 `gen_catalog.py` 生成） |
| `CommandInfo.*` | 命令语义与错误码中文解读 |
| `SimDevice.*` | 设备状态机：轮询应答、订阅推送、故障注入 |
| `SerialPort.*` | Win32 串口封装，注册表枚举 COM 口 |
| `MainDlg.*` | 主对话框 |
| `SimApp.*` | 应用入口、`--selftest` 无窗口自检 |
