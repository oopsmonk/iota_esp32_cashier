# IOTA Cashier

A payment device powered by IOTA CClient on ESP32.  

It generates a QR code from an unspent address for costumers paying IOTA tokens, after the owner withdrawal tokens from this address the device will seek for next unspent address and refresh QR code, it prevents to reuse an address.  

## Flowchart  

![](https://github.com/oopsmonk/iota_esp32_cashier/raw/master/image/IOTA%20Cashier.png)  

## Demonstration  

**Receiving tokens**  
[![](http://img.youtube.com/vi/Vp9J2ntikcc/0.jpg)](http://www.youtube.com/watch?v=Vp9J2ntikcc)

**Updating the QR code after a withdraw**  
[![](http://img.youtube.com/vi/a_qEPlbzrig/0.jpg)](http://www.youtube.com/watch?v=a_qEPlbzrig)

## Requirements  

* [ESP32-DevKitC V4](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started/get-started-devkitc.html#esp32-devkitc-v4-getting-started-guide)
* xtensa-esp32 toolchain
* ESP-IDF v3.2.2

## ESP32 build system setup  

Please follow documentations to setup your toolchain and development framework.

Linux and MacOS:  
* [xtensa-esp32 toolchain(Linux)](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started-cmake/linux-setup.html) 
* [xtensa-esp32 toolchain(MacOS)](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started-cmake/macos-setup.html) 
* [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started-cmake/index.html#get-esp-idf) 

Windows:
* [xtensa-esp32 toolchain](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started-cmake/windows-setup.html#standard-setup-of-toolchain-for-windows-cmake) 
* [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v3.2.2/get-started-cmake/index.html#windows-command-prompt) 

**Notice: We use the ESP-IDF v3.2.2**

```
git clone -b v3.2.2 --recursive https://github.com/espressif/esp-idf.git
```


Now, you can test your develop environment via the [hello_world](https://github.com/espressif/esp-idf/tree/release/v3.2/examples/get-started/hello_world) project.  

```shell
cd ~/esp
cp -r $IDF_PATH/examples/get-started/hello_world .
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash && idf.py -p /dev/ttyUSB0 monitor
```

The output would be something like:  

```shell
I (0) cpu_start: App cpu up.
I (184) heap_init: Initializing. RAM available for dynamic allo
cation:
I (191) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (197) heap_init: At 3FFB2EF8 len 0002D108 (180 KiB): DRAM
I (204) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (210) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (216) heap_init: At 40089560 len 00016AA0 (90 KiB): IRAM
I (223) cpu_start: Pro cpu start user code
I (241) cpu_start: Starting scheduler on PRO CPU.
I (0) cpu_start: Starting scheduler on APP CPU.
Hello world!
This is ESP32 chip with 2 CPU cores, WiFi/BT/BLE, silicon revision 1, 4MB external flash
Restarting in 10 seconds...
Restarting in 9 seconds...
```

You can press `Ctrl` + `]` to exit the monitor and ready for the next setup.  

## Building and flashing to ESP32

### Step 1: cloning repository  

```shell
git clone --recursive https://github.com/oopsmonk/iota_esp32_cashier.git
```

Or (if you didn't put the `--recursive` command during clone)  

```shell
git clone https://github.com/oopsmonk/iota_esp32_cashier.git
cd iota_esp32_cashier
git submodule update --init --recursive
```

### Step 2: initializing components

The `init.sh` helps us to generate files and switch to the right branch for the components.  

Linux and MacOS:

```shell
cd iota_esp32_cashier
bash ./init.sh
```

Windows: use **Git Bash** to run the command above.

### Step 3: Configuration  

In this step, you need to set up the WiFi, SNTP, IRI node, and a recursive.  

```
idf.py menuconfig
# WiFi SSID & Password
[IOTA Cashier] -> [WiFi]
# SNTP Client
[IOTA Cashier] -> [SNTP]
# Default IRI node
[IOTA Cashier] -> [IRI Node]
# The time of monitoring
[IOTA Cashier] -> (30) Monitor interval (s)
# Do you wanna update address automatically?
[IOTA Cashier] -> [ ] Auto refresh address
# Enable LCD driver?
[IOTA Cashier] -> [ ] Support LCD 
```

You can check configures in `sdkconfig` file.  

Please make sure you assigned the receiver(`CONFIG_MSG_RECEIVER`), Here is an example for your configuration:  

**LCD support with auto address update**  
```shell
CONFIG_WIFI_SSID="YOUR_SSID"
CONFIG_WIFI_PASSWORD="YOUR_PWD"
CONFIG_SNTP_SERVER="pool.ntp.org"
CONFIG_SNTP_TZ="CST-8"
CONFIG_IRI_NODE_URI="nodes.thetangle.org"
CONFIG_IRI_NODE_PORT=443
CONFIG_ENABLE_HTTPS=y
CONFIG_INTERVAL=30
CONFIG_ADDRESS_REFRESH=y
# IOTA Seed, it's a 81 characters string 
CONFIG_IOTA_SEED="SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9SEED9S"
# IOTA security level, could be 1, 2, or 3.
CONFIG_IOTA_SECURITY=2
# The start index of finding an unspent address.
CONFIG_IOTA_ADDRESS_START_INDEX=0
CONFIG_FTF_LCD=y
CONFIG_ST7735_BL_PIN=17
CONFIG_USE_COLOR_RBG565=y
# CONFIG_USE_COLOR_RGB565 is not set
CONFIG_ST7735_HOST_VSPI=y
# CONFIG_ST7735_HOST_HSPI is not set
```

**LCD support without auto address update**  
```shell
CONFIG_WIFI_SSID="YOUR_SSID"
CONFIG_WIFI_PASSWORD="YOUR_PWD"
CONFIG_SNTP_SERVER="pool.ntp.org"
CONFIG_SNTP_TZ="CST-8"
CONFIG_IRI_NODE_URI="nodes.thetangle.org"
CONFIG_IRI_NODE_PORT=443
CONFIG_ENABLE_HTTPS=y
CONFIG_INTERVAL=30
CONFIG_ADDRESS_REFRESH=
# Address with checksum, it's a 90 characters string 
CONFIG_IOTA_RECEIVER="RECEIVER9CHECHSUM9RECEIVER9CHECHSUM9RECEIVER9CHECHSUM9RECEIVER9CHECHSUM9RECEIVER9CHECHSUM9"
CONFIG_FTF_LCD=y
CONFIG_ST7735_BL_PIN=17
CONFIG_USE_COLOR_RBG565=y
CONFIG_USE_COLOR_RGB565=
CONFIG_ST7735_HOST_VSPI=y
CONFIG_ST7735_HOST_HSPI=
```

The `CONFIG_SNTP_TZ` follows the [POSIX Timezone string](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.json)  

### Step 4: Build & Run

```shell
idf.py build
idf.py -p /dev/ttyUSB0 flash && idf.py -p /dev/ttyUSB0 monitor
```

Output:  
```shell
I (4310) main: WiFi Connected
I (4310) main: IRI Node: nodes.thetangle.org, port: 443, HTTPS:True
I (4380) main: Initializing SNTP: pool.ntp.org, Timezone: CST-8
I (4390) main: Waiting for system time to be set... (1/10)
I (6390) main: The current date/time is: Wed Oct  2 17:06:29 2019
I (6450) cashier: Get unspent address from 5
E (11380) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (11380) task_wdt:  - IDLE0 (CPU 0)
E (11380) task_wdt: Tasks currently running:
E (11380) task_wdt: CPU 0: main
E (11380) task_wdt: CPU 1: IDLE1
E (21940) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (21940) task_wdt:  - IDLE0 (CPU 0)
E (21940) task_wdt: Tasks currently running:
E (21940) task_wdt: CPU 0: main
E (21940) task_wdt: CPU 1: IDLE1
Get balance [6]RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDR
I (29960) main: Initial balance: 4800i, interval 30
Get balance [6]RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDRESS9RECEIVER9ADDR
= 4800i
```

`Ctrl` + `]` to exit.  


## LCD Wiring Diagram

**ESP32 VSPI**  
![](https://github.com/oopsmonk/esp32_lib_st7735/raw/master/image/ESP32-ST7735-Wiring-VSPI.jpg)

**ESP32 HVSPI**  
![](https://github.com/oopsmonk/esp32_lib_st7735/raw/master/image/ESP32-ST7735-Wiring-HSPI.jpg)


## Troubleshooting

`CONFIG_IOTA_RECEIVER` or `CONFIG_IOTA_SEED` is not set or is invalid:  
```shell
I (0) cpu_start: Starting scheduler on APP CPU.
E (3443) main: please set a valid hash(CONFIG_IOTA_RECEIVER or CONFIG_IOTA_SEED) in sdkconfig!
I (3443) main: Restarting in 5 seconds...
I (4443) main: Restarting in 4 seconds...
I (5443) main: Restarting in 3 seconds...
```

`CONFIG_MAIN_TASK_STACK_SIZE` is too small, you need to enlarge it:  
```shell
***ERROR*** A stack overflow in task main has been detected.
abort() was called at PC 0x4008af7c on core 0
```
