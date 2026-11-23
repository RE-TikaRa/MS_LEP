# MS_LEP

本分支构建 GTK3 桌面程序：[界面用法与依赖](ui/README.md)。

JPT YDFLP-E3-100-M7 的 C 通信库。协议、串口与 TCP 传输、异步设备客户端和测试在 `main` 维护。桌面程序位于 `GTK3` 和 `GTK4` 分支；两个前端链接同一个 `ms_lep_core`。

## 构建与测试

Ubuntu 18.04 起可使用系统提供的 C11 编译器、CMake 3.10 和 GLib/GIO 2.56。核心不需要显示服务或 GTK 开发包。

```sh
sudo apt install build-essential cmake pkg-config libglib2.0-dev python3
mkdir -p build-core && cd build-core
cmake -DMS_LEP_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
ctest --output-on-failure
```

## 接口

- `jpt_protocol.h`：按长度编码、解析小端 JPT 帧；不依赖通信方式。
- `jpt_device.h`：解码设备版本、优先模式、累计时间、状态、监控、M7 截止表及报警记录。返回数据字段，名称和单位的显示由前端处理。
- `jpt_transport.h`：枚举 Linux 串口，优先采用 by-id 名称，同一设备只列一次；串口和 TCP 共用请求 / 应答。串口默认参数为 57600、8N1、无流控。设备拒绝、超时和连接中断是不同的结果。
- `jpt_client.h`：I/O 在工作线程串行执行，结果经调用方的 GLib 主上下文返回。轮询返回含本次会话设备信息的快照。`jpt_client_safe_disconnect_async` 读取控制来源、关闭软件控制的输出并核对结果；应答错误或输出未关闭时不报告成功。打开连接不修改设备参数。

核心测试使用合成数据，不验证光学输出、硬件联锁或现场接线。连接真实设备时，状态读回不能替代物理防护。

报警缓存的长帧格式、采样单位和原厂资料出处见[报警记录与回放](docs/alarm-replay.md)。

## 虚拟串口

在 Linux 仓库根目录运行 `python3 tools/virtual_device.py`，终端会打印一个新建的 `/dev/pts/N`。保持进程运行，在 GTK 客户端选择“手动输入设备路径”，填入打印的路径即可。按 Ctrl+C 结束。模拟器不打开 `/dev/tty*`，不转发请求；重启会重置状态。

模拟范围为《JPT-E 串口指令说明 20240401》表 1.0。设置类命令回显成功写入的字，读取类命令返回相应寄存器或变长数据；不支持的命令、错误长度、越界值及非法帧返回单字节 `0x45`。默认 SN、控制模式、传感器和报警样本取自手册示例。还提供合成的固件版本、累计时间和优先模式读数。输出标志只表示内存状态，不模拟光学行为、引脚时序或联锁。

`ctest --output-on-failure` 会启动独立的虚拟设备进程，将手册样例按字节比对，并让生产客户端完成串口初始化、写入回读和安全断开。模拟器可以用 `--fault reject-stop`、`--fault bad-ack` 或 `--fault stuck-status` 检查未确认断开的情形。

虚拟设备的报警缓存有三条合成记录：温度传感器、TEC、供电电压。用 `--empty-alarm-cache` 启动可检查空缓存。
