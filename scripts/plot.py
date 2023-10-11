import sys
import shutil
import numpy as np
import pandas as pd
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.collections import LineCollection

NPROC = 100
idx = -1
labels = [] # for legend
start_point = [] # start points of line
sp_idx = [0] * NPROC # index of start point
fig, ax = plt.subplots()
logfile_path = 'log/log.csv'
colors = ['#CD5C5C', '#FFA07A', '#FF0000', '#FFC0CB', \
          '#FF1493', '#FF7F50', '#FF4500', '#FFFF00', \
          '#FFDAB9', '#BDB76B', '#D8BFD8', '#BA55D3', \
          '#8B008B', '#483D8B', '#ADFF2F', '#3CB371', \
          '#008000', '#66CDAA', '#00FFFF', '#4682B4', \
          '#00008B', '#FFF8DC', '#DEB887', '#F4A460', \
          '#DAA520', '#D2691E', '#A52A2A', '#FFE4E1', \
          '#808080', '#745399', '#233B6C', '#B1063A']
# colors = ['#00CED1', '#8c564b', '#DAA520', '#ff0000', \
#           '#008000', '#0000ff', '#ffd700', '#ff69b4', \
#           '#800080', '#808080', '#90ee90', '#8b4513', \
#           '#e9967a', '#008080', '#ff00ff', '#556b2f']

# import log
df = pd.read_csv(logfile_path, header=0)

# use decimal and elapsed clock
df['clock'] = df['clock'].apply(lambda x: int(x, 16))
df['clock'] -= df['clock'][0]

# extract RUNNING infomation
df = df.query('pstate_prev == 4 or pstate_next == 4') \
    .loc[:,['clock', 'cpu', 'pid', 'pstate_prev', 'pstate_next']] \
    .astype('int')

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

    found_samepid = False

    for sp in start_point:
        if (sp[2] == pid):
            found_samepid = True

            if (sp[1] == ep[1] and sp[4] == 4 and ep[3] == 4):
                # draw line
                lc = LineCollection([[[sp[0], sp[1]], [ep[0], ep[1]]]], \
                                    colors=colors[pid % len(colors)], linewidth=30)
                ax.add_collection(lc)

            # update start_point
            start_point[sp_idx[pid]] = ep

    if (not found_samepid):
        start_point.append(ep)
        idx += 1
        sp_idx[pid] = idx

    # draw invisible end point
    if (pid in labels):
        ax.scatter(ep[0], ep[1], c=colors[pid % len(colors)], s=100, alpha=0.0)
    else:
        ax.scatter(ep[0], ep[1], c=colors[pid % len(colors)], label=pid, s=100, alpha=0.0)
        labels.append(pid)

# let legend visible
leg = ax.legend(title="pid", title_fontsize=11)
for leha in leg.legend_handles:
    leha.set_alpha(1.0)

    # for debugging
    # if (pid in labels):
    #     ax.scatter(ep[0], ep[1], c=colors[pid % len(colors)])
    # else:
    #     ax.scatter(ep[0], ep[1], c=colors[pid % len(colors)], label=pid)
    #     labels.append(pid)

# use integer in y axis
plt.gca() \
    .get_yaxis() \
    .set_major_locator(ticker.MaxNLocator(integer=True))

# save log and figure
dt = datetime.now()
exe_time = dt.strftime("%Y_%m%d_%H%M")
args = sys.argv
if (len(args) == 2):
    if (args[1] == "-s"):
        shutil.copyfile(logfile_path, "log/" + exe_time + ".csv")
        plt.savefig("fig/" + exe_time + ".png")

plt.xlabel("elapsed clock", fontsize=11)
plt.ylabel("CPU number", fontsize=11)
plt.grid()
plt.show()
