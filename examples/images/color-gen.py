# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

def print_scale(color, lighten, steps=10):
    diff = 1.0 / steps
    for i in range(steps):
        print("[", end="")
        step = i * diff
        for j in range(len(color)):
            if j != 0:
                print(", ", end="")
            if lighten:
                val = color[j] + (1.0 - color[j]) * step
            else:
                val = color[j] * (1.0 - step)
            print(round(val * 100), end="")
        print("]")
    print()


def from_hue(hue):
    h = hue * 6.0
    assert(h >= 0 and h < 6)
    x = h - int(h)
    if h < 1:
        return [1.0, x, 0.0]
    elif h < 2:
        return [1.0 - x, 1.0, 0.0]
    elif h < 3:
        return [0.0, 1.0, x]
    elif h < 4:
        return [0.0, 1.0 - x, 1.0]
    elif h < 5:
        return [x, 0.0, 1.0]
    elif h < 6:
        return [1.0, 0.0, 1.0 - x]


def column(hue, lighten):
    print_scale(from_hue(hue), lighten)


COLUMNS = 14
hue0 = 0
hue1 = COLUMNS // 2
for i in range(COLUMNS):
    print(i, ":", sep="")
    if i % 2 == 0:
        column(hue0 / COLUMNS, False)
        hue0 += 1
    else:
        column(hue1 / COLUMNS, True)
        hue1 += 1
