#!/usr/bin/env python3
import subprocess
import sys
import argparse

def run_pre_commit(start_commit:str):
    if start_commit == 'HEAD':
        start_commit = 'HEAD~1'
    try:
        subprocess.run(['git', 'rebase', start_commit, '--exec', f"pre-commit run --from-ref HEAD~1 --to-ref HEAD"])
    except subprocess.CalledProcessError as e:
        print(f'pre-commit failed with exit code {e.returncode}.')
        sys.exit(1)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Checks a range of commits one by one with pre-commit.")
    parser.add_argument('start_commit', nargs='?', default='HEAD~1', type=str,
                        help='Initial commit (refspec) to start the check.')
    args = parser.parse_args()
    run_pre_commit(args.start_commit or 'HEAD~1')
