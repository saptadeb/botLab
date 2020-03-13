import pygame
import geometry
import math

class Mbot:
    def __init__(self):
        self._pose = geometry.Pose(320, 200, math.pi / 2)
        self._radius = 10
        self._primary_color = pygame.Color(0, 0, 255)
        self._secondary_color = pygame.Color(255, 0, 0)
        self._dir_indicator_arc_length = math.pi / 3

    def render(self, surf):
        update_rect = pygame.draw.circle(surf, self._primary_color, (int(self._pose.x), int(self._pose.y)),
                                         math.ceil(self._radius))
        pygame.draw.arc(
            surf,
            self._secondary_color,
            update_rect,
            self._pose.rotation() - self._dir_indicator_arc_length / 2,
            self._pose.rotation() + self._dir_indicator_arc_length / 2,
            5
        )
