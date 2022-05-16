# Orain ETC-Core Development Guide.

# Table Of Contents
* [ Installing Prerequisites ](#Prerequisites) <br>
* [ Hardware ](#Hardware) <br>
* [ Instruction ](#Instruction) <br>
* [ Flash ](#Flash) <br>
  
<a name="Prerequisites"></a>
# Installing Prerequisites
* **Python 3.9 and above** : Download and install Python from <a href="https://www.python.org/downloads/">here</a>
* **Git** : Get the latest git from <a href ="https://git-scm.com/downloads">here</a>
* **nRF Command Line Tools** : Get the latest nrfprog from <a href ="https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools">here</a>


**Note:** Make sure West, Python and Git are available on the system environment PATH.

<a name="Hardware"></a>
# Hardware

*  **nRF52833** : Reference link from <a href = "https://www.nordicsemi.com/Products/Development-hardware/nRF52833-DK"> here </a>
*  **BG95NBIoT module** : Reference link from <a href = "https://www.quectel.com/product/lpwa-bg95-m3"> here </a>
*  **NINA B406** : ETC development kit from <a href = "https://www.digikey.com/en/products/detail/u-blox/NINA-B406-00B/11561852"> here </a>

<a name="Instruction"></a>
# Instruction

* Step 0: Follow the instruction to compile the  <a href = "https://docs.zephyrproject.org/latest/develop/getting_started/index.html#"> example blinky </a>

* Step 1: Initialize new Zephyr with Nordic SDK from new location with command

```
west init -m git@bitbucket.org:etcengineering/etc-core-fw.git --mr <branch you want>

west update
```

* Step 2: Install all libraries for python

```
pip3 install -r {ROOT}\zephyr\scripts\requirements.txt

pip3 install -r {ROOT}\nrf\scripts\requirements.txt

pip3 install -r {ROOT}\bootloader\mcuboot\scripts\requirements.txt
```

* Step 3: Apply patch code for nRF library. Copy the patch code from `etc-core-fw\patch\nrf` to `{ROOT}\nrf` and apply the patch code with command 

```
git apply 0001-aws_iot-ignore-certification-step.patch
```

* Step 4: Build `etc-core` firmware at `applications\etc-core` folder with command

```
west build -b etc_ninab4
```

<a name="Flash"></a>
# Flash

* Step 1: Connect the board NINA B406 and flash firmware over JLINK

```
west flash
```
