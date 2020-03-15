import pygame
from map import Map
from mbot import Mbot
from timing import Rate
from lidar import Lidar
import math
from pygame.locals import *
import sys
import lcm
sys.path.append('../lcmtypes')
from mbot_motor_command_t import mbot_motor_command_t


class Gui:
    def __init__(self):
        self._running = True
        self._display_surf = None
        self._map = None
        self._size = self._width, self._height = 640, 400
        self._sprites = pygame.sprite.RenderUpdates()
        self._mbot = Mbot()
        self._sprites.add(self._mbot)
        self._frame_rate = 50
        self._lcm = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
        self._lidar = Lidar()

    def on_init(self):
        # Pygame
        pygame.init()
        pygame.display.set_mode(self._size, pygame.HWSURFACE | pygame.DOUBLEBUF)
        self._display_surf = pygame.display.get_surface()
        # Map
        self._map = Map(self._width, self._height)
        self._map.load_from_file('../../data/astar/maze.map')
        self._display_surf.blit(self._map.render(), (0, 0))
        pygame.display.flip()
        # Start
        self._running = True
        self._lidar.start()

    def on_event(self, event):
        if event.type == pygame.QUIT:
            self._running = False

    def on_loop(self):
        msg = mbot_motor_command_t()
        msg.trans_v = 1.0
        msg.angular_v = 0.1
        self._lcm.publish('MBOT_MOTOR_COMMAND', msg.encode())

    def on_render(self):
        self._sprites.clear(self._display_surf, self._map.image)
        self._sprites.update()
        self._sprites.draw(self._display_surf)
        pygame.display.flip()

    def on_cleanup(self):
        pygame.quit()

    def on_execute(self):
        if self.on_init() == False:
            self._running = False

        while( self._running ):
            with Rate(self._frame_rate):
                for event in pygame.event.get():
                    self.on_event(event)
                self.on_loop()
                self.on_render()

        self.on_cleanup()

if __name__ == "__main__" :
    sim = Gui()
    sim.on_execute()
