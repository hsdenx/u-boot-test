import os
import sys

boards = [
        { 
            "boardname":"cxg3",
            "defconfig":"capricorn_cxg3",
            "binaries":["bl31.bin", "mx8qx-mek-scfw-tcm.bin", "mx8qxc0-ahab-container.img"],
            "binpath":"tbottesting/tbotconfig-cxg3/cxg3/binaries/",
            "resultbinaries":["flash.bin"],
            "makelist":["flash.bin"],
        },
        ]
