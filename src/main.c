/**
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <zephyr/random/random.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/class/usb_cdc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>

#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(main);





#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

#define UART_BUF_SIZE 500

static struct bt_conn *current_conn;
static struct bt_conn *auth_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);

	current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
};

static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;

static void write_hid(const char *data, size_t size);

static char keys_buffer[UART_BUF_SIZE];
static uint16_t keys_len;
struct k_work send_keys_work;

static void send_keys(struct k_work *work)
{
	write_hid(keys_buffer, keys_len);
}


static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	memcpy(keys_buffer, data, len);
	keys_len = len;
	k_work_submit(&send_keys_work);
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};


/* HID */

static const uint8_t hid_mouse_report_desc[] = HID_MOUSE_REPORT_DESC(2);
static const uint8_t hid_kbd_report_desc[] = HID_KEYBOARD_REPORT_DESC();

static K_SEM_DEFINE(usb_sem, 1, 1);	/* starts off "available" */

#define MOUSE_BTN_REPORT_POS	0
#define MOUSE_X_REPORT_POS	1
#define MOUSE_Y_REPORT_POS	2

#define MOUSE_BTN_LEFT		BIT(0)
#define MOUSE_BTN_RIGHT		BIT(1)
#define MOUSE_BTN_MIDDLE	BIT(2)

static void in_ready_cb(const struct device *dev)
{
	ARG_UNUSED(dev);
	k_sem_give(&usb_sem);
}

static const struct hid_ops ops = {
	.int_in_ready = in_ready_cb,
};

static int ascii_to_hid(uint8_t ascii)
{
	if (ascii < 32) {
		/* Character not supported */
		return -1;
	} else if (ascii < 48) {
		/* Special characters */
		switch (ascii) {
		case 32:
			return HID_KEY_SPACE;
		case 33:
			return HID_KEY_1;
		case 34:
			return HID_KEY_APOSTROPHE;
		case 35:
			return HID_KEY_3;
		case 36:
			return HID_KEY_4;
		case 37:
			return HID_KEY_5;
		case 38:
			return HID_KEY_7;
		case 39:
			return HID_KEY_APOSTROPHE;
		case 40:
			return HID_KEY_9;
		case 41:
			return HID_KEY_0;
		case 42:
			return HID_KEY_8;
		case 43:
			return HID_KEY_EQUAL;
		case 44:
			return HID_KEY_COMMA;
		case 45:
			return HID_KEY_MINUS;
		case 46:
			return HID_KEY_DOT;
		case 47:
			return HID_KEY_SLASH;
		default:
			return -1;
		}
	} else if (ascii < 58) {
		/* Numbers */
		if (ascii == 48U) {
			return HID_KEY_0;
		} else {
			return ascii - 19;
		}
	} else if (ascii < 65) {
		/* Special characters #2 */
		switch (ascii) {
		case 58:
			return HID_KEY_SEMICOLON;
		case 59:
			return HID_KEY_SEMICOLON;
		case 60:
			return HID_KEY_COMMA;
		case 61:
			return HID_KEY_EQUAL;
		case 62:
			return HID_KEY_DOT;
		case 63:
			return HID_KEY_SLASH;
		case 64:
			return HID_KEY_2;
		default:
			return -1;
		}
	} else if (ascii < 91) {
		/* Uppercase characters */
		return ascii - 61U;
	} else if (ascii < 97) {
		/* Special characters #3 */
		switch (ascii) {
		case 91:
			return HID_KEY_LEFTBRACE;
		case 92:
			return HID_KEY_BACKSLASH;
		case 93:
			return HID_KEY_RIGHTBRACE;
		case 94:
			return HID_KEY_6;
		case 95:
			return HID_KEY_MINUS;
		case 96:
			return HID_KEY_GRAVE;
		default:
			return -1;
		}
	} else if (ascii < 123) {
		/* Lowercase letters */
		return ascii - 93;
	} else if (ascii < 128) {
		/* Special characters #4 */
		switch (ascii) {
		case 123:
			return HID_KEY_LEFTBRACE;
		case 124:
			return HID_KEY_BACKSLASH;
		case 125:
			return HID_KEY_RIGHTBRACE;
		case 126:
			return HID_KEY_GRAVE;
		case 127:
			return HID_KEY_DELETE;
		default:
			return -1;
		}
	}
	return -1;
}

static bool needs_shift(uint8_t ascii)
{
	if ((ascii < 33) || (ascii == 39U)) {
		return false;
	} else if ((ascii >= 33U) && (ascii < 44)) {
		return true;
	} else if ((ascii >= 44U) && (ascii < 58)) {
		return false;
	} else if ((ascii == 59U) || (ascii == 61U)) {
		return false;
	} else if ((ascii >= 58U) && (ascii < 91)) {
		return true;
	} else if ((ascii >= 91U) && (ascii < 94)) {
		return false;
	} else if ((ascii == 94U) || (ascii == 95U)) {
		return true;
	} else if ((ascii > 95) && (ascii < 123)) {
		return false;
	} else if ((ascii > 122) && (ascii < 127)) {
		return true;
	} else {
		return false;
	}
}

static void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
	LOG_INF("Status %d", status);
}

enum mouse_state {
	MOUSE_UP,
	MOUSE_DOWN,
	MOUSE_RIGHT,
	MOUSE_LEFT,
	MOUSE_CLEAR,
};

static const char mouse_cmds[][4] = {
	[MOUSE_UP] = {0x00, 0x00, 0xE0, 0x00},
	[MOUSE_DOWN] = {0x00, 0x00, 0x20, 0x00},
	[MOUSE_RIGHT] = {0x00, 0x20, 0x00, 0x00},
	[MOUSE_LEFT] = {0x00, 0xE0, 0x00, 0x00},
	[MOUSE_CLEAR] = {0x00, 0x00, 0x00, 0x00},
};

static const char kbd_clear[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

static const char toggle_caps_lock[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, HID_KEY_CAPSLOCK
};

static const char enter_cmd[] = {
	0x00, 0x00, HID_KEY_ENTER, 0x00,
	0x00, 0x00, 0x00, 0x00
};

/* Build a report for ctrl+alt+n (open terminal) */
static const uint8_t open_terminal_cmd[] = {
	HID_KBD_MODIFIER_LEFT_CTRL | HID_KBD_MODIFIER_LEFT_ALT,  /* Modifiers */
	0x00,                                                  /* Reserved */
	HID_KEY_T,                                             /* 'n' key */
	0x00, 0x00, 0x00, 0x00, 0x00                           /* Remaining bytes */
};

const struct device *hid0_dev, *hid1_dev;

static void open_terminal();
static void send_enter();
static void open_url(const char *url);
static void write_hid(const char *data, size_t size)
{
	int ret;
	for (size_t i = 0; i < size; i++) {
		if (i < size - 1 && data[i] == '\\') {
			if (data[i + 1] == 'n') {
				send_enter();
				i++;
			} else if (data[i + 1] == 't') {
				open_terminal();
				i++;
			} else if (data[i + 1] == 'r') {
				open_url("https://www.youtube.com/watch?v=xvFZjo5PgG0");
				i++;
			} else if (data[i + 1] == 'c') {
				hid_int_ep_write(hid1_dev, toggle_caps_lock, sizeof(toggle_caps_lock), NULL);
				i++;
			} else if (data[i + 1] == 's') {
				k_sleep(K_MSEC(1000));
				i++;
				continue;
			} else if (data[i + 1] == 'u') {
				static char url[256];
				memcpy(url, &data[i + 2], size - i - 2);
				url[size - i - 2] = '\0';
				open_url(url);
				return;
			} else {
				continue;
			}
		}
		else {
			int key = ascii_to_hid(data[i]);
			if (key < 0) {
				continue;  // Skip unsupported characters
			}
			uint8_t report[8] = {0};  // [modifier, reserved, key1, key2, ..., key6]

			if (needs_shift(data[i])) {
				report[0] |= HID_KBD_MODIFIER_RIGHT_SHIFT;
			}
			/* Place key code in first key position (index 2) */
			report[2] = key;

			ret = hid_int_ep_write(hid1_dev, report, sizeof(report), NULL);
			if (ret < 0) {
				LOG_ERR("Failed to write key press report");
				return;
			}
		}

		/* Small delay to simulate key press duration */
		k_sleep(K_MSEC(10));

		/* Send release report */
		ret = hid_int_ep_write(hid1_dev, kbd_clear, sizeof(kbd_clear), NULL);
		if (ret < 0) {
			LOG_ERR("Failed to write key release report");
			return;
		}

		/* Small delay between keys */
		k_sleep(K_MSEC(10));
	}
}




int main(void)
{
	int ret;

	/* Configure devices */
	hid0_dev = device_get_binding("HID_0");
	if (hid0_dev == NULL) {
		LOG_ERR("Cannot get USB HID 0 Device");
		return 0;
	}

	hid1_dev = device_get_binding("HID_1");
	if (hid1_dev == NULL) {
		LOG_ERR("Cannot get USB HID 1 Device");
		return 0;
	}

	k_work_init(&send_keys_work, send_keys);

	/* Initialize HID devices */
	usb_hid_register_device(hid0_dev, hid_mouse_report_desc,
				sizeof(hid_mouse_report_desc), &ops);
	usb_hid_register_device(hid1_dev, hid_kbd_report_desc,
				sizeof(hid_kbd_report_desc), &ops);

	usb_hid_init(hid0_dev);
	usb_hid_init(hid1_dev);


	ret = usb_enable(status_cb);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	k_busy_wait(USEC_PER_SEC);
	k_sleep(K_MSEC(1000));


	ret = bt_enable(NULL);
	if (ret) {
		return 0;
	}


	ret = bt_nus_init(&nus_cb);
	if (ret) {
		LOG_ERR("Failed to initialize UART service (err: %d)", ret);
		return 0;
	}


	ret = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (ret) {
		LOG_ERR("Advertising failed to start (err %d)", ret);
		return 0;
	}


}



// ============================ special sequences ============================

static void open_url(const char *url)
{
	static char cmd[256];
	int len = sprintf(cmd, "xdg-open %s", url);

	open_terminal();
	k_sleep(K_MSEC(1500));
	write_hid(cmd, len);
	k_sleep(K_MSEC(10));
	send_enter();
}

static void open_terminal()
{
	hid_int_ep_write(hid1_dev, open_terminal_cmd, sizeof(open_terminal_cmd), NULL);
	k_sleep(K_MSEC(10));
	hid_int_ep_write(hid1_dev, kbd_clear, sizeof(kbd_clear), NULL);
}

static void send_enter()
{
	hid_int_ep_write(hid1_dev, enter_cmd, sizeof(enter_cmd), NULL);
	k_sleep(K_MSEC(10));
	hid_int_ep_write(hid1_dev, kbd_clear, sizeof(kbd_clear), NULL);
}





