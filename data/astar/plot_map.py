import numpy as np
import matplotlib.pyplot as plt
import sys
mapName = str(sys.argv[1])
map = np.loadtxt(mapName, dtype=int, skiprows=1)
plt.imshow(map, cmap='gray_r', vmin = -127, vmax = 127)
plt.show()