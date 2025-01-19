# SPDX-License-Identifier: GPL-2.0+

import os
import sys

import tbot

from tbot.context import Optional
from tbot.machine import linux
from tbot.machine import board

from tbottest.common.ubootbuild import UBBUILDMAN
from tbotconfig import labcallbacks

from boardtestconfig import boards as B

@tbot.testcase
def tbottest_install_board(b) -> int:  # noqa: D107
    """
    Install the new U-Boot image on the board b
    """
    try:
        tbot.log.message(tbot.log.c(f"Install bootloader image").green)
        funcname = b['install']
        func = getattr(labcallbacks, funcname)
        ret = func()
        if ret != True:
            tbot.log.message(tbot.log.c(f"calling bootloader install failed {b['install']} with {ret}").yellow)
            return 1
    except Exception as e:
        tbot.log.message(tbot.log.c(f"calling bootloader install failed {b['install']} with exception " + str(e)).yellow)
        return 1

    return 0

from tbot_contrib import uboot

@tbot.testcase
def run_testpy(b, bmcfg) -> None:
    with tbot.ctx.request(tbot.role.BuildHost) as h:
        # location of the U-Boot paths (source and build)
        uboot_sources = bmcfg.basedir
        uboot_builddir = bmcfg.builddirbuildman

        h.exec0("pip", "install", "-r", uboot_sources / "test/py/requirements.txt")
        # subshell for the build environment
        with h.subshell():
            uboot.testpy(
                uboot_sources,
                testpy_args=["--maxfail", "6", f"--junitxml=/{bmcfg.builddirbuildman._local_str()}/{b['boardname']}/results.xml"],
                uboot_builddir = uboot_builddir,
            )
        td = f"{uboot_sources._local_str()}/results/{b['boardname']}"
        h.exec0("mkdir", "-p", td)
        h.exec("cp", f"{bmcfg.builddirbuildman._local_str()}/{b['boardname']}/results.xml", f"{td}/results.xml")
        h.exec("cp", f"{bmcfg.builddirbuildman._local_str()}/multiplexed_log.css", f"{td}/multiplexed_log.css")
        h.exec("cp", f"{bmcfg.builddirbuildman._local_str()}/test-log.html", f"{td}/test-log.html")

@tbot.testcase
def tbottest_test_board(b, bmcfg) -> int:  # noqa: D107
    """
    Test the new installed U-Boot on the board b
    """
    try:
        tbot.log.message(tbot.log.c(f"Boot and Test bootloader image").green)
        funcname = b['boottest']
        func = getattr(labcallbacks, funcname)
        ret = func()
        if ret != True:
            tbot.log.message(tbot.log.c(f"calling Boot and Test failed {b['boottest']} with {ret}").yellow)
    except Exception as e:
        tbot.log.message(tbot.log.c(f"calling Boot and Test failed {b['boottest']} with exception " + str(e)).yellow)
        return 1

    run_testpy(b, bmcfg)

    return 0

@tbot.testcase
def _tbottest_one_board(
    lab: linux.LinuxShell = None,
    bh: linux.LinuxShell = None,
    b = None,
) -> int:  # noqa: D107
    """
    Build, install and test U-Boot for board b
    """
    bmcfg = UBBUILDMAN(lab, bh, b['ubootpatchsubpath'], b["binsubpath"],
                        b["defconfig"],
                        b["resultbinaries"],
                        b["makelist"])

    bmcfg.bm_build_board()
    bmcfg.bm_copy_results2lab()

    if tbottest_install_board(b):
        return 1

    return tbottest_test_board(b, bmcfg)

from tbottest.initconfig import generic_get_boardname

@tbot.testcase
def tbottest_one_board(
    lab: linux.LinuxShell = None,
    bh: linux.LinuxShell = None,
    boardname: str = None,
) -> int:  # noqa: D107
    """
    Build, install and test U-Boot for board b
    """
    with tbot.ctx() as cx:
        if bh is None:
            bh = cx.request(tbot.role.BuildHost)

        if lab is None:
            lab = cx.request(tbot.role.LabHost)

        bname = generic_get_boardname()
        for bi in B:
            if bi['boardname'] == bname:
                b = bi

        if _tbottest_one_board(lab, bh, b):
            raise RuntimeError(f"Testing {bname} failed")

    return 0
