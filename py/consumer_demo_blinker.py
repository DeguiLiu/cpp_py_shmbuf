# -*- coding: utf-8 -*-
"""Python consumer demo with Blinker event bus: event-driven architecture.

Usage:
    pip install blinker numpy opencv-python
    python consumer_demo_blinker.py [shm_name]

Architecture:
    ShmReader → frame_received → FrameDecoder → frame_decoded → FrameDisplay
                                                              ↓
                                                         FPSCounter
"""

import sys
import time
from typing import Optional

import numpy as np
from blinker import signal

sys.path.insert(0, ".")
from shm_channel import ShmConsumer

# 定义信号
frame_received = signal("frame.received")
frame_decoded = signal("frame.decoded")
frame_displayed = signal("frame.displayed")
fps_updated = signal("fps.updated")
error_occurred = signal("error.occurred")

# 常量
WIDTH = 1920
HEIGHT = 1080
CHANNELS = 3
FRAME_SIZE = WIDTH * HEIGHT * CHANNELS


class ShmReader:
    """负责从共享内存读取原始数据"""

    def __init__(self, shm_name: str):
        self.consumer = ShmConsumer(shm_name)
        print(f"Connected to {shm_name} (capacity: {self.consumer.capacity} bytes)")

    def poll(self) -> bool:
        """轮询共享内存，有数据时发送 frame_received 信号"""
        if not self.consumer.has_data():
            return False

        data = self.consumer.read()
        if data is None or len(data) != FRAME_SIZE:
            error_occurred.send(self, error="Invalid frame size")
            return False

        frame_received.send(self, data=data)
        return True

    def close(self):
        self.consumer.close()


class FrameDecoder:
    """负责解码原始字节为 numpy 数组"""

    def __init__(self):
        frame_received.connect(self.decode)

    def decode(self, sender, data: bytes):
        """解码帧数据"""
        try:
            frame = np.frombuffer(data, dtype=np.uint8).reshape(
                (HEIGHT, WIDTH, CHANNELS)
            )
            frame_decoded.send(self, frame=frame, frame_idx=self._extract_frame_idx(data))
        except Exception as e:
            error_occurred.send(self, error=f"Decode failed: {e}")

    @staticmethod
    def _extract_frame_idx(data: bytes) -> int:
        """从帧数据中提取帧索引（假设前 4 字节是帧索引）"""
        return int.from_bytes(data[:4], byteorder="little")


class FrameDisplay:
    """负责显示帧（如果有 OpenCV）"""

    def __init__(self):
        self.has_cv2 = self._check_opencv()
        if self.has_cv2:
            frame_decoded.connect(self.display)
        else:
            print("OpenCV not available, display disabled")

    @staticmethod
    def _check_opencv() -> bool:
        try:
            import cv2  # noqa: F401

            return True
        except ImportError:
            return False

    def display(self, sender, frame: np.ndarray, frame_idx: int):
        """显示帧"""
        import cv2

        cv2.imshow("SHM Consumer (Blinker)", frame)
        if (cv2.waitKey(1) & 0xFF) == ord("q"):
            raise KeyboardInterrupt("User pressed 'q'")
        frame_displayed.send(self, frame_idx=frame_idx)


class FPSCounter:
    """FPS 统计"""

    def __init__(self, report_interval: int = 100):
        self.count = 0
        self.start_time = time.monotonic()
        self.report_interval = report_interval
        frame_displayed.connect(self.on_frame)

    def on_frame(self, sender, frame_idx: int):
        self.count += 1
        if self.count % self.report_interval == 0:
            elapsed = time.monotonic() - self.start_time
            fps = self.count / elapsed if elapsed > 0 else 0
            fps_updated.send(self, fps=fps, total_frames=self.count)
            print(f"Frame #{frame_idx}, Total: {self.count}, FPS: {fps:.1f}")


class ErrorLogger:
    """错误日志"""

    def __init__(self):
        error_occurred.connect(self.log_error)

    def log_error(self, sender, error: str):
        print(f"[ERROR] {error}")


def main():
    shm_name = sys.argv[1] if len(sys.argv) > 1 else "shm_video"

    # 初始化所有组件（事件驱动）
    reader = ShmReader(shm_name)
    decoder = FrameDecoder()
    display = FrameDisplay()
    fps_counter = FPSCounter(report_interval=100)
    error_logger = ErrorLogger()

    print("Event-driven consumer started. Press 'q' to quit.")

    try:
        while True:
            if not reader.poll():
                time.sleep(0.001)  # 无数据时短暂休眠
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        reader.close()
        print(f"Total frames processed: {fps_counter.count}")


if __name__ == "__main__":
    main()
