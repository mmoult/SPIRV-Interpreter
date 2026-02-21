#!/usr/bin/env python3
# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
import os
import platform


def recursive_test(interp_path, launch_dir, verbose):
    # Check that the interpreter has been built
    if not os.path.isfile(interp_path):
        print("Could not find interpreter! Is it built?")
        print("Looking at:", interp_path)
        return 1

    def extract_num(name, prefix):
        pref_len = len(prefix)
        dot = name.find(".", pref_len)
        if dot == -1:
            dot = len(name)
        if dot == pref_len:
            return 0
        return int(name[pref_len:dot])

    def check_file(root, out_file, stdout):
        if out_file is None:
            return True
        with open(os.path.join(root, out_file), 'rb') as f:
            seen = f.read()
        return seen == stdout

    # Read through passlist.txt:
    # Each line is the path of a test to run
    fails = 0
    total = 0
    import subprocess
    # All but "in" are types of output files. There must be a run for each distinct number. Multiple types can use the
    # same run if they share the same number. Input is expected, but optional.
    file_types = ["in", "out", "print"]
    for (root, dirs, files) in os.walk(os.path.abspath(launch_dir), topdown=True):
        program = None
        configs = dict()
        error = False
        for file in files:
            for i in range(len(file_types)):
                prefix = file_types[i]
                if file.startswith(prefix):
                    try:
                        num = extract_num(file, prefix)
                    except ValueError:
                        continue
                    if not num in configs:
                        configs[num] = [None] * len(file_types)
                    if configs[num][i] is not None:
                        print("Run configuration ", i, " has more than one ", file_types[i], "!", sep='')
                        error = True
                        break
                    configs[num][i] = file
            else:
                if file.endswith(".spv"):
                    program = file
            if error:
                break

        if program is not None:
            for num, files in configs.items():
                cmd = [interp_path, program]
                output = False
                out_file = None

                for i, file in enumerate(files):
                    if file is None:
                        continue
                    match i:
                        case 0:  # in
                            cmd += ["-i", file]
                        case 1:  # out
                            output = True
                            cmd += ["-c", file]
                        case 2:  # print
                            output = True
                            out_file = file
                if output:
                    total += 1
                    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=root)

                    status = "-"
                    to_print = verbose
                    if res.returncode != 0 or not check_file(root, out_file, res.stdout):
                        status = "X"
                        fails += 1
                        to_print = True
                    if to_print:
                        print(status, os.path.relpath(os.path.join(root, program), launch_dir), end=' ')
                        if len(configs) > 1:
                            print("#", num, sep='', end='')
                        print()
                    continue

    # Print results
    if total == 0:
        print("No tests run!")
        return 1
    else:
        if fails == 0:
            print("PASS", end='')
        else:
            print("FAIL", end='')
    print(": ", (total - fails), "/", total)
    return fails


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(prog="example-runner", description="Recursively test with the interpreter")
    parser.add_argument("search_dir", nargs="?",
                        help="Directory to search for tests. By default, the \"examples\" directory is used.")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print the result of all tests found.")
    args = parser.parse_args()

    executable_name = "spirv-run"
    if platform.system() == "Windows":
        executable_name += ".exe"

    root_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    interp_path = os.path.abspath(os.path.join(root_path, "build", "src", executable_name))

    # Recursively search through the examples directory or the path passed in
    if args.search_dir is not None:
        launch_dir = os.path.join(os.getcwd(), args.search_dir)
    else:
        launch_dir = os.path.join(root_path, "examples")

    import sys
    sys.exit(recursive_test(interp_path, launch_dir, args.verbose))
