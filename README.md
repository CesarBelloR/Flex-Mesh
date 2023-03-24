# Monitor 2.0 Development Guide.

# Table Of Contents
* [ Installing Prerequisites ](#Prerequisites) <br>
* [ Hardware ](#Hardware) <br>
* [ Instruction ](#Instruction) <br>
* [ Flash ](#Flash) <br>
* [ Retrieve data ](#Retrieve) <br>
  
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
west init -m git@bitbucket.org:etcengineering/etc-core-fw.git

west update
```

* Step 2: Install all libraries for python

```
pip3 install -r {ROOT}\zephyr\scripts\requirements.txt

pip3 install -r {ROOT}\nrf\scripts\requirements.txt

pip3 install -r {ROOT}\bootloader\mcuboot\scripts\requirements.txt
```

* Step 3: Apply patches for Zephyr. Copy the patch code from `etc-firmware\patch` to `{ROOT}\zephyr` and apply the patch code with command 
(only needed for LwM2M)
    * If you use a system that supports bash, use the script to apply all applicable patches:
        ```
        cd {ROOT}
        etc-firmware/scripts/apply_patches.sh 
        ```
    * Otherwise, apply patches manually:
        ```
        cd {ROOT}/zephyr
        git apply ../etc-firmware/patch/zephyr/0001-net-lwm2m-add-callback-for-send-confirmation.patch
        ```

* Step 4: Build `etc-app` firmware at `applications\etc-app` folder
    * Build with AWS IoT support with logging over RTT
        ```
        cd {ROOT}
        west build -b etc -s etc-firmware/applications/etc-app -- -DOVERLAY_CONFIG="rtt.conf aws-overlay.conf"
        ```

    * Build with LwM2M support with logging over RTT
        ```
        cd {ROOT}
        west build -b etc -s etc-firmware/applications/etc-app -- -DOVERLAY_CONFIG="rtt.conf lwm2m-overlay.conf"
        ```

<a name="Flash"></a>
# Flash

* Step 1: Connect a JLink Debugger to the Monitor 2.0 board and flash the device:

```
west flash
```

<a name="Retrieve"></a>
# Retrieve Data
Use the [retrieve_data script](scripts/retrieve_data/)
