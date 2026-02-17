# -*- coding: utf-8 -*-
"""Python consumer demo: reads frames from shared memory and displays via OpenCV.

Usage:
    python consumer_demo.py [shm_name]

Requires: numpy, opencv-python
    pip install numpy opencv-python
"""

import sys
import time

import numpy as np

sys.path.insert(0, ".")
from shm_channel import ShmConsumer

WIDTH = 1920
HEIGHT = 1080
CHANNELS = 3
FRAME_SIZE = WIDTH * HEIGHT * CHANNELS


def main():
    shm_name = sys.argv[1] if len(sys.argv) > 1 else "shm_video"

    print(f"Connecting to shared memory: {shm_name}")
    consumer = ShmConsumer(shm_name)
    print(f"Connected (capacity: {consumer.capacity} bytes)")

    frame_count = 0
    t0 = time.monotonic()

    try:
        import cv2
        has_cv2 = True
    except ImportError:
        has_cv2 = False
        print("OpenCV not available, printing frame info only")

    while True:
        if not consumer.has_data():
            time.sleep(0.001)
            continue

        data = consumer.read()
        if data is None or len(data) != FRAME_SIZE:
            continue

        frame_count += 1

        if has_cv2:
            img = np.frombuffer(data, dtype=np.uint8).reshape((HEIGHT, WIDTH, CHANNELS))
            cv2.imshow("SHM Consumer", img)
            if (cv2.waitKey(1) & 0xFF) == ord('q'):
                break
        else:
            # No OpenCV: just print stats
            frame_idx = int.from_bytes(data[:4], byteorder='little')
            if frame_count % 100 == 0:
                elapsed = time.monotonic() - t0
                fps = frame_count / elapsed if elapsed > 0 else 0
                print(f"Frame #{frame_idx}, total: {frame_count}, FPS: {fps:.1f}")

    consumer.close()
    print(f"Total frames received: {frame_count}")


if __name__ == "__main__":
    main()
