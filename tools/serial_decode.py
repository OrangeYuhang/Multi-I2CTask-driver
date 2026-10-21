#!/usr/bin/env python3
"""解码 GY521 的串口帧并打印读数（纯标准库，无第三方依赖）。

帧格式（见 Hardware/usart1.h）：
    | 0xAA | 0x55 | LEN | DATA[0..LEN-1] | SUM |
    SUM = (0xAA + 0x55 + LEN + sum(DATA)) & 0xFF

DATA 是 ASCII 文本行："ax=  1234 ay= -5678 ... gz=  ..."
输出原始行 + 物理量（换算系数见 GY521.h）。

用法：
    python3 serial_decode.py [端口]
    端口默认 /dev/ttyUSB0；不插参会用第一遍扫描找到的 USB 串口。

退出：Ctrl-C
"""

import os
import re
import sys
import termios
import tty

# 115200 / 8 数据位 / 无校验 / 1 停止位（和固件一致，见 usart1.c 说明）
BAUD = termios.B115200

FRAME_HEAD1 = 0xAA
FRAME_HEAD2 = 0x55
FRAME_DATA_MAX = 64

# GY521.h 里的量程换算系数
ACCEL_LSB_PER_G = 16384.0   # ±2g
GYRO_LSB_PER_DPS = 16.4     # ±2000°/s

LINE_RE = re.compile(
    r"ax=\s*(-?\d+)\s+ay=\s*(-?\d+)\s+az=\s*(-?\d+)\s+"
    r"gx=\s*(-?\d+)\s+gy=\s*(-?\d+)\s+gz=\s*(-?\d+)"
)


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for name in os.listdir("/dev"):
        if name.startswith("ttyUSB") or name.startswith("ttyACM"):
            return f"/dev/{name}"
    sys.exit("没找到 USB 串口。插上 USB-TTL 后重试，或手动指定："
             "python3 serial_decode.py /dev/ttyUSB0")


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)

    attrs[0] = 0                    # iflag：全关
    attrs[1] = 0                    # oflag
    attrs[2] &= ~termios.CSTOPB     # 1 停止位
    attrs[2] &= ~termios.CSIZE
    attrs[2] |= termios.CS8         # 8 数据位
    attrs[2] &= ~termios.PARENB     # 无校验（8N1）
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[3] = 0                    # lflag
    attrs[4] = BAUD                 # 输入波特率
    attrs[5] = BAUD                 # 输出波特率
    attrs[6][termios.VMIN] = 0      # 非阻塞读
    attrs[6][termios.VTIME] = 0

    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def read_byte(fd):
    try:
        b = os.read(fd, 1)
    except BlockingIOError:
        return None
    return b[0] if b else None


def read_exact(fd, n):
    out = bytearray()
    while len(out) < n:
        b = read_byte(fd)
        if b is not None:
            out.append(b)
    return out


def frames(fd):
    """同步帧头，校验 SUM，产出 DATA 字节串。失步自动重同步。"""
    state = 0
    while True:
        b = read_byte(fd)
        if b is None:
            continue

        if state == 0:                      # 等 0xAA
            if b == FRAME_HEAD1:
                state = 1
        elif state == 1:                    # 等 0x55
            if b == FRAME_HEAD2:
                state = 2
            elif b != FRAME_HEAD1:
                state = 0
        elif state == 2:                    # 读长度
            length = b
            if length == 0 or length > FRAME_DATA_MAX:
                state = 0
                continue
            rest = read_exact(fd, length + 1)
            data, rsum = bytes(rest[:length]), rest[length]
            expect = (FRAME_HEAD1 + FRAME_HEAD2 + length + sum(data)) & 0xFF
            if rsum != expect:
                print(f"[帧校验失败] 期望 {expect:02X} 收到 {rsum:02X}，丢弃", flush=True)
            else:
                yield data
            state = 0


def show(text):
    line = text.decode("ascii", errors="replace")
    m = LINE_RE.match(line)
    if not m:
        print(f"[无法解析] {line}", flush=True)
        return
    ax, ay, az, gx, gy, gz = (int(v) for v in m.groups())
    one = (f"ax={ax / ACCEL_LSB_PER_G:+6.3f}g "
           f"ay={ay / ACCEL_LSB_PER_G:+6.3f}g "
           f"az={az / ACCEL_LSB_PER_G:+6.3f}g | "
           f"gx={gx / GYRO_LSB_PER_DPS:+6.1f}°/s "
           f"gy={gy / GYRO_LSB_PER_DPS:+6.1f}°/s "
           f"gz={gz / GYRO_LSB_PER_DPS:+6.1f}°/s")

    if os.isatty(1):
        # 原地刷新：回行首 + 清到行尾，屏幕只留一行跳动
        sys.stdout.write("\r\x1b[K" + one)
        sys.stdout.flush()
    else:
        print(one, flush=True)


def main():
    port = find_port()
    print(f"打开 {port}（115200 8N1），Ctrl-C 退出。")
    fd = open_port(port)
    try:
        for data in frames(fd):
            show(data)
    except KeyboardInterrupt:
        pass
    finally:
        os.close(fd)
    if os.isatty(1):
        print()  # 退出时换行，别让 shell 提示符粘在数据行后面


if __name__ == "__main__":
    main()
