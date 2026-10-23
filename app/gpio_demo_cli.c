// SPDX-License-Identifier: GPL-2.0
/*
 * gpio_demo_cli - userspace test/control app for gpio_demo.ko
 *
 * Talks to:  /sys/bus/platform/devices/gpio-demo/{led,button}
 * Override:  GPIO_DEMO_SYSFS=/some/other/path ./gpio_demo_cli status
 *
 * Usage:
 *   gpio_demo_cli on | off | toggle | status
 *   gpio_demo_cli blink <count> <ms>
 *   gpio_demo_cli watch          (live LED/button changes, Ctrl+C to exit)
 *   gpio_demo_cli test           (automated driver test, exit code = #fails)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_BASE   "/sys/bus/platform/devices/gpio-demo"
#define BTN_TIMEOUT_MS 10000

static const char *base = DEFAULT_BASE;
static volatile sig_atomic_t stop;
static int fails;

#define CHECK(cond, msg) do {                          \
	if (cond) {                                    \
		printf("  [PASS] %s\n", msg);          \
	} else {                                       \
		printf("  [FAIL] %s\n", msg);          \
		fails++;                               \
	}                                              \
} while (0)

static void on_sigint(int sig) { (void)sig; stop = 1; }

static void msleep(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* returns value (>=0) or -errno */
static int read_attr(const char *name)
{
	char path[256], val[16];
	ssize_t r;
	int fd, err;

	snprintf(path, sizeof(path), "%s/%s", base, name);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	r = read(fd, val, sizeof(val) - 1);
	err = errno;
	close(fd);
	if (r < 0)
		return -err;
	val[r] = '\0';
	return atoi(val);
}

/* returns 0 or -errno */
static int write_attr(const char *name, const char *val)
{
	char path[256];
	ssize_t r;
	int fd, err;

	snprintf(path, sizeof(path), "%s/%s", base, name);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -errno;
	r = write(fd, val, strlen(val));
	err = errno;
	close(fd);
	return r < 0 ? -err : 0;
}

static int check_ret(int ret, const char *what)
{
	if (ret >= 0)
		return ret;
	fprintf(stderr, "error: %s: %s\n", what, strerror(-ret));
	if (ret == -ENOENT)
		fprintf(stderr, "  is gpio_demo.ko loaded and overlay applied? (%s)\n", base);
	if (ret == -EACCES)
		fprintf(stderr, "  run with sudo\n");
	exit(1);
}

static void set_led(int on)
{
	check_ret(write_attr("led", on ? "1" : "0"), "write led");
}

static int get_led(void)    { return check_ret(read_attr("led"), "read led"); }
static int get_button(void) { return check_ret(read_attr("button"), "read button"); }

static void cmd_status(void)
{
	printf("LED    : %s\n", get_led() ? "ON" : "OFF");
	printf("Button : %s\n", get_button() ? "PRESSED" : "released");
}

static void cmd_blink(int count, int ms)
{
	signal(SIGINT, on_sigint);
	for (int i = 0; i < count && !stop; i++) {
		set_led(1);
		msleep(ms);
		set_led(0);
		msleep(ms);
		printf("\rblink %d/%d", i + 1, count);
		fflush(stdout);
	}
	set_led(0);
	printf("\n");
}

static void cmd_watch(void)
{
	int led = -1, btn = -1;

	signal(SIGINT, on_sigint);
	printf("watching %s (Ctrl+C to exit)\n", base);
	while (!stop) {
		int l = get_led(), b = get_button();

		if (l != led || b != btn) {
			printf("[%8ld ms] LED=%-3s  Button=%s\n", now_ms() % 100000000,
			       l ? "ON" : "OFF", b ? "PRESSED" : "released");
			fflush(stdout);
			led = l;
			btn = b;
		}
		msleep(20);
	}
}

static int wait_button(int want, long timeout_ms)
{
	long deadline = now_ms() + timeout_ms;

	while (now_ms() < deadline) {
		if (get_button() == want)
			return 1;
		msleep(10);
	}
	return 0;
}

static int cmd_test(void)
{
	int before, after, ret;

	printf("gpio_demo driver test (%s)\n\n", base);

	printf("1. sysfs interface\n");
	CHECK(access(base, F_OK) == 0, "device directory exists");
	CHECK(read_attr("led") >= 0, "led attribute readable");
	CHECK(read_attr("button") >= 0, "button attribute readable");
	if (fails) {
		printf("\nabort: driver not bound\n");
		return fails;
	}

	printf("2. LED write/readback\n");
	CHECK(write_attr("led", "1") == 0 && read_attr("led") == 1, "led=1 reads back 1");
	msleep(300);
	CHECK(write_attr("led", "0") == 0 && read_attr("led") == 0, "led=0 reads back 0");
	CHECK(write_attr("led", "on") == 0 && read_attr("led") == 1, "kstrtobool accepts \"on\"");
	CHECK(write_attr("led", "off") == 0 && read_attr("led") == 0, "kstrtobool accepts \"off\"");

	printf("3. input validation\n");
	ret = write_attr("led", "xyz");
	CHECK(ret == -EINVAL, "invalid value rejected with EINVAL");
	ret = write_attr("button", "1");
	CHECK(ret == -EACCES || ret == -EPERM || ret == -EIO, "button attribute is read-only");

	printf("4. button idle state\n");
	CHECK(read_attr("button") == 0, "button reads 0 when released (pull-up + active-low OK)");

	printf("5. IRQ toggle (press the button within %d s)\n", BTN_TIMEOUT_MS / 1000);
	set_led(0);
	before = get_led();
	if (!wait_button(1, BTN_TIMEOUT_MS)) {
		CHECK(0, "button press detected");
	} else {
		CHECK(1, "button press detected (gpiod_get_value)");
		msleep(100);                    /* let threaded IRQ run */
		after = get_led();
		CHECK(after != before, "IRQ handler toggled LED");
		wait_button(0, BTN_TIMEOUT_MS);
		msleep(300);                    /* past 200 ms debounce */
		CHECK(get_led() == after, "no extra toggle on release (debounce/edge OK)");
	}

	set_led(0);
	printf("\nresult: %s (%d failure%s)\n", fails ? "FAIL" : "PASS",
	       fails, fails == 1 ? "" : "s");
	return fails;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s on|off|toggle|status\n"
		"       %s blink <count> <ms>\n"
		"       %s watch\n"
		"       %s test\n", prog, prog, prog, prog);
	exit(2);
}

int main(int argc, char **argv)
{
	const char *env = getenv("GPIO_DEMO_SYSFS");

	if (env && *env)
		base = env;
	if (argc < 2)
		usage(argv[0]);

	if (!strcmp(argv[1], "on"))
		set_led(1);
	else if (!strcmp(argv[1], "off"))
		set_led(0);
	else if (!strcmp(argv[1], "toggle"))
		set_led(!get_led());
	else if (!strcmp(argv[1], "status"))
		cmd_status();
	else if (!strcmp(argv[1], "blink") && argc == 4)
		cmd_blink(atoi(argv[2]), atoi(argv[3]));
	else if (!strcmp(argv[1], "watch"))
		cmd_watch();
	else if (!strcmp(argv[1], "test"))
		return cmd_test();
	else
		usage(argv[0]);

	return 0;
}
