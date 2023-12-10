from datetime import datetime
import random
import pathlib
import time

PROC_NUM = 72
LOGS_PATH = f"../data/decentralized/producer_consumer_0/"
SHIFT = 5

logs_path = pathlib.Path(LOGS_PATH)

elapsed_time_cnt = 0

for i in range(1, PROC_NUM + 1):
    log_path_i = logs_path / f"{i}"
    log_path_i.mkdir()

    rand_value = (1 + random.randint(-SHIFT, SHIFT)) * 10.1
    elapsed_time_delta = rand_value if rand_value > 0 else 1
    elapsed_time_cnt += elapsed_time_delta

    for rank in range(i):
        cur_datetime = datetime.utcnow()
        cur_datetime_str = cur_datetime.strftime("%Y-%m-%d-%H-%M-%S")

        log_path = log_path_i / f"Rank_{rank}_benchmark_{cur_datetime_str}.log"
        with open(log_path, 'w') as f:
            data_log = f"procs {PROC_NUM}, rank {rank}, elapsed (sec) {elapsed_time_cnt}, total (sec) {elapsed_time_cnt}"
            f.write(data_log)
