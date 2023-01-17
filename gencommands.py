#!/usr/bin/env python3

# Copyright (C) 2023 Viktor Soderqvist <viktor dot soderqvist at est dot tech>
# This file is released under the BSD license, see the COPYING file

# This script generates cmddef.h from the JSON files in the Redis repo
# describing the commands. This is done manually when commands have been added
# to Redis.
#
# Usage: ./gencommands.py path/to/redis > cmddef.h

import glob
import json
import os
import sys

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
    subcommand = None
    if container != "":
        subcommand = name
        name = container
    return (name, subcommand, props["arity"], firstkeymethod, firstkeypos);

# Checks that the filename matches the command. We need this because we rely on
# the alphabetic order.
def check_filename(filename, cmd):
    if cmd[1] is None:
        expect = "%s" % cmd[0]
    else:
        expect = "%s-%s" % (cmd[0], cmd[1])
    expect = expect.lower() + ".json"
    assert os.path.basename(filename) == expect

def collect_commands_from_files(filenames):
    commands = []
    for filename in filenames:
        with open(filename, "r") as f:
            try:
                d = json.load(f)
                for name, props in d.items():
                    cmd = extract_command_info(name, props)
                    check_filename(filename, cmd)
                    commands.append(cmd)
            except json.decoder.JSONDecodeError as err:
                print("Error processing %s: %s" % (filename, err))
                exit(1)
    return commands

def generate_c_code(commands):
    print("/* This file was generated using gencommands.py */")
    print("")
    print("/* clang-format off */")
    commands_that_have_subcommands = set()
    for (name, subcmd, arity, firstkeymethod, firstkeypos) in commands:
        if subcmd is None:
            if name in commands_that_have_subcommands:
                continue # only include the command with its subcommands
            print("COMMAND(%s, \"%s\", NULL, %d, %s, %d)" %
                  (name.replace("-", "_"), name, arity, firstkeymethod, firstkeypos))
        else:
            commands_that_have_subcommands.add(name)
            print("COMMAND(%s_%s, \"%s\", \"%s\", %d, %s, %d)" %
                  (name.replace("-", "_"), subcmd.replace("-", "_"),
                   name, subcmd, arity, firstkeymethod, firstkeypos))

# MAIN

if len(sys.argv) < 2 or sys.argv[1] == "--help":
    print("Usage: %s REDIS-DIR > cmddef.h" % sys.argv[0])
    exit(1)

redisdir = sys.argv[1]
jsondir = os.path.join(redisdir, "src", "commands")
if not os.path.isdir(jsondir):
    print("The path %s doesn't point to a Redis source directory." % redisdir)
    exit(1)

# Collect all command info
filenames = glob.glob(os.path.join(jsondir, "*.json"))
filenames.sort()
commands = collect_commands_from_files(filenames)

# Print C code
generate_c_code(commands)
