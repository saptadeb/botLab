import pygame
from map import Map
from mbot import Mbot
from timing import Rate
from lidar import Lidar
from geometry import SpaceConverter
import math
import time
import threading
from pygame.locals import *
import sys
import lcm
sys.path.append('../lcmtypes')
from odometry_t import odometry_t
from mbot_motor_command_t import mbot_motor_command_t
from lidar_t import lidar_t
from timestamp_t import timestamp_t


class Gui:
    def __init__(self):
        # Model
        self._map = None
        self._mbot = Mbot()
        self._lidar = Lidar()

        # Controller
        self._lcm = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
        # LCM update rate
        self._lcm_handle_rate = 50
        # LCM channel names
        self._odometry_channel = 'ODOMETRY'
        self._lidar_channel = 'LIDAR'
        self._motor_command_channel = 'MBOT_MOTOR_COMMAND'
        self._timesync_channel = 'MBOT_TIMESYNC'
        # Subscribe to lcm topics
        self._lcm.subscribe(self._odometry_channel, self._odometry_handler)
        self._lcm.subscribe(self._lidar_channel, self._lidar_handler)
        self._lcm.subscribe(self._motor_command_channel, self._motor_command_handler)
        self._lcm.subscribe(self._timesync_channel, self._timesync_handler)
        # Start callback thread
        self._lcm_thread = threading.Thread(target=self._handle_lcm)
        self._lcm_thread.start()

        # View
        self._running = True
        self._display_surf = None
        self._size = self._width, self._height = 640, 400 * 2
        self._sprites = pygame.sprite.RenderUpdates()
        self._max_frame_rate = 50
        self._sprites.add(self._mbot)
        self._space_converter = None

    def on_init(self):
        # Pygame
        pygame.init()
        pygame.display.set_mode(self._size, pygame.HWSURFACE | pygame.DOUBLEBUF)
        self._display_surf = pygame.display.get_surface()
        # Map
        self._map = Map()
        self._map.load_from_file('../../data/astar/maze.map')
        self._space_converter = SpaceConverter(self._map.meters_per_cell * self._map.width / self._width)
        self._display_surf.blit(self._map.render(self._space_converter), (0, 0))
        pygame.display.flip()
        # Start
        self._running = True
        self._lidar.start()

    """ Controller """

    def on_event(self, event):
        if event.type == pygame.QUIT:
            self._running = False

    def on_loop(self):
        msg = mbot_motor_command_t()
        msg.trans_v = 0.01
        msg.angular_v = 0.1
        # self._lcm.publish('MBOT_MOTOR_COMMAND', msg.encode())

    def on_execute(self):
        if self.on_init() == False:
            self._running = False

        while( self._running ):
            with Rate(self._max_frame_rate):
                for event in pygame.event.get():
                    self.on_event(event)
                self.on_loop()
                self.on_render()

        self.on_cleanup()

    """ Controller LCM """

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
        # Override utime with sim time
        msg.utime = int(time.time() * 1e6)
        self.mbot.current_motor_commands.append(msg)

    def _timesync_handler(self, channel, data):
        msg = timestamp_t.decode(data)

    """ View """

    def on_render(self):
        self._sprites.clear(self._display_surf, self._map.image)
        self._sprites.update(self._space_converter)
        self._sprites.draw(self._display_surf)
        pygame.display.flip()

    def on_cleanup(self):
        pygame.quit()


if __name__ == "__main__" :
    sim = Gui()
    sim.on_execute()
