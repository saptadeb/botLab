import pygame
import math


class Map:
    def __init__(self, draw_width=640, draw_height=480):
        self._draw_width = draw_width
        self._draw_height = draw_height
        self._occupied_cells = []
        self._global_origin_x = 0
        self._global_origin_y = 0
        self._width = 10
        self._height = 10
        self._meters_per_cell = 0.05

    """ GUI """

    def render(self):
        # Calculate cell size
        cell_width = math.ceil(self._draw_width / self._width)
        cell_height = math.ceil(self._draw_height / self._height)

        # Draw the map on a new surface
        image = pygame.Surface((self._draw_width, self._draw_height))
        # Draw the background white
        image.fill((255, 255, 255))
        # Draw occupied cells
        for index in self._occupied_cells:
            row, col = self.index_to_row_col(index)
            rect = pygame.Rect(col * cell_width, row * cell_height, cell_width, cell_height)
            pygame.draw.rect(image, (0, 0, 0), rect)

        return image

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
