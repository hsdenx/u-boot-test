# SPDX-License-Identifier: GPL-2.0+

import os
import re
import sys
import tbot
import time
from tbot.context import Optional

from tbot.machine import linux
from tbot.machine import board
from tbot.machine import connector

from tbottest.boardgeneric import cfggeneric
from tbottest.tc.uboot import board_ub_unit_test

cfg = cfggeneric
si = cfg.get_default_config("serverip", "None")


################################################
# U-Boot
################################################
@tbot.testcase
def _cxg3_ub_install(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
) -> bool:  # noqa: D107
    """
    install U-Boot with uuu tool
    """
    # install U-Boot with uuu tool
    tbot.log.message(tbot.log.c("----------- request U-Boot from USB SDP -----------").yellow)
    oldflags = tbot.flags.copy()
    try:
        tbot.flags.remove("bootmode:emmc")
    except:
        pass
    tbot.flags.add("bootmode:usb")
    tbot.flags.add("uuuloader")

    with tbot.ctx() as cx:
        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        ub = cx.request(tbot.role.BoardUBoot)

        if "Boot:  USB" not in ub.bootlog:
            raise RuntimeError(f"Boot:  USB not found in U-Boot bootlog")

    return True

@tbot.testcase
def _cxg3_ub_boot_and_test(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
) -> bool:  # noqa: D107
    """
    boot the new U-Boot from emmc and test it.
    """
    tbot.log.message(tbot.log.c("----------- request U-Boot from emmc-----------").yellow)
    try:
        tbot.flags.add("bootmode:emmc")
        tbot.flags.remove("bootmode:usb")
        tbot.flags.remove("uuuloader")
    except:
        pass

    with tbot.ctx() as cx:
        lab = cx.request(tbot.role.LabHost)
        ub = cx.request(tbot.role.BoardUBoot)

        if "Boot:  MMC0" not in ub.bootlog:
            raise RuntimeError(f"Boot:  MMC0 not found in U-Boot bootlog")

        # just a fast ethernet test, more later
        ub.exec0("ping", si)

        # may unit test command is activated, so call it
        return board_ub_unit_test(ub)

    return False
