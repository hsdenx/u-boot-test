# SPDX-License-Identifier: GPL-2.0+

import os
import sys

boards = [
        {
            "boardname" : "cxg3",
            "tbotargs"  : "@tbotconfig/hs/args/argshs-github-ci",
            "ubootpatchsubpath": "uboot-patches",
            "binsubpath":"binaries",
            "defconfig":"capricorn_cxg3",
            "resultbinaries":["flash.bin"],
            "makelist":["flash.bin"],
            "install":"cxg3_ub_install",
            "boottest":"cxg3_ub_boot_and_test",
        },
        ]
