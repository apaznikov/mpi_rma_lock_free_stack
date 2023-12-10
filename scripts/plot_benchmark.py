import pathlib
import re
import math
import click
import numpy as np
from matplotlib import pyplot as plt


@click.command()
@click.argument('logs_path')
@click.argument('plot_out_path')
@click.option("--total_ops", "-ops", default=15000,type=int)
@click.option("--x_step", default=1,type=int)
def main(logs_path, plot_out_path, total_ops, x_step):
    logs_path = pathlib.Path(logs_path)

    procs_folders = []
    for procs_folder in logs_path.glob('*'):
        if all([c.isdigit() for c in procs_folder.stem]):
            procs_folders += [procs_folder]

    max_proc_num = len(procs_folders)

    ops = []

    for proc_folder in procs_folders:
        log_file = list(proc_folder.glob("Rank_0_benchmark_*.log"))[0]
        with open(log_file, 'r') as f:
            data_log = f.read().strip()
            pattern = r'procs \d+, rank \d+, elapsed \(sec\) \d+, total \(sec\) (\d+)'
            total_time = re.findall(pattern, data_log)[0]
            total_time = int(total_time)
            ops_per_second = math.floor(total_ops / total_time)
            ops += [ops_per_second]

    procs = np.arange(1, max_proc_num + 1, 1)
    f, ax = plt.subplots(1)
    ax.set_xlim(xmin=1,xmax=max_proc_num + 0.1)

    ax.plot(procs, ops)
    ax.grid()
    x_ticks = np.arange(2, max_proc_num + 1, x_step)
    plt.xticks(x_ticks)
    plt.xlabel("Количество процессов")
    plt.ylabel("Время, мс")
    plot_path = pathlib.Path(plot_out_path)
    plt.savefig(plot_path)


if __name__ == "__main__":
    main()