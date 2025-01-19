#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0+

# -*- coding: utf-8 -*-

import inspect
import os
import sys
currentdir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
sys.path.insert(0, currentdir + "/tbot")
sys.path.insert(0, currentdir + "/tbottest")

from tbot.newbot import main  # noqa: E402

from boardtestconfig import boards as B

if __name__ == "__main__":
    """
    Build, install and test U-Boot for all boards
    """
    failed = 0
    success = 0
    count = 0
    tests = len(B)
    orgargv = sys.argv.copy()
    for b in B:
        count += 1
        print(f"--------- start test {b['boardname']} {count} / {tests} suc: {success} fail: {failed} ---------")
        newargv = orgargv.copy()
        newargv = newargv[1:]
        newargv.append(b['tbotargs'])
        newargv.remove("--usetbotflags")
        newargv.append("-f")
        newargv.append(f"boardname:{b['boardname']}")
        newargv.append("tbotboardtests.tbottest_one_board")

        try:
            # call tbot
            main(newargv)
            print(f"test {b['boardname']} succeeded")
            success += 1
        except Exception as e:
            print(f"test {b['boardname']} failed with exception " + str(e))
            failed += 1

    print(f"--------- tests {count} / {tests} suc: {success} fail: {failed} ---------")
    if failed:
        sys.exit(1)

    sys.exit(0)
