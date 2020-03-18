import pygame
import geometry
import math
import lcm
import sys
import time
import threading
from copy import copy, deepcopy
from timing import Rate

sys.path.append('../lcmtypes')
from mbot_motor_command_t import mbot_motor_command_t


class Mbot(pygame.sprite.Sprite):
    def __init__(self):
        super(Mbot, self).__init__()

        # Model
        self._pose = geometry.Pose(0, 0, 0)
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
        self.rect.center = (space_converter * self._pose.translation())[0:2].T.tolist()[0]
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

    def interpolate_pose(self, at_time):
        print('-----------------------')
        print_cmd = lambda name, cmd: print('{}: ({}, {}, {})'.format(name, cmd.trans_v, cmd.angular_v, cmd.utime))
        last_cmd = self.current_motor_commands[0]
        start_time = (last_cmd.utime / 1e6)
        for i, cmd in enumerate(self.current_motor_commands):
            print_cmd(i, cmd)

        if start_time > at_time:
            raise Exception("Cannot interpolate pose backwards in time")

        # Interpolate with infinite acceleration model
        done = False
        pose = copy(self._pose)
        end_time = start_time
        for cmd_index in range(1, len(self.current_motor_commands) + 1):
            # Set start time to previous end time
            start_time = end_time
            # Get the end time from the at_time or the next command
            next_cmd = None
            end_time = at_time
            if cmd_index < len(self.current_motor_commands):
                next_cmd = self.current_motor_commands[cmd_index]
                # Check if at_time is before the next command
                if next_cmd.utime / 1e6 < at_time: # or last motor command:
                    print('Using next cmd utime')
                    end_time = next_cmd.utime / 1e6
                else:
                    # Reached at_time before all motor commands
                    print('Got to at time before all motor commands handled')
                    done = True
            # Calculate time diff
            dt = end_time - start_time
            print('start_time', start_time)
            print('end_time', end_time)
            print('dt', dt)
            # Calculate the updates to x y and theta
            # dx     = int_ti^tf trans_v * cos(angular_v * t + theta_i - angular_v * ti) dt
            #        = angular_v != 0 --> (trans_v / angular_v) * (sin(angular_v * t + theta_i - angular_v * ti)) |_ti^tf
            #        = angular_v == 0 --> trans_v * cos(theta_i) * (tf - ti)
            # dy     = int_ti^tf tans_v * sin(angular_v * t + theta_i - angular_v * ti) dt
            #        = angular_v != 0 --> (-trans_v / angular_v) * (cos(angular_v * t + theta_i - angular_v * ti)) |_ti^tf
            #        = angular_v == 0 --> trans_v * sin(theta_i) * (tf - ti)
            # dtheta = angular_v * (t - ti)
            dx = 0
            dy = 0
            dtheta = last_cmd.angular_v * dt
            if last_cmd.angular_v <= 1e-5:  # Small values of angular_v would be numerically unstable so treat as 0
                dx = last_cmd.trans_v * math.cos(pose.theta) * dt
                dy = last_cmd.trans_v * math.sin(pose.theta) * dt
            else:
                trans_over_ang = last_cmd.trans_v / last_cmd.angular_v
                init_angle = pose.theta - last_cmd.angular_v * start_time
                dx = trans_over_ang * (math.sin(last_cmd.angular_v * end_time + init_angle) - math.sin(pose.theta))
                dy = -trans_over_ang * (math.cos(last_cmd.angular_v * end_time + init_angle) - math.cos(pose.theta))
            print('d_pose:', geometry.Pose(dx, dy, dtheta))
            pose += geometry.Pose(dx, dy, dtheta)
            print('pose:', pose)
            # Check if done
            if done:
                break
        # Reached the end
        return pose

    def reset_motor_cmds(self):
        cmd = self.current_motor_commands[-1]
        cmd.utime = int(time.time() * 1e6)
        self.current_motor_commands = [cmd]
