# SPDX-License-Identifier: GPL-2.0+

import configparser
from configparser import ExtendedInterpolation

import tbot

from tbottest.initconfig import generic_get_boardname
from tbottest.initconfig import replace_in_file
from tbottest.initconfig import find_in_file_and_delete


SERVERIP = None


def cxg3_get_lab_serverip(filename: str = None):
    """
    helper to save ini file reads
    """
    global SERVERIP

    if SERVERIP is not None:
        return SERVERIP

    if filename is None:
        raise RuntimeError("serverip not setup")

    # read from ini file
    config_parser = configparser.RawConfigParser(interpolation=ExtendedInterpolation())
    config_parser.read(filename)
    for s in config_parser.sections():
        if "IPSETUP" in s:
            nm = s.split("_")[1]
            dev = s.split("_")[2]

            if nm == generic_get_boardname() and dev == "eth0":
                SERVERIP = config_parser.get(s, "serverip")
                return SERVERIP

    raise RuntimeError("serverip setup not found")


IPADDR = None


def cxg3_get_board_ipaddr(filename: str = None):
    """
    helper to save ini file reads
    """
    global IPADDR

    if IPADDR is not None:
        return IPADDR

    if filename is None:
        raise RuntimeError("ipaddr not setup")

    # read from ini file
    config_parser = configparser.RawConfigParser(interpolation=ExtendedInterpolation())
    config_parser.read(filename)
    for s in config_parser.sections():
        if "IPSETUP" in s:
            nm = s.split("_")[1]
            dev = s.split("_")[2]

            if nm == generic_get_boardname() and dev == "eth0":
                IPADDR = config_parser.get(s, "ipaddr")
                return IPADDR

    raise RuntimeError("ipaddr not found")


def print_log(msg):
    try:
        if tbot.selectable.printed:
            return
    except:
        pass

    tbot.log.message(tbot.log.c(msg).yellow)


def set_ub_board_specific(self):  # noqa: C901
    """
    sample implementation for changing U-Boot setup dependent on tbot.flags
    """
    if "silent" in tbot.flags:
        self.env("console", "silent")


def set_board_cfg(temp: str = None, filename: str = None):  # noqa: C901
    """
    setup board specific stuff in ini files before they get parsed
    """
    # print big fat warning, that example is used
    print_log(f"TBOT.FLAGS {sorted(tbot.flags)}")

    replace_in_file(filename, "@@TBOTDATE@@", "20250208")
    boardname = generic_get_boardname()
    print_log(f"boardname now {boardname}")

    tftpsubdir = "${board}/${date}"
    replace_in_file(filename, "@@TBOTBOARD@@", boardname)
    replace_in_file(filename, "@@TFTPSUBDIR@@", tftpsubdir)
    replace_in_file(filename, "@@TBOTSERVERIP@@", cxg3_get_lab_serverip(filename))
    replace_in_file(filename, "@@TBOTIPADDR@@", cxg3_get_board_ipaddr(filename))
    replace_in_file(filename, "@@PICOCOMDELAY@@", "3")

FLAGS = {
    "boardname": "set theboardname of the board format boardname:<name>",
}
