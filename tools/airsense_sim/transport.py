"""Pluggable, zero-dependency transports for the AirSense simulator.

* SerialPort - real USB-UART adapter. Windows uses the Win32 COM API through
  ctypes; POSIX uses termios. No pyserial / pip install required.
* TcpServer  - useful with a TCP-to-serial bridge or a virtual COM pair.
* Loopback   - in-memory duplex used by the selftest.

All transports are non-blocking: read() returns b"" when no data is available.
"""

from __future__ import annotations

import os
import socket
import struct
import sys


class Transport:
    def read(self, size: int = 4096) -> bytes:
        raise NotImplementedError

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def close(self) -> None:
        pass


class Loopback(Transport):
    """In-memory link. The simulator owns the device end; tests own the host end."""

    def __init__(self):
        self.to_host = bytearray()
        self.to_device = bytearray()

    def read(self, size: int = 4096) -> bytes:
        data = bytes(self.to_device[:size])
        del self.to_device[:size]
        return data

    def write(self, data: bytes) -> None:
        self.to_host.extend(data)

    def host_write(self, data: bytes) -> None:
        self.to_device.extend(data)

    def host_read(self) -> bytes:
        data = bytes(self.to_host)
        self.to_host.clear()
        return data


class TcpServer(Transport):
    def __init__(self, host: str, port: int):
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((host, port))
        self.srv.listen(1)
        self.srv.setblocking(False)
        self.conn = None

    def read(self, size: int = 4096) -> bytes:
        if self.conn is None:
            try:
                self.conn, _ = self.srv.accept()
                self.conn.setblocking(False)
            except (BlockingIOError, OSError):
                return b""
        try:
            data = self.conn.recv(size)
        except BlockingIOError:
            return b""
        except OSError:
            self.conn = None
            return b""
        if not data:
            self.conn.close()
            self.conn = None
            return b""
        return data

    def write(self, data: bytes) -> None:
        if self.conn is not None:
            self.conn.sendall(data)

    def close(self) -> None:
        if self.conn is not None:
            self.conn.close()
        self.srv.close()


class SerialPort(Transport):
    """57600 8N1 by default, matching src/uart_arbiter.cpp:62."""

    def __init__(self, port: str, baud: int = 57600):
        self.port = port
        self.baud = baud
        self._fd = None
        self._h = None
        if sys.platform == "win32":
            self._win_open(port, baud)
        else:
            self._posix_open(port, baud)

    # ---- Windows -----------------------------------------------------
    def _win_open(self, port: str, baud: int) -> None:
        import ctypes
        from ctypes import wintypes

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateFileW.argtypes = [
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
        ]
        k32.CreateFileW.restype = ctypes.c_void_p
        k32.GetCommState.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        k32.SetCommState.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        k32.SetCommTimeouts.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        k32.PurgeComm.argtypes = [ctypes.c_void_p, wintypes.DWORD]
        k32.ReadFile.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
        ]
        k32.WriteFile.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
        ]
        k32.CloseHandle.argtypes = [wintypes.HANDLE]

        path = "\\\\.\\" + port.replace("\\", "")
        handle = k32.CreateFileW(
            path, 0x80000000 | 0x40000000, 0, None, 3, 0, None  # OPEN_EXISTING
        )
        invalid = (1 << (ctypes.sizeof(ctypes.c_void_p) * 8)) - 1
        if handle is None or handle == invalid:
            raise OSError(ctypes.get_last_error(), "cannot open serial port " + port)

        self._k32 = k32
        self._win = wintypes
        self._h = handle
        h = ctypes.c_void_p(handle)

        # DCB is 28 bytes. Let Windows fill it, then change only the fields
        # we care about (BaudRate at +4, ByteSize/Parity/StopBits at +18).
        dcb = ctypes.create_string_buffer(64)
        struct.pack_into("<I", dcb, 0, 28)  # DCBlength
        if not k32.GetCommState(h, dcb):
            raise OSError(ctypes.get_last_error(), "GetCommState failed")
        struct.pack_into("<I", dcb, 4, baud)
        flags = struct.unpack_from("<I", dcb, 8)[0]
        flags = (flags | 0x01) & ~0x02  # fBinary=1, fParity=0
        struct.pack_into("<I", dcb, 8, flags)
        struct.pack_into("<BBB", dcb, 18, 8, 0, 0)  # 8 data bits, no parity, 1 stop
        if not k32.SetCommState(h, dcb):
            raise OSError(ctypes.get_last_error(), "SetCommState failed")

        class COMMTIMEOUTS(ctypes.Structure):
            _fields_ = [
                ("ReadIntervalTimeout", wintypes.DWORD),
                ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
                ("ReadTotalTimeoutConstant", wintypes.DWORD),
                ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
                ("WriteTotalTimeoutConstant", wintypes.DWORD),
            ]

        timeouts = COMMTIMEOUTS(0, 0, 20, 0, 0)  # 20 ms read timeout
        k32.SetCommTimeouts(h, ctypes.byref(timeouts))
        k32.PurgeComm(h, 0x000F)  # clear RX/TX buffers

    def _win_read(self, size: int) -> bytes:
        import ctypes
        buf = ctypes.create_string_buffer(size)
        count = self._win.DWORD()
        ok = self._k32.ReadFile(
            ctypes.c_void_p(self._h), buf, size, ctypes.byref(count), None
        )
        if not ok:
            raise OSError(ctypes.get_last_error(), "ReadFile failed")
        return buf.raw[:count.value]

    def _win_write(self, data: bytes) -> None:
        import ctypes
        buf = ctypes.create_string_buffer(data)
        written = self._win.DWORD()
        ok = self._k32.WriteFile(
            ctypes.c_void_p(self._h), buf, len(data), ctypes.byref(written), None
        )
        if not ok:
            raise OSError(ctypes.get_last_error(), "WriteFile failed")

    # ---- POSIX -------------------------------------------------------
    def _posix_open(self, port: str, baud: int) -> None:
        import termios

        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._fd = fd
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag: raw
        attrs[1] = 0  # oflag: raw
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0  # lflag: no canonical / echo
        speed_names = {
            9600: "B9600", 19200: "B19200", 57600: "B57600",
            115200: "B115200", 230400: "B230400", 460800: "B460800",
        }
        speed = getattr(termios, speed_names.get(baud, "B57600"), termios.B57600)
        attrs[4] = speed
        attrs[5] = speed
        cc = list(attrs[6])
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        attrs[6] = cc
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # ---- Transport API ----------------------------------------------
    def read(self, size: int = 4096) -> bytes:
        if self._h is not None:
            return self._win_read(size)
        try:
            return os.read(self._fd, size)
        except BlockingIOError:
            return b""
        except OSError:
            return b""

    def write(self, data: bytes) -> None:
        if not data:
            return
        if self._h is not None:
            self._win_write(data)
            return
        offset = 0
        while offset < len(data):
            offset += os.write(self._fd, data[offset:])

    def close(self) -> None:
        if self._h is not None:
            self._k32.CloseHandle(self._win.HANDLE(self._h))
            self._h = None
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None
