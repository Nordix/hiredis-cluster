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

# Returns the position of the first key, where 1 is the first arg after the
# command name. Special return values: 0 = no keys, -1 = unknown.
def firstkey(props):
    unknown = False
    i = 1
    for arg in props.get("arguments", []):
        # If the key or any args before it is optional, we can't be sure about
        # the position of the key.
        if arg.get("optional", False):
            unknown = True
        if arg["type"] == "key":
            return (-1 if unknown else i)
        # If any args before the key can occure multiple times, we can't be sure
        # about the position of the key.
        if arg.get("multiple", False):
            unknown = True
        i = i + 1
    # None of the arguments was a key.
    return 0

def collect_command(filename, name, props):
    keypos = firstkey(props)
    container = props.get("container", "")
    subcommand = None
    if container != "":
        subcommand = name
        name = container
        if keypos > 0:
            keypos = keypos + 1
    return (name, subcommand, keypos, props["arity"]);

def collect_commands(filename, items):
    commands = []
    for name, props in items:
        cmd = collect_command(filename, name, props)
        commands.append(cmd)
    return commands

def generate_c_code(commands):
    for (name, subcmd, keypos, arity) in commands:
        if subcmd is None:
            print("COMMAND(%s, \"%s\", NULL, %s, %s)" %
                  (name.replace("-", "_"), name, keypos, arity))
        else:
            print("COMMAND(%s_%s, \"%s\", \"%s\", %s, %s)" %
                  (name.replace("-", "_"), subcmd.replace("-", "_"),
                   name, subcmd, keypos, arity))

# MAIN

if len(sys.argv) < 2 or sys.argv[1] == "--help":
    print("Usage: %s REDIS-DIR > cmddef.h" % sys.argv[0])
    exit(1)

redisdir = sys.argv[1]
jsondir = os.path.join(redisdir, "src", "commands")
if not os.path.isdir(jsondir):
    print("The path %s doesn't point to a Redis source directory." % redisdir)
    exit(1)

# Create all command objects
filenames = glob.glob(os.path.join(jsondir, "*.json"))
filenames.sort()
print("/* This file was generated using gencommands.py */")
print("")
print("/* clang-format off */")
for filename in filenames:
    with open(filename, "r") as f:
        try:
            d = json.load(f)
            commands = collect_commands(filename, d.items())
            generate_c_code(commands)
        except json.decoder.JSONDecodeError as err:
            print("Error processing %s: %s" % (filename, err))
            exit(1)
