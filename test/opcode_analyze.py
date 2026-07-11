#!/usr/bin/env python3
# © SPIRV-Interpreter @ https://github.com/mmoult/SPIRV-Interpreter
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""
Disassemble SPIR-V shaders and tally opcode frequencies.

Walks a directory for *.spv files, runs `spirv-dis` on each, extracting the SPIR-V opcodes (the `Op...` token or
operation in extended sets) to present the results.
"""
import os
import subprocess
import sys


class Record:
    """Record of opcode counts for a single shader file, including base SPIR-V and extended sets."""
    def __init__(self, path, err=""):
        self.path = path
        self.counts = dict()
        self.ext_counts = dict()
        self.err = err

    def _inc_opcode(self, opcode, self_counts):
        if opcode in self_counts:
            self_counts[opcode] += 1
        else:
            self_counts[opcode] = 1

    def count_opcodes(self, text):
        """
        Parses the file from the given text, saving count data into this and the total record
        """
        line = []
        curr = ""
        comment = False  # We only parse SPIR-V, which only has whole line comments
        back = False
        quote = False
        ext_sets = dict()

        for ch in text:
            if comment:
                if ch != "\n":
                    continue
                comment = False
            elif ch == ";" and len(curr) == 0:
                comment = True
            elif not quote and ch.isspace():
                if len(curr) > 0:
                    line.append(curr)
                    curr = ""
                if ch == "\n":
                    # We reached line end: process it as a whole
                    # Rearrange result, if present
                    result_val = None
                    if line[0].startswith("%"):
                        result_val = line[0]
                        line = line[2:]

                    # => %float = [OpTypeVector, %v4float, 4]
                    opcode = line[0]
                    self._inc_opcode(opcode, self.counts)

                    # If the instruction is OpExtInstImport, save the imported set
                    if opcode == "OpExtInstImport":
                        # %1 = [OpExtInstImport, "GLSL.std.450"]
                        assert len(line) == 2
                        ext_sets[result_val] = line[1]
                    elif opcode == "OpExtInst":
                        # %36 = [OpExtInst, %v4float, %1, Normalize, %35]
                        # %13 = [OpExtInst, %void, %12, 1, %10, %11]
                        assert len(line) > 3
                        ext_op = line[3]
                        ext_set = ext_sets[line[2]]
                        if ext_set in self.ext_counts:
                            self._inc_opcode(ext_op, self.ext_counts[ext_set])
                        else:
                            ext_count = {ext_op : 1}
                            self.ext_counts[ext_set] = ext_count
                    line.clear()
            elif ch == '"' and not back:
                quote = not quote
            elif ch == "\\":
                back = True
                continue
            else:
                curr += ch

            back = False


class Totaler:
    """
    Holds similar data to Record, but also tracks the number of files that have each instruction
    """
    def __init__(self):
        self.counts = dict()
        self.ext_counts = dict()

    def __iadd__(self, other : Record):
        for opcode, count in other.counts.items():
            if opcode in self.counts:
                op_list = self.counts[opcode]
                op_list[0] += count
                op_list[1] += 1
            else:
                self.counts[opcode] = [count, 1]
        for ext_name, other_set in other.ext_counts.items():
            if not ext_name in self.ext_counts:
                ext_set = dict()
                self.ext_counts[ext_name] = ext_set
            else:
                ext_set = self.ext_counts[ext_name]

            for opcode, count in other_set.items():
                if opcode in self.counts:
                    op_list = ext_set[opcode]
                    op_list[0] += count
                    op_list[1] += 1
                else:
                    ext_set[opcode] = [count, 1]
        return self


def disassemble(spv: str, dis: str) -> str:
    """Return the textual SPIR-V for one .spv file, or raise on failure."""
    result = subprocess.run(
        [dis, "--raw-id", spv],
        capture_output=True,
        text=True,
        check=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"spirv-dis failed on {spv}")
    return result.stdout


def opcode_collect(search, disasm_path="spirv-dis"):
    """
    Collects opcode data for each file. The first element in the return array is the totals
    """
    results = []
    for (root, _, files) in os.walk(search, topdown=True):
        for file in files:
            if not file.endswith(".spv"):
                continue
            spv = os.path.join(root, file)
            rel = os.path.relpath(spv, search)

            try:
                text = disassemble(spv, disasm_path)
            except (RuntimeError, FileNotFoundError, OSError) as e:
                results.append(Record(rel, str(e)))
                continue

            record = Record(rel)
            record.count_opcodes(text)
            results.append(record)
    return results


def display_find_results(results, find, order_desc):
    """
    Display the results of searching for a particular opcode
    """
    # Go through each of the records. Identify hits and failures
    failures = []
    hits = []
    total_count = 0
    header_field = "# file:"
    max_name = len(header_field)
    max_count = 0

    for record in results:
        if len(record.err) > 0:
            failures.append(record.path)
        else:
            count = 0
            if find in record.counts:
                count += record.counts[find]
            for ext_counts in record.ext_counts.values():
                if find in ext_counts:
                    count += ext_counts[find]
            if count > 0:
                max_name = max(max_name, len(record.path))
                hits.append((count, record.path))
                max_count = max(max_count, count)
                total_count += count

    n_shaders = len(hits)
    print(f"# {find}: {total_count} use(s) in {n_shaders} of {len(results) - len(failures)} shaders")
    for spv, err in failures:
        print(f"# FAILED: {spv}: {err}")
    print()
    hits.sort(reverse=order_desc)
    max_count = max(len(str(max_count)), len("count"))
    print(f"{header_field:<{max_name}} {"count":>{max_count}}")
    for n, path in hits:
        print(f"{path:<{max_name}} {n:>{max_count}}")


def display_table(result, is_total, order_desc, sort):
    """
    Print the table for the record or total data
    """
    def display_set(name, set_dict):
        # Construct a list of entries, which we can sort and display
        max_name = len(name) + 1
        max_count = 0
        max_file = 0
        entries = []
        for op, val in set_dict.items():
            max_name = max(max_name, len(op))
            if is_total:
                count = val[0]
                file = val[1]
                max_file = max(max_file, file)
                entries.append((op, count, file))
            else:
                count = val
                entries.append((op, count))
            max_count = max(max_count, count)

        sort_mode = sort
        if not is_total and sort_mode > 1:
            sort_mode = 1

        if len(entries) == 0:
            return

        entries.sort(key=lambda item: item[sort_mode], reverse=order_desc)
        print()
        max_count = max(len(str(max_count)), len("count"))
        if is_total:
            max_file = max(len(str(max_file)), len("file"))
            print(f"{name + '=':<{max_name}} {"count":>{max_count}}, {"file":>{max_file}}")
            for op, count, file in entries:
                print(f"{op:<{max_name}} {count:>{max_count}}, {file:>{max_file}}")
        else:
            print(f"{name + '=':<{max_name}} {"count":>{max_count}}")
            for op, count in entries:
                print(f"{op:<{max_name}} {count:>{max_count}}")

    display_set("Base SPIR-V", result.counts)
    for set_name, set_dict in result.ext_counts.items():
        display_set(set_name, set_dict)


def display_opcode_results(results, per_file, order_desc, sort):
    """
    Display the opcode result tables
    """
    failures = []
    totals = Totaler()
    for record in results:
        if len(record.err) > 0:
            failures.append(record.path)
        else:
            totals += record

    instruction_count = 0
    opcode_count = 0

    for count, _ in totals.counts.values():
        # Recall that all extension insts are counted in the basic set
        instruction_count += count
        opcode_count += 1
    for ext_dict in totals.ext_counts.values():
        opcode_count += len(ext_dict)
    if len(totals.ext_counts) > 0:
        # Don't count extension as a unique instruction since the extension set has each variant counted
        opcode_count -= 1

    print(f"# {len(results) - len(failures)}/{len(results)} shaders processed, {instruction_count} instructions, " \
          f"{opcode_count} distinct opcodes")
    for spv, err in failures:
        print(f"# FAILED: {spv}: {err}")

    if per_file:
        if len(results) > 0:
            print()

        for record in results:
            print(f"== {record.path} ==", end="")
            display_table(record, False, order_desc, sort)
            print()
        print("== TOTAL ==", end="")
    display_table(totals, True, order_desc, sort)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?",
                        help="directory to search")
    parser.add_argument("-d", "--dis", default="spirv-dis",
                        help="spirv-dis binary (default: spirv-dis on PATH)")
    parser.add_argument("-o", "--order", choices=("desc", "asc"),
                        help="sort order: descending or ascending")
    # settings exclusive to regular mode
    parser.add_argument("-p", "--per-file", action="store_true",
                        help="print instruction counts per file instead of for the whole directory")
    parser.add_argument("-s", "--sort", choices={"alpha", "count", "file"},
                        help="sort tables alphabetically, by occurrence count (default), or the number of shaders " \
                             "each instruction appears in")
    # settings exclusive to find mode
    parser.add_argument("-f", "--find", metavar="NAME",
                        help="list shaders containing this opcode or extended-set instruction, sorted by use count")

    args = parser.parse_args()

    # Sort Modes
    SORT_ALPHA = 0
    SORT_COUNT = 1
    SORT_FILE = 2

    if args.find:
        if args.per_file:
            print("Cannot print per-file results for an instruction find search!", file=sys.stderr)
            sys.exit(1)
        if args.sort:
            print("Cannot specify sort mode when running an instruction find search!", file=sys.stderr)
            sys.exit(1)
    else:
        if args.sort == "alpha":
            args.sort = SORT_ALPHA
        elif args.sort == "file":
            args.sort = SORT_FILE
        else:
            args.sort = SORT_COUNT

        if not args.order:
            if args.sort == SORT_ALPHA:
                args.order = "asc"
            else:
                args.order = "desc"

    # Default to the examples directory if no path is selected
    root_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    launch_path = os.path.join(os.getcwd(), args.directory) if args.directory is not None else \
                  os.path.join(root_path, "examples")

    if not os.path.isdir(launch_path):
        print("Search path must be a directory to search!", file=sys.stderr)
        sys.exit(1)

    # Recursively search through the files, amassing opcode data
    records = opcode_collect(os.path.abspath(launch_path), args.dis)

    # Display the selected results
    if args.find:
        display_find_results(records, args.find, args.order != "asc")
    else:
        display_opcode_results(records, args.per_file, args.order == "desc", args.sort)

    sys.exit(0)
