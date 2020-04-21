import pygame
import geometry
import math
import sys
import time
import numpy
import threading
from copy import copy, deepcopy

sys.path.append('../lcmtypes')
from mbot_motor_command_t import mbot_motor_command_t


class Mbot(pygame.sprite.Sprite):
    class State:
        def __init__(self, pose, twist, stamp):
            self.stamp = stamp
            self.pose = pose
            self.twist = twist

    def __init__(self, world_map, max_trans_speed, max_angular_speed):
        super(Mbot, self).__init__()

        # Model
        self._map = world_map
        self._pose = geometry.Pose(0, 0, 0)
        stop = mbot_motor_command_t()
        stop.utime = int(0)
        self._current_motor_commands = [stop]
        self._radius = 0.1
        # Initialize trajectory to all 0 poses from now - trajectory_length to now for every trajectory step in time
        self._trajectory_length = 10.0  # Seconds
        self._trajectory_step = 0.005  # Seconds
        num_steps = int(self._trajectory_length / self._trajectory_step)
        start_time = time.perf_counter() - (num_steps * self._trajectory_step)
        self._trajectory = [
            Mbot.State(geometry.Pose(0, 0, 0), geometry.Twist(0, 0, 0), start_time + (i * self._trajectory_step)) for i
            in range(num_steps)]
        # TODO(???): Properly model the dependence between translation and angular speed, e.g. if at max angular speed
        #            then translation speed must be zero
        self._max_trans_speed = max_trans_speed
        self._max_angular_speed = max_angular_speed
        self._moving = False

        # View
        self._primary_color = pygame.Color(0, 0, 255)
        self._secondary_color = pygame.Color(255, 0, 0)
        self._dir_indicator_arc_length = math.pi / 3
        self.image = pygame.Surface([10, 10])
        self.image.set_colorkey((0, 0, 0))
        self.rect = self.image.get_rect()

        # Control
        self._trajectory_lock = threading.Lock()

    """ View """

    def _render(self, space_converter):
        radius = space_converter.to_pixel(self._radius) + 1
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
        # Update current pose
        self._pose = self.get_current_pose()
        with self._trajectory_lock:
            # Remove old data
            old_time = time.perf_counter() - self._trajectory_length
            self._trajectory = list(filter(lambda pose: pose.stamp > old_time, self._trajectory))
            last_motor_cmd = self._current_motor_commands[-1]
            self._current_motor_commands = list(
                filter(lambda cmd: cmd.utime > old_time * 1e6, self._current_motor_commands))
            # Add the last motor command with current time back if it was deleted
            if len(self._current_motor_commands) == 0:
                last_motor_cmd.utime = int(time.perf_counter() * 1e6)
                self._current_motor_commands.append(last_motor_cmd)
        # Render
        self._render(space_converter)

    def add_motor_cmd(self, cmd):
        with self._trajectory_lock:
            # Remove any poses calculated after this motor command in time
            self._trajectory = list(filter(lambda pose: pose.stamp <= cmd.utime, self._trajectory))
            self._current_motor_commands.append(cmd)
            self._moving = False
            if cmd.trans_v != 0 or cmd.angular_v != 0:
                self._moving = True

    @property
    def moving(self):
        return self._moving

    def get_last_motor_cmd(self, cmd):
        with self._trajectory_lock:
            return copy(self._current_motor_commands[-1])

    def get_current_pose(self):
        return self.get_pose(time.perf_counter())

    def get_pose(self, at_time):
        with self._trajectory_lock:
            # Check if the requested time is before the oldest pose
            earliest_time = self._trajectory[0].stamp
            if earliest_time > at_time:
                raise Exception("Pose at time {} is before the oldest pose in mbot trajectory ({}s long)".format(
                    at_time, self._trajectory_length))
            # Check if the pose has already been calculated
            newest_time = self._trajectory[-1].stamp
            if newest_time > at_time:
                return self._interpolate_pose(at_time)
            # Use the motion model to predict the future
            return self._model_motion(at_time)

    def _interpolate_pose(self, at_time):
        # Get the calculated pose
        prior_state = self._trajectory[0]
        index = 0
        for index, state in enumerate(self._trajectory):
            if state.stamp > at_time:
                break
            prior_state = state
        next_state = self._trajectory[index]
        # Assume constant acceleration model
        accel = (next_state.twist.as_numpy() - prior_state.twist.as_numpy()) / (next_state.stamp - prior_state.stamp)
        dt = at_time - prior_state.stamp
        dpose = 0.5 * accel * dt * dt + prior_state.twist.as_numpy() * dt
        return geometry.Pose.from_numpy(prior_state.pose.as_numpy() + dpose)

    def _model_motion(self, at_time):
        state = self._trajectory[-1]
        if state.stamp > at_time:
            print('Should never see motion model calling get_pose')
            return self.get_pose(at_time)

        # Interpolate with infinite acceleration model
        num_steps = math.floor((at_time - state.stamp) / self._trajectory_step)
        times = [state.stamp + i * self._trajectory_step for i in range(num_steps)]
        start_time = state.stamp
        last_state = deepcopy(state)
        for start_time in times:
            end_time = start_time + self._trajectory_step
            # Determine if there are any motor commands in this step
            ustart = start_time * 1e6
            uend = end_time * 1e6
            motor_cmds = list(filter(lambda cmd: cmd.utime > ustart and cmd.utime < uend, self._current_motor_commands))
            # Handle each section of this time interval with the correct motor command
            for cmd in motor_cmds:
                # Calculate time diff
                cmd_time = cmd.utime / 1e6
                dt = cmd_time - start_time
                dpose = self._const_vel_motion(last_state, dt)
                # Update last state and add to trajectory
                start_time = cmd_time
                final_pose = self._handle_collision(last_state.pose + dpose, last_state.twist)
                # Clamp speed
                if cmd.trans_v > self._max_trans_speed:
                    cmd.trans_v = self._max_trans_speed
                elif cmd.trans_v < -self._max_trans_speed:
                    cmd.trans_v = -self._max_trans_speed
                if cmd.angular_v > self._max_angular_speed:
                    cmd.angular_v = self._max_angular_speed
                elif cmd.angular_v < -self._max_angular_speed:
                    cmd.angular_v = -self._max_angular_speed
                last_state = Mbot.State(final_pose,
                                        geometry.Twist(cmd.trans_v * numpy.cos(last_state.pose.theta),
                                                       cmd.trans_v * numpy.sin(last_state.pose.theta),
                                                       cmd.angular_v),
                                        cmd_time)
                self._trajectory.append(last_state)
            # Calculate to the end of this step
            dt = end_time - start_time
            dpose = self._const_vel_motion(last_state, dt)
            final_pose = self._handle_collision(last_state.pose + dpose, last_state.twist)
            last_state = Mbot.State(final_pose, last_state.twist, end_time)
            self._trajectory.append(last_state)
        return last_state.pose

    def _const_vel_motion(self, state, dt, angular_velocity_tolerance=1e-5):
        """!
        @brief      Model constant velocity motion

        Equations of motion:
        dx     = int_ti^tf trans_v * cos(vtheta * (t - ti) + theta_i) dt
               = vtheta != 0 --> (trans_v / vtheta) * (sin(vtheta * (t - ti) + theta_i)) - sin(theta_i)
               = vtheta == 0 --> trans_v * cos(theta_i) * (tf - ti)
        dy     = int_ti^tf tans_v * sin(vtheta * (t - ti) + theta_i) dt
               = vtheta != 0 --> (-trans_v / vtheta) * (cos(vtheta * (t - ti) + theta_i)) - cos(theta_i)
               = vtheta == 0 --> trans_v * sin(theta_i) * (tf - ti)
        dtheta = vtheta * (t - ti)

        @param      state                           The state at the start of the motion
        @param      dt                              Delta time
        @param      angular_velocity_tolerance      Any angular velocity magnitude less than this is considered zero for
                                                    numerical stability

        """
        # Calculate the updates to x y and theta
        dx = 0
        dy = 0
        dtheta = state.twist.vtheta * dt
        # Small values are numerically unstable so treat as 0
        if numpy.abs(state.twist.vtheta) <= angular_velocity_tolerance:
            dx = state.twist.vx * dt
            dy = state.twist.vy * dt
            dtheta = 0
        else:
            trans_over_ang = numpy.sqrt(state.twist.vx ** 2 + state.twist.vy ** 2) / state.twist.vtheta
            dx = trans_over_ang * (numpy.sin(state.twist.vtheta * dt + state.pose.theta) - numpy.sin(state.pose.theta))
            dy = -trans_over_ang * (numpy.cos(state.twist.vtheta * dt + state.pose.theta) - numpy.cos(state.pose.theta))

        return geometry.Pose(dx, dy, dtheta)

    def _handle_collision(self, pose, twist):
        while any(map(lambda pose: self._map.at_xy(pose.x, pose.y), self._edge_pose_generator(pose, 30))):
            pose.x -= twist.vx * self._trajectory_step / 3.0
            pose.y -= twist.vy * self._trajectory_step / 3.0
        return pose

    def _edge_pose_generator(self, pose, num_steps):
        step_size = 2 * numpy.pi / num_steps
        theta = -numpy.pi
        while theta < numpy.pi:
            yield geometry.Pose(pose.x + numpy.cos(theta) * self._radius,
                                pose.y + numpy.sin(theta) * self._radius,
                                pose.theta)
            theta += step_size
