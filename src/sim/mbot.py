import pygame
import geometry
import math
import lcm
import sys
import threading
from timing import Rate

sys.path.append('../lcmtypes')
from odometry_t import odometry_t
from mbot_motor_command_t import mbot_motor_command_t
from lidar_t import lidar_t
from timestamp_t import timestamp_t


class Mbot(pygame.sprite.Sprite):
    def __init__(self):
        super(Mbot, self).__init__()
        self._pose = geometry.Pose(320, 200, math.pi / 2)
        self._radius = 10
        self._primary_color = pygame.Color(0, 0, 255)
        self._secondary_color = pygame.Color(255, 0, 0)
        self._dir_indicator_arc_length = math.pi / 3
        self.image = pygame.Surface([self._radius * 2.5, self._radius * 2.5])
        self.image.set_colorkey((0, 0, 0))
        self.rect = self.image.get_rect()
        # LCM update rate
        self._lcm_handle_rate = 50
        # LCM channel names
        self._odometry_channel = 'ODOMETRY'
        self._lidar_channel = 'LIDAR'
        self._motor_command_channel = 'MBOT_MOTOR_COMMAND'
        self._timesync_channel = 'MBOT_TIMESYNC'
        # Subscribe to lcm topics
        self._lcm = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
        self._lcm.subscribe(self._odometry_channel, self._odometry_handler)
        self._lcm.subscribe(self._lidar_channel, self._lidar_handler)
        self._lcm.subscribe(self._motor_command_channel, self._motor_command_handler)
        self._lcm.subscribe(self._timesync_channel, self._timesync_handler)
        # Start callback thread
        self._lcm_thread = threading.Thread(target=self._handle_lcm)
        self._lcm_thread.start()

    def _handle_lcm(self):
        try:
            while True:
                with Rate(self._lcm_handle_rate):
                    self._lcm.handle_timeout(1000.0 / self._lcm_handle_rate)
        except KeyboardInterrupt:
            print("lcm exit!")
            sys.exit()

    def _odometry_handler(self, channel, data):
        msg = odometry_t.decode(data)

    def _lidar_handler(self, channel, data):
        msg = lidar_t.decode(data)

    def _motor_command_handler(self, channel, data):
        msg = mbot_motor_command_t.decode(data)
        self._pose.theta += msg.angular_v
        self._pose.x += msg.trans_v * math.cos(-self._pose.theta)
        self._pose.y += msg.trans_v * math.sin(-self._pose.theta)

    def _timesync_handler(self, channel, data):
        msg = timestamp_t.decode(data)

    def update(self):
        self.rect.center = [int(x) for x in self._pose.translation()]
        self.image.fill((0, 0, 0))
        update_rect = pygame.draw.circle(self.image, self._primary_color, (int(self._radius), int(self._radius)),
                                         math.ceil(self._radius))
        pygame.draw.arc(
            self.image,
            self._secondary_color,
            update_rect,
            self._pose.rotation() - self._dir_indicator_arc_length / 2,
            self._pose.rotation() + self._dir_indicator_arc_length / 2,
            5
        )
