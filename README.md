# MS_LEP

MS_LEP 是 YDFLP-E3-100-M7 脉冲光纤激光器的 Linux 调试控制台，使用 GTK4 + Libadwaita 原生界面，支持 USB-RS232 串口和激光器 TCP 服务端。

## 功能

- 串口设备自动枚举，显示设备名称与路径，支持刷新和手动指定路径；串口 `57600 / 8N1`、无流控
- 连接页按所选方式显示串口设备或 TCP 地址；串口参数可展开配置，默认使用手册要求的 `57600 / 8N1 / 无流控`
- TCP 连接，默认地址 `192.168.0.10:5000`
- 功率、频率、脉宽、Simmer 和默认参数写入
- 实时参数支持回车单项写入和批量应用
- JCZ/DB25 外控 / MS_LEP 内控模式，连接后沿用设备读回的控制来源
- GUI PA、GUI MO、红光信号控制
- 实时功率、频率、脉宽、传感器温度、种子源 TEC、输出电流监控
- 状态字、报警状态、报警次数和 D0-D7 输入状态
- 未完成设备读取前禁用控制页，顶部状态灯显示连接、报警和命令状态
- 原始 HEX TX/RX 通信日志
- 协议工具页覆盖说明书中的读写命令，并支持厂商 TCP IP 地址与端口写入
- 浅色/深色系统主题和 HiDPI 友好布局

应用启动时保持断开，PA、MO、红光均关闭。输出按钮需要先勾选防护确认，顶部的“紧急关闭”会依次发送 PA=0、MO=0、红光=0。

## 界面设计

界面视觉与交互参考当前公开的 Apple Human Interface Guidelines、Material 3 Foundations、GNOME Human Interface Guidelines 和 Libadwaita Styles & Appearance：

- 控制台、监控、设备设置、HEX 日志、协议工具、报警记录和型号扩展采用平坦导航；宽窗口显示侧边栏，窄窗口使用 Libadwaita 覆盖式侧边栏。
- 启动及断开状态在主区域显示连接设置；设备信息读取完成后再显示控制台、监控、设备设置、HEX 日志和协议工具。
- 连接页采用紧凑左右分栏：左侧使用说明书中的设备实物图和型号识别，右侧集中处理连接方式、设备地址与串口参数；窄窗口切换为垂直布局。
- 连接状态、设备身份、最近命令和错误信息位于同一条状态区域，用户不需要切换页面寻找反馈。
- 控制台保留实时参数、内控/外控切换、MO/PA/红光和安全确认；连接后沿用设备读回的控制来源。
- 设备设置单独承载 GUI 控制位、DB25 选项、锁定策略和默认参数，避免与高频输出操作混在同一页。
- 型号扩展页面提供 M7 截止频率和报警缓存的专用操作；调 Q 等跨型号扩展通过协议工具按设备能力手动读取。
- 参数写入和输出操作保持明确的控件分组；不可用状态会随连接状态、控制来源和安全确认变化。
- 页面分组、属性行、参数行、状态行和导航均使用 GTK4/Libadwaita 官方控件，遵循系统主题样式。
- 使用 Libadwaita 的系统 accent、success、warning、error 和 dark-mode 语义色，不在应用内硬编码一套与桌面冲突的浅色/深色主题。
- 监控数字使用稳定的固定区域，报警同时使用文字、颜色和累计次数表达，不依赖颜色单独传达状态。

参考：

- Apple HIG: https://developer.apple.com/design/human-interface-guidelines/
- Material 3 Foundations: https://m3.material.io/foundations
- GNOME HIG Navigation: https://developer.gnome.org/hig/guidelines/navigation.html
- Libadwaita Styles & Appearance: https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/styles-and-appearance.html
- Rnote: https://github.com/flxzt/rnote
- WhiteSur GTK theme: https://github.com/vinceliuice/WhiteSur-gtk-theme

## 设备连接

激光器 RS-232 DB9 定义：

| 激光器引脚 | 定义 |
| --- | --- |
| 2 | RXD，激光器接收 |
| 3 | TXD，激光器发送 |
| 5 | GND |

使用真正的 USB-RS232 转换器，不能使用 TTL 或 RS-485 转换器。串口线应交叉连接 TX/RX，并确认激光器完成上电初始化后再连接。

激光器使用 24 V DC 供电，接线前检查电源极性。1064 nm 为不可见 IV 级激光，说明书提示即使功率设为 0%，输出端仍可能有约 100 mW 激光。调试时应使用符合设备要求的防护眼镜和联锁。

## 构建

Ubuntu 24.04：

```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libgtk-4-dev libadwaita-1-dev fonts-noto-cjk
cmake -S . -B build-linux -DMS_LEP_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
./build-linux/ms_lep
```

没有 GTK4 开发包时，可以先构建协议测试：

```bash
cmake -S . -B build-core -DMS_LEP_BUILD_GUI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Linux 用户需要访问串口设备时，通常将当前用户加入 `dialout` 组并重新登录。设备路径以现场枚举结果为准，不要复用历史端口号。

## 协议边界

JPT 帧格式为：`0x24` 帧头、2 字节小端命令码、1 字节 payload 长度、payload、`0x2A` 帧尾。SN 只读请求为：

```text
24 3F 00 02 00 00 2A
```

程序按长度字段读取变长回复，保留原始 TX/RX 字节，并在设备返回 `0x45` 时显示协议拒绝信息。串口和 TCP 共享同一协议编解码器，阻塞 I/O 位于 GLib 工作线程，不占用 GTK 主线程。

状态字 `0x38` 的 bit0..3 分别表示 PA、MO、红光和泵浦；bit4..11 表示 DB25 的 D7..D0。手册示例 `0x18` 表示泵浦已使能、D7 输入为高，外部功率值为 `0x80`，即约 50%。监控页同时显示 8 路输入位和合成数值。

重要写命令：`0x01` 功率、`0x02` 频率、`0x03` 脉宽、`0x04` Simmer、`0x05` 默认脉宽、`0x06` 锁定频率、`0x07` 参数选择、`0x08` 控制来源、`0x09` PA、`0x0A` MO、`0x0B` 红光、`0x17` 锁定功率、`0x18` 红光功率、`0x19` 锁定选项。

原厂 GUI 使用 `0x6E` 写入 TCP IPv4 地址、`0x6F` 写入 TCP 端口。MS_LEP 将这两个命令放在协议工具页，写入前需要安全确认，设备重启后新地址才会生效。

协议工具也提供原厂维护命令的手动入口，包括报警屏蔽 `0x0F/0x41`、报警复位 `0x10`、报警统计清除 `0x11`、出光时间清除 `0x14`、ControlSel `0x16`、优先模式 `0x1E/0x28`、报警缓存 `0x93/0x96`、系统参数状态 `0x95`、机型 `0x9B/0x9C` 和扩展报警次数 `0xA1`。这些扩展命令不会在连接初始化时自动发送。

## 验证边界

当前已在 Ubuntu 24.04 WSL 中完成 GTK4/Libadwaita 编译、协议测试和 `clangd --check`。当前环境没有连接 YDFLP-E3-100-M7，因此串口回包、TCP 设备响应、真实出光、JCZ 控制卡和 SG7110 振镜尚未进行实机验证。
