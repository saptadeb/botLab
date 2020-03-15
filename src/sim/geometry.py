import math


class Pose:
    def __init__(self, x, y, theta):
        self.x = x
        self.y = y
        self.theta = theta

    def as_list(self):
        return [self.x, self.y, self.theta]

    def translation(self):
        return [self.x, self.y]

    def rotation(self):
        return self.theta


class SpaceConverter:
    def __init__(self, meters_per_pixel):
        self._meters_per_pixel = meters_per_pixel

    @property
    def meters_per_pixel(self):
        return self._meters_per_pixel

    @meters_per_pixel.setter
    def meters_per_pixel(self, meters_per_pixel):
        self._meters_per_pixel = meters_per_pixel

    def to_pixel(self, meters):
        return math.ceil(meters / self._meters_per_pixel)

    def to_meters(self, pixels):
        return pixels * self._meters_per_pixel
