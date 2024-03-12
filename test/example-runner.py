#!/usr/bin/python3
import os

# Check that the interpreter has been built
test_path = os.path.dirname(__file__)
interp_path = os.path.abspath(os.path.join(test_path, "..", "build", "spirv-run"))
if not os.path.isfile(interp_path):
    print("Could not find interpreter! Is it built?")
    print("Looking at:", interp_path)
    exit(1)

# Read through passlist.txt:
# Each line is the path of a test to run
fails = 0
total = 0
import subprocess
with open(os.path.join(test_path, "passlist.txt")) as fp:
    for line in fp:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        test = os.path.normpath(line)
        dir = os.path.dirname(test)
        tname = os.path.basename(test)
        total += 1
        res = subprocess.run([interp_path, "-i", "in.toml", "-c", "out.toml", tname],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=dir)
        if res.returncode != 0:
            fails += 1
            print("X", test)

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
