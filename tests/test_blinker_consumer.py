#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unit tests for Blinker event-driven consumer.

Usage:
    python test_blinker_consumer.py
"""

import os
import sys
import unittest
from unittest.mock import MagicMock, patch

import numpy as np
from blinker import signal

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "py"))

# Import signals and classes
from consumer_demo_blinker import (
    FrameDecoder,
    FrameDisplay,
    FPSCounter,
    ErrorLogger,
    frame_received,
    frame_decoded,
    frame_displayed,
    fps_updated,
    error_occurred,
    WIDTH,
    HEIGHT,
    CHANNELS,
    FRAME_SIZE,
)


class TestBlinkerEventFlow(unittest.TestCase):
    """测试 Blinker 事件流"""

    def setUp(self):
        """每个测试前清理信号连接"""
        # 清理所有信号连接
        for sig in [
            frame_received,
            frame_decoded,
            frame_displayed,
            fps_updated,
            error_occurred,
        ]:
            sig.receivers.clear()

    def test_frame_decoder_event_flow(self):
        """测试帧解码事件流"""
        # 创建解码器
        decoder = FrameDecoder()

        # 创建接收器来验证 frame_decoded 信号
        received_frames = []

        @frame_decoded.connect
        def capture_decoded(sender, frame, frame_idx):
            received_frames.append((frame.shape, frame_idx))

        # 发送 frame_received 信号
        fake_data = b"\x01\x00\x00\x00" + b"\x00" * (FRAME_SIZE - 4)
        frame_received.send(None, data=fake_data)

        # 验证 frame_decoded 被触发
        self.assertEqual(len(received_frames), 1)
        self.assertEqual(received_frames[0][0], (HEIGHT, WIDTH, CHANNELS))
        self.assertEqual(received_frames[0][1], 1)  # frame_idx = 1

    def test_fps_counter(self):
        """测试 FPS 计数器"""
        fps_counter = FPSCounter(report_interval=5)

        # 模拟 5 帧
        for i in range(5):
            frame_displayed.send(None, frame_idx=i)

        # 验证计数
        self.assertEqual(fps_counter.count, 5)

    def test_error_logger(self):
        """测试错误日志"""
        error_logger = ErrorLogger()

        # 捕获 print 输出
        with patch("builtins.print") as mock_print:
            error_occurred.send(None, error="Test error")
            mock_print.assert_called_once_with("[ERROR] Test error")

    def test_invalid_frame_size(self):
        """测试无效帧大小处理"""
        decoder = FrameDecoder()

        # 创建错误接收器
        errors = []

        @error_occurred.connect
        def capture_error(sender, error):
            errors.append(error)

        # 发送无效大小的数据
        invalid_data = b"\x00" * 100  # 太小
        frame_received.send(None, data=invalid_data)

        # 验证错误被捕获
        self.assertEqual(len(errors), 1)
        self.assertIn("Decode failed", errors[0])

    def test_signal_isolation(self):
        """测试信号隔离性"""
        # 创建两个独立的解码器
        decoder1 = FrameDecoder()
        decoder2 = FrameDecoder()

        decoded_count = [0]

        @frame_decoded.connect
        def count_decoded(sender, frame, frame_idx):
            decoded_count[0] += 1

        # 发送一次信号
        fake_data = b"\x00" * FRAME_SIZE
        frame_received.send(None, data=fake_data)

        # 两个解码器都连接到 frame_received，各自触发一次 frame_decoded
        self.assertEqual(decoded_count[0], 2)


class TestComponentIntegration(unittest.TestCase):
    """测试组件集成"""

    def setUp(self):
        """清理信号"""
        for sig in [
            frame_received,
            frame_decoded,
            frame_displayed,
            fps_updated,
            error_occurred,
        ]:
            sig.receivers.clear()

    def test_full_pipeline(self):
        """测试完整流水线: received → decoded → displayed"""
        # 初始化组件
        decoder = FrameDecoder()

        # 跳过 FrameDisplay（需要 OpenCV）
        display_called = [False]

        @frame_decoded.connect
        def mock_display(sender, frame, frame_idx):
            display_called[0] = True
            frame_displayed.send(sender, frame_idx=frame_idx)

        fps_counter = FPSCounter(report_interval=1)

        # 发送帧
        fake_data = b"\x05\x00\x00\x00" + b"\xFF" * (FRAME_SIZE - 4)
        frame_received.send(None, data=fake_data)

        # 验证整个流水线
        self.assertTrue(display_called[0])
        self.assertEqual(fps_counter.count, 1)

    def test_multiple_frames(self):
        """测试多帧处理"""
        decoder = FrameDecoder()
        fps_counter = FPSCounter(report_interval=10)

        # 模拟显示
        @frame_decoded.connect
        def mock_display(sender, frame, frame_idx):
            frame_displayed.send(sender, frame_idx=frame_idx)

        # 发送 20 帧
        for i in range(20):
            frame_idx_bytes = i.to_bytes(4, byteorder="little")
            fake_data = frame_idx_bytes + b"\x00" * (FRAME_SIZE - 4)
            frame_received.send(None, data=fake_data)

        # 验证计数
        self.assertEqual(fps_counter.count, 20)


def run_tests():
    """运行所有测试"""
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()

    suite.addTests(loader.loadTestsFromTestCase(TestBlinkerEventFlow))
    suite.addTests(loader.loadTestsFromTestCase(TestComponentIntegration))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    return result.wasSuccessful()


if __name__ == "__main__":
    success = run_tests()
    sys.exit(0 if success else 1)
