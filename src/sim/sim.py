import pygame
from map import Map
from pygame.locals import *

class Gui:
    def __init__(self):
        self._running = True
        self._display_surf = None
        self._size = self._width, self._height = 640, 400

    def on_init(self):
        # Pygame
        pygame.init()
        pygame.display.set_mode(self._size, pygame.HWSURFACE | pygame.DOUBLEBUF)
        self._display_surf = pygame.display.get_surface()
        # Map
        self._map = Map(self._width, self._height)
        self._map.load_from_file('../../data/astar/maze.map')
        self._display_surf.blit(self._map.render(), (0, 0))
        pygame.display.flip()
        # Start
        self._running = True

    def on_event(self, event):
        if event.type == pygame.QUIT:
            self._running = False

    def on_loop(self):
        pass

    def on_render(self):
        pass

    def on_cleanup(self):
        pygame.quit()

    def on_execute(self):
        if self.on_init() == False:
            self._running = False

        while( self._running ):
            for event in pygame.event.get():
                self.on_event(event)
            self.on_loop()
            self.on_render()
        self.on_cleanup()

if __name__ == "__main__" :
    sim = Gui()
    sim.on_execute()
