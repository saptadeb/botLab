import pygame
from map import Map
from mbot import Mbot
from timing import Rate
from lidar import Lidar
import geometry
import time
import threading
from pygame.locals import *
import sys
import numpy
import argparse
import lcm
sys.path.append('../lcmtypes')
from odometry_t import odometry_t
from mbot_motor_command_t import mbot_motor_command_t


class Gui:
    def __init__(self, map_file, render_lidar, use_noise, lidar_dist_measure_sigma, lidar_theta_step_sigma,
                 lidar_num_ranges_noise, odom_trans_sigma, odom_rot_sigma, width, mbot_max_trans_speed,
                 mbot_max_angular_speed):
        # Model
        self._map_file = map_file
        self._use_noise = use_noise
        self._lidar_dist_measure_sigma = lidar_dist_measure_sigma
        self._lidar_theta_step_sigma = lidar_theta_step_sigma
        self._lidar_num_ranges_noise = lidar_num_ranges_noise
        self._odom_trans_sigma = odom_trans_sigma
        self._odom_rot_sigma = odom_rot_sigma
        self._mbot_max_trans_speed = mbot_max_trans_speed
        self._mbot_max_angular_speed = mbot_max_angular_speed
        self._map = None
        self._mbot = None
        self._lidar = None
        self._odom_pose = geometry.Pose(0, 0, 0)
        self._last_pose = geometry.Pose(0, 0, 0)

        # Controller
        self._lcm = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
        # LCM update rate
        self._lcm_handle_rate = 100
        # LCM channel names
        self._odometry_channel = 'ODOMETRY'
        self._motor_command_channel = 'MBOT_MOTOR_COMMAND'
        # Subscribe to lcm topics
        self._lcm.subscribe(self._motor_command_channel, self._motor_command_handler)
        # LCM callback thread
        self._lcm_thread = threading.Thread(target=self._handle_lcm)

        # View
        self._render_lidar = render_lidar
        self._running = True
        self._display_surf = None
        self._width = width
        self._sprites = pygame.sprite.RenderUpdates()
        self._max_frame_rate = 50
        self._space_converter = None

    def on_init(self):
        # Map
        self._map = Map()
        self._map.load_from_file(self._map_file)
        self._space_converter = geometry.SpaceConverter(self._width / (self._map.meters_per_cell * self._map.width),
                                                        (self._map._global_origin_x, self._map._global_origin_y))
        # Mbot
        self._mbot = Mbot(self._map, max_trans_speed=self._mbot_max_trans_speed,
                          max_angular_speed=self._mbot_max_angular_speed)
        # Lidar
        self._lidar = Lidar(lambda at_time: self._mbot.get_pose(at_time), self._map, self._space_converter,
                            use_noise=self._use_noise, dist_measure_sigma=self._lidar_dist_measure_sigma,
                            theta_step_sigma=self._lidar_theta_step_sigma,
                            num_ranges_noise=self._lidar_num_ranges_noise)
        # Pygame
        pygame.init()
        height = self._space_converter.to_pixel(self._map.height * self._map.meters_per_cell)
        pygame.display.set_mode((self._width, height), pygame.HWSURFACE | pygame.DOUBLEBUF)
        self._display_surf = pygame.display.get_surface()
        self._display_surf.blit(self._map.render(self._space_converter), (0, 0))
        pygame.display.flip()
        # Add sprites
        if self._render_lidar:
            self._sprites.add(self._lidar)
        self._sprites.add(self._mbot)
        # Start
        self._lcm_thread.start()
        self._lidar.start()
        self._running = True

    """ Controller """

    def on_event(self, event):
        if event.type == pygame.QUIT:
            self._running = False

    def on_loop(self):
        # Publish odometry
        pose = self._mbot.get_current_pose()
        dpose = pose - self._last_pose
        if self._use_noise and self._mbot.moving:
            trans = numpy.sqrt(dpose.x ** 2 + dpose.y ** 2) + numpy.random.normal(0, self._odom_trans_sigma)
            dpose.theta += numpy.random.normal(0, self._odom_rot_sigma)
            dpose.x = trans * numpy.cos(self._odom_pose.theta + dpose.theta)
            dpose.y = trans * numpy.sin(self._odom_pose.theta + dpose.theta)
        self._odom_pose += dpose
        msg = odometry_t()
        msg.x = self._odom_pose.x
        msg.y = self._odom_pose.y
        msg.theta = self._odom_pose.theta
        msg.utime = int(time.perf_counter() * 1e6)
        self._lcm.publish(self._odometry_channel, msg.encode())
        # Update
        self._last_pose = pose

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


def str2bool(v):
    if isinstance(v, bool):
        return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')


def parse_args():
    """
    Parse input arguments
    """
    parser = argparse.ArgumentParser(description='Simulate the MBot')

    # Main arguments, used for paper's experiments
    parser.add_argument('map_file', type=str, help='Load this map file into the simulator')
    parser.add_argument('--render_lidar', default=False, type=str2bool, help='Render the lidar rays')
    parser.add_argument('-n', '--use_noise', default=True, type=str2bool, help='Simulate noise')
    parser.add_argument('--lidar_dist_measure_sigma', default=0.005, type=float,
                        help='Standard deviation of a 0 mean gaussian distribution used to add random noise to the '
                        'lidar distance measurements')
    parser.add_argument('--lidar_theta_step_sigma', default=0.0002, type=float,
                        help='Standard deviation of a 0 mean gaussian distribution used to add random noise to the '
                        'angles between each lidar scan')
    parser.add_argument('--lidar_num_ranges_noise', default=3, type=int,
                        help='Half the size of a uniform distribution centered at 0 over the integers used to '
                        'randomize the number of lidar measurements')
    parser.add_argument('--odom_trans_sigma', default=1e-3, type=float,
                        help='Standard deviation of a 0 mean gaussian distribution used to add random noise to the '
                        'odometry translation')
    parser.add_argument('--odom_rot_sigma', default=3e-3, type=float,
                        help='Standard deviation of a 0 mean gaussian distribution used to add random noise to the '
                        'odometry rotation')
    parser.add_argument('--width', default=640, type=int, help='Width of the screen')
    parser.add_argument('--mbot_max_trans_speed', default=3.0, type=float, help='Mbot\'s maximum translation speed')
    parser.add_argument('--mbot_max_angular_speed', default=3.0, type=float, help='Mbot\'s maximum angular speed')

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    print('Running simulator with:\n\t{}'.format(args))
    sim = Gui(map_file=args.map_file,
              render_lidar=args.render_lidar,
              use_noise=args.use_noise,
              lidar_dist_measure_sigma=args.lidar_dist_measure_sigma,
              lidar_theta_step_sigma=args.lidar_theta_step_sigma,
              lidar_num_ranges_noise=args.lidar_num_ranges_noise,
              odom_trans_sigma=args.odom_trans_sigma,
              odom_rot_sigma=args.odom_rot_sigma,
              width=args.width,
              mbot_max_trans_speed=args.mbot_max_trans_speed,
              mbot_max_angular_speed=args.mbot_max_angular_speed)
    sim.on_execute()
