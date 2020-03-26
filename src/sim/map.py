import pygame
import math
import numpy


class Map:
    def __init__(self):
        self._occupied_cells = set()
        self._global_origin_x = 0
        self._global_origin_y = 0
        self._width = 10
        self._height = 10
        self._meters_per_cell = 0.05
        self.image = None

    """ Properties """
    @property
    def width(self):
        return self._width

    @property
    def height(self):
        return self._height

    @property
    def meters_per_cell(self):
        return self._meters_per_cell

    @property
    def occupied_cells(self):
        return set(self._occupied_cells)

    """ GUI """

    def render(self, space_converter):
        # Calculate cell size
        cell_size = space_converter.to_pixel(self._meters_per_cell) + 1

        # Draw the map on a new surface
        width = space_converter.to_pixel(self._meters_per_cell * self._width)
        height = space_converter.to_pixel(self._meters_per_cell * self._height)
        self.image = pygame.Surface((width, height))
        # Draw the background white
        self.image.fill((255, 255, 255))
        # Draw occupied cells
        for index in self._occupied_cells:
            row, col = self.index_to_row_col(index)
            pixels_cords = space_converter * numpy.matrix([
                [col * self._meters_per_cell + self._global_origin_x],
                # Add one to row since the y is fliped and we need the top not the bottom
                [(row + 1) * self._meters_per_cell + self._global_origin_y],
                [1]])
            rect = pygame.Rect(pixels_cords[0, 0], pixels_cords[1, 0], cell_size, cell_size)
            pygame.draw.rect(self.image, (0, 0, 0), rect)

        return self.image

    """ File I/O """

    def load_from_file(self, file_name):
        # Read the file all at once
        lines = None
        with open(file_name, 'r') as file:
            lines = file.readlines()

        # Grab the header information
        (self._global_origin_x,
         self._global_origin_y,
         self._width,
         self._height,
         self._meters_per_cell) = [convert(value) for value, convert in
                                   zip(lines[0].split(), [float, float, int, int, float])]

        # Load in the map
        for row, line in enumerate(lines[1:]):
            self._occupied_cells |= {self.row_col_to_index(row, col) for col, cell in
                                     enumerate(line.split()) if int(cell) > 0}

    """ Data access """
    def at_xy(self, x, y):
        row = math.floor((y - self._global_origin_y) / self._meters_per_cell)
        col = math.floor((x - self._global_origin_x) / self._meters_per_cell)
        index = self.row_col_to_index(row, col)
        if index in self._occupied_cells:
            return True
        return False

    """ Map Math """

    def row_col_to_index(self, row, col):
        return row * self._width + col

    def index_to_row_col(self, index):
        row = math.floor(index / self._width)
        col = index - row * self._width
        return (row, col)
