/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2016 Canonical Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <glib.h>

//TODO: remove
#include <glib/gprintf.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <ofono/modem.h>
#include <ofono/types.h>
#include <ofono/radio-settings.h>
#include <gril.h>

#include "common.h"
#include "ril_constants.h"
#include "rilmodem-test-server.h"

static GMainLoop *mainloop;

static const struct ofono_radio_settings_driver *rsdriver;

struct rilmodem_rs_data {
	GRil *ril;
	struct ofono_modem *modem;
	gconstpointer test_data;
	struct ofono_radio_settings *rs;
	struct server_data *serverd;
};

typedef gboolean (*StartFunc)(gpointer data);

struct rs_data {
	StartFunc start_func;
	gint param_int1;
	gint param_int2;

	struct rilmodem_test_data rtd;
	enum ofono_error_type error_type;
	enum ofono_radio_access_mode cb_mode;	
	gint cb_int1;
	gint cb_int2;
};

static void query_rat_mode_callback(const struct ofono_error *error,
					enum ofono_radio_access_mode mode,
					gpointer data)
{
	struct rilmodem_rs_data *rrd = data;
	const struct rs_data *rsd = rrd->test_data;

	g_assert(error->type == rsd->error_type);
	g_printf("DICK!\n");	

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR)
		g_assert(mode == rsd->cb_mode);

	g_main_loop_quit(mainloop);
}

static gboolean trigger_query_rat_mode(gpointer data)
{
	struct rilmodem_rs_data *rrd = data;
	g_printf("trigger_query_rat_mode!\n");	

	g_assert(rsdriver->query_rat_mode != NULL);
	rsdriver->query_rat_mode(rrd->rs, query_rat_mode_callback, rrd);

	return FALSE;
}

/* RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE */
static const guchar req_get_pref_network_type_parcel_1[] = {
	0x00, 0x00, 0x00, 0x08, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* reply data for GET_PREF_NETWORK_TYPE: */
static const guchar rsp_get_pref_network_type_data_1[] = {
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};

static const struct rs_data testdata_query_rat_mode_valid_1 = {
	.start_func = trigger_query_rat_mode,
	.rtd = {
		.req_data = req_get_pref_network_type_parcel_1,
		.req_size = sizeof(req_get_pref_network_type_parcel_1),
		.rsp_data = rsp_get_pref_network_type_data_1,
		.rsp_size = sizeof(rsp_get_pref_network_type_data_1),
		.rsp_error = RIL_E_SUCCESS,
	},
	.cb_mode = OFONO_RADIO_ACCESS_MODE_GSM,
	.error_type = OFONO_ERROR_TYPE_NO_ERROR,
};

/* Declarations && Re-implementations of core functions. */
void ril_radio_settings_exit(void);
void ril_radio_settings_init(void);

struct ofono_modem {
	ofono_bool_t dummy;
};

struct ofono_modem dummy_modem = { .dummy = TRUE, };

struct ofono_radio_settings {
	void *driver_data;
};

ofono_bool_t ofono_modem_get_boolean(struct ofono_modem *modem, const char *key)
{
	return TRUE;
}

struct ofono_radio_settings *ofono_radio_settings_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data)
{
	struct rilmodem_rs_data *rrd = data;
	struct ofono_radio_settings *rs = g_new0(struct ofono_radio_settings, 1);
	int retval;

	retval = rsdriver->probe(rs, OFONO_RIL_VENDOR_AOSP, rrd->ril);
	g_assert(retval == 0);

	return rs;
}

int ofono_radio_settings_driver_register(const struct ofono_radio_settings_driver *d)
{
	if (rsdriver == NULL)
		rsdriver = d;

	return 0;
}

void ofono_radio_settings_set_data(struct ofono_radio_settings *rs, void *data)
{
	rs->driver_data = data;
}

void *ofono_radio_settings_get_data(struct ofono_radio_settings *rs)
{
	return rs->driver_data;
}

struct ofono_modem *ofono_radio_settings_get_modem(struct ofono_radio_settings *rs)
{
	return &dummy_modem;
}

void ofono_radio_settings_register(struct ofono_radio_settings *rs)
{
}

void ofono_radio_settings_driver_unregister(const struct ofono_radio_settings_driver *d)
{
}

static void server_connect_cb(gpointer data)
{
	struct rilmodem_rs_data *rrd = data;
	const struct rs_data *rsd = rrd->test_data;

	g_printf("server_connect_cb!\n");	

	/* This causes local impl of _create() to call driver's probe func. */
	rrd->rs = ofono_radio_settings_create(NULL, OFONO_RIL_VENDOR_AOSP,
							"rilmodem", rrd);

	/* add_idle doesn't work, read blocks main loop!!! */
	g_assert(rsd->start_func(rrd) == FALSE);
}

/*
 * As all our architectures are little-endian except for
 * PowerPC, and the Binder wire-format differs slightly
 * depending on endian-ness, the following guards against test
 * failures when run on PowerPC.
 */
#if BYTE_ORDER == LITTLE_ENDIAN

/*
 * This unit test:
 *  - does some test data setup
 *  - configures a dummy server socket
 *  - creates a new gril client instance
 *    - triggers a connect to the dummy
 *      server socket
 *  - starts a mainloop
 */
static void test_rs_func(gconstpointer data)
{
	const struct rs_data *rsd = data;
	struct rilmodem_rs_data *rrd;

	ril_radio_settings_init();

	rrd = g_new0(struct rilmodem_rs_data, 1);

	rrd->test_data = rsd;

	rrd->serverd = rilmodem_test_server_create(&server_connect_cb,
							&rsd->rtd, rrd);

	g_printf("\ntest_rs_func!\n");

	rrd->ril = g_ril_new("/tmp/unittestril", OFONO_RIL_VENDOR_AOSP);
	g_assert(rrd->ril != NULL);

	mainloop = g_main_loop_new(NULL, FALSE);

	g_main_loop_run(mainloop);
	g_main_loop_unref(mainloop);

	rsdriver->remove(rrd->rs);
	g_ril_unref(rrd->ril);
	g_free(rrd);

	rilmodem_test_server_close(rrd->serverd);

	ril_radio_settings_exit();
}

#endif

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

/*
 * As all our architectures are little-endian except for
 * PowerPC, and the Binder wire-format differs slightly
 * depending on endian-ness, the following guards against test
 * failures when run on PowerPC.
 */
#if BYTE_ORDER == LITTLE_ENDIAN
	g_test_add_data_func("/testrilmodemrs/query_rat_mode/valid/1",
					&testdata_query_rat_mode_valid_1,
					test_rs_func);

#endif
	return g_test_run();
}
