import argparse
import os
import random
import shlex
import signal
import subprocess
import time

AMMUNITION = [
    'localhost:8080/api/v1/maps',
    'localhost:8080/api/v1/maps/map1',
]

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('server', type=str, help='Command to start the server')
    return parser.parse_args()

def start_server(server_command):
    args = shlex.split(server_command)
    return subprocess.Popen(args)

def start_perf(server_pid):
    perf_command = f"perf record -g -p {server_pid} -o perf.data"
    args = shlex.split(perf_command)
    return subprocess.Popen(args)

def shoot(ammo):
    with open(os.devnull, 'w') as devnull:
        subprocess.Popen(['curl', '-s', ammo], stdout=devnull, stderr=devnull)

def make_shots():
    for _ in range(50):
        ammo = random.choice(AMMUNITION)
        shoot(ammo)
        time.sleep(0.1)

def stop_process(proc, signum=signal.SIGTERM):
    if proc.poll() is None:
        proc.send_signal(signum)
        proc.wait()

def generate_flamegraph():
    p1 = subprocess.Popen(['perf', 'script', '-i', 'perf.data'], stdout=subprocess.PIPE)
    p2 = subprocess.Popen(['./FlameGraph/stackcollapse-perf.pl'], stdin=p1.stdout, stdout=subprocess.PIPE)
    p1.stdout.close()
    
    with open('graph.svg', 'w') as svg_file:
        p3 = subprocess.Popen(['./FlameGraph/flamegraph.pl'], stdin=p2.stdout, stdout=svg_file)
        p2.stdout.close()
        p3.wait()

def main():
    args = parse_args()

    server_proc = start_server(args.server)
    time.sleep(0.5)

    perf_proc = start_perf(server_proc.pid)
    time.sleep(0.5)

    make_shots()

    stop_process(perf_proc, signal.SIGINT)
    stop_process(server_proc, signal.SIGTERM)

    generate_flamegraph()

if __name__ == '__main__':
    main()