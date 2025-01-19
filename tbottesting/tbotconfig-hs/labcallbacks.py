# SPDX-License-Identifier: GPL-2.0+

import tbot
import time

from tbot.context import Optional
from tbot.machine import board
from tbot.machine import linux

from tbotconfig.tc_cxg3 import _cxg3_ub_install
from tbotconfig.tc_cxg3 import _cxg3_ub_boot_and_test

################################################
## Lab specific
################################################

fitzroyinstallpath = "/home/pi/source/fitzroy-command"
fitzroyserial = "CES3088"
bootmode = "notset"

def cxg3_lab_run_fitzroy(
    lab: linux.LinuxShell = None,
    mode: str = None,
) -> None:  # noqa: D107
    """
    """
    global bootmode

    if bootmode != mode:
        lab.exec0(f"{fitzroyinstallpath}/fitzroy", "-s", fitzroyserial, "bootmode", mode)
        #lab.exec0(f"{fitzroyinstallpath}/fitzroy", "-s", fitzroyserial, "frontend", "program")
        lab.exec0(f"{fitzroyinstallpath}/fitzroy", "-s", fitzroyserial, "reset")
        bootmode = mode
        time.sleep(2)


################################################
## callback called from ./tbottest/labgeneric.py
################################################
def cxg3_setbootmode_usb(
    lab: linux.LinuxShell = None,
) -> None:  # noqa: D107
    """
    set cxg3 board into bootmode USB SDP
    """
    cxg3_lab_run_fitzroy(lab, "usb")
    return True

def cxg3_setbootmode_emmc(
    lab: linux.LinuxShell = None,
) -> None:  # noqa: D107
    """
    set cxg3 board into bootmode emmc
    """
    cxg3_lab_run_fitzroy(lab, "emmc")
    return True

################################################
## callback called from ./tbottesting/boardtests.py
################################################
@tbot.testcase
def cxg3_ub_install(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
) -> bool:  # noqa: D107
    """
    install U-Boot with uuu tool
    """
    return _cxg3_ub_install(lab, ub)

@tbot.testcase
def cxg3_ub_boot_and_test(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
) -> bool:  # noqa: D107
    return _cxg3_ub_boot_and_test(lab, ub)
