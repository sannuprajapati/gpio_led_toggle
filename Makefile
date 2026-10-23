# gpio_led_toggle - top-level Makefile
#
# On Ubuntu/WSL2 (x86_64): cross-compiles against KDIR for arm64
# On the Pi     (aarch64): builds natively against installed kernel headers
#
#   make                      -> build/gpio_demo.ko, gpio-demo.dtbo, gpio_demo_cli
#   make info                 -> show kernel release the module is built for
#   make deploy PI=sannu@<ip> -> copy build/ + Pi scripts to the Pi
#   make clean

ARCH := arm64
HOST_ARCH := $(shell uname -m)

ifeq ($(HOST_ARCH),aarch64)
  KDIR          ?= /lib/modules/$(shell uname -r)/build
  CROSS_COMPILE ?=
else
  # absolute path: $(HOME) becomes /root under sudo
  KDIR          ?= /home/sannu/linux-rpi-6.18.y/linux-rpi-6.18.y
  CROSS_COMPILE ?= aarch64-linux-gnu-
endif

CC_APP    := $(CROSS_COMPILE)gcc
BUILD     := $(CURDIR)/build
PI_DIR    ?= gpio_led_toggle
KMAKE     := $(MAKE) -C $(KDIR) M=$(CURDIR)/driver ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

all: check module dtbo app
	@echo
	@echo "Built for kernel: $$($(MAKE) -s -C $(KDIR) ARCH=$(ARCH) kernelrelease 2>/dev/null)"
	@ls -l $(BUILD)

check:
	@test -d $(KDIR) || { echo "ERROR: KDIR not found: $(KDIR)"; \
		echo "  Ubuntu: run scripts/setup_ubuntu.sh, or make KDIR=/path/to/linux"; \
		echo "  Pi    : sudo apt install linux-headers-rpi-v8"; exit 1; }
	@test -f $(KDIR)/include/generated/autoconf.h || { \
		echo "ERROR: kernel tree not prepared: $(KDIR)"; \
		echo "  run scripts/setup_ubuntu.sh"; exit 1; }
	@test -f $(KDIR)/Module.symvers || { \
		echo "ERROR: $(KDIR)/Module.symvers missing (kernel never fully built)"; \
		echo "  run scripts/setup_ubuntu.sh (builds Image + modules once)"; exit 1; }

$(BUILD):
	@mkdir -p $(BUILD)

module: check | $(BUILD)
	$(KMAKE) modules
	cp driver/gpio_demo.ko $(BUILD)/

dtbo: | $(BUILD)
	dtc -@ -q -I dts -O dtb -o $(BUILD)/gpio-demo.dtbo dts/gpio-demo-overlay.dts

# static: no glibc version mismatch between Ubuntu and Pi OS
app: | $(BUILD)
	$(CC_APP) -O2 -Wall -Wextra -static -o $(BUILD)/gpio_demo_cli app/gpio_demo_cli.c

info:
	@echo "KDIR          : $(KDIR)"
	@echo "CROSS_COMPILE : $(CROSS_COMPILE)"
	@echo "kernelrelease : $$($(MAKE) -s -C $(KDIR) ARCH=$(ARCH) kernelrelease)"
	@test -f $(BUILD)/gpio_demo.ko && modinfo -F vermagic $(BUILD)/gpio_demo.ko | sed 's/^/vermagic      : /' || true

deploy: all
	@test -n "$(PI)" || { echo "usage: make deploy PI=sannu@<pi-ip>"; exit 1; }
	ssh $(PI) "mkdir -p ~/$(PI_DIR)"
	scp $(BUILD)/gpio_demo.ko $(BUILD)/gpio-demo.dtbo $(BUILD)/gpio_demo_cli \
	    scripts/pi_setup.sh scripts/pi_test.sh $(PI):~/$(PI_DIR)/
	@echo "Deployed. On the Pi: cd ~/$(PI_DIR) && sudo ./pi_setup.sh"

clean:
	-$(KMAKE) clean 2>/dev/null
	rm -rf $(BUILD)

.PHONY: all check module dtbo app info deploy clean
