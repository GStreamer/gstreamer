from analysis_runner import AnalysisRunner
from parser import Parser


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('file', nargs='?', default='debug.log')
    args = parser.parse_args()

    with Parser(args.file) as log:
        runner = AnalysisRunner(log)
        runner.run()
