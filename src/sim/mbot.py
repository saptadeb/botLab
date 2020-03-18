import pygame
import geometry
import math
import lcm
import sys
import time
import threading
from timing import Rate

sys.path.append('../lcmtypes')
from mbot_motor_command_t import mbot_motor_command_t


class Mbot(pygame.sprite.Sprite):
    def __init__(self):
        super(Mbot, self).__init__()

        # Model
        self._pose = geometry.Pose(4.5, 4.5, math.pi / 2)
        stop = mbot_motor_command_t()
        stop.utime = int(time.time() * 1e6)
        self.current_motor_commands = [stop]
        self._radius = 0.1

        # View
        self._primary_color = pygame.Color(0, 0, 255)
        self._secondary_color = pygame.Color(255, 0, 0)
        self._dir_indicator_arc_length = math.pi / 3
        self.image = pygame.Surface([10, 10])
        self.image.set_colorkey((0, 0, 0))
        self.rect = self.image.get_rect()

    """ View """

    def _render(self, space_converter):
        radius = space_converter.to_pixel(self._radius)
        self.image = pygame.Surface([radius * 2.5, radius * 2.5])
        self.image.set_colorkey((0, 0, 0))
        self.rect = self.image.get_rect()
        self.rect.center = [space_converter.to_pixel(x) for x in self._pose.translation()]
        self.image.fill((0, 0, 0))
        update_rect = pygame.draw.circle(self.image, self._primary_color, (radius, radius), radius)
        pygame.draw.arc(
            self.image,
            self._secondary_color,
            update_rect,
            self._pose.rotation() - self._dir_indicator_arc_length / 2,
            self._pose.rotation() + self._dir_indicator_arc_length / 2,
            5
        )

    """ Controller """

    def update(self, space_converter):
        self._render(space_converter)
