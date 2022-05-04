// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Power Delivery protocol stack.
 */

/*
 * Modified 2021 by Maxim Integrated for MAX25432
 */

#include <stdint.h>
#include <stdbool.h>
#include <errno.h> /* error constants used in tcpm.c */
#include <string.h> /* needed for memset */
#include <stdlib.h> /* needed for malloc */
#include "pd_ext_sdb.h" /* needed for USB_PD_EXT_SDB_EVENT_FLAGS used below */
#include "pd_bdo.h"
#include "mx_time.h"

#include "tcpm_deps.h"
#include "tcpm.h"

static inline void * ERR_PTR(long error)
{
	return (void *) error;
}

static void tcpm_init(struct tcpm_port *port);

unsigned int tcpm_negotiated_rev;

#define FOREACH_STATE(S)			\
	S(INVALID_STATE),			\
	S(DRP_TOGGLING),			\
	S(SRC_UNATTACHED),			\
	S(SRC_ATTACH_WAIT),			\
	S(SRC_ATTACHED),			\
	S(SRC_STARTUP),				\
	S(SRC_DISC_CABLE),			\
	S(SRC_WAIT_CABLE),			\
	S(SRC_SEND_CAPABILITIES),		\
	S(SRC_NEGOTIATE_CAPABILITIES),		\
	S(SRC_TRANSITION_SUPPLY),		\
	S(SRC_READY),				\
	S(SRC_WAIT_NEW_CAPABILITIES),		\
						\
	S(SNK_UNATTACHED),			\
	S(SNK_ATTACH_WAIT),			\
	S(SNK_DEBOUNCED),			\
	S(SNK_ATTACHED),			\
	S(SNK_STARTUP),				\
	S(SNK_DISCOVERY),			\
	S(SNK_DISCOVERY_DEBOUNCE),		\
	S(SNK_DISCOVERY_DEBOUNCE_DONE),		\
	S(SNK_WAIT_CAPABILITIES),		\
	S(SNK_NEGOTIATE_CAPABILITIES),		\
	S(SNK_NEGOTIATE_PPS_CAPABILITIES),	\
	S(SNK_TRANSITION_SINK),			\
	S(SNK_TRANSITION_SINK_VBUS),		\
	S(SNK_READY),				\
						\
	S(ACC_UNATTACHED),			\
	S(DEBUG_ACC_ATTACHED),			\
	S(AUDIO_ACC_ATTACHED),			\
	S(AUDIO_ACC_DEBOUNCE),			\
						\
	S(HARD_RESET_SEND),			\
	S(HARD_RESET_START),			\
	S(SRC_HARD_RESET_VBUS_OFF),		\
	S(SRC_HARD_RESET_VBUS_ON),		\
	S(SNK_HARD_RESET_SINK_OFF),		\
	S(SNK_HARD_RESET_WAIT_VBUS),		\
	S(SNK_HARD_RESET_SINK_ON),		\
						\
	S(SOFT_RESET),				\
	S(SOFT_RESET_SEND),			\
						\
	S(DR_SWAP_ACCEPT),			\
	S(DR_SWAP_SEND),			\
	S(DR_SWAP_SEND_TIMEOUT),		\
	S(DR_SWAP_CANCEL),			\
	S(DR_SWAP_CHANGE_DR),			\
						\
	S(PR_SWAP_ACCEPT),			\
	S(PR_SWAP_SEND),			\
	S(PR_SWAP_SEND_TIMEOUT),		\
	S(PR_SWAP_CANCEL),			\
	S(PR_SWAP_START),			\
	S(PR_SWAP_SRC_SNK_TRANSITION_OFF),	\
	S(PR_SWAP_SRC_SNK_SOURCE_OFF),		\
	S(PR_SWAP_SRC_SNK_SOURCE_OFF_CC_DEBOUNCED), \
	S(PR_SWAP_SRC_SNK_SINK_ON),		\
	S(PR_SWAP_SNK_SRC_SINK_OFF),		\
	S(PR_SWAP_SNK_SRC_SOURCE_ON),		\
	S(PR_SWAP_SNK_SRC_SOURCE_ON_VBUS_RAMPED_UP),    \
						\
	S(VCONN_SWAP_ACCEPT),			\
	S(VCONN_SWAP_SEND),			\
	S(VCONN_SWAP_SEND_TIMEOUT),		\
	S(VCONN_SWAP_CANCEL),			\
	S(VCONN_SWAP_START),			\
	S(VCONN_SWAP_WAIT_FOR_VCONN),		\
	S(VCONN_SWAP_TURN_ON_VCONN),		\
	S(VCONN_SWAP_TURN_OFF_VCONN),		\
						\
	S(SNK_TRY),				\
	S(SNK_TRY_WAIT),			\
	S(SNK_TRY_WAIT_DEBOUNCE),               \
	S(SNK_TRY_WAIT_DEBOUNCE_CHECK_VBUS),    \
	S(SRC_TRYWAIT),				\
	S(SRC_TRYWAIT_DEBOUNCE),		\
	S(SRC_TRYWAIT_UNATTACHED),		\
						\
	S(SRC_TRY),				\
	S(SRC_TRY_WAIT),                        \
	S(SRC_TRY_DEBOUNCE),			\
	S(SNK_TRYWAIT),				\
	S(SNK_TRYWAIT_DEBOUNCE),		\
	S(SNK_TRYWAIT_VBUS),			\
	S(BIST_RX),				\
						\
	S(GET_STATUS_SEND),			\
	S(GET_STATUS_SEND_TIMEOUT),		\
	S(GET_PPS_STATUS_SEND),			\
	S(GET_PPS_STATUS_SEND_TIMEOUT),		\
						\
	S(ERROR_RECOVERY),			\
	S(PORT_RESET),				\
	S(PORT_RESET_WAIT_OFF),     \
                        \
    S(SRC_HARD_RESET_DISABLE_RP), \
    S(SRC_HARD_RESET_VBUS_OFF_DISABLE)

#define GENERATE_ENUM(e)	e
#define GENERATE_STRING(s)	#s

enum tcpm_state {
	FOREACH_STATE(GENERATE_ENUM)
};

static const char * const tcpm_states[] = {
	FOREACH_STATE(GENERATE_STRING)
};

enum vdm_states {
	VDM_STATE_ERR_BUSY = -3,
	VDM_STATE_ERR_SEND = -2,
	VDM_STATE_ERR_TMOUT = -1,
	VDM_STATE_DONE = 0,
	/* Anything >0 represents an active state */
	VDM_STATE_READY = 1,
	VDM_STATE_BUSY = 2,
	VDM_STATE_WAIT_RSP_BUSY = 3,
};

enum pd_msg_request {
	PD_MSG_NONE = 0,
	PD_MSG_CTRL_REJECT,
	PD_MSG_CTRL_WAIT,
	PD_MSG_CTRL_NOT_SUPP,
	PD_MSG_DATA_SINK_CAP,
	PD_MSG_DATA_SOURCE_CAP,
	PD_MSG_PPS_STATUS,
};

/* Events from low level driver */

#define TCPM_CC_EVENT		BIT(0)
#define TCPM_VBUS_EVENT		BIT(1)
#define TCPM_RESET_EVENT	BIT(2)
#define TCPM_RX_EVENT            BIT(3)

#define LOG_BUFFER_ENTRIES	1024
#define LOG_BUFFER_ENTRY_SIZE	128

struct pd_rx_event {
	struct tcpm_port *port;
	struct pd_message msg;
};

struct pd_pps_data {
	u32 min_volt;
	u32 max_volt;
	u32 max_curr;
	u32 out_volt;
	u32 op_curr;
	bool supported;
	bool active;
};

struct tcpm_port {
	uint8_t id;

	struct workqueue_struct *wq;

	struct typec_capability typec_caps;
	struct typec_port *typec_port;

	struct tcpc_dev	*tcpc;
	struct usb_role_switch *role_sw;

	enum typec_role vconn_role;
	enum typec_role pwr_role;
	enum typec_data_role data_role;
	enum typec_pwr_opmode pwr_opmode;

	struct usb_pd_identity partner_ident;
	struct typec_partner_desc partner_desc;
	struct typec_partner *partner;

	enum typec_cc_status cc_req;

	enum typec_cc_status cc1;
	enum typec_cc_status cc2;
	enum typec_cc_polarity polarity;

	bool attached;
	bool connected;
	enum typec_port_type port_type;
	bool vbus_present;
	bool vbus_never_low;
	bool vbus_source;
	bool vbus_charge;

	bool send_discover;
	bool op_vsafe5v;

	int try_role;
	int try_snk_count;
	int try_src_count;

	enum pd_msg_request queued_message;

	enum tcpm_state enter_state;
	enum tcpm_state prev_state;
	enum tcpm_state state;
	enum tcpm_state delayed_state;
	unsigned long delayed_runtime;
	unsigned long delay_ms;

	bool pd_do_work;
	u32 pd_events;

	bool state_machine_running;
	uint8_t run_state_machine_flag;

	volatile bool tx_complete;
	enum tcpm_transmit_status tx_status;

	struct pd_rx_event rx_event;

	bool swap_pending;
	bool non_pd_role_swap;
	int swap_status;

	unsigned int negotiated_rev;
	unsigned int message_id;
	unsigned int message_id_p;     // mm added to track SOP and SOP' message IDs separately
	unsigned int disc_cable_count; //JRF
	unsigned int caps_count;
	unsigned int hard_reset_count;
	bool pd_capable;
	bool explicit_contract;
	unsigned int rx_msgid;

	/* Partner capabilities/requests */
	u32 sink_request;
	u32 source_caps[PDO_MAX_OBJECTS];
	unsigned int nr_source_caps;
	u32 sink_caps[PDO_MAX_OBJECTS];
	unsigned int nr_sink_caps;

	/* Local capabilities */
	u32 src_pdo[PDO_MAX_OBJECTS];
	unsigned int nr_src_pdo;
	unsigned int nr_snk_pdo;

    unsigned int pdo_index; 
    unsigned int last_pdo_index; // needed to determine if transition should be done immediately
    
	unsigned int operating_snk_mw;
	bool update_sink_caps;

	/* Requested current / voltage */
	u32 current_limit;	/* max advertised pdo current */
	//u32 apdo_op_current;
	u32 supply_voltage;
	u32 last_vbus_hv_voltage;
	u8 pdo_type;

	u32 bist_request;

	/* PPS */
	struct pd_pps_data pps_data;
	bool pps_pending;
	int pps_status;
	u8 cl_cv_status;

	/* Deadline in jiffies to exit src_try_wait state */
	unsigned long max_wait;

	/* port belongs to a self powered device */
	bool self_powered;
    
    bool force_disable_flag; // Force tcpm to remove Rp for period of time
};

// This is to be able to print to terminal or log from .c files
#define MAKE_LOG 0
#if MAKE_LOG
#include <stdarg.h>
#include <stdio.h>
#define LOG_LENGTH 10
char log_arr[LOG_LENGTH][128];
uint8_t log_index;
void tcpm_log(struct tcpm_port *port, char *format, ...)
{
  va_list argptr;
  va_start(argptr, format);
    char output[128];
    vsnprintf(output, 128, format, argptr);
    sprintf(log_arr[log_index++ % LOG_LENGTH], "[%d] %s\n", mx_tcpm_get_ms_clk(), output);
}
#else 
void tcpm_log(struct tcpm_port *port, char *format, ...){}
#endif

// Run the state machine
static void queue_work_mx(struct tcpm_port *port)
{
	port->run_state_machine_flag = 1;
}

extern u32 src_pdo[];

#define tcpm_cc_is_sink(cc) \
	((cc) == TYPEC_CC_RP_DEF || (cc) == TYPEC_CC_RP_1_5 || \
	 (cc) == TYPEC_CC_RP_3_0)

#define tcpm_port_is_sink(port) \
	((tcpm_cc_is_sink((port)->cc1) && !tcpm_cc_is_sink((port)->cc2)) || \
	 (tcpm_cc_is_sink((port)->cc2) && !tcpm_cc_is_sink((port)->cc1)))

#define tcpm_cc_is_source(cc) ((cc) == TYPEC_CC_RD)
#define tcpm_cc_is_audio(cc) ((cc) == TYPEC_CC_RA)
#define tcpm_cc_is_open(cc) ((cc) == TYPEC_CC_OPEN)

#define tcpm_port_is_source(port) \
	((tcpm_cc_is_source((port)->cc1) && \
	 !tcpm_cc_is_source((port)->cc2)) || \
	 (tcpm_cc_is_source((port)->cc2) && \
	  !tcpm_cc_is_source((port)->cc1)))

#define tcpm_port_is_debug(port) \
	(tcpm_cc_is_source((port)->cc1) && tcpm_cc_is_source((port)->cc2))

#define tcpm_port_is_audio(port) \
	(tcpm_cc_is_audio((port)->cc1) && tcpm_cc_is_audio((port)->cc2))

#define tcpm_port_is_audio_detached(port) \
	((tcpm_cc_is_audio((port)->cc1) && tcpm_cc_is_open((port)->cc2)) || \
	 (tcpm_cc_is_audio((port)->cc2) && tcpm_cc_is_open((port)->cc1)))

#define tcpm_try_snk(port) \
	((port)->try_snk_count == 0 && (port)->try_role == TYPEC_SINK && \
	(port)->port_type == TYPEC_PORT_DRP)

#define tcpm_try_src(port) \
	((port)->try_src_count == 0 && (port)->try_role == TYPEC_SOURCE && \
	(port)->port_type == TYPEC_PORT_DRP)

static enum tcpm_state tcpm_default_state(struct tcpm_port *port)
{
	if (port->port_type == TYPEC_PORT_DRP) {
		if (port->try_role == TYPEC_SINK)
			return SNK_UNATTACHED;
		else if (port->try_role == TYPEC_SOURCE)
			return SRC_UNATTACHED;
		else if (port->tcpc->config->default_role == TYPEC_SINK)
			return SNK_UNATTACHED;
		/* Fall through to return SRC_UNATTACHED */
	} else if (port->port_type == TYPEC_PORT_SNK) {
		return SNK_UNATTACHED;
	}
	return SRC_UNATTACHED;
}

static bool tcpm_port_is_disconnected(struct tcpm_port *port)
{
	return (!port->attached && port->cc1 == TYPEC_CC_OPEN &&
		port->cc2 == TYPEC_CC_OPEN) ||
	       (port->attached && ((port->polarity == TYPEC_POLARITY_CC1 &&
				    port->cc1 == TYPEC_CC_OPEN) ||
				   (port->polarity == TYPEC_POLARITY_CC2 &&
				    port->cc2 == TYPEC_CC_OPEN)));
}

static void tcpm_set_state_cond(struct tcpm_port *port, enum tcpm_state state,
				uint32_t delay_ms); //JRF



 static int tcpm_pd_transmit(struct tcpm_port *port,
 			    enum tcpm_transmit_type type,
 			    const struct pd_message *msg)
 {
 	unsigned long timeout;
 	int ret;
    //printf("PD TX, header: %#x\n", le16_to_cpu(msg->header));

 	/*if (msg)
 		tcpm_log(port, "PD TX, header: %#x", le16_to_cpu(msg->header));
 	else
 		tcpm_log(port, "PD TX, type: %#x", type);*/

	port->tx_complete = false;
 	ret = port->tcpc->pd_transmit(port->tcpc, type, msg);
 	if (ret < 0)
 		return ret;

	// Wait for TX complete
	//print("tcpm_pd_transmit wait for tx_complete.");
	uint32_t tnow = get_ms_clk();
	while (port->tx_complete == false) {
		if (get_ms_clk() >= tnow + PD_T_TCPC_TX_TIMEOUT) {
			//print("TIMEOUT\n");
			break;
		}
	}

 	switch (port->tx_status) {
	case TCPC_TX_SUCCESS:
		tcpm_log(port, "TCPC_TX_SUCCESS");
		if (type == TCPC_TX_SOP) {
			port->message_id = (port->message_id + 1) & PD_HEADER_ID_MASK;
		}
		if (type == TCPC_TX_SOP_PRIME) {
			port->message_id_p = (port->message_id_p + 1) & PD_HEADER_ID_MASK;
		}
		else if (type == TCPC_TX_SOP_PRIME_PRIME) {
			// SOP'' not supported by tcpm
		}
		return 0;
 	case TCPC_TX_DISCARDED:
		tcpm_log(port, "TCPC_TX_DISCARDED");
 		return -EAGAIN;
 	case TCPC_TX_FAILED:
		tcpm_log(port, "TCPC_TX_FAILED");	
		if (type == TCPC_TX_SOP) {
			port->message_id = (port->message_id + 1) & PD_HEADER_ID_MASK;
		}
		if (type == TCPC_TX_SOP_PRIME) {
			port->message_id_p = (port->message_id_p + 1) & PD_HEADER_ID_MASK;
		}
		else if (type == TCPC_TX_SOP_PRIME_PRIME) {
			// SOP'' not supported by tcpm
		}        
 		if (port->pd_capable)
 			tcpm_set_state_cond(port, SOFT_RESET_SEND, 0); //JRF: Message not sent after retries, send soft reset
 		return -EIO;
 	default:
 		return -EIO;
 	}
}


 void tcpm_pd_transmit_complete(struct tcpm_port *port, enum tcpm_transmit_status status)
{
	tcpm_log(port, "PD TX complete, status: %u", status);
	port->tx_status = status;
	//if (status == TCPC_TX_SUCCESS)
		port->tx_complete = true; // mcm
}

// Added for MAX25432
void tcpm_pd_set_cl_cv_status(struct tcpm_port *port, bool status)
{
	tcpm_log(port, "cl_cv_status: %u", status);
	port->cl_cv_status = status;
}

static int tcpm_set_polarity(struct tcpm_port *port,
			     enum typec_cc_polarity polarity)
{
	int ret;

	tcpm_log(port, "polarity %d", polarity);

	ret = port->tcpc->set_polarity(port->tcpc, polarity);
	if (ret < 0)
		return ret;

	port->polarity = polarity;

	return 0;
}

//JRF
static int tcpm_set_bist_test_mode(struct tcpm_port *port,
			     enum bist_test_mode mode)
{
	int ret;

	ret = port->tcpc->set_bist_test_mode(port->tcpc, mode, port->polarity);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpm_set_vconn(struct tcpm_port *port, bool enable)
{
	int ret;

	tcpm_log(port, "vconn:=%d", enable);

	ret = port->tcpc->set_vconn(port->tcpc, enable);

	if (!ret) {
		port->vconn_role = enable ? TYPEC_SOURCE : TYPEC_SINK;
	}
	return ret;
}

static u32 tcpm_get_current_limit(struct tcpm_port *port)
{
	enum typec_cc_status cc;
	u32 limit;

	cc = port->polarity ? port->cc2 : port->cc1;
	switch (cc) {
	case TYPEC_CC_RP_1_5:
		limit = 1500;
		break;
	case TYPEC_CC_RP_3_0:
		limit = 3000;
		break;
	case TYPEC_CC_RP_DEF:
	default:
		if (port->tcpc->get_current_limit)
			limit = port->tcpc->get_current_limit(port->tcpc);
		else
			limit = 0;
		break;
	}

	return limit;
}

static int tcpm_set_current_limit(struct tcpm_port *port, u32 max_ma, u32 mv)
{
	int ret = -EOPNOTSUPP;

	port->supply_voltage = mv;
	port->current_limit = max_ma;

	if (port->tcpc->set_current_limit){
		//mcm ret = port->tcpc->set_current_limit(port->tcpc, max_ma, mv);
	}

	return ret;
}

/*
 * Determine RP value to set based on maximum current supported
 * by a port if configured as source.
 * Returns CC value to report to link partner.
 */
static enum typec_cc_status tcpm_rp_cc(struct tcpm_port *port)
{
	const u32 *src_pdo = port->src_pdo;
	int nr_pdo = port->nr_src_pdo;
	int i;

	/*
	 * Search for first entry with matching voltage.
	 * It should report the maximum supported current.
	 */
	for (i = 0; i < nr_pdo; i++) {
		const u32 pdo = src_pdo[i];

		if (pdo_type(pdo) == PDO_TYPE_FIXED &&
		    pdo_fixed_voltage(pdo) == 5000) {
			unsigned int curr = pdo_max_current(pdo);

			if (curr >= 3000)
				return TYPEC_CC_RP_3_0;
			else if (curr >= 1500)
				return TYPEC_CC_RP_1_5;
			return TYPEC_CC_RP_DEF;
		}
	}

	return TYPEC_CC_RP_3_0;
}

static int tcpm_set_attached_state(struct tcpm_port *port, bool attached)
{
	return port->tcpc->set_roles(port->tcpc, attached, port->pwr_role,
				     port->data_role);
}

static int tcpm_set_roles(struct tcpm_port *port, bool attached,
			  enum typec_role role, enum typec_data_role data)
{
	int ret;
	port->pwr_role = role;
	port->data_role = data;

	ret = port->tcpc->set_roles(port->tcpc, attached, role, data);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpm_set_pwr_role(struct tcpm_port *port, enum typec_role role)
{
	int ret;

	ret = port->tcpc->set_roles(port->tcpc, true, role,
				    port->data_role);
	if (ret < 0)
		return ret;

	port->pwr_role = role;
	typec_set_pwr_role(port->typec_port, role);

	return 0;
}

static void tcpm_swap_complete(struct tcpm_port *port, int result)
{
	if (port->swap_pending) {
		port->swap_status = result;
		port->swap_pending = false;
		port->non_pd_role_swap = false;
	}
}

static inline enum tcpm_state ready_state(struct tcpm_port *port)
{
	if (port->pwr_role == TYPEC_SOURCE)
		return SRC_READY;
	else
		return SNK_READY;
}

static int tcpm_pd_send_source_caps(struct tcpm_port *port)
{
	struct pd_message msg;
	int i;
	tcpm_log(port, "tcpm_pd_send_source_caps");
	memset(&msg, 0, sizeof(msg));
    
    // Get number of fixed PDOs in case this is PD2
    unsigned int num_pdos_to_send = 0;
    for (i = 0; i < port->nr_src_pdo; i++)
	{
        uint32_t pdo = port->src_pdo[i];
        enum pd_pdo_type type = pdo_type(pdo);
        if (port->negotiated_rev < PD_REV30 && type == PDO_TYPE_APDO) {
            // Don't send APDOs in PD2
            break;
        }
        num_pdos_to_send++;
		msg.payload[i] = cpu_to_le32(src_pdo[i]); //JRF3 need to use updayed PDOs based on cable detect
		tcpm_log(port, "tcpm_pd_send_source_caps: msg.payload[%d] = %08x", i, msg.payload[i]);
	}
    
	if (!port->nr_src_pdo) {
		/* No source capabilities defined, sink only */
		msg.header = PD_HEADER_LE(PD_CTRL_REJECT,
					  port->pwr_role,
					  port->data_role,
					  port->negotiated_rev,
					  port->message_id, 0);
	} else {
		msg.header = PD_HEADER_LE(PD_DATA_SOURCE_CAP,
					  port->pwr_role,
					  port->data_role,
					  port->negotiated_rev,
					  port->message_id,
					  num_pdos_to_send /*port->nr_src_pdo*/);
	}
	tcpm_log(port, "msg.header: %08x",msg.header);

	return tcpm_pd_transmit(port, TCPC_TX_SOP, &msg);
}

#define PD_HEADER_MM(type, pwr, data, rev, id, cnt, ext_hdr)		\
	((((type) & PD_HEADER_TYPE_MASK) << PD_HEADER_TYPE_SHIFT) |	\
	 ((pwr) == TYPEC_SOURCE ? PD_HEADER_PWR_ROLE : 0) |		\
	 ((data) == TYPEC_HOST ? PD_HEADER_DATA_ROLE : 0) |		\
	 (rev << PD_HEADER_REV_SHIFT) |					\
	 (((id) & PD_HEADER_ID_MASK) << PD_HEADER_ID_SHIFT) |		\
	 (((cnt) & PD_HEADER_CNT_MASK) << PD_HEADER_CNT_SHIFT) |	\
	 ((ext_hdr) ? PD_HEADER_EXT_HDR : 0))

#define PD_HEADER_LE_MM(type, pwr, data, rev, id, cnt) \
	cpu_to_le16(PD_HEADER_MM((type), (pwr), (data), (rev), (id), (cnt), (1)))

static int tcpm_pd_send_pps_status(struct tcpm_port *port)
{
	struct pd_message msg;

	memset(&msg, 0, sizeof(msg));

	msg.header = PD_HEADER_LE_MM(PD_EXT_PPS_STATUS,
				  port->pwr_role,
				  port->data_role,
				  port->negotiated_rev,
				  port->message_id,2);

	msg.ext_msg.header = PD_EXT_HDR_LE(4, 0, 0, 1); // datasize = 4, chunked = 1

	// See USB-PD spec table 6-57 and figure 6-11 for padding for chunked extended message
	msg.ext_msg.data[5] = 0x00; // padding see spec
	msg.ext_msg.data[4] = 0x00; // padding
	msg.ext_msg.data[3] = 0x00 | (port->cl_cv_status << 3);
	msg.ext_msg.data[2] = 0xFF; // current /50mA
	msg.ext_msg.data[1] = 0xFF; // voltage /20mA high byte
	msg.ext_msg.data[0] = 0xFF; // voltage /20mA low byte

	//print("send pps status: msg.payload = %08x", msg.payload[0]);

	return tcpm_pd_transmit(port, TCPC_TX_SOP, &msg);
}

unsigned int cable_message_id = 0;

static int tcpm_pd_send_cable_disc(struct tcpm_port *port)
{
	tcpm_log(port, "tcpm_pd_send_cable_disc");
	struct pd_message msg;
	int i;

	memset(&msg, 0, sizeof(msg));

	msg.header = 0x104F + ((port->message_id_p & PD_HEADER_ID_MASK) << 9);

	port->message_id_p = (port->message_id_p + 1) & PD_HEADER_ID_MASK;

	msg.payload[0] = 0xFF008001;

	return tcpm_pd_transmit(port, TCPC_TX_SOP_PRIME, &msg);
}

static int tcpm_pd_send_sink_caps(struct tcpm_port *port)
{
    struct pd_message msg;
	int i;

	memset(&msg, 0, sizeof(msg));
    
    /* No sink capabilities defined, source only */
	if (port->negotiated_rev < PD_REV30) {
		msg.header = PD_HEADER_LE(PD_CTRL_REJECT,
					port->pwr_role,
					port->data_role,
					port->negotiated_rev,
					port->message_id, 0);
	} 
    else {
        msg.header = PD_HEADER_LE(PD_CTRL_NOT_SUPP,
                    port->pwr_role,
                    port->data_role,
                    port->negotiated_rev,
                    port->message_id, 0);
    }

	return tcpm_pd_transmit(port, TCPC_TX_SOP, &msg);
}

static void tcpm_set_cc(struct tcpm_port *port, enum typec_cc_status cc); //JRF Hack

static void tcpm_set_state(struct tcpm_port *port, enum tcpm_state state,
			   unsigned int delay_ms)
{
	if (delay_ms) {
		tcpm_log(port, "pending state change %s -> %s @ %u ms", tcpm_states[port->state], tcpm_states[state], delay_ms);
		port->delayed_state = state;
        port->delayed_runtime = get_ms_clk() + delay_ms;
        port->delay_ms = delay_ms;
	} else {
		tcpm_log(port, "state change %s -> %s", tcpm_states[port->state], tcpm_states[state]);
		//JRF txSinkNG
		if (port->prev_state == SRC_READY && state != SRC_READY) // Starting AMS
			tcpm_set_cc(port, TYPEC_CC_RP_1_5);

		//JRF txSinkOK
		if (port->prev_state != SRC_READY && state == SRC_READY && port->pd_capable) // Ending AMS
			tcpm_set_cc(port, TYPEC_CC_RP_3_0);

		port->delayed_state = INVALID_STATE;
		port->prev_state = port->state;
		port->state = state;
		/*
		 * Don't re-queue the state machine work item if we're currently
		 * in the state machine and we're immediately changing states.
		 * tcpm_state_machine_work() will continue running the state
		 * machine.
		 */
		if (!port->state_machine_running) {
			queue_work_mx(port);
		}
	}
}

static void tcpm_set_state_cond(struct tcpm_port *port, enum tcpm_state state,
				uint32_t delay_ms)
{
	if (port->enter_state == port->state) {
		tcpm_set_state(port, state, delay_ms);
    }
	else {
		tcpm_log(port, "skipped %sstate change %s -> %s [%u ms], context state %s",
			 delay_ms ? "delayed " : "",
			 tcpm_states[port->state], tcpm_states[state],
			 delay_ms, tcpm_states[port->enter_state]);
    }
}

static void tcpm_queue_message(struct tcpm_port *port,
			       enum pd_msg_request message)
{
	port->queued_message = message;
}


/*
* PD (data, control) command handling functions
*/

static int tcpm_pd_send_control(struct tcpm_port *port,
				enum pd_ctrl_msg_type type)
{
	struct pd_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.header = PD_HEADER_LE(type, port->pwr_role,
				  port->data_role,
				  port->negotiated_rev,
				  port->message_id, 0);
    //printf("tcpm_pd_send_control msg.header: %04x\n",msg.header);
	return tcpm_pd_transmit(port, TCPC_TX_SOP, &msg);
}

/*
 * Send queued message without affecting state.
 */
static bool tcpm_send_queued_message(struct tcpm_port *port)
{
	enum pd_msg_request queued_message;

	do {
		queued_message = port->queued_message;
		port->queued_message = PD_MSG_NONE;

		switch (queued_message) {
		case PD_MSG_CTRL_WAIT:
			tcpm_pd_send_control(port, PD_CTRL_WAIT);
			break;
		case PD_MSG_CTRL_REJECT:
			tcpm_pd_send_control(port, PD_CTRL_REJECT);
			break;
		case PD_MSG_CTRL_NOT_SUPP:
			tcpm_pd_send_control(port, PD_CTRL_NOT_SUPP);
			break;
		case PD_MSG_DATA_SINK_CAP:
			tcpm_pd_send_sink_caps(port);
			break;
		case PD_MSG_DATA_SOURCE_CAP:
			tcpm_pd_send_source_caps(port);
			break;
		case PD_MSG_PPS_STATUS:
			tcpm_pd_send_pps_status(port);
			break;
		default:
			break;
		}
	} while (port->queued_message != PD_MSG_NONE);

	if (port->delayed_state != INVALID_STATE) {
		port->delayed_state = INVALID_STATE;
	}
	return false;
}

static enum typec_pwr_opmode tcpm_get_pwr_opmode(enum typec_cc_status cc)
{
	switch (cc) {
	case TYPEC_CC_RP_1_5:
		return TYPEC_PWR_MODE_1_5A;
	case TYPEC_CC_RP_3_0:
		return TYPEC_PWR_MODE_3_0A;
	case TYPEC_CC_RP_DEF:
	default:
		return TYPEC_PWR_MODE_USB;
	}
}

static void tcpm_typec_disconnect(struct tcpm_port *port)
{
	if (port->connected) {
		port->partner = NULL;
		port->connected = false;
	}
}

static int tcpm_init_vconn(struct tcpm_port *port)
{
	int ret;

	ret = port->tcpc->set_vconn(port->tcpc, false);
	port->vconn_role = TYPEC_SINK;
	return ret;
}

static int tcpm_init_vbus(struct tcpm_port *port)
{
	int ret;

	ret = port->tcpc->set_vbus(port->tcpc, false, false);
	port->vbus_source = false;
	port->vbus_charge = false;
	return ret;
}


static void tcpm_reset_port(struct tcpm_port *port)
{
	tcpm_log(port, "in tcpm_reset_port");
	tcpm_init_vconn(port); //JRF need to do this first to meet tVconnOff
	//tcpm_unregister_altmodes(port);
	tcpm_typec_disconnect(port);
	port->attached = false;
	port->pd_capable = false;
	port->pps_data.supported = false;
    port->cl_cv_status = 0;

	/*
	 * First Rx ID should be 0; set this to a sentinel of -1 so that
	 * we can check tcpm_pd_rx_handler() if we had seen it before.
	 */
	port->rx_msgid = -1;
	//print("tcpm_reset_port: set_pd_rx = false");
	//port->tcpc->set_pd_rx(port->tcpc, false);
	tcpm_init_vbus(port);	/* also disables charging */
	//tcpm_init_vconn(port);
	tcpm_set_current_limit(port, 0, 0);
	//tcpm_set_polarity(port, TYPEC_POLARITY_CC1); // MM: No need to set polarity to default, keep last known polarity
	tcpm_set_bist_test_mode(port, BIST_DISABLED); // JRF Hack for BIST mode
	tcpm_set_attached_state(port, false);
	port->try_src_count = 0;
	port->try_snk_count = 0;
}

void tcpm_cc_change(struct tcpm_port *port)
{
	port->pd_events |= TCPM_CC_EVENT;
}

void tcpm_vbus_change(struct tcpm_port *port)
{
	port->pd_events |= TCPM_VBUS_EVENT;
}

void tcpm_tcpc_reset(struct tcpm_port *port)
{
	/* XXX: Maintain PD connection if possible? */
	tcpm_init(port);
}

void tcpm_pd_hard_reset(struct tcpm_port *port)
{
	port->pd_events |= TCPM_RESET_EVENT;
}

static int tcpm_set_vbus(struct tcpm_port *port, bool enable)
{
	int ret;

	tcpm_log(port, "tcpm_set_vbus: vbus:=%d charge=%d", enable, port->vbus_charge);

	if (enable && port->vbus_charge)
		return -EINVAL;

	//port->tcpc->set_pd_rx(port->tcpc, enable); //JRF enable PD RX with VBUS

	ret = port->tcpc->set_vbus(port->tcpc, enable, port->vbus_charge);
	if (ret < 0)
		return ret;

	port->vbus_source = enable;

	return 0;
}

static int tcpm_src_attach(struct tcpm_port *port)
{
	tcpm_log(port, "tcpm_src_attach");
	enum typec_cc_polarity polarity =
				port->cc2 == TYPEC_CC_RD ? TYPEC_POLARITY_CC2
							 : TYPEC_POLARITY_CC1;
	int ret;

	if (port->attached)
		return 0;

	ret = tcpm_set_polarity(port, polarity);
	if (ret < 0)
		return ret;

	ret = tcpm_set_roles(port, true, TYPEC_SOURCE, TYPEC_HOST);
	if (ret < 0)
		return ret;

	////print("tcpm_src_attach: set_pd_rx = true");
	ret = port->tcpc->set_pd_rx(port->tcpc, true);
	if (ret < 0)
		goto out_disable_mux;



	/*
	 * USB Type-C specification, version 1.2,
	 * chapter 4.5.2.2.8.1 (Attached.SRC Requirements)
	 * Enable VCONN only if the non-RD port is set to RA.
	 */
	if ((polarity == TYPEC_POLARITY_CC1 && port->cc2 == TYPEC_CC_RA) ||
	    (polarity == TYPEC_POLARITY_CC2 && port->cc1 == TYPEC_CC_RA)) {
		ret = tcpm_set_vconn(port, true);
		if (ret < 0)
			goto out_disable_pd;
	}

	ret = tcpm_set_vbus(port, true);
	if (ret < 0)
		goto out_disable_vconn;

	port->pd_capable = false;

	port->partner = NULL;

	port->attached = true;
	port->send_discover = true;

	return 0;

out_disable_vconn:
	tcpm_set_vconn(port, false);
out_disable_pd:
	////print("out_disable_pd: set_pd_rx = false");
	port->tcpc->set_pd_rx(port->tcpc, false);
out_disable_mux:
	return ret;
}

static void tcpm_detach(struct tcpm_port *port)
{
	if (!port->attached)
		return;

	if (tcpm_port_is_disconnected(port))
		port->hard_reset_count = 0;

	tcpm_reset_port(port);
}

static void tcpm_src_detach(struct tcpm_port *port)
{
	tcpm_detach(port);
}

static int tcpm_acc_attach(struct tcpm_port *port)
{
	int ret;

	if (port->attached)
		return 0;

	ret = tcpm_set_roles(port, true, TYPEC_SOURCE, TYPEC_HOST);
	if (ret < 0)
		return ret;

	port->partner = NULL;

	port->attached = true;

	return 0;
}

static void tcpm_acc_detach(struct tcpm_port *port)
{
	tcpm_detach(port);
}

static inline enum tcpm_state hard_reset_state(struct tcpm_port *port)
{
	if (port->hard_reset_count < PD_N_HARD_RESET_COUNT)
		return HARD_RESET_SEND;
	if (port->pd_capable)
		return ERROR_RECOVERY;
	if (port->pwr_role == TYPEC_SOURCE)
		return SRC_UNATTACHED;
	if (port->state == SNK_WAIT_CAPABILITIES)
		return SNK_READY;
	return SNK_UNATTACHED;
}

static inline enum tcpm_state unattached_state(struct tcpm_port *port)
{
	if (port->port_type == TYPEC_PORT_DRP) {
		if (port->pwr_role == TYPEC_SOURCE)
			return SRC_UNATTACHED;
		else
			return SNK_UNATTACHED;
	} else if (port->port_type == TYPEC_PORT_SRC) {
		return SRC_UNATTACHED;
	}

	return SNK_UNATTACHED;
}

static int tcpm_pd_check_request(struct tcpm_port *port)
{
	u32 pdo, rdo = port->sink_request;
	unsigned int max, op, pdo_max, index;
	enum pd_pdo_type type;

	// mcm: added for pps
	unsigned int req_v;

	index = rdo_index(rdo);
    port->pdo_index = index;

	if (!index || index > port->nr_src_pdo)
		return -EINVAL;

	pdo = src_pdo[index - 1]; //JRF check negotiated PDO

	type = pdo_type(pdo);
	switch (type) {
	case PDO_TYPE_FIXED:
	case PDO_TYPE_VAR:
		max = rdo_max_current(rdo);
		op = rdo_op_current(rdo);
		pdo_max = pdo_max_current(pdo);

		if (op > pdo_max)
			return -EINVAL;

		if (max > pdo_max && !(rdo & RDO_CAP_MISMATCH))
			return -EINVAL;

		if (type == PDO_TYPE_FIXED) {
			tcpm_log(port,
				"Requested %u mV, %u mA for %u / %u mA",
				pdo_fixed_voltage(pdo), pdo_max, op, max);
			port->supply_voltage = pdo_fixed_voltage(pdo);
			// for fixed PDO, set ILIM to the PDO advertised current
			port->current_limit = pdo_max;
			port->pdo_type = PDO_TYPE_FIXED;


		} else {
			tcpm_log(port,
				"Requested %u -> %u mV, %u mA for %u / %u mA",
				pdo_min_voltage(pdo), pdo_max_voltage(pdo),
				pdo_max, op, max);
		}

		break;
	case PDO_TYPE_BATT:
		max = rdo_max_power(rdo);
		op = rdo_op_power(rdo);
		pdo_max = pdo_max_power(pdo);

		if (op > pdo_max)
			return -EINVAL;
		if (max > pdo_max && !(rdo & RDO_CAP_MISMATCH))
			return -EINVAL;
		tcpm_log(port,
			"Requested %u -> %u mV, %u mW for %u / %u mW",
			pdo_min_voltage(pdo), pdo_max_voltage(pdo),
			pdo_max, op, max);
		break;

	case PDO_TYPE_APDO: // Aadded for MAX25432 PPS
		req_v = ((rdo >> 9) & 0x7FF) * 20; 	// requested voltage mV
		op = (rdo & 0x7F) * 50;    		// requested current mA
		pdo_max = pdo_pps_apdo_max_current(pdo); // PDO max current

		tcpm_log(port,
			"PPS Requested %u mV of %u mV, %u mA of %u",
			req_v, pdo_pps_apdo_max_voltage(pdo), op, pdo_max);


		// Check for valid voltage request
		if (req_v < pdo_pps_apdo_min_voltage(pdo))
			return -EINVAL;
		if (req_v > pdo_pps_apdo_max_voltage(pdo))
			return -EINVAL;

		// Check for valid operating current
		if (op > pdo_max)
			return -EINVAL;
		if (op < 1000)
			return -EINVAL;

		port->supply_voltage = req_v;		// This sets vbus
		port->current_limit = op;		// This sets ILIM
		port->pdo_type = PDO_TYPE_APDO;
		break;

	default:
		return -EINVAL;
	}

	port->op_vsafe5v = index == 1;

	return 0;
}

static void run_state_machine(struct tcpm_port *port)
{
	int ret;
	enum typec_pwr_opmode opmode;
	unsigned int msecs;

	tcpm_log(port, "in run_state_machine, port->state: %s", tcpm_states[port->state]);

	port->enter_state = port->state;
	switch (port->state) {
	case DRP_TOGGLING:
		break;
	/* SRC states */
	case SRC_UNATTACHED:
		tcpm_set_cc(port, TYPEC_CC_RP_3_0); //JRF Move sooner
		tcpm_src_detach(port);
		break;
	case SRC_ATTACH_WAIT:
        
        if (tcpm_port_is_debug(port))
			tcpm_set_state(port, DEBUG_ACC_ATTACHED,
				       PD_T_CC_DEBOUNCE);
		else if (tcpm_port_is_audio(port))
			tcpm_set_state(port, AUDIO_ACC_ATTACHED,
				       PD_T_CC_DEBOUNCE);
		else if (tcpm_port_is_source(port))
			tcpm_set_state(port, SRC_ATTACHED, PD_T_CC_DEBOUNCE);
		break;
	case SRC_TRYWAIT:
		tcpm_set_cc(port, tcpm_rp_cc(port));
		if (port->max_wait == 0) {
			port->max_wait = get_ms_clk() + PD_T_DRP_TRY;
			tcpm_set_state(port, SRC_TRYWAIT_UNATTACHED,
							 PD_T_DRP_TRY);
		} else {
			if (time_is_after_jiffies(port->max_wait))
				tcpm_set_state(port, SRC_TRYWAIT_UNATTACHED, port->max_wait - get_ms_clk());
			else
				tcpm_set_state(port, SNK_UNATTACHED, 0);
		}
		break;
	case SRC_TRYWAIT_DEBOUNCE:
		tcpm_set_state(port, SRC_ATTACHED, PD_T_CC_DEBOUNCE);
		break;
	case SRC_TRYWAIT_UNATTACHED:
		tcpm_set_state(port, SNK_UNATTACHED, 0);
		break;
	case SRC_ATTACHED:
		ret = tcpm_src_attach(port);
		tcpm_set_cc(port, tcpm_rp_cc(port));
		tcpm_set_state(port, SRC_UNATTACHED,
						 ret < 0 ? 0 : PD_T_PS_SOURCE_ON);
		break;
	case SRC_STARTUP:
		port->attached = true; // MCM add, in case we are here after hard reset
		opmode =  tcpm_get_pwr_opmode(tcpm_rp_cc(port));
		port->pwr_opmode = TYPEC_PWR_MODE_USB;
		port->disc_cable_count = 0;
		port->caps_count = 0;
		port->negotiated_rev = PD_MAX_REV;
		port->message_id = 0;
		port->message_id_p = 0;
		port->rx_msgid = -1;
		port->explicit_contract = false;
        port->last_pdo_index = 0; // This is used to determine if a request is APDO->APDO Set to zero here.
		port->last_vbus_hv_voltage = 0; //mm: reset, no vbus_hv requested yet
		tcpm_set_state(port, SRC_DISC_CABLE, 100);
		//tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0);
		break;
	case SRC_DISC_CABLE:
		//mdelay(100);
		tcpm_log(port, "cable discovery vconn_role: %d", port->vconn_role);
		if (port->vconn_role == TYPEC_SOURCE) {
			port->disc_cable_count++;
			if (port->disc_cable_count > 2) {
				tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0);
				break;
			}
			ret = tcpm_pd_send_cable_disc(port); //JRF2 Cable Discovery
			if (ret < 0)
				tcpm_set_state(port, SRC_DISC_CABLE, 45); //wait 20 ms before resending
			else
				tcpm_set_state(port, SRC_WAIT_CABLE, 30); //wait tVDMSenderRepsone
		} else {
			tcpm_set_state(port, SRC_SEND_CAPABILITIES, 50 /*0*/); // 50ms delay for when no cable discovery is done
		}
		break;
	case SRC_WAIT_CABLE:
		//mdelay(30); //wait tVDMSenderRepsone
		tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0);
		break;
	case SRC_SEND_CAPABILITIES:
		port->caps_count++;
		if (port->caps_count > PD_N_CAPS_COUNT) {
			tcpm_set_state(port, SRC_READY, 0);
			break;
		}

		// JFR: If TX soft Reset Fails (-EIO), break and let state go to soft reset after initial startup
		ret = tcpm_pd_send_source_caps(port);
		if (ret == -EIO && port->pd_capable)
			break;

		if (ret < 0) {
			tcpm_set_state(port, SRC_SEND_CAPABILITIES,
							 PD_T_SEND_SOURCE_CAP);
		} else {
			/*
			 * Per standard, we should clear the reset counter here.
			 * However, that can result in state machine hang-ups.
			 * Reset it only in READY state to improve stability.
			 */
			port->hard_reset_count = 0; //JRF Hack
			port->caps_count = 0;
			port->pd_capable = true;

			tcpm_set_state_cond(port, hard_reset_state(port), PD_T_SENDER_RESPONSE); // JRF FIX for PD.SRC.E5 SenderResponseTimer
		}
		break;
	case SRC_NEGOTIATE_CAPABILITIES:
		ret = tcpm_pd_check_request(port);
		if (ret < 0) {
			tcpm_pd_send_control(port, PD_CTRL_REJECT);
			if (!port->explicit_contract) {
				tcpm_set_state(port,
								 SRC_WAIT_NEW_CAPABILITIES, 0);
			} else {
				tcpm_set_state(port, SRC_READY, 0);
			}
		} else {
			tcpm_pd_send_control(port, PD_CTRL_ACCEPT);
			if (port->pdo_type == PDO_TYPE_APDO && port->pdo_index == port->last_pdo_index) {
                // New request within same APDO, transition the supply immediately
    		 	tcpm_set_state(port, SRC_TRANSITION_SUPPLY, 0);
			}
			else {
                // All other transition, use tSrcTransition
				tcpm_set_state(port, SRC_TRANSITION_SUPPLY, PD_T_SRC_TRANSITION);
			}
            port->last_pdo_index = port->pdo_index;
		}
		break;
	case SRC_TRANSITION_SUPPLY:
		// Set vbus voltage
		port->tcpc->set_vbus_hv(port->tcpc, port->supply_voltage, port->current_limit, port->pdo_type);
		// Wait for vbus
		int32_t vbus_step = port->supply_voltage-port->last_vbus_hv_voltage;
		u32 src_delay = abs(vbus_step) / 95; // 1ms for every 95mV
		if (src_delay < 5)
			src_delay = 5;
		mdelay(src_delay);
		port->last_vbus_hv_voltage = port->supply_voltage; // update last requested voltage seen here
		// Send the SRC_READY
		tcpm_pd_send_control(port, PD_CTRL_PS_RDY);
		port->explicit_contract = true;
		//typec_set_pwr_opmode(port->typec_port, TYPEC_PWR_MODE_PD);
		port->pwr_opmode = TYPEC_PWR_MODE_PD;
		tcpm_set_state_cond(port, SRC_READY, 0);
		break;
	case SRC_READY:
		// Set timeout for pps_status
		if (port->explicit_contract && port->pdo_type == PDO_TYPE_APDO) {
			tcpm_set_state_cond(port, hard_reset_state(port), 13000);
		}
		port->hard_reset_count = 0;
		port->try_src_count = 0;
		//tcpm_check_send_discover(port); //mm removed 2/4/21 no longer needed for PD2.0 and above
		/*
		 * 6.3.5
		 * Sending ping messages is not necessary if
		 * - the source operates at vSafe5V
		 * or
		 * - The system is not operating in PD mode
		 * or
		 * - Both partners are connected using a Type-C connector
		 *
		 * There is no actual need to send PD messages since the local
		 * port type-c and the spec does not clearly say whether PD is
		 * possible when type-c is connected to Type-A/B
		 */
		break;
	case SRC_WAIT_NEW_CAPABILITIES:
		/* Nothing to do... */
		break;
        
    /* Accessory states */
	case ACC_UNATTACHED:
		tcpm_acc_detach(port);
		tcpm_set_state(port, SRC_UNATTACHED, 0);
		break;
	case DEBUG_ACC_ATTACHED:
	case AUDIO_ACC_ATTACHED:
		ret = tcpm_acc_attach(port);
		if (ret < 0)
			tcpm_set_state(port, ACC_UNATTACHED, 0);
		break;
	case AUDIO_ACC_DEBOUNCE:
		tcpm_set_state(port, ACC_UNATTACHED, PD_T_CC_DEBOUNCE);
		break;    
        
        
	/* Hard_Reset states */
	case HARD_RESET_SEND:
		tcpm_pd_transmit(port, TCPC_TX_HARD_RESET, NULL);
		tcpm_set_state(port, HARD_RESET_START, 0);
		break;
	case HARD_RESET_START:
		port->message_id = 0;     // mm added 4/9/21 for compliance
		port->message_id_p = 0;
		port->rx_msgid = -1;      // mm added 4/9/21 for compliance
		port->hard_reset_count++;
		port->tcpc->set_pd_rx(port->tcpc, false);
		//mcm tcpm_unregister_altmodes(port);
		port->send_discover = true;
        if (port->pwr_role == TYPEC_SOURCE) {
            // Center timing within spec (Lecroy)
            u32 tPsHardReset = 23 - port->supply_voltage / 2000; // subtract the time to dV down 5%, dV rate is 0.1V/ms
            if(port->force_disable_flag) {
                tcpm_set_state(port, SRC_HARD_RESET_DISABLE_RP, tPsHardReset /*PD_T_PS_HARD_RESET*/);
            }
            else {
                tcpm_set_state(port, SRC_HARD_RESET_VBUS_OFF, tPsHardReset /*PD_T_PS_HARD_RESET*/);
            }
		}
		else
			tcpm_set_state(port, SNK_HARD_RESET_SINK_OFF, 0);
		break;
	case SRC_HARD_RESET_VBUS_OFF:
		tcpm_set_vconn(port, false); //Need to turn off VCONN on hard reset
		tcpm_set_vbus(port, false);
		tcpm_set_roles(port, port->self_powered, TYPEC_SOURCE, TYPEC_HOST);
        tcpm_set_state(port, SRC_HARD_RESET_VBUS_ON, 660 /*PD_T_SRC_RECOVER*/); // mm: this plus fixed delay in tcpci_set_vbus < 1s
		break; 
	case SRC_HARD_RESET_VBUS_ON:
		port->tcpc->set_pd_rx(port->tcpc, true);
		//mdelay(1000); // JRF allow time for RX to turn on
		tcpm_set_vbus(port, true);
		if ((port->polarity == TYPEC_POLARITY_CC1 && port->cc2 == TYPEC_CC_RA) || (port->polarity == TYPEC_POLARITY_CC2 && port->cc1 == TYPEC_CC_RA))
			tcpm_set_vconn(port, true);	// Turn VCONN Back on
		//port->tcpc->set_pd_rx(port->tcpc, true);
		tcpm_set_attached_state(port, true);
		tcpm_set_state(port, SRC_UNATTACHED, PD_T_PS_SOURCE_ON);
		break;
	/* Soft_Reset states */
	case SOFT_RESET:
		port->message_id = 0;
		port->message_id_p = 0;
		port->rx_msgid = -1;
		tcpm_pd_send_control(port, PD_CTRL_ACCEPT);
		if (port->pwr_role == TYPEC_SOURCE)
			tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0);
		else
			tcpm_set_state(port, SNK_WAIT_CAPABILITIES, 0);
		break;
	case SOFT_RESET_SEND:
		port->message_id = 0;
		port->message_id_p = 0;
		port->rx_msgid = -1;
		if (tcpm_pd_send_control(port, PD_CTRL_SOFT_RESET))
			tcpm_set_state_cond(port, hard_reset_state(port), 0);
		else
			tcpm_set_state_cond(port, hard_reset_state(port),
							PD_T_SENDER_RESPONSE);
		break;
	
    case VCONN_SWAP_ACCEPT:
		tcpm_pd_send_control(port, PD_CTRL_ACCEPT);
		tcpm_set_state(port, VCONN_SWAP_START, 0);
		break;
	case VCONN_SWAP_SEND:
		tcpm_pd_send_control(port, PD_CTRL_VCONN_SWAP);
		tcpm_set_state(port, VCONN_SWAP_SEND_TIMEOUT,
			       PD_T_SENDER_RESPONSE);
		break;
	case VCONN_SWAP_SEND_TIMEOUT:
		tcpm_swap_complete(port, -ETIMEDOUT);
		tcpm_set_state(port, ready_state(port), 0);
		break;
	case VCONN_SWAP_START:
		if (port->vconn_role == TYPEC_SOURCE)
			tcpm_set_state(port, VCONN_SWAP_WAIT_FOR_VCONN, 0);
		else
			tcpm_set_state(port, VCONN_SWAP_TURN_ON_VCONN, 0);
		break;
	case VCONN_SWAP_WAIT_FOR_VCONN:
		tcpm_set_state(port, hard_reset_state(port),
			       PD_T_VCONN_SOURCE_ON);
		break;
	case VCONN_SWAP_TURN_ON_VCONN:
		tcpm_set_vconn(port, true);
		tcpm_pd_send_control(port, PD_CTRL_PS_RDY);
		tcpm_set_state(port, ready_state(port), 0);
		break;
	case VCONN_SWAP_TURN_OFF_VCONN:
		tcpm_set_vconn(port, false);
		tcpm_set_state(port, ready_state(port), 0);
		break;

	case DR_SWAP_CANCEL:
	case PR_SWAP_CANCEL:
	case VCONN_SWAP_CANCEL:
		tcpm_swap_complete(port, port->swap_status);
		if (port->pwr_role == TYPEC_SOURCE)
			tcpm_set_state(port, SRC_READY, 0);
		else
			tcpm_set_state(port, SNK_READY, 0);
		break;
 
    case BIST_RX:
		switch (BDO_MODE_MASK(port->bist_request)) {
		case BDO_MODE_CARRIER2:
			tcpm_pd_transmit(port, TCPC_TX_BIST_MODE_2, NULL);
			tcpm_set_state(port, unattached_state(port), 0); //JRF hack for BIST
			break;
		case BDO_MODE_TESTDATA:
			//tcpm_set_bist_test_mode(port, BIST_ENABLED); //JRF hack for BIST
			break;
		default:
			break;
		}
		/* Always switch to unattached state */
		// tcpm_set_state(port, unattached_state(port), 0); //JRF hack for BIST
		break;
	case GET_STATUS_SEND:
		break;
	case GET_STATUS_SEND_TIMEOUT:
		break;
	case GET_PPS_STATUS_SEND:
		break;
	case GET_PPS_STATUS_SEND_TIMEOUT:
		break;
	case ERROR_RECOVERY:
		break;
	case PORT_RESET:
		tcpm_reset_port(port);
		tcpm_set_cc(port, TYPEC_CC_OPEN);
		tcpm_set_state(port, PORT_RESET_WAIT_OFF, PD_T_ERROR_RECOVERY);
		break;
	case PORT_RESET_WAIT_OFF:
		tcpm_set_state(port, tcpm_default_state(port), port->vbus_present ? PD_T_PS_SOURCE_OFF : 0);
		break;
    
    // Disable Rp hard reset sequence
    case SRC_HARD_RESET_DISABLE_RP:
        tcpm_set_cc(port, TYPEC_CC_OPEN); // Disable Rp
        tcpm_set_state(port, SRC_HARD_RESET_VBUS_OFF_DISABLE, 300);
        break;

	case SRC_HARD_RESET_VBUS_OFF_DISABLE:
		tcpm_set_vconn(port, false); //Need to turn off VCONN on hard reset
		tcpm_set_vbus(port, false);
		tcpm_set_roles(port, port->self_powered, TYPEC_SOURCE, TYPEC_HOST);
        port->force_disable_flag = false;
        tcpm_set_state(port, SRC_UNATTACHED, 2000);
		break;
        
	default:
		break;
	}
}

void tcpm_state_machine_work(struct tcpm_port *port)
{
	enum tcpm_state prev_state;

	port->state_machine_running = true;

	if (port->queued_message && tcpm_send_queued_message(port))
		goto done;

	/*
	 * Continue running as long as we have (non-delayed) state changes
	 * to make.
	 */
	do {
		prev_state = port->state;
		//tcpm_log(port, "in tcpm_state_machine_work calling run_state_machine, state = %s\n",  tcpm_states[port->state]);
		run_state_machine(port);
		if (port->queued_message) {
			tcpm_send_queued_message(port);
		}
	} while (port->state != prev_state && !port->delayed_state);

done:
	port->state_machine_running = false;
}


static void tcpm_set_cc(struct tcpm_port *port, enum typec_cc_status cc)
{
	//tcpm_log(port, "cc:=%d", cc);
	port->cc_req = cc;
	port->tcpc->set_cc(port->tcpc, cc);
}



static void _tcpm_cc_change(struct tcpm_port *port, enum typec_cc_status cc1,
			    enum typec_cc_status cc2)
{
	enum typec_cc_status old_cc1, old_cc2;
	enum tcpm_state new_state;

	old_cc1 = port->cc1;
	old_cc2 = port->cc2;
	port->cc1 = cc1;
	port->cc2 = cc2;

	tcpm_log(port, "_tcpm_cc_change CC1: %u -> %u, CC2: %u -> %u [state %s, polarity %d, %s]",
		       old_cc1, cc1, old_cc2, cc2, tcpm_states[port->state],
		       port->polarity,
		       tcpm_port_is_disconnected(port) ? "disconnected"
						       : "connected");

	switch (port->state) {

	case SRC_UNATTACHED:
		if (tcpm_port_is_debug(port) || tcpm_port_is_audio(port) ||
		tcpm_port_is_source(port))
		tcpm_set_state(port, SRC_ATTACH_WAIT, 0);
		break;
	case SRC_ATTACH_WAIT:
		if (tcpm_port_is_disconnected(port) ||
		    tcpm_port_is_audio_detached(port))
			tcpm_set_state(port, SRC_UNATTACHED, 0);
		else if (cc1 != old_cc1 || cc2 != old_cc2)
			tcpm_set_state(port, SRC_ATTACH_WAIT, 0);
		break;
	case SRC_ATTACHED:
	case SRC_SEND_CAPABILITIES:
	case SRC_READY:
		if (tcpm_port_is_disconnected(port)/* || //JRF HACK
		    !tcpm_port_is_source(port)*/) {
			tcpm_set_state(port, SRC_UNATTACHED, 0);
		}
		break;
	case SRC_TRYWAIT:
		/* Hand over to state machine if needed */
		if (!port->vbus_present && tcpm_port_is_source(port))
			tcpm_set_state(port, SRC_TRYWAIT_DEBOUNCE, 0);
		break;
	case SRC_TRYWAIT_DEBOUNCE:
		if (port->vbus_present || !tcpm_port_is_source(port))
			tcpm_set_state(port, SRC_TRYWAIT, 0);
		break;
	case SRC_TRY_WAIT:
		if (tcpm_port_is_source(port))
			tcpm_set_state(port, SRC_TRY_DEBOUNCE, 0);
		break;
	case SRC_TRY_DEBOUNCE:
		tcpm_set_state(port, SRC_TRY_WAIT, 0);
		break;

	case AUDIO_ACC_ATTACHED:
		if (cc1 == TYPEC_CC_OPEN || cc2 == TYPEC_CC_OPEN)
			tcpm_set_state(port, AUDIO_ACC_DEBOUNCE, 0);
		break;
	case AUDIO_ACC_DEBOUNCE:
		if (tcpm_port_is_audio(port))
			tcpm_set_state(port, AUDIO_ACC_ATTACHED, 0);
		break;

	case DEBUG_ACC_ATTACHED:
		if (cc1 == TYPEC_CC_OPEN || cc2 == TYPEC_CC_OPEN)
			tcpm_set_state(port, ACC_UNATTACHED, 0);
		break;
    
    case SRC_HARD_RESET_DISABLE_RP:
    case SRC_HARD_RESET_VBUS_OFF_DISABLE:
        // Do nothing in this case
        break;        
        
	default:
		if (tcpm_port_is_disconnected(port)) {
			tcpm_log(port, "in _tcpm_cc_change case=default setting state to SRC_UNATTACHED");
			tcpm_set_state(port, unattached_state(port), 0);
		}
		break;
	}
}

static int tcpm_copy_pdos(u32 *dest_pdo, const u32 *src_pdo,
			  unsigned int nr_pdo)
{
	unsigned int i;

	if (nr_pdo > PDO_MAX_OBJECTS)
		nr_pdo = PDO_MAX_OBJECTS;

	for (i = 0; i < nr_pdo; i++)
		dest_pdo[i] = src_pdo[i];

	return nr_pdo;
}

static int tcpm_copy_caps(struct tcpm_port *port,
			  const struct tcpc_config *tcfg)
{
	port->nr_src_pdo = tcpm_copy_pdos(port->src_pdo, tcfg->src_pdo,
					  tcfg->nr_src_pdo);

	port->typec_caps.prefer_role = tcfg->default_role;
	port->typec_caps.type = tcfg->type;
	port->typec_caps.data = tcfg->data;
	port->self_powered = port->tcpc->config->self_powered;

	return 0;
}

uint8_t port_index = 0;
struct tcpm_port *tcpm_register_port(struct tcpc_dev *tcpc)
{
	struct tcpm_port *port;
	int i, err;

	if (!tcpc ||
	    !tcpc->get_vbus || !tcpc->set_cc || !tcpc->get_cc ||
	    !tcpc->set_polarity || !tcpc->set_vconn || !tcpc->set_vbus ||
	    !tcpc->set_pd_rx || !tcpc->set_roles || !tcpc->pd_transmit)
		return ERR_PTR(-EINVAL);

	port = malloc(sizeof(*port));
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->id = port_index++;
	port->tcpc = tcpc;

	// Forcing some initial values
	port->delayed_state = INVALID_STATE;
	port->prev_state = INVALID_STATE;
	port->state = INVALID_STATE;
	port->cc1 = TYPEC_CC_OPEN;
	port->cc2 = TYPEC_CC_OPEN;

	// Forcing some initialization here for dedicated charger
	port->port_type = TYPEC_PORT_SRC; // mcm added
	port->pwr_role = TYPEC_SOURCE;
	port->data_role = TYPEC_HOST;
	port->vconn_role = TYPEC_SOURCE;
	err = tcpm_copy_caps(port, tcpc->config); // also sets port->nr_src_pdo
	if (err < 0)
		goto out_destroy_wq;

	port->typec_caps.fwnode = tcpc->fwnode;
	port->typec_caps.revision = 0x0120;	/* Type-C spec release 1.2 */
	port->typec_caps.pd_revision = 0x0300;	/* USB-PD spec release 3.0 */

	port->partner_desc.identity = &port->partner_ident;
	port->port_type = port->typec_caps.type;

    port->force_disable_flag = false;
	tcpm_init(port);

	return port;

	out_destroy_wq:

	return port;
}

static void _tcpm_pd_vbus_on(struct tcpm_port *port)
{
	tcpm_log(port, "_tcpm_pd_vbus_on");
	port->vbus_present = true;
	switch (port->state) {
	case SNK_TRANSITION_SINK_VBUS:
		port->explicit_contract = true;
		tcpm_set_state(port, SNK_READY, 0);
		break;
	case SNK_DISCOVERY:
		tcpm_set_state(port, SNK_DISCOVERY, 0);
		break;

	case SNK_DEBOUNCED:
		tcpm_set_state(port, tcpm_try_src(port) ? SRC_TRY
							: SNK_ATTACHED,
				       0);
		break;
	case SNK_HARD_RESET_WAIT_VBUS:
		tcpm_set_state(port, SNK_HARD_RESET_SINK_ON, 0);
		break;
	case SRC_ATTACHED:
		tcpm_set_state(port, SRC_STARTUP, 0);
		break;
	case SRC_HARD_RESET_VBUS_ON:
		//mdelay(100); //JRF wait for cable to power up
		tcpm_set_state(port, SRC_STARTUP, 100);
		break;

	case SNK_TRY:
		/* Do nothing, waiting for timeout */
		break;
	case SRC_TRYWAIT:
		/* Do nothing, Waiting for Rd to be detected */
		break;
	case SRC_TRYWAIT_DEBOUNCE:
		tcpm_set_state(port, SRC_TRYWAIT, 0);
		break;
	case SNK_TRY_WAIT_DEBOUNCE:
		/* Do nothing, waiting for PD_DEBOUNCE to do be done */
		break;
	case SNK_TRYWAIT:
		/* Do nothing, waiting for tCCDebounce */
		break;
	case SNK_TRYWAIT_VBUS:
		if (tcpm_port_is_sink(port))
			tcpm_set_state(port, SNK_ATTACHED, 0);
		break;
	case SNK_TRYWAIT_DEBOUNCE:
		/* Do nothing, waiting for Rp */
		break;
	case SRC_TRY_WAIT:
	case SRC_TRY_DEBOUNCE:
		/* Do nothing, waiting for sink detection */
		break;
	default:
		break;
	}
}

static void _tcpm_pd_vbus_off(struct tcpm_port *port)
{
	tcpm_log(port, "_tcpm_pd_vbus_off");
	port->vbus_present = false;
	port->vbus_never_low = false;
	switch (port->state) {
	case SNK_HARD_RESET_SINK_OFF:
		tcpm_set_state(port, SNK_HARD_RESET_WAIT_VBUS, 0);
		break;
	case SRC_HARD_RESET_VBUS_OFF:
		/* Do nothing, waiting for timeout */
		//tcpm_set_state(port, SRC_HARD_RESET_VBUS_ON, 0);
		break;
	case HARD_RESET_SEND:
		break;

	case SNK_TRY:
		/* Do nothing, waiting for timeout */
		break;
	case SRC_TRYWAIT:
		/* Hand over to state machine if needed */
		if (tcpm_port_is_source(port))
			tcpm_set_state(port, SRC_TRYWAIT_DEBOUNCE, 0);
		break;
	case SNK_TRY_WAIT_DEBOUNCE:
		/* Do nothing, waiting for PD_DEBOUNCE to do be done */
		break;
	case SNK_TRYWAIT:
	case SNK_TRYWAIT_VBUS:
	case SNK_TRYWAIT_DEBOUNCE:
		break;
	case SNK_ATTACH_WAIT:
		tcpm_set_state(port, SNK_UNATTACHED, 0);
		break;

	case SNK_NEGOTIATE_CAPABILITIES:
		break;

	case PR_SWAP_SRC_SNK_TRANSITION_OFF:
		tcpm_set_state(port, PR_SWAP_SRC_SNK_SOURCE_OFF, 0);
		break;

	case PR_SWAP_SNK_SRC_SINK_OFF:
		/* Do nothing, expected */
		break;

	case PORT_RESET_WAIT_OFF:
		tcpm_set_state(port, tcpm_default_state(port), 0);
		break;
	case SRC_TRY_WAIT:
	case SRC_TRY_DEBOUNCE:
		/* Do nothing, waiting for sink detection */
		break;
	default:
		if (port->pwr_role == TYPEC_SINK &&
		    port->attached)
			tcpm_set_state(port, SNK_UNATTACHED, 0);
		break;
	}
}


static void tcpm_pd_rx_handler();

struct pd_rx_event _event;
void tcpm_pd_receive(struct tcpm_port *port, const struct pd_message *msg)
{
	struct pd_rx_event *event = &_event;
	event->port = port;
	memcpy(&event->msg, msg, sizeof(*msg));

    port->pd_events |= TCPM_RX_EVENT;
}

/*
 * PD (data, control) command handling functions
 */

static void tcpm_handle_alert(struct tcpm_port *port, const __le32 *payload,
			      int cnt)
{
}

static void tcpm_handle_vdm_request(struct tcpm_port *port,
				    const __le32 *payload, int cnt)
{
}

static void tcpm_pd_ctrl_request(struct tcpm_port *port,
				 const struct pd_message *msg)
{
	enum pd_ctrl_msg_type type = pd_header_type_le(msg->header);
	enum tcpm_state next_state;

	switch (type) {
	case PD_CTRL_GOOD_CRC:
	case PD_CTRL_PING:
		break;
	case PD_CTRL_GET_SOURCE_CAP:
		switch (port->state) {
		case SRC_READY:
			if (port->pd_capable)
				port->caps_count = PD_N_CAPS_COUNT - 1; //JRF Only send one Source Cap after PD is established

			tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0); //JRF: TDA.2.2.9: BMC-PROT-GSC-REC
			break;
		case SNK_READY:
			tcpm_queue_message(port, PD_MSG_DATA_SOURCE_CAP);
			break;
		default:
			tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			break;
		}
		break;
	case PD_CTRL_GET_SINK_CAP:
		switch (port->state) {
		case SRC_SEND_CAPABILITIES:
			tcpm_set_state(port, SOFT_RESET_SEND, 0); //JRF: Invalid, should send SOFTRESET TD.PD.SRC.E14
			break;
		case SNK_READY:
		case SRC_READY:
			tcpm_queue_message(port, PD_MSG_DATA_SINK_CAP);
			break;
		default:
			tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			break;
		}
		break;
	case PD_CTRL_GOTO_MIN:
		break;
	case PD_CTRL_PS_RDY:
		switch (port->state) {
		case SNK_TRANSITION_SINK:
			if (port->vbus_present) {
				tcpm_set_current_limit(port,
						       port->current_limit,
						       port->supply_voltage);
				port->explicit_contract = true;
				tcpm_set_state(port, SNK_READY, 0);
			} else {
				/*
				 * Seen after power swap. Keep waiting for VBUS
				 * in a transitional state.
				 */
				tcpm_set_state(port,
					       SNK_TRANSITION_SINK_VBUS, 0);
			}
			break;
		case PR_SWAP_SRC_SNK_SOURCE_OFF_CC_DEBOUNCED:
			tcpm_set_state(port, PR_SWAP_SRC_SNK_SINK_ON, 0);
			break;
		case PR_SWAP_SNK_SRC_SINK_OFF:
			tcpm_set_state(port, PR_SWAP_SNK_SRC_SOURCE_ON, 0);
			break;
		case VCONN_SWAP_WAIT_FOR_VCONN:
			tcpm_set_state(port, VCONN_SWAP_TURN_OFF_VCONN, 0);
			break;
		default:
			break;
		}
		break;
	case PD_CTRL_REJECT:
	case PD_CTRL_WAIT:
	case PD_CTRL_NOT_SUPP:
		switch (port->state) {
		case SNK_NEGOTIATE_CAPABILITIES:
			/* USB PD specification, Figure 8-43 */
			if (port->explicit_contract)
				next_state = SNK_READY;
			else
				next_state = SNK_WAIT_CAPABILITIES;
			tcpm_set_state(port, next_state, 0);
			break;
		case SNK_NEGOTIATE_PPS_CAPABILITIES:
			/* Revert data back from any requested PPS updates */
			port->pps_data.out_volt = port->supply_voltage;
			port->pps_data.op_curr = port->current_limit;
			port->pps_status = (type == PD_CTRL_WAIT ?
					    -EAGAIN : -EOPNOTSUPP);
			tcpm_set_state(port, SNK_READY, 0);
			break;
		case DR_SWAP_SEND:
			port->swap_status = (type == PD_CTRL_WAIT ?
					     -EAGAIN : -EOPNOTSUPP);
			tcpm_set_state(port, DR_SWAP_CANCEL, 0);
			break;
		case PR_SWAP_SEND:
			port->swap_status = (type == PD_CTRL_WAIT ?
					     -EAGAIN : -EOPNOTSUPP);
			tcpm_set_state(port, PR_SWAP_CANCEL, 0);
			break;
		case VCONN_SWAP_SEND:
			port->swap_status = (type == PD_CTRL_WAIT ?
					     -EAGAIN : -EOPNOTSUPP);
			tcpm_set_state(port, VCONN_SWAP_CANCEL, 0);
			break;
		default:
			break;
		}
		break;
	case PD_CTRL_ACCEPT:
		switch (port->state) {
		case SNK_NEGOTIATE_CAPABILITIES:
			port->pps_data.active = false;
			tcpm_set_state(port, SNK_TRANSITION_SINK, 0);
			break;
		case SNK_NEGOTIATE_PPS_CAPABILITIES:
			port->pps_data.active = true;
			port->supply_voltage = port->pps_data.out_volt;
			port->current_limit = port->pps_data.op_curr;
			tcpm_set_state(port, SNK_TRANSITION_SINK, 0);
			break;
		case SOFT_RESET_SEND:
			port->message_id = 1; // JRF Hack
			port->rx_msgid = -1;
			if (port->pwr_role == TYPEC_SOURCE)
				next_state = SRC_SEND_CAPABILITIES;
			else
				next_state = SNK_WAIT_CAPABILITIES;
			tcpm_set_state(port, next_state, 0);
			break;
		case DR_SWAP_SEND:
			tcpm_set_state(port, DR_SWAP_CHANGE_DR, 0);
			break;
		case PR_SWAP_SEND:
			tcpm_set_state(port, PR_SWAP_START, 0);
			break;
		case VCONN_SWAP_SEND:
			tcpm_set_state(port, VCONN_SWAP_START, 0);
			break;
		default:
			//JRF6
			tcpm_set_state(port, SOFT_RESET_SEND, 0); //JRF: Need to send soft reset for TD.PD.SRC3.E24
			break;
		}
		break;
	case PD_CTRL_SOFT_RESET:
		tcpm_set_state(port, SOFT_RESET, 0);
		break;
	case PD_CTRL_DR_SWAP:
		if (port->port_type != TYPEC_PORT_DRP) {
			tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			break;
		}
		/*
		 * XXX
		 * 6.3.9: If an alternate mode is active, a request to swap
		 * alternate modes shall trigger a port reset.
		 */
		switch (port->state) {
		case SRC_READY:
		case SNK_READY:
			tcpm_set_state(port, DR_SWAP_ACCEPT, 0);
			break;
		default:
			tcpm_queue_message(port, PD_MSG_CTRL_WAIT);
			break;
		}
		break;
	case PD_CTRL_PR_SWAP:
		if (port->port_type != TYPEC_PORT_DRP) {
			if (port->negotiated_rev < PD_REV30) //JRF7
				tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			else
				tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
			break;
		}
		switch (port->state) {
		case SRC_READY:
		case SNK_READY:
			tcpm_set_state(port, PR_SWAP_ACCEPT, 0);
			break;
		default:
			tcpm_queue_message(port, PD_MSG_CTRL_WAIT);
			break;
		}
		break;
	case PD_CTRL_VCONN_SWAP:
		switch (port->state) {
		case SRC_READY:
		case SNK_READY:
			tcpm_set_state(port, VCONN_SWAP_ACCEPT, 0);
			break;
		default:
			tcpm_queue_message(port, PD_MSG_CTRL_WAIT);
			break;
		}
		break;
	case PD_CTRL_GET_PPS_STATUS:
		tcpm_queue_message(port, PD_MSG_PPS_STATUS); /* Added for MAX25432 */
		break;
	case PD_CTRL_GET_SOURCE_CAP_EXT:
	case PD_CTRL_GET_STATUS:
	case PD_CTRL_FR_SWAP:
	case PD_CTRL_GET_COUNTRY_CODES:
		/* Currently not supported */
		tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
		break;
	default:
        if (port->negotiated_rev < PD_REV30)
            tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
        else
            tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
        break;
        
		tcpm_log(port, "Unhandled ctrl message type %#x", type);
		break;
	}
}

static void tcpm_pd_ext_msg_request(struct tcpm_port *port,
				    const struct pd_message *msg)
{
	enum pd_ext_msg_type type = pd_header_type_le(msg->header);
	unsigned int data_size = pd_ext_header_data_size_le(msg->ext_msg.header);
    
    //printf("tcpm_pd_ext_msg_request type %#x\n", type);
    
	if (!(msg->ext_msg.header & PD_EXT_HDR_CHUNKED)) {
		mdelay(42); //Wait tChunkingNotSupported
		tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
		tcpm_log(port, "Unchunked extended messages unsupported");
		return;
	}

	if (data_size > PD_EXT_MAX_CHUNK_DATA) {
		mdelay(42); //Wait tChunkingNotSupported
		tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
		tcpm_log(port, "Chunk handling not yet supported");
		return;
	}

	switch (type) {
	case PD_EXT_STATUS:
		/*
		 * If PPS related events raised then get PPS status to clear
		 * (see USB PD 3.0 Spec, 6.5.2.4)
		 */
		if (msg->ext_msg.data[USB_PD_EXT_SDB_EVENT_FLAGS] &
		    USB_PD_EXT_SDB_PPS_EVENTS)
			tcpm_set_state(port, GET_PPS_STATUS_SEND, 0);
		else
			tcpm_set_state(port, ready_state(port), 0);
		break;
	case PD_EXT_PPS_STATUS:
		/*
		 * For now the PPS status message is used to clear events
		 * and nothing more.
		 */
		tcpm_set_state(port, ready_state(port), 0);
		break;
	case PD_EXT_SOURCE_CAP_EXT:
	case PD_EXT_GET_BATT_CAP:
	case PD_EXT_GET_BATT_STATUS:
	case PD_EXT_BATT_CAP:
	case PD_EXT_GET_MANUFACTURER_INFO:
	case PD_EXT_MANUFACTURER_INFO:
	case PD_EXT_SECURITY_REQUEST:
	case PD_EXT_SECURITY_RESPONSE:
	case PD_EXT_FW_UPDATE_REQUEST:
	case PD_EXT_FW_UPDATE_RESPONSE:
	case PD_EXT_COUNTRY_INFO:
	case PD_EXT_COUNTRY_CODES:
    case PD_EXT_EXTENDED_CONTROL: // Spec requires we reply to EPR_Get_Source_Cap message with "not supported"
        //printf("Unhandled extended message type %#x\n", type);
		tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
		break;
	default:
		tcpm_log(port, "Unhandled extended message type %#x", type);
		break;
	}
}

static void tcpm_pd_data_request(struct tcpm_port *port,
				 const struct pd_message *msg)
{
	enum pd_data_msg_type type = pd_header_type_le(msg->header);
	unsigned int cnt = pd_header_cnt_le(msg->header);
	unsigned int rev = pd_header_rev_le(msg->header);
	unsigned int i;

	switch (type) {
	case PD_DATA_SOURCE_CAP:
		if (port->pwr_role != TYPEC_SINK)
			break;

		for (i = 0; i < cnt; i++)
			port->source_caps[i] = le32_to_cpu(msg->payload[i]);

		port->nr_source_caps = cnt;

		/*
		 * Adjust revision in subsequent message headers, as required,
		 * to comply with 6.2.1.1.5 of the USB PD 3.0 spec. We don't
		 * support Rev 1.0 so just do nothing in that scenario.
		 */
		if (rev == PD_REV10)
			break;

		if (rev < PD_MAX_REV)
			port->negotiated_rev = rev;

		tcpm_negotiated_rev = port->negotiated_rev;

		/*
		 * This message may be received even if VBUS is not
		 * present. This is quite unexpected; see USB PD
		 * specification, sections 8.3.3.6.3.1 and 8.3.3.6.3.2.
		 * However, at the same time, we must be ready to
		 * receive this message and respond to it 15ms after
		 * receiving PS_RDY during power swap operations, no matter
		 * if VBUS is available or not (USB PD specification,
		 * section 6.5.9.2).
		 * So we need to accept the message either way,
		 * but be prepared to keep waiting for VBUS after it was
		 * handled.
		 */
		tcpm_set_state(port, SNK_NEGOTIATE_CAPABILITIES, 0);
		break;
	case PD_DATA_REQUEST:
		if (port->pwr_role != TYPEC_SOURCE ||
		    cnt != 1) {
			tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			break;
		}

		/*
		 * Adjust revision in subsequent message headers, as required,
		 * to comply with 6.2.1.1.5 of the USB PD 3.0 spec. We don't
		 * support Rev 1.0 so just reject in that scenario.
		 */
		if (rev == PD_REV10) {
			tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
			break;
		}

		if (rev < PD_MAX_REV)
			port->negotiated_rev = rev;

		tcpm_negotiated_rev = port->negotiated_rev;

		port->sink_request = le32_to_cpu(msg->payload[0]);
		tcpm_set_state(port, SRC_NEGOTIATE_CAPABILITIES, 0);
		break;
	case PD_DATA_SINK_CAP:
		/* We don't do anything with this at the moment... */
		for (i = 0; i < cnt; i++)
			port->sink_caps[i] = le32_to_cpu(msg->payload[i]);
		port->nr_sink_caps = cnt;
		break;
	case PD_DATA_VENDOR_DEF:
		// We will not handle any VDM request
        if (port->negotiated_rev < PD_REV30) {
            // Ignore VDM message if PD2
            //tcpm_queue_message(port, PD_MSG_CTRL_REJECT);
        }
        else {			
            tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
        }
		break;
	case PD_DATA_BIST: //JRF4
		if ((port->state == SRC_READY || port->state == SNK_READY) && (port->supply_voltage == 5000)) {
			port->bist_request = le32_to_cpu(msg->payload[0]);
			tcpm_set_state(port, BIST_RX, 0);
		}
		break;
	case PD_DATA_ALERT:
		tcpm_handle_alert(port, msg->payload, cnt);
		break;
	case PD_DATA_BATT_STATUS:
	case PD_DATA_GET_COUNTRY_INFO:
    case PD_DATA_EPR_MODE:
		/* Currently unsupported */
		tcpm_queue_message(port, PD_MSG_CTRL_NOT_SUPP);
		break;
    case PD_DATA_EPR_REQUEST:
        tcpm_set_state_cond(port, HARD_RESET_SEND, 0);
	default:
		tcpm_log(port, "Unhandled data message type %#x", type);
		break;
	}
}

static void tcpm_pd_rx_handler()
{
	struct pd_rx_event *event = &_event;
	const struct pd_message *msg = &event->msg;
	unsigned int cnt = pd_header_cnt_le(msg->header);
	struct tcpm_port *port = event->port;

	//printf("PD RX, header: %#x [%d]\n", le16_to_cpu(msg->header), port->attached);

	if (port->attached) {
		enum pd_ctrl_msg_type type = pd_header_type_le(msg->header);
		unsigned int msgid = pd_header_msgid_le(msg->header);

		/*
		 * USB PD standard, 6.7.1.2:
		 * "... if MessageID value in a received Message is the
		 * same as the stored value, the receiver shall return a
		 * GoodCRC Message with that MessageID value and drop
		 * the Message (this is a retry of an already received
		 * Message). Note: this shall not apply to the Soft_Reset
		 * Message which always has a MessageID value of zero."
		 */
		if (msgid == port->rx_msgid && type != PD_CTRL_SOFT_RESET) {
			goto done;
        }
		port->rx_msgid = msgid;

		/*
		 * If both ends believe to be DFP/host, we have a data role
		 * mismatch.
		 */

		if (!!(le16_to_cpu(msg->header) & PD_HEADER_DATA_ROLE) ==
		    (port->data_role == TYPEC_HOST)) {
			tcpm_log(port, "Data role mismatch, initiating error recovery");
			tcpm_set_state(port, ERROR_RECOVERY, 0);
		} else {
			if (msg->header & PD_HEADER_EXT_HDR) {
				tcpm_pd_ext_msg_request(port, msg);
            }
			else if (cnt) {
				tcpm_pd_data_request(port, msg);
            }
			else {
				tcpm_pd_ctrl_request(port, msg);
            }
		}
	}

done:
	return;
}

static void tcpm_init(struct tcpm_port *port)
{
	tcpm_log(port, "tcpm_init");
	enum typec_cc_status cc1, cc2;

	port->tcpc->init(port->tcpc);

	tcpm_reset_port(port);

	/*
	 * XXX
	 * Should possibly wait for VBUS to settle if it was enabled locally
	 * since tcpm_reset_port() will disable VBUS.
	 */

	port->vbus_present = port->tcpc->get_vbus(port->tcpc);
	if (port->vbus_present)
		port->vbus_never_low = true;

	//port->state = INVALID_STATE; // mcm this is required
	tcpm_set_state(port, tcpm_default_state(port), 0);
	port->pd_events = 0;

	if (port->tcpc->get_cc(port->tcpc, &cc1, &cc2) == 0)
		_tcpm_cc_change(port, cc1, cc2);

	/*
	 * Some adapters need a clean slate at startup, and won't recover
	 * otherwise. So do not try to be fancy and force a clean disconnect.
	 */

	tcpm_set_state(port, PORT_RESET, 0);
}

static void _tcpm_pd_hard_reset(struct tcpm_port *port)
{
	//tcpm_log_force(port, "Received hard reset");
	/*
	 * If we keep receiving hard reset requests, executing the hard reset
	 * must have failed. Revert to error recovery if that happens.
	 */
	tcpm_set_state(port,
		       port->hard_reset_count < PD_N_HARD_RESET_COUNT ?
				HARD_RESET_START : ERROR_RECOVERY,
		       0);
}

void tcpm_pd_event_handler_mx(struct tcpm_port *port)
{
	u32 events;

	while (port->pd_events) {
		events = port->pd_events;
		//port->pd_events = 0;
		if (events & TCPM_RESET_EVENT) {
			tcpm_log(port, "***EVENT HANDLER: TCPM_RESET_EVENT");
            if (!tcpm_port_is_disconnected(port)) {
                _tcpm_pd_hard_reset(port);
            }
			port->pd_events &= ~TCPM_RESET_EVENT;
		}
		if (events & TCPM_VBUS_EVENT) {
			bool vbus;
			vbus = port->tcpc->get_vbus(port->tcpc);
			tcpm_log(port, "***EVENT HANDLER: TCPM_VBUS_EVENT");

			if (vbus)
				_tcpm_pd_vbus_on(port);
			else
				_tcpm_pd_vbus_off(port);
			port->pd_events &= ~TCPM_VBUS_EVENT;

		}
		if (events & TCPM_CC_EVENT) {
			tcpm_log(port, "***EVENT HANDLER: TCPM_CC_EVENT");
			enum typec_cc_status cc1, cc2;
			mdelay(5); // Software debounce: wait for BOTH CC to settle before reading status
			if (port->tcpc->get_cc(port->tcpc, &cc1, &cc2) == 0)
				_tcpm_cc_change(port, cc1, cc2);
			port->pd_events &= ~TCPM_CC_EVENT;
		}
        if (events & TCPM_RX_EVENT) {
            tcpm_log(port, "***EVENT HANDLER: TCPM_RX_EVENT");
            tcpm_pd_rx_handler();
            port->pd_events &= ~TCPM_RX_EVENT;
        }
	}

	// Run the state machine
	if (port->run_state_machine_flag)
	{
		tcpm_state_machine_work(port);
		port->run_state_machine_flag = 0;
	}

    if (port->queued_message) {
        queue_work_mx(port);
    }

	// Check if it is time to run a delayed state
	if (port->delayed_state !=INVALID_STATE && get_ms_clk() >= port->delayed_runtime) {
        tcpm_log(port, "event handler delayed event timeout: %s", tcpm_states[port->delayed_state]);
        tcpm_set_state(port, port->delayed_state, 0);
	}
}

// API functions for external control of TCPM

int tcpm_update_source_capabilities(struct tcpm_port *port, const u32 *pdo,
                    unsigned int nr_pdo)
{
    port->nr_src_pdo = tcpm_copy_pdos(src_pdo, pdo, nr_pdo);
    switch (port->state) {
    case SRC_UNATTACHED:
    case SRC_ATTACH_WAIT:
    case SRC_TRYWAIT:
        tcpm_set_cc(port, tcpm_rp_cc(port));
        break;
    case SRC_SEND_CAPABILITIES:
    case SRC_NEGOTIATE_CAPABILITIES:
    case SRC_READY:
    case SRC_WAIT_NEW_CAPABILITIES:
        tcpm_set_cc(port, tcpm_rp_cc(port));
        tcpm_set_state(port, SRC_SEND_CAPABILITIES, 0);
        break;
    default:
        break;
    }

    return 0;
}

uint8_t get_src_pdos(struct tcpm_port *port, u32 *dest_pdo) 
{
    tcpm_copy_pdos(dest_pdo, src_pdo, port->nr_src_pdo);
    return port->nr_src_pdo; // return the number of PDOs
}

void mx_pd_hard_reset_value(struct tcpm_port *port)
{
    port->hard_reset_count = 0; //JRF Hack
	tcpm_set_state_cond(port, HARD_RESET_SEND, 0);
}

void tcpm_pd_disable(struct tcpm_port *port)
{
    port->force_disable_flag = true;
	tcpm_set_state_cond(port, HARD_RESET_SEND, 0);
}