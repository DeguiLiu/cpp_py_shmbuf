#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Minimal unit tests for Blinker event flow (no numpy/opencv required).

Usage:
    python test_blinker_minimal.py
"""

import unittest
from blinker import signal


class TestBlinkerSignalFlow(unittest.TestCase):
    """测试 Blinker 基本信号流"""

    def test_signal_send_receive(self):
        """测试信号发送和接收"""
        test_signal = signal("test")
        received = []

        @test_signal.connect
        def handler(sender, **kwargs):
            received.append(kwargs)

        # 发送信号
        test_signal.send(None, data="hello", count=42)

        # 验证接收
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0]["data"], "hello")
        self.assertEqual(received[0]["count"], 42)

    def test_multiple_subscribers(self):
        """测试多个订阅者"""
        test_signal = signal("multi")
        results = []

        @test_signal.connect
        def handler1(sender, value):
            results.append(f"h1:{value}")

        @test_signal.connect
        def handler2(sender, value):
            results.append(f"h2:{value}")

        # 发送信号
        test_signal.send(None, value=100)

        # 验证两个处理器都收到
        self.assertEqual(len(results), 2)
        self.assertIn("h1:100", results)
        self.assertIn("h2:100", results)

    def test_signal_chain(self):
        """测试信号链: sig1 → sig2 → sig3"""
        sig1 = signal("sig1")
        sig2 = signal("sig2")
        sig3 = signal("sig3")

        results = []

        @sig1.connect
        def on_sig1(sender, data):
            results.append(f"sig1:{data}")
            sig2.send(sender, data=data * 2)

        @sig2.connect
        def on_sig2(sender, data):
            results.append(f"sig2:{data}")
            sig3.send(sender, data=data + 10)

        @sig3.connect
        def on_sig3(sender, data):
            results.append(f"sig3:{data}")

        # 触发链
        sig1.send(None, data=5)

        # 验证链式传播
        self.assertEqual(len(results), 3)
        self.assertEqual(results[0], "sig1:5")
        self.assertEqual(results[1], "sig2:10")  # 5 * 2
        self.assertEqual(results[2], "sig3:20")  # 10 + 10

    def test_event_driven_pipeline(self):
        """测试事件驱动流水线（模拟视频处理）"""
        # 定义信号
        frame_received = signal("frame.received")
        frame_decoded = signal("frame.decoded")
        frame_displayed = signal("frame.displayed")

        # 统计
        stats = {"received": 0, "decoded": 0, "displayed": 0}

        # 解码器
        @frame_received.connect
        def decoder(sender, data):
            stats["received"] += 1
            # 模拟解码
            decoded = data.upper()
            frame_decoded.send(sender, frame=decoded)

        # 显示器
        @frame_decoded.connect
        def display(sender, frame):
            stats["decoded"] += 1
            # 模拟显示
            frame_displayed.send(sender)

        # 计数器
        @frame_displayed.connect
        def counter(sender):
            stats["displayed"] += 1

        # 模拟 3 帧
        for i in range(3):
            frame_received.send(None, data=f"frame{i}")

        # 验证流水线
        self.assertEqual(stats["received"], 3)
        self.assertEqual(stats["decoded"], 3)
        self.assertEqual(stats["displayed"], 3)

    def test_error_handling(self):
        """测试错误处理"""
        data_signal = signal("data")
        error_signal = signal("error")

        errors = []

        @data_signal.connect
        def processor(sender, value):
            if value < 0:
                error_signal.send(sender, error=f"Invalid value: {value}")
            else:
                # 正常处理
                pass

        @error_signal.connect
        def error_logger(sender, error):
            errors.append(error)

        # 发送正常数据
        data_signal.send(None, value=10)
        self.assertEqual(len(errors), 0)

        # 发送异常数据
        data_signal.send(None, value=-5)
        self.assertEqual(len(errors), 1)
        self.assertIn("Invalid value: -5", errors[0])


def run_tests():
    """运行所有测试"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestBlinkerSignalFlow)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return result.wasSuccessful()


if __name__ == "__main__":
    import sys

    print("=" * 60)
    print("Testing Blinker Event-Driven Architecture")
    print("=" * 60)
    success = run_tests()
    print("\n" + "=" * 60)
    if success:
        print("✅ All tests passed!")
    else:
        print("❌ Some tests failed!")
    print("=" * 60)
    sys.exit(0 if success else 1)
