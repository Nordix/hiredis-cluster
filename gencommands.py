#!/usr/bin/env python3

# Copyright (C) 2023 Viktor Soderqvist <viktor dot soderqvist at est dot tech>
# This file is released under the BSD license, see the COPYING file

# This script generates cmddef.h from the JSON files in the Redis repo
# describing the commands. This is done manually when commands have been added
# to Redis.
#
# Usage: ./gencommands.py path/to/redis/src/commands/*.json > cmddef.h

import glob
import json
import os
import sys
import re

# Returns a tuple (method, index) where method is one of the following:
#
#     NONE          = No keys
#     UNKNOWN       = The position of the first key is unknown or too
#                     complex to describe (example XREAD)
#     INDEX         = The first key is the argument at index i
#     KEYNUM        = The argument at index i specifies the number of keys
#                     and the first key is the next argument, unless the
#                     number of keys is zero in which case there are no
#                     keys (example EVAL)
def firstkey(props):
    if not "key_specs" in props or len(props["key_specs"]) == 0:
        # No keys
        return ("NONE", 0)
    # We detect the first key spec and only if the begin_search is by index.
    # Otherwise we return -1 for unknown (for example if the first key is
    # indicated by a keyword like KEYS or STREAMS).
    begin_search = props["key_specs"][0]["begin_search"]
    if not "index" in begin_search:
        return ("UNKNOWN", 0)
    pos = begin_search["index"]["pos"]
    find_keys = props["key_specs"][0]["find_keys"]
    if "range" in find_keys:
        # The first key is the arg at index pos.
        return ("INDEX", pos)
    elif "keynum" in find_keys:
        # The arg at pos is the number of keys and the next arg is the first key
        assert find_keys["keynum"]["keynumidx"] == 0
        assert find_keys["keynum"]["firstkey"] == 1
        return ("KEYNUM", pos)
    else:
        return ("UNKNOWN", 0)

def extract_command_info(name, props):
    (firstkeymethod, firstkeypos) = firstkey(props)
    container = props.get("container", "")
    name = name.upper()
    subcommand = None
    if container != "":
        subcommand = name
        name = container.upper()
    return (name, subcommand, props["arity"], firstkeymethod, firstkeypos);

def collect_commands_from_files(filenames):
    # The keys in the dicts are "command" or "command_subcommand".
    commands = dict()
    commands_that_have_subcommands = set()
    for filename in filenames:
        with open(filename, "r") as f:
            try:
                d = json.load(f)
                for name, props in d.items():
                    cmd = extract_command_info(name, props)
                    (name, subcmd, _, _, _) = cmd

                    # For commands with subcommands, we want only the
                    # command-subcommand pairs, not the container command alone
                    if subcmd is not None:
                        commands_that_have_subcommands.add(name)
                        if name in commands:
                            del commands[name]
                        name += "_" + subcmd
                    elif name in commands_that_have_subcommands:
                        continue

                    commands[name] = cmd

            except json.decoder.JSONDecodeError as err:
                print("Error processing %s: %s" % (filename, err))
                exit(1)
    return commands

def generate_c_code(commands):
    print("/* This file was generated using gencommands.py */")
    print("")
    print("/* clang-format off */")
    for key in sorted(commands):
        (name, subcmd, arity, firstkeymethod, firstkeypos) = commands[key]
        # Make valid C identifier (macro name)
        key = re.sub(r'\W', '_', key)
        if subcmd is None:
            print("COMMAND(%s, \"%s\", NULL, %d, %s, %d)" %
                  (key, name, arity, firstkeymethod, firstkeypos))
        else:
            print("COMMAND(%s, \"%s\", \"%s\", %d, %s, %d)" %
                  (key, name, subcmd, arity, firstkeymethod, firstkeypos))

# MAIN

if len(sys.argv) < 2 or sys.argv[1] == "--help":
    print("Usage: %s path/to/redis/src/commands/*.json > cmddef.h" % sys.argv[0])
    exit(1)

# Fine all JSON files
filenames = []
for filename in sys.argv[1:]:
    if os.path.isdir(filename):
        # A redis repo root dir (accepted for backward compatibility)
        jsondir = os.path.join(filename, "src", "commands")
        if not os.path.isdir(jsondir):
            print("The directory %s is not a Redis source directory." % filename)
            exit(1)

        filenames += glob.glob(os.path.join(jsondir, "*.json"))
    else:
        filenames.append(filename)

# Collect all command info
commands = collect_commands_from_files(filenames)

# Print C code
generate_c_code(commands)
