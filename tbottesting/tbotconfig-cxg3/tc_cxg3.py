import os
import re
import sys
import tbot
import time
from datetime import datetime
from tbot.context import Optional

from tbot.machine import linux
from tbot.machine import board
from tbot.machine import connector

from tbottest.boardgeneric import cfggeneric
from tbottest.labgeneric import cfgt as cfglab

cfg = cfggeneric
server_nfs_path = cfg.get_default_config("nfs_path", "None")
si = cfg.get_default_config("serverip", "None")
nfspath = f"{cfg.tmpdir}/nfs"


################################################
# U-Boot
################################################
@tbot.testcase
def cxg3_ub_dummy(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
) -> bool:  # noqa: D107
    """
    Dummy U-Boot example
    """
    with tbot.ctx() as cx:
        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        if ub is None:
            ub = cx.request(tbot.role.BoardUBoot)

        ub.exec0("echo", "Hello World!")

from tbotconfig import tc_cxg3

ub_testcases = ["cxg3_ub_dummy"]

@tbot.testcase
def cxg3_ub_all(
    lab: Optional[linux.LinuxShell]=None,
    ub: Optional[board.UBootShell]=None,
    interactive = False,
) -> None:  # noqa: D107
    """
    call all U-Boot tests
    """
    with tbot.ctx() as cx:
        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        if ub is None:
            ub = cx.request(tbot.role.BoardUBoot)

        failed = 0
        success = 0
        count = 0
        tests = len(ub_testcases)

        for t in ub_testcases:
            count += 1
            tbot.log.message(tbot.log.c(f"---- start test {t} {count} / {tests} suc: {success} fail: {failed} ----").yellow)
            try:
                func = getattr(tc_abb, t)
                ret = func()
            except:
                tbot.log.message(tbot.log.c(f"---- test {t} not found ----").red)
                continue

        if failed == 0:
            tbot.log.message(tbot.log.c(f"---- tests {t} {count} / {tests} suc: {success} fail: {failed} ----").green)
        else:
            tbot.log.message(tbot.log.c(f"---- tests {t} {count} / {tests} suc: {success} fail: {failed} ----").red)

        if interactive:
            ub.interactive()

################################################
# Linux
################################################
@tbot.testcase
def cxg3_lx_dummy(
    lab: linux.LinuxShell = None,
    lnx: linux.LinuxShell = None,
    interactive = False,
) -> None:  # noqa: D107
    """
    """
    with tbot.ctx() as cx:
        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        if lnx is None:
            lnx = cx.request(tbot.role.BoardLinux)

        lnx.exec0("echo", "Hello Linux World!")

        if interactive:
            lnx.interactive()

################################################
# Build
################################################

from tbottest.common.ubootbuild import UBBUILDMAN
from boardtests import boards as B

@tbot.testcase
def tbottest_build_board(
    lab: linux.LinuxShell = None,
    bh: linux.LinuxShell = None,
) -> None:  # noqa: D107
    """
    setup all stuff we need for building U-Boot
    """
    with tbot.ctx() as cx:
        if bh is None:
            bh = cx.request(tbot.role.BuildHost)

        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        for b in B:
            bmcfg = UBBUILDMAN(lab, bh, b["defconfig"], b["binaries"],
                                b["binpath"], b["resultbinaries"],
                                b["makelist"])
            bmcfg.bm_build_board()

            bmcfg.bm_copy_results2lab()
