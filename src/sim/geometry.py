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
