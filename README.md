# MS_LEP

MS_LEP 是面向 JPT YDFLP-E3-100-M7 脉冲光纤激光器的 Linux 调试与控制台。应用使用 GTK4、Libadwaita 和 GLib，提供 USB-RS232 串口与激光器 TCP 服务端两种连接方式。

当前项目范围是 JPT 激光器本体的诊断、参数读写和输出控制。JCZ 控制卡、SG7110 振镜以及未来的上层作业编排属于独立接口，不会被当前串口/TCP 协议层隐式代替。

## 安全边界

1064 nm 激光不可见。设备说明书提示，即使功率设为 0%，输出端仍可能存在约 100 mW 激光。调试前必须使用符合设备要求的防护眼镜、联锁和防护区域。

程序启动时保持未连接，不会自动向设备发送 PA、MO、红光或功率命令。控制台中的输出操作必须先勾选安全确认，且会根据设备读回的控制来源和 GUI 控制位启用相应控件。

顶部的“紧急停光”只发送 GUI PA=0、GUI MO=0 和红光关闭命令，设备连接保持在线。它用于立即停止软件可控制的输出，不负责断开通信。

窗口关闭和“安全断开”使用同一套安全流程：

1. 显示不可关闭的进度窗口，暂停后台轮询。
2. 重新读取设备当前控制模式。
3. 仅对由软件控制的 PA、MO 和功率发送 0；红光发送关闭命令。
4. 重新读取状态和实时功率，确认输出已经关闭。
5. 确认成功后断开连接；窗口关闭操作再继续退出。

读取模式、写入输出或状态确认失败时，程序保持打开并显示错误弹窗，不会把未经确认的设备状态当作安全状态。

## 功能

- 自动枚举串口设备，显示设备名称和路径，支持刷新、手动指定路径和串口参数展开配置。
- 串口设备通过独立选择窗口选择，条目同时显示设备名称和真实路径，例如 `CHG (COM7)` 或 `CHG (/dev/ttyUSB0)`；切换到 TCP 时不会留下串口弹层。
- 串口默认使用 `57600 / 8N1 / 无流控`；支持 `9600`、`19200`、`38400`、`57600`、`115200`，5 至 8 数据位、1 或 2 停止位、无校验/偶校验/奇校验以及硬件流控。
- TCP 默认连接 `192.168.0.10:5000`，串口和 TCP 使用同一套 JPT 帧编解码器。
- 连接失败和设备初始化读取失败使用独立弹窗显示详细错误；串口/TCP 切换会收起当前打开的下拉层。
- 实时功率、频率、脉宽和 Simmer 支持单项写入与批量应用。
- 默认脉宽、默认 Simmer、锁定频率、锁定功率、红光功率和锁定选项可在设备设置页维护。
- 支持 JCZ/DB25 外控与 MS_LEP 内控两种模式。连接后先沿用设备读回的模式，明确切换后才写入控制模式。
- 支持 GUI PA、GUI MO、红光控制，以及输出许可和安全确认。
- 监控实时功率、频率、脉宽、传感器温度、种子源 TEC、输出电流、设备序列号、泵浦/PA/MO/红光状态和 DB25 输入。
- 显示状态字、报警状态、八路报警次数和报警状态变化记录。
- 保存原始 HEX TX/RX 通信日志，支持清空日志。
- 协议工具提供设备读写命令、原始 HEX 请求、TCP IP/端口写入和安全维护确认。
- 型号扩展页提供 M7 截止频率和报警缓存操作。
- 支持系统浅色/深色主题、Libadwaita 语义色、宽窗口侧边栏和窄窗口覆盖式导航。

## 界面结构

主窗口按功能分为以下页面：

- **控制台**：安全确认、实时参数、控制来源、PA/MO/红光操作和批量应用。
- **监控**：实时参数、功率进度、设备信号、DB25 输入和协议监控值。
- **设备设置**：GUI 控制位、Pin9/Pin23/外部频率等 DB25 选项、锁定策略和默认参数。
- **HEX 日志**：原始 TX/RX 字节和协议错误记录。
- **协议工具**：按命令类别读取或写入设备，支持原始命令、地址和 payload 输入。
- **报警记录**：当前报警、累计次数和状态变化历史。
- **型号扩展**：M7 截止频率、报警缓存和设备能力相关操作。

启动和未连接状态只显示连接页。设备完成序列号、参数、状态、监控和报警数据读取后，才开放工作台页面。宽窗口使用侧边栏，窗口收窄后切换为 Libadwaita 覆盖式导航；连接页、控制台和监控页在窄窗口切换为纵向布局。

页面使用 GTK4/Libadwaita 原生控件。状态同时使用文字、语义色和累计数值表达，不依赖颜色单独传达信息。图标按钮提供工具提示和辅助技术名称，异步连接、设备读取、协议命令和安全关闭均有明确的进行中、成功和失败反馈。

## 设备连接

激光器 RS-232 DB9 定义如下：

| 激光器引脚 | 定义 |
| --- | --- |
| 2 | RXD，激光器接收 |
| 3 | TXD，激光器发送 |
| 5 | GND |

使用真正的 USB-RS232 转换器，不能使用 TTL 或 RS-485 转换器。串口线需要交叉连接 TX/RX。确认激光器完成上电初始化后再建立连接。

激光器使用 24 V DC 供电，接线前检查电源极性。Linux 串口设备通常需要当前用户具备 `dialout` 组权限；设备路径以现场枚举结果为准，不复用历史端口号。

当前 transport 实现依赖 Unix/POSIX 串口和 GLib TCP socket，项目构建目标为 Linux/WSL。Windows 原生 GUI 和串口 transport 不在当前 CMake 构建路径内。

## JPT 协议

帧格式为：

```text
0x24 | command: uint16 little-endian | payload_length: uint8 | payload | 0x2A
```

普通读请求和 `uint16` 写请求使用两个 little-endian payload 字节。程序按照长度字段读取变长回复，并保留原始发送和接收字节。设备返回单字节 `0x45` 时显示为协议拒绝。

SN 只读请求示例：

```text
24 3F 00 02 00 00 2A
```

### 状态与监控

状态命令 `0x38` 的位定义：

| 位 | 含义 |
| --- | --- |
| bit0 | PA |
| bit1 | MO |
| bit2 | 红光 |
| bit3 | 泵浦 |
| bit4..11 | DB25 D7..D0 输入 |

DB25 输入按照设备手册映射为八位外部功率值。示例 `0x18` 表示泵浦使能、D7 为高，外部功率值为 `0x80`，约为 50%。

监控命令 `0x3C` 的 8 字节 payload 顺序为传感器 2、传感器 1、种子源 TEC、输出电流，均为 little-endian `uint16`。传感器温度为 `raw / 10 - 20 °C`，`0x04A6` 表示传感器故障；种子源 TEC 和输出电流分别按 `raw / 10` 换算。报警次数命令 `0x42` 返回 8 个 little-endian `uint16`，共 16 字节。

### 命令覆盖

协议工具当前登记以下命令类别：

- 基础参数：功率 `0x01`、频率 `0x02`、脉宽 `0x03`、Simmer `0x04`、默认脉宽 `0x05`、锁定频率 `0x06`、参数选择 `0x07`。
- 控制与输出：控制模式 `0x08`、GUI PA `0x09`、GUI MO `0x0A`、红光 `0x0B`、ControlSel `0x16`。
- 锁定与红光：锁定功率 `0x17`、红光功率 `0x18`、锁定选项 `0x19`。
- 状态读取：默认参数 `0x32..0x35`、控制模式 `0x36`、设备状态 `0x38`、实时功率/脉宽/频率 `0x39..0x3B`、监控 `0x3C`、SN `0x3F`、报警状态 `0x40`、报警次数 `0x42`、出光时间 `0x46`、锁定参数 `0x47..0x49`。
- 维护与扩展：报警屏蔽 `0x0F/0x41`、报警复位 `0x10`、清除报警次数 `0x11`、清除出光时间 `0x14`、截止频率 `0x0C/0x43`、M7 截止频率 `0x15/0x53`、优先模式 `0x1E/0x28`、报警缓存 `0x93/0x96`、系统参数状态 `0x95`、机型 `0x9B/0x9C`、扩展报警次数 `0xA1`、E100 能力 `0x68`、调 Q 参数 `0x8D..0x92`。
- 网络配置：TCP IPv4 地址 `0x6E`、TCP 端口 `0x6F`。写入后需要按设备要求重启才能生效，写入前需要协议工具中的安全维护确认。

连接初始化只读取设备信息和监控数据，不自动发送维护、参数写入或出光命令。协议工具中的写命令由用户明确执行，并受安全确认和当前连接状态限制。

## 构建

当前验证环境为 Ubuntu 24.04 WSL。需要 CMake、pkg-config、GTK4、Libadwaita、GLib 和 C17 编译器。

Ubuntu 24.04 可安装构建依赖：

```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libgtk-4-dev libadwaita-1-dev fonts-noto-cjk
```

构建 GUI、运行协议测试并生成 compile commands：

```bash
cmake -S . -B build-linux \
  -DMS_LEP_BUILD_GUI=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

只构建协议核心和测试：

```bash
cmake -S . -B build-core -DMS_LEP_BUILD_GUI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

静态检查 GUI 源文件：

```bash
clangd --check=src/laser_console.c --compile-commands-dir=build-linux
```

构建完成后，Linux/WSL 环境可运行：

```bash
./build-linux/ms_lep
```

## 验证边界

已完成的自动化检查：

- GTK4/Libadwaita GUI 全量构建。
- 协议测试通过。
- `clangd --check=src/laser_console.c --compile-commands-dir=build-linux` 通过。
- 工作区差异通过 `git diff --check` 检查。

当前环境没有连接 YDFLP-E3-100-M7，因此串口回包、TCP 设备响应、真实出光、JCZ 控制卡和 SG7110 振镜仍需在具备设备和防护条件的现场验证。构建和协议单元测试不能替代硬件联锁、输出状态和关机流程的实机确认。

## 设计参考

界面实现以 GTK4/Libadwaita 的当前原生控件和自适应布局为主，参考以下公开规范和项目：

- [GNOME Human Interface Guidelines](https://developer.gnome.org/hig/)
- [Libadwaita adaptive layouts](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/adaptive-layouts.html)
- [GTK accessibility guidance](https://docs.gtk.org/gtk4/section-accessibility.html)
- [Libadwaita styles and appearance](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/styles-and-appearance.html)
- [Material Design 3](https://m3.material.io/)
- [Apple Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/)
- [Microsoft Fluent 2](https://fluent2.microsoft.design/)

实际采用的设计原则是：连接状态优先、输出操作显式确认、页面按工作任务分组、异步操作显示过程和结果、窄窗口保持可操作、状态信息不依赖颜色单独表达，以及尽量使用框架原生控件保持键盘和辅助技术支持。
