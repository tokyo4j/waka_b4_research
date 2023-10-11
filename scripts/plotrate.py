import sys
import shutil
import numpy as np
import pandas as pd
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.collections import LineCollection

NPROC = 36
clock_total = 0
clock_sum = [0] * NPROC
clock_start = [0] * NPROC
logfile_path = 'log/log.csv'
colors = ['#CD5C5C', '#FFA07A', '#FF0000', '#FFC0CB', \
          '#FF1493', '#FF7F50', '#FF4500', '#FFFF00', \
          '#FFDAB9', '#BDB76B', '#D8BFD8', '#BA55D3', \
          '#8B008B', '#483D8B', '#ADFF2F', '#3CB371', \
          '#008000', '#66CDAA', '#00FFFF', '#4682B4', \
          '#00008B', '#FFF8DC', '#DEB887', '#F4A460', \
          '#DAA520', '#D2691E', '#A52A2A', '#FFE4E1', \
          '#808080', '#745399', '#233B6C', '#B1063A']

# import log
df = pd.read_csv(logfile_path, header=0)

# use decimal and elapsed clock
df['clock'] = df['clock'].apply(lambda x: int(x, 16))
df['clock'] -= df['clock'][0]

# extract RUNNING infomation
df = df \
    .query('pstate_prev == 4 or pstate_next == 4') \
    .loc[:,['clock', 'cpu', 'pid', 'pstate_prev', 'pstate_next']] \
    .astype('int')

x = [[] for i in range(NPROC)]
y = [[] for i in range(NPROC)]

# draw line segment
# [0]: clock, [1]: cpu, [2]: pid, [3]: pstate_prev, [4]: pstate_next
for ep in df.itertuples(name=None, index=False):
    pid = ep[2]
    # skip blank data
    if (pid == 0):
        continue
    # skip lock log
    if (pid == -1):
        continue

    if (ep[4] == 4):
        clock_start[pid] = ep[0]

    if (ep[3] == 4):
        if (clock_start[pid] == 0):
            continue

        clock_sum[pid] += ep[0] - clock_start[pid]
        clock_total += ep[0] - clock_start[pid]

        for i in range(NPROC):
            x[i].append(ep[0])
            clock_sum_below = 0
            for j in range(NPROC):
                if (j <= i):
                    clock_sum_below += clock_sum[j]
            y[i].append(clock_sum_below / clock_total * 100)

for i in range(NPROC):
    if (i <= 3):
        continue

    plt.plot(x[i], y[i], color = colors[i % len(colors)], label=str(i))

plt.legend()
plt.xlabel("elapsed clock", fontsize=11)
plt.ylabel("rate", fontsize=11)
plt.grid()
plt.show()
