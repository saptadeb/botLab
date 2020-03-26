import sys
import time
import threading
from timing import Rate
import math
import pygame
import numpy
from copy import deepcopy
# Lcm
import lcm
sys.path.append('../lcmtypes')
from lidar_t import lidar_t


class Lidar(pygame.sprite.Sprite):
    def __init__(self, get_current_pose, world_map, space_converter, use_noise, dist_measure_sigma, theta_step_sigma,
                 num_ranges_noise):
        super(Lidar, self).__init__()
        # Model
        self._get_current_pose = get_current_pose
        self._map = world_map
        # Aproximate true lidar parameters
        self._num_ranges = 290
        self._max_distance = 8
        self._scan_rate = 10
        self._beam_period = 1 / (self._num_ranges * self._scan_rate)
        self._thetas = []
        self._ranges = []
        self._times = []
        # Noise
        self._add_noise = use_noise
        self._dist_measure_sigma = dist_measure_sigma
        self._theta_step_sigma = theta_step_sigma
        self._num_ranges_noise = num_ranges_noise

        # Control
        self._lidar_channel = 'LIDAR'
        self._lcm = lcm.LCM('udpm://239.255.76.67:7667?ttl=2')
        self._thread = threading.Thread(target=self.scan)
        self._running = False

        # View
        self._space_converter = space_converter
        self._beam_start_poses = []
        self._beam_end_poses = []
        self._beam_color = (0, 255, 0)
        width = space_converter.to_pixel(self._map._meters_per_cell * self._map._width)
        height = space_converter.to_pixel(self._map._meters_per_cell * self._map._height)
        self.image = pygame.Surface([width, height])
        self.image.set_colorkey((0, 0, 0))
        self.rect = self.image.get_rect()
        self._ready_to_render = False
        self._render_beam_start_poses = []
        self._render_beam_end_poses = []
        self._render_lock = threading.Lock()

    @property
    def num_ranges(self):
        return self._num_ranges

    @num_ranges.setter
    def num_ranges(self, num_ranges):
        self._num_ranges = num_ranges
        self._thetas = [2 * math.pi * x / self._num_ranges for x in range(self._num_ranges)]

    def start(self):
        self._running = True
        self._thread.start()

    def stop(self):
        self._running = False
        self._thread.join()

    def scan(self):
        while self._running:
            with Rate(self._scan_rate):
                now = time.perf_counter()
                num_ranges = self._num_ranges
                theta = 0
                theta_step_size = 2 * math.pi / num_ranges
                beam_period = 1 / (num_ranges * self._scan_rate)
                if self._add_noise:
                    theta = numpy.random.random() * 2 * numpy.pi
                    num_ranges += numpy.random.randint(-self._num_ranges_noise, self._num_ranges_noise)
                    theta_step_size = 2 * math.pi / num_ranges
                    beam_period = 1 / (num_ranges * self._scan_rate)
                for _ in range(num_ranges):
                    self._thetas.append(theta)
                    self._ranges.append(self._beam_scan(now, theta))
                    self._times.append(int(1e6 * now))
                    now -= beam_period
                    theta += theta_step_size
                    if self._add_noise:
                        theta += numpy.random.normal(0, self._theta_step_sigma)
                self._publish()
                self._thetas = []
                self._ranges = []
                self._times = []
                with self._render_lock:
                    self._ready_to_render = True
                    self._render_beam_start_poses = list(self._beam_start_poses)
                    self._render_beam_end_poses = list(self._beam_end_poses)
                self._beam_start_poses = []
                self._beam_end_poses = []

    def _beam_scan(self, at_time, theta):
        # Get the origin of the scan
        pose = deepcopy(self._get_current_pose(at_time))
        # Rotate the pose to point in the direction of the beam
        pose.theta -= theta  # Lidar is z-down (I think)
        self._beam_start_poses.append(pose.as_list()[:2])
        # Ray trace along map until edge of map, max distance, or hit an object
        for x, y, dist in self._beam_step_generator(pose):
            if self._map.at_xy(x, y):
                if self._add_noise:
                    noise = numpy.random.normal(0, self._dist_measure_sigma)
                    dist += noise
                    x += numpy.cos(pose.theta) * noise
                    y += numpy.sin(pose.theta) * noise
                self._beam_end_poses.append((x, y))
                # TODO(Kevin): Calculate exact distance
                return dist
        self._beam_end_poses.append((pose.x + self._max_distance * numpy.cos(pose.theta),
                                     pose.y + self._max_distance * numpy.sin(pose.theta)))
        return self._max_distance

    def _beam_step_generator(self, pose):
        step_size = self._map._meters_per_cell / 2
        dx = numpy.cos(pose.theta) * step_size
        dy = numpy.sin(pose.theta) * step_size
        x = pose.x
        y = pose.y
        dist = 0
        while dist <= self._max_distance:
            yield (x, y, dist)
            x += dx
            y += dy
            dist += step_size

    def _publish(self):
        msg = lidar_t()
        num_ranges = len(self._ranges)
        msg.num_ranges = num_ranges
        msg.ranges = self._ranges
        msg.thetas = self._thetas
        msg.times = self._times
        msg.intensities = [0] * num_ranges
        self._lcm.publish(self._lidar_channel, msg.encode())

    """ View """

    def _render(self, space_converter):
        self.image.fill((0, 0, 0))
        start_pixels = numpy.ones((3, len(self._render_beam_start_poses)))
        start_pixels[:2, :] = numpy.matrix(self._render_beam_start_poses).T
        start_pixels = (space_converter * start_pixels)[:2, :]

        end_pixels = numpy.ones((3, len(self._render_beam_end_poses)))
        end_pixels[:2, :] = numpy.matrix(self._render_beam_end_poses).T
        end_pixels = (space_converter * end_pixels)[:2, :]

        for start, end in zip(start_pixels.T.tolist(), end_pixels.T.tolist()):
            pygame.draw.line(self.image, self._beam_color, start, end)

    def update(self, space_converter):
        if self._ready_to_render:
            with self._render_lock:
                self._render(self._space_converter)
