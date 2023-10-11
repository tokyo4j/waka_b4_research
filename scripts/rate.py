import pandas as pd

NCPU = 8
NPROC = 64
running_clock = [0] * NCPU
not_running_clock = [0] * NCPU
start_clock = [-1] * NCPU
response_time = [0] * NPROC
turnaround_time = [0] * NPROC
logfile_path = 'log/log.csv'
logclock_path = 'log/clock.csv'
finished_fork = False

# import log
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
for line in df.itertuples(name=None, index=False):
    clock = line[0]
    cpuid = line[1]
    pid = line[2]
    pstate_prev = line[3]
    pstate_next = line[4]

    # if finished_fork:
    if (pstate_prev == 3 and pstate_next == 4 and start_clock[cpuid] != -1):
        not_running_clock[cpuid] += clock - start_clock[cpuid]
        # print("pid %d: not running_clock : %d" %(pid, clock - start_clock[cpuid]))
    elif (pstate_prev == 4 and pstate_next == 3 and start_clock[cpuid] != -1):
        running_clock[cpuid] += clock - start_clock[cpuid]
        # print("pid %d: running_clock : %d" %(pid, clock - start_clock[cpuid]))
    start_clock[cpuid] = clock

#     if (pid == 3 and pstate_prev == 4 and pstate_next == 2):
#         finished_fork = True

# calculate response time and turn around time
cnt = 0
res_sum = 0
tur_sum = 0
for line in df_clock.itertuples(name=None, index=False):
    response = line[2] - line[1]
    turnaround = line[3] - line[1]
    if (line[3] == 0):
        continue;
    cnt += 1
    res_sum += response
    tur_sum += turnaround
    print("pid:%d, response:%d turnaround:%d" \
          %(line[0], response, turnaround))
print("response_ave:%d, turnaround_ave:%d\n" %(res_sum / cnt, tur_sum / cnt))

for i in range(NCPU):
    if running_clock[i] != 0:
        print("cpu %d usage : %9f percent" \
          %(i, 100 * running_clock[i] / (running_clock[i] + not_running_clock[i])))
