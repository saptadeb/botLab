import math
import numpy


def clamp(rads):
    while rads > math.pi:
        rads -= 2 * math.pi
    while rads <= -math.pi:
        rads += 2 * math.pi
    return rads


class Pose:
    def __init__(self, x, y, theta):
        self.x = x
        self.y = y
        self.theta = theta

    @property
    def theta(self):
        return self._theta

    @theta.setter
    def theta(self, theta):
        self._theta = clamp(theta)

    def as_list(self):
        return [self.x, self.y, self.theta]

    def as_numpy_tf(self):
        return numpy.matrix([
            [numpy.cos(self.theta), -numpy.sin(self.theta), self.x],
            [numpy.sin(self.theta), numpy.cos(self.theta), self.y],
            [0, 0, 1]])

    def as_numpy(self):
        return numpy.matrix([[self.x], [self.y], [self.theta]])

    @classmethod
    def from_numpy(cls, mat):
        return cls(mat[0, 0], mat[1, 0], mat[2, 0])

    def translation(self):
        return numpy.matrix([[self.x], [self.y], [1]])

    def rotation(self):
        return self.theta

    def __add__(self, other):
        return Pose(self.x + other.x, self.y + other.y, self.theta + other.theta)

    def __iadd__(self, other):
        self.x += other.x
        self.y += other.y
        self.theta += other.theta
        return self

    def __sub__(self, other):
        return Pose(self.x - other.x, self.y - other.y, self.theta - other.theta)

    def __str__(self):
        return '({} m, {} m, {} rad)'.format(self.x, self.y, self.theta)


class Twist:
    def __init__(self, vx, vy, vtheta):
        self.vx = vx
        self.vy = vy
        self.vtheta = vtheta

    def as_numpy(self):
        return numpy.matrix([[self.vx], [self.vy], [self.vtheta]])


class SpaceConverter:
    def __init__(self, pixels_per_meter, origin):
        self._pixels_per_meter = pixels_per_meter
        self._origin = numpy.array(origin)
        self._world_to_pixel = numpy.matrix(
            [[self._pixels_per_meter,    0,                          -self._origin[0] * self._pixels_per_meter],
             [0,                         -self._pixels_per_meter,    -self._origin[1] * self._pixels_per_meter],
             [0,                         0,                          1]])

    def to_pixel(self, meters):
        return math.ceil(meters * self._pixels_per_meter)

    def to_meters(self, pixels):
        return pixels / self._pixels_per_meter

    def __mul__(self, other):
        return self._world_to_pixel * other
