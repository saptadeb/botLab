import numpy as np
import matplotlib.pyplot as plt
import sys
mapName = str(sys.argv[1])
mapRender = np.loadtxt(mapName, dtype=int, skiprows=1)
plt.imshow(mapRender, cmap='gray_r', vmin = -127, vmax = 127)
plt.show()