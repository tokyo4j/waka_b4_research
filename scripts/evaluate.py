import pandas as pd
import numpy as np
import sys
import yaml
import math
import shutil
import subprocess
from datetime import datetime

# NPROC must be < 100
NCPU = NPROC = PROC_MAX = LOGSIZE = 0
policy = ''
workload = ''
FORK_NUM = ''
input_path = 'log/tmp.log'
logfile_path = 'log/log.csv'
logclock_path = 'log/clock.csv'

# split inputfile to csv
input_file = pd.read_csv(input_path)
for line in input_file.itertuples(name=None, index=False):
    if (len(line[0]) == 16):
        LOGSIZE += 1
        if (int(line[6]) > NCPU):
            NCPU = int(line[6])
    if (len(line[0]) <= 2):
        NPROC += 1
    if (line[0] == "NPROC"):
        PROC_MAX = int(line[1])
    if (line[0] == "policy"):
        policy = line[1]
    if (line[0] == "workload"):
        workload = line[1]
    if (line[0] == "forknum"):
        FORK_NUM = int(line[1])
NCPU += 1

input_file.iloc[:LOGSIZE:].to_csv(logfile_path, index=False)
input_file.iloc[LOGSIZE:LOGSIZE + NPROC:] \
    .to_csv(logclock_path, header=False, index=False)
df = pd.read_csv(logfile_path, header=0)
df_clock = pd.read_csv(logclock_path, header=0)

# use decimal and elapsed clock
df['clock'] = df['clock'].apply(lambda x: int(x, 16))
df['clock'] -= df['clock'][0]
df_clock['fork'] = df_clock['fork'].apply(lambda x: int(x, 16))
df_clock['run'] = df_clock['run'].apply(lambda x: int(x, 16))
df_clock['exit'] = df_clock['exit'].apply(lambda x: int(x, 16))

# extract RUNNING infomation
df = df.query('pstate_prev == 4 or pstate_next == 4') \
    .loc[:,['clock', 'cpu', 'pid', 'pstate_prev', 'pstate_next']] \
    .astype('int')

# get CPU usage
start_clock = [-1] * NCPU
cnt_running = cnt_not_running = sum_running = sum_not_running = 0
running_clock = [0] * NCPU
not_running_clock = [0] * NCPU
for line in df.itertuples(name=None, index=False):
    clock = line[0]
    cpuid = line[1]
    if (line[3] == 3 and line[4] == 4 and start_clock[cpuid] != -1):
        not_running_clock[cpuid] += clock - start_clock[cpuid]
        sum_not_running += clock - start_clock[cpuid]
        cnt_not_running += 1
    elif (line[3] == 4 and line[4] == 3 and start_clock[cpuid] != -1):
        running_clock[cpuid] += clock - start_clock[cpuid]
        sum_running += clock - start_clock[cpuid]
        cnt_running += 1
    start_clock[cpuid] = clock

summary = {}

output_name = "log/" + workload + "/" + policy \
    + "_cpu" + str(NCPU) + "_nproc" + str(PROC_MAX) + "_fork" \
    + str(FORK_NUM) + "_logsize" + str(LOGSIZE)

env = {}
env["policy"] = policy
env["NCPU"] = NCPU
env["NPROC"] = PROC_MAX
env["forknum"] = FORK_NUM
env["LOGSIZE"] = LOGSIZE
env["workload"] = workload
env["date"] = datetime.now().strftime("%Y/%m/%d %H:%M")
summary["_environment"] = env

clock_start = [0] * NPROC
clock_sum = [0] * NPROC
balance_rate = []
clock_total = 0
# [0]: clock, [1]: cpu, [2]: pid, [3]: pstate_prev, [4]: pstate_next
for ep in df.itertuples(name=None, index=False):
    pid = ep[2]
    # skip blank data
    if (pid == 0):
        continue

    if (ep[4] == 4):
        clock_start[pid] = ep[0]

    if (ep[3] == 4):
        if (clock_start[pid] == 0):
            continue
        clock_sum[pid] += ep[0] - clock_start[pid]
        clock_total += ep[0] - clock_start[pid]

balancing = {}
for i in range(NPROC):
    if (clock_sum[i] == 0):
        continue;
    balance_rate.append(clock_sum[i] / clock_total * 100)
    balancing["_data_pid" + "{:0=2}".format(i)] = clock_sum[i] / clock_total * 100
balancing["size"] = len(balance_rate)
balancing["average"] = float(np.mean(balance_rate))
balancing["standard"] = float(np.std(balance_rate))
balancing["std/ave"] = float(np.std(balance_rate) / np.mean(balance_rate) * 100)
balancing["total(for debug)"] = sum(balance_rate)
summary["balancing"] = balancing

response = []
for line in df_clock.itertuples(name=None, index=False):
    # skip unforked or unexited process
    if (line[3] == 0):
        continue;
    response.append(line[2] - line[1])
    # print("pid %2d  : %13d" %(line[0], line[2] - line[1]))
responsetime = {}
responsetime["size"] = len(response)
responsetime["average"] = float(np.mean(response))
responsetime["standard"] = float(np.std(response))
responsetime["std/ave"] = float(np.std(response) / np.mean(response) * 100)
summary["time_response"] = responsetime

turnaround = []
for line in df_clock.itertuples(name=None, index=False):
    # skip unforked or unexited process
    if (line[3] == 0):
        continue;
    turnaround.append(line[3] - line[1])
    # print("pid %2d  : %13d" %(line[0], line[3] - line[1]))
turnaroundtime = {}
turnaroundtime["size"] = len(turnaround)
turnaroundtime["average"] = float(np.mean(turnaround))
turnaroundtime["standard"] = float(np.std(turnaround))
turnaroundtime["std/ave"] = float(np.std(turnaround) / np.mean(turnaround) * 100)
summary["time_turnaround"] = turnaroundtime

clock = {}
clock["running_size"] = cnt_running
clock["running_average"] = float(sum_running / cnt_running)
clock["idle_size"] = cnt_not_running
clock["idle_average"] = float(sum_not_running / cnt_not_running)
summary["clock"] = clock

ave_cpu = 100 * sum_running / (sum_running + sum_not_running)
std_cpu = 0
cpuusage = {}
for i in range(NCPU):
    if running_clock[i] != 0:
        diff_cpu = 100 * running_clock[i] / (running_clock[i] + not_running_clock[i])
        std_cpu += (ave_cpu - diff_cpu) ** 2
std_cpu /= NCPU
std_cpu = math.sqrt(std_cpu)
for i in range(NCPU):
    if running_clock[i] != 0:
        cpuusage["_data_pid" + "{:0=2}".format(i)] = 100 * running_clock[i] / (running_clock[i] + not_running_clock[i])

cpuusage["average"] = ave_cpu
cpuusage["standard"] = std_cpu
cpuusage["std/ave"] = std_cpu / ave_cpu * 100
summary["cpu_usage"] = cpuusage

# save data
shutil.copyfile(input_path, output_name + ".csv")
with open(output_name + ".yaml", 'w') as file:
    yaml.dump(summary, file)