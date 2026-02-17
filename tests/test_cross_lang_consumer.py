#!/usr/bin/env python3
"""Cross-language test: read messages written by C++ producer."""

import sys

sys.path.insert(0, ".")

from shm_channel import ShmConsumer

expected = ["hello_from_cpp", "message_2", "cross_language_test", "1234567890", "end"]

consumer = ShmConsumer("test_cross_lang", size=4096 + 16)
passed = 0
failed = 0

for exp in expected:
    data = consumer.read()
    if data is not None and data.decode("utf-8") == exp:
        passed += 1
        print(f"  PASS: read '{exp}'")
    else:
        failed += 1
        print(f"  FAIL: expected '{exp}', got {data!r}")

# Should be empty now
data = consumer.read()
if data is None:
    passed += 1
else:
    failed += 1
    print(f"  FAIL: expected None, got {data!r}")

consumer.close()
print(f"\nCross-language: {passed} passed, {failed} failed")
sys.exit(1 if failed > 0 else 0)
