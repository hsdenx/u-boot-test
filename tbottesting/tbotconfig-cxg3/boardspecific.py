import configparser
from configparser import ExtendedInterpolation

import tbot

from tbottest.initconfig import generic_get_boardname
from tbottest.initconfig import replace_in_file
from tbottest.initconfig import find_in_file_and_delete

TBOTFILE = None
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


def board_set_boardname() -> str:
    """
    sample implementation as example how to set own boardname

    This name is used for selecting the sections in the ini files

    So you can have in one lab setup more than one boards
    """
    # do not use selectableboardname flag
    BOARDNAME = "cxg3"
    for f in tbot.flags:
        if "8G" in f:
            if len(f) == 2:
                BOARDNAME = "cxg38g"

    return BOARDNAME


ROOTFSNAME = None


def cxg3_get_rootfsname():
    """
    helper to save ini file reads
    """
    global ROOTFSNAME

    if ROOTFSNAME is not None:
        return ROOTFSNAME

    for f in tbot.flags:
        if "rootfsname" in f:
            ROOTFSNAME = f.split(":")[1]
            return ROOTFSNAME

    ROOTFSNAME = "CUSTOMER-image-qt-dev-denx"
    return ROOTFSNAME


MACHINENAME = None


def cxg3_get_machinename():
    """
    helper to save ini file reads
    """
    global MACHINENAME

    if MACHINENAME is not None:
        return MACHINENAME

    MACHINENAME = "CUSTOMER-board"
    return MACHINENAME


def print_log(msg):
    try:
        if tbot.selectable.printed:
            return
    except:
        pass

    tbot.log.message(tbot.log.c(msg).yellow)


def set_ub_board_specific(self):  # noqa: C901
    """
    sample implementation for U-Boot setup dependent on tbot.flags
    """
    if "silent" in tbot.flags:
        self.env("console", "silent")


def set_board_cfg(temp: str = None, filename: str = None):  # noqa: C901
    """
    setup board specific stuff in ini files before they get parsed
    """
    # print big fat warning, that example is used
    print_log(f"TBOT.FLAGS {sorted(tbot.flags)}")

    if "tbot.ini" in str(filename):
        global TBOTFILE

        TBOTFILE = filename

    replace_in_file(filename, "@@TBOTDATE@@", "20230406")
    replace_in_file(filename, "@@TBOTMACHINE@@", cxg3_get_machinename())
    boardname = generic_get_boardname()
    print_log(f"boardname now {boardname}")

    kascontainer = "False"
    sshkeypath = None
    kasfilename = "kas-denx-withdldir.yml"
    tftpsubdir = "${board}/${date}"
    tftpsubdir_addbranch = None
    tftpsubdir_addkas = False
    tftpsubdir_addsisyphus = False
    for f in tbot.flags:
        if "buildername:sisyphus" in f:
            # simulate production build, remove some stuff in ini file
            kascontainer = "True"
            sshkeypath = "/work/hs/yocto/.ssh"
            # we want to build without meta-denx layer
            # remove lines with "dev-denx"
            find_in_file_and_delete(filename, "-dev-denx")
            tftpsubdir_addkas = True
            tftpsubdir_addsisyphus = True

        if "kas" in f and "kaslayerbranch" not in f:
            tftpsubdir_addkas = True

        if "partition" in f:
            part = f.split(":")[1]
            replace_in_file(filename, "rootfspart = 3", f"rootfspart = 0:{part}")

        if "kasfilename" in f:
            kasfilename = f.split(":")[1]

        if "lauterbachloader" in f:
            replace_in_file(
                filename, "uboot_autoboot_iter = 1", "uboot_autoboot_iter = 2"
            )

        if "kaslayerbranch" in f:
            br = f.split(":")[1]
            tftpsubdir_addbranch = br
            replace_in_file(
                filename,
                "subdir = ${vendor}/${machine}",
                f"subdir = ${{vendor}}/${{machine}}/{br}",
            )
            replace_in_file(
                filename, "denxlayerbranch = master", f"denxlayerbranch = {br}"
            )
            if "kirkstone" in br:
                # build kirkstone based sources in ubuntu 20.04 container
                replace_in_file(filename, "port = 11606", "port = 12004")

    # create tftpsubdir
    if tftpsubdir_addbranch:
        tftpsubdir = f"${{board}}/{tftpsubdir_addbranch}/${{date}}"
    if tftpsubdir_addkas:
        tftpsubdir = f"{tftpsubdir}/kas"
    if tftpsubdir_addsisyphus:
        tftpsubdir = "{tftpsubdir}/sisyphus"

    if "oldbsp" in tbot.flags:
        tftpsubdir = "${{board}}//${{date}}"

    replace_in_file(filename, "@@TBOTBOARD@@", boardname)
    replace_in_file(filename, "@@TFTPSUBDIR@@", tftpsubdir)
    tmp = cxg3_get_rootfsname()
    replace_in_file(filename, "@@ROOTFSNAME@@", tmp)
    print_log(f"Using kas file {kasfilename}")
    replace_in_file(filename, "@@KASFILENAME@@", kasfilename)
    replace_in_file(filename, "@@KASCONTAINER@@", kascontainer)
    if sshkeypath:
        replace_in_file(filename, "/home/hs/.ssh", sshkeypath)

    replace_in_file(filename, "@@TBOTSERVERIP@@", cxg3_get_lab_serverip(filename))
    replace_in_file(filename, "@@TBOTIPADDR@@", cxg3_get_board_ipaddr(filename))

    # only for creating docs!
    replace_in_file(filename, "@@PICOCOMDELAY@@", "3")

    # replace in board file setttings from tbot.ini
    if "tbot.ini" not in str(filename):
        config_parser = configparser.RawConfigParser(
            interpolation=ExtendedInterpolation()
        )
        config_parser.read(TBOTFILE)
        for s in config_parser.sections():
            if "LABHOST" in s:
                try:
                    basepath = config_parser.get(s, "nfs_base_path")
                except:
                    raise RuntimeError(
                        f"Could not find 'nfs_base_path' key in section 'default' in {filename}"
                    )
        replace_in_file(filename, "@@TBOTLABBASENFSPATH@@", basepath)


FLAGS = {
    "selectableboardname": "set value of tbot.selectable.boardname format selectableboardname:<name>",
}
