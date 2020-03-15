import pygame
import geometry
import math

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
