import pygame
from map import Map
from mbot import Mbot
from timing import Rate
from lidar import Lidar
from geometry import SpaceConverter
import time
import threading
from pygame.locals import *
import sys
import lcm
sys.path.append('../lcmtypes')
from odometry_t import odometry_t
from mbot_motor_command_t import mbot_motor_command_t


class Gui:
    def __init__(self):
        # Model
        self._map = None
        self._mbot = None
        self._lidar = None

        # Controller
        self._lcm = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
        # LCM update rate
        self._lcm_handle_rate = 100
        # LCM channel names
        self._odometry_channel = 'ODOMETRY'
        self._motor_command_channel = 'MBOT_MOTOR_COMMAND'
        # Subscribe to lcm topics
        self._lcm.subscribe(self._motor_command_channel, self._motor_command_handler)
        # Start callback thread
        self._lcm_thread = threading.Thread(target=self._handle_lcm)

        # View
        self._running = True
        self._display_surf = None
        self._size = self._width, self._height = 1000, 1000
        self._sprites = pygame.sprite.RenderUpdates()
        self._max_frame_rate = 50
        self._space_converter = None

    def on_init(self):
        # Pygame
        pygame.init()
        pygame.display.set_mode(self._size, pygame.HWSURFACE | pygame.DOUBLEBUF)
        self._display_surf = pygame.display.get_surface()
        # Map
        self._map = Map()
        self._map.load_from_file('../../data/obstacle_slam_10mx10m_5cm.map')
        self._space_converter = SpaceConverter(self._width / (self._map.meters_per_cell * self._map.width),
                                               (self._map._global_origin_x, self._map._global_origin_y))
        self._display_surf.blit(self._map.render(self._space_converter), (0, 0))
        pygame.display.flip()
        # Mbot
        self._mbot = Mbot(self._map)
        # Lidar
        self._lidar = Lidar(lambda at_time: self._mbot.get_pose(at_time), self._map, self._space_converter)
        self._lidar.start()
        # Add sprites
        self._sprites.add(self._lidar)
        self._sprites.add(self._mbot)
        # Start
        self._lcm_thread.start()
        self._running = True

    """ Controller """

    def on_event(self, event):
        if event.type == pygame.QUIT:
            self._running = False

    def on_loop(self):
        # Publish odometry
        pose = self._mbot.get_current_pose()
        msg = odometry_t()
        msg.x = pose.x
        msg.y = pose.y
        msg.theta = pose.theta
        self._lcm.publish(self._odometry_channel, msg.encode())

    def on_execute(self):
        if self.on_init() is False:
            self._running = False

        while self._running:
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

    def _motor_command_handler(self, channel, data):
        msg = mbot_motor_command_t.decode(data)
        # Override utime with sim time
        msg.utime = int(time.perf_counter() * 1e6)
        self._mbot.add_motor_cmd(msg)

    """ View """

    def on_render(self):
        self._sprites.clear(self._display_surf, self._map.image)
        self._sprites.update(self._space_converter)
        self._sprites.draw(self._display_surf)
        pygame.display.flip()

    def on_cleanup(self):
        self._lidar.stop()
        pygame.quit()


if __name__ == "__main__":
    sim = Gui()
    sim.on_execute()
