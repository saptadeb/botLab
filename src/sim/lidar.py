import sys
import time
import threading
from timing import Rate
import math
# Lcm
import lcm
sys.path.append('../lcmtypes')
from lidar_t import lidar_t

class Lidar:
    def __init__(self):
        self._num_ranges = 360
        self._thetas = [2 * math.pi * x / self._num_ranges for x in range(self._num_ranges)]
        self._max_distance = 100
        self._scan_rate = 10
        self._beam_rate = self._num_ranges * self._scan_rate
        self._ranges = []
        self._times = []
        self._lidar_channel = 'LIDAR'
        self._lcm = lcm.LCM('udpm://239.255.76.67:7667?ttl=2')
        self._thread = threading.Thread(target=self.scan)

    def start(self):
        self._thread.start()

    def scan(self):
        while True:
            with Rate(self._beam_rate):
                self._ranges.append(self._max_distance)
                self._times.append(int(1e6 * time.time()))
                if len(self._ranges) == self._num_ranges:
                    self._publish()
                    self._ranges = []
                    self._times = []

    def _publish(self):
        msg = lidar_t()
        msg.num_ranges = self._num_ranges
        msg.ranges = self._ranges
        msg.thetas = self._thetas
        msg.times = self._times
        msg.intensities = [0] * self._num_ranges
        self._lcm.publish(self._lidar_channel, msg.encode())
