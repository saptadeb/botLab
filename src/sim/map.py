import pygame
import math
import numpy


class Map:
    def __init__(self):
        self._occupied_cells = []
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
        return list(self._occupied_cells)

    """ GUI """

    def render(self, space_converter):
        # Calculate cell size
        cell_size = space_converter.to_pixel(self._meters_per_cell)

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
                [row * self._meters_per_cell + self._global_origin_y],
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
            self._occupied_cells.extend([self.row_col_to_index(row, col) for col, cell in
                enumerate(line.split()) if int(cell) > 0])

    """ Map Math """

    def row_col_to_index(self, row, col):
        return Map.row_col_width_to_index(row, col, self._width)

    def index_to_row_col(self, index):
        return Map.index_width_to_row_col(index, self._width)

    @staticmethod
    def row_col_width_to_index(row, col, width):
        return row * width + col

    @staticmethod
    def index_width_to_row_col(index, width):
        row = math.floor(index / width)
        col = index - row * width
        return (row, col)
