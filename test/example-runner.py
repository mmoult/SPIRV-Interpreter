#!/usr/bin/python3
# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
import os

# Check that the interpreter has been built
test_path = os.path.dirname(__file__)
interp_path = os.path.abspath(os.path.join(test_path, "..", "build", "src", "spirv-run"))
if not os.path.isfile(interp_path):
    print("Could not find interpreter! Is it built?")
    print("Looking at:", interp_path)
    exit(1)

# Recursively search through the examples directory
# Or if a cmd line arg was provided, try that directory
import sys
if len(sys.argv) > 1:
    launch_dir = os.path.join(os.getcwd(), sys.argv[1])
else:
    launch_dir = os.path.join(test_path, "..", "examples")

def extract_num(name, prefix):
    pref_len = len(prefix)
    dot = name.find(".", pref_len)
    if dot == -1:
        dot = len(name)
    if dot == pref_len:
        return 0
    return int(name[pref_len:dot])

# Read through passlist.txt:
# Each line is the path of a test to run
fails = 0
total = 0
ionames = ["in", "out"]
import subprocess
for (root, dirs, files) in os.walk(os.path.abspath(launch_dir), topdown=True): 
    iopairs = {}
    program = None
    for file in files:
        for i in range(len(ionames)):
            prefix = ionames[i]
            if file.startswith(prefix):
                num = extract_num(file, prefix)
                if not (num in iopairs):
                    iopairs[num] = [None, None]
                if iopairs[num][i] is not None:
                    print("Run configuration ", i, " has more than one ", "input" if i == 0 else "output", "!", sep='')
                iopairs[num][i] = file
                break
        else:
            if file.endswith(".spv"):
                program = file

    if program is not None:
        for num, pair in iopairs.items():
            input = pair[0]
            output = pair[1]
            if input is not None and output is not None:
                total += 1
                res = subprocess.run([interp_path, "-i", input, "-c", output, program],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=root)
                if res.returncode != 0:
                    fails += 1
                    print("X", os.path.relpath(os.path.join(root, program), launch_dir), end=' ')
                    if len(iopairs) > 1:
                        print("#", num, sep='')
                continue

# Print results
if total == 0:
    print("No tests run!")
    exit(1)
else:
    if fails == 0:
        print("PASS", end='')
    else:
        print("FAIL", end='')
print(": ", (total - fails), "/", total)
exit(fails)
