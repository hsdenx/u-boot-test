# testing U-Boot with tbot

This subdirectory contains a full tbot test setup for
building, installing and running U-Boot tests in
the U-Boot shell on the hardware.

You find all supported boards here

[boards](./boardtestconfig.py)

short explanation of the keys:

[config options](#boards_config_options)

| key                | description                                                                                                                    |
| ---                | ---                                                                                                                            |
| boardname          | Name of the board in your lab                                                                                                  |
| ubootpatchsubpath  | subdir on your lab hosts tftp path for your board with downstream U-Boot patches, tbot copies them from lab host to build host |
| binsubpath         | subdir on your lab hosts tftp path for your board with binary blobs, needed for full U-Boot build                              |
| defconfig          | defconfig name for your board                                                                                                  |
| resultbinaries     | list of binaries, tbot transfers after successfull build to your lab hosts tftp directory                                      |
| makelist           | U-Boot is builded with buildman, but may you want to call after that some make commands too, list them here                    |
| install            | functionname, which installs U-Boot on the board                                                                               |
| boottest           | functionname, which boots and tests the new installed U-Boot                                                                   |

If you have a full working tbot setup, [boardtests.py](./boardtests.py)
will make the following

- create the build dir on build host
- copy the binaries found on lab hosts tftpdirectory in subdirectory ```binsubpath```
  to the build directory on the build host
- copy downstream U-Boot patches found on lab hosts tftpdirectory in subdirectory ```ubootpatchsubpath```
  to the U-Boot source directory on the build host, and apply them
- build U-Boot with buildman
- call make target defined in ```makelist```
- copy resulting binaries defined in ```resultbinaries``` from build host builddir
  to lab hosts tftp directory.
- call tbot testcase name defined ```install``` for installing the new U-Boot image

  example cxg3 board:
  set bootmode USB SDP, load SPL / U-Boot with ```uuu``` tool enter ```fastboot``` mode
  and install SPL/U-Boot image with ```uuu``` tool.

- call tbot testcase name defined ```boottest``` for booting the new U-Boot image

  example cxg3 board:
  enter emmc bootmode and boot into U-Boot, make there a ```ping``` test and
  start ```ut``` U-Boot command.

- call U-Boot test framework (in u-boot:/test/py)


# Adding new boards

in theory it should be enough to add your board here:

[boardconfig](./boardtestconfig.py)

and add your tbotconfig in subdirectory [tbottesting}(.)
see as an example [tbotconfig-hs](./tbotconfig-hs)

Of course, you need to add your ssh secrets to github, so contact
me.

Heiko Schocher <hs@denx.de>

If you have not yet an tbotconfig, there are 2 options. Send me the board
and I add it in my lab, or may the easiest approach to copy above folder and
rename it too ```tbotconfig-foobar``` and make some adaptions, described in the
following sections. It may help if you read [1](https://tbot.tools) and [3](https://hsdenx.github.io/tbottest/quickstart.html)

The following sections exaplain how to adapt ```tbotconfig-hs```
subdirectory, but please do it in your own new created ```tbotconfig-foobar```!

[!NOTE]
May we can add a script which does this automagically.

## setup new boards

give the board a name, lets say ```foobar```

add your board now in

tbottesting/boardtestconfig.py

(copy and paste the lines 5-15)

Add replace the old boardname ```cxg3``` with the new name ```foobar```

[boardname](./boardtestconfig.py#L6)

Adapt also there the U-Boot build configs, see table #boards_config_options

so, that this fits with the needs for your board

### adapt board config in tbot.ini

Keep in mind, that you can add in ```tbot.ini``` as much boards as you want!
The following chapters are only an example for one board in tbot.ini

For full explanation read:

[tbottest doc: tbot.ini](https://hsdenx.github.io/tbottest/generic.html#tbot-ini-sections)


#### adapt lab host setup

adapt section [LABHOST] in

[LABHOST](./tbotconfig-hs/hs/tbot.ini)

to your local lab host setup, see full doc:

[tbottest doc: labhost setup](https://hsdenx.github.io/tbottest/generic.html#labhost)

check that you have working ssh access to your lab host!

#### adapt build host setup

starting tbot from github means, that we use ```[BUILDHOST_local]```
for our U-Boot build, so you do not need to change here in this
section anything. But if you are interested, you can add as many
build hosts here as you have (for example big machines which do
for you a whole yocto project build...)

[tbottest doc: build host setup](https://hsdenx.github.io/tbottest/generic.html#buildhost)

#### setup serial console on lab host

change in [tbot.ini](tbotconfig-hs/hs/tbot.ini#L67) ```PICOCOM_cxg3```
to ```PICOCOM_foobar```

and adapt the serial device in [tbot.ini](tbotconfig-hs/hs/tbot.ini#L69)

If you do not want to use ```picocom``` see doc for:

[tbottest doc: kermit](https://hsdenx.github.io/tbottest/generic.html#kermit-boardname)
[tbottest doc: script](https://hsdenx.github.io/tbottest/generic.html#scriptcom-boardname)

You do not need to adapt ```tbot.flags``` for this, as
tbottest tries to autodetect the used terminal tool:

[autodetect terminal tool](https://github.com/hsdenx/tbottest/blob/master/tbottest/labgeneric.py#L269)

As you see in code, you can force it with setting it in ```tbot.flags```

#### setup power off / on

change in [tbot.ini](tbotconfig-hs/hs/tbot.ini#L67) ```SISPMCTRL_cxg3```
to ```SISPMCTRL_foobar```

and adapt the address in [tbot.ini](tbotconfig-hs/hs/tbot.ini#L79)

If you do not have a sispmctrl based controller, see doc for other
supported devices:

[gpio](https://hsdenx.github.io/tbottest/generic.html#gpiopmctrl-boardname)
[script](https://hsdenx.github.io/tbottest/generic.html#powershellscript-boardname)
[tinkerforge](https://hsdenx.github.io/tbottest/generic.html#tf-boardname)

replace section ```SISPMCTRL_foobar``` with the power controller
you own. You do not need to adapt tbot.flags for this, as
tbottest tries to autodetect the used power controller:

[autodetect powercontroller](https://github.com/hsdenx/tbottest/blob/master/tbottest/labgeneric.py#L161)

As you see in code, you can force it with setting it in ```tbot.flags```

#### bootmode setup

you can setup different bootmodes for your board with section

[BOOTMODE_foobar](https://hsdenx.github.io/tbottest/generic.html#bootmode-boardname)

example config:

[bootmode](./tbotconfig-hs/hs/tbot.ini#L109)

see therefore also below section labcallbacks.py

#### ethernet setup

see doc:

[ethernet config](https://hsdenx.github.io/tbottest/generic.html#ipsetup-boardname-ethdevice-board)

example for cxg3 board and ethernet device eth0:

[ethernet](./tbotconfig-hs/hs/tbot.ini#L90)

adapt it to your needs!

#### U-Boot Environment setup

You can define a board specific U-Boot Environment in section ```[TC]```
in your boardname.ini file through key ```ub_env```, see as an example

[U-Boot Environment](tbotconfig-hs/hs/cxg3.ini#L37)

Setup there all stuff you need for your tests (may, you want to boot
linux with different setups like tftp fitImage and boot with_nfs rootfs)

pattern_lab_mode = re.compile('{lab mode.*}')

### boardspecific code

#### labcallbacks.py

There you find the functions:

cxg3_ub_install

  -> execute the commands, which are needed to update U-Boot on your board

  This functions is called from your [boardconfig](./boardtestconfig.py#L13)

cxg3_ub_boot_and_test

  -> boot installed U-Boot and test. Do here tests, you want to see
     additionally.

  This functions is called from your [boardconfig](./boardtestconfig.py#L14)

So this both functions depends highly on your board! Hope this example helps!

cxg3_setbootmode_usb

  -> function which runs on lab host. Sets USB SDP bootmode

  This functions is called through tbot flag ```bootmode:usb``` and
  your [bootmode](./tbotconfig-hs/hs/tbot.ini#L109)

cxg3_setbootmode_emmc

  -> function which runs on lab host. Sets emmc bootmode

  This functions is called through tbot flag ```bootmode:emmc``` and
  your [bootmode](./tbotconfig-hs/hs/tbot.ini#L109)


So this both functions depends highly on your lab setup!
Hope this example helps!

To be continued!

ToDo

- explain argument list

# tbot commands

ToDo:

Add here some tbot commands, which can be executed from U-Boot source code!

# Links

[1] https://tbot.tools

[2] https://tbot.tools/recipes.html

[3] https://hsdenx.github.io/tbottest/quickstart.html
