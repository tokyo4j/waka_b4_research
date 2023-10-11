import pandas as pd

df = pd.read_csv('log/log.csv')

df['clock'] = df['clock'].apply(lambda x: int(x, 16))
df['clock'] -= df['clock'][0]

print(df.replace({'event_name': {0: 'allocproc', 1: 'wakeup', 2: 'yield', 3: 'fork', 4: 'scheduled', 5: 'exit', 6: 'wait', 7: 'sleep'}, 'pstate_prev': {0: 'UNUSED', 1: 'EMBRYO', 2: 'SLEEPING', 3: 'RUNNABLE', 4: 'RUNNING', 5: 'ZOMBIE'}, 'pstate_next': {0: 'UNUSED', 1: 'EMBRYO', 2: 'SLEEPING', 3: 'RUNNABLE', 4: 'RUNNING', 5: 'ZOMBIE'}}) \
      .query('(pstate_prev == "RUNNING" or pstate_next == "RUNNING")') \
      .to_string(index=False))
