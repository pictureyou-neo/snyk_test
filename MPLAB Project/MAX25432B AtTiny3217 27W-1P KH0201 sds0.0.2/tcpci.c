// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Type-C Port Controller Interface.
 */

/*
 * Modified 2021 by Maxim Integrated for MAX25432
 */

#include <stdlib.h> /* needed for malloc */
#include <string.h> /* needed for memcpy */

#include "tcpm.h"
#include "pd_bdo.h" /* needed for constants  in tcpci.c */
#include <errno.h> /* error constants used in tcpci.c */
#include "irqreturn.h" /* needed for irqreturn_t typedef */

#include "tcpci_deps.h"
#include "tcpci.h"
#include "mx_time.h"

#define PD_RETRY_COUNT 2

uint16_t AUTO_CDP_DCP_MODE = 0b01;

//#define USE_100W_PDOS
//#define USE_33W_PDOS
#define USE_27W_PDOS

#ifdef USE_27W_PDOS
#define NUM_PDOS 3
static const u32 src_pdo_3A[] = {
	PDO_FIXED(5000,  3000, 0x04000000),
	PDO_FIXED(9000,  3000, 0),
    PDO_PPS_APDO(3300, 11000, 3000, 0),
};

static const u32 src_pdo_5A[] = {
	PDO_FIXED(5000,  3000, 0x04000000),
	PDO_FIXED(9000,  3000, 0),
    PDO_PPS_APDO(3300, 11000, 3000, 0),
};

u32 src_pdo[] = {
	PDO_FIXED(5000,  3000, 0x04000000),
	PDO_FIXED(9000,  3000, 0),
    PDO_PPS_APDO(3300, 11000, 3000, 0),
};
#endif

#ifdef USE_33W_PDOS
#define NUM_PDOS 5
static const u32 src_pdo_3A[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 2200, 0),
	PDO_PPS_APDO(3300, 11000, 3000, 0),
    PDO_PPS_APDO(3300, 16000, 2200, 0),
};

static const u32 src_pdo_5A[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 2200, 0),
	PDO_PPS_APDO(3300, 11000, 3000, 0),
    PDO_PPS_APDO(3300, 16000, 2200, 0),
};

u32 src_pdo[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 2200, 0),
	PDO_PPS_APDO(3300, 11000, 3000, 0),
    PDO_PPS_APDO(3300, 16000, 2200, 0),
};
#endif

#ifdef USE_100W_PDOS
#define NUM_PDOS 5
static const u32 src_pdo_3A[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 3000, 0),
	PDO_FIXED(20000, 3000, 0),
	PDO_PPS_APDO(3300, 21000, 3000, 0),

};

static const u32 src_pdo_5A[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 3000, 0),
	PDO_FIXED(20000, 5000, 0),
	PDO_PPS_APDO(3300, 21000, 5000, 0),

};

u32 src_pdo[] = {
	PDO_FIXED(5000,  3000, 0),
	PDO_FIXED(9000,  3000, 0),
	PDO_FIXED(15000, 3000, 0),
	PDO_FIXED(20000, 3000, 0),
	PDO_PPS_APDO(3300, 21000, 3000, 0),
};
#endif

static const u32 snk_pdo[] = {
	PDO_FIXED(5000, 500, 0),
};

struct tcpci {

	struct tcpm_port *port;

	struct regmap *regmap;

	bool controls_vbus;

	struct tcpc_dev tcpc;
	struct tcpci_data *data;
};

static inline struct tcpci *tcpc_to_tcpci(struct tcpc_dev *tcpc)
{
	return container_of(tcpc, struct tcpci, tcpc);
}

static int tcpci_read16(struct tcpci *tcpci, unsigned int reg, u16 *val)
{
	uint8_t _val[2];
	int res;
	res = regmap_raw_read(tcpci->regmap, reg, _val, 2);
	*val = _val[0] | ((_val[1] << 8) & 0xFF00);
	return res;
}

static int tcpci_write16(struct tcpci *tcpci, unsigned int reg, u16 _val)
{
	uint8_t val[2];
	val[0] = _val;
	val[1] = _val>>8;
	return regmap_raw_write(tcpci->regmap, reg, &val, sizeof(u16));
}

static int tcpci_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	//print("in tcpci_set_cc cc=%d\n", cc);
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	switch (cc) {
	case TYPEC_CC_RA:
		reg = (TCPC_ROLE_CTRL_CC_RA << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_RA << TCPC_ROLE_CTRL_CC2_SHIFT);
		break;
	case TYPEC_CC_RD:
		reg = (TCPC_ROLE_CTRL_CC_RD << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_RD << TCPC_ROLE_CTRL_CC2_SHIFT);
		break;
	case TYPEC_CC_RP_DEF:
		reg = (TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC2_SHIFT) |
			(TCPC_ROLE_CTRL_RP_VAL_DEF <<
			 TCPC_ROLE_CTRL_RP_VAL_SHIFT);
		break;
	case TYPEC_CC_RP_1_5:
		reg = (TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC2_SHIFT) |
			(TCPC_ROLE_CTRL_RP_VAL_1_5 <<
			 TCPC_ROLE_CTRL_RP_VAL_SHIFT);
		break;
	case TYPEC_CC_RP_3_0:
		reg = (TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_RP << TCPC_ROLE_CTRL_CC2_SHIFT) |
			(TCPC_ROLE_CTRL_RP_VAL_3_0 <<
			 TCPC_ROLE_CTRL_RP_VAL_SHIFT);
		break;
	case TYPEC_CC_OPEN:
	default:
		reg = (TCPC_ROLE_CTRL_CC_OPEN << TCPC_ROLE_CTRL_CC1_SHIFT) |
			(TCPC_ROLE_CTRL_CC_OPEN << TCPC_ROLE_CTRL_CC2_SHIFT);
		break;
	}

	ret = regmap_write(tcpci->regmap, TCPC_ROLE_CTRL, reg);
	if (ret < 0)
		return ret;

	return 0;
}

static enum typec_cc_status tcpci_to_typec_cc(unsigned int cc, bool sink)
{
	switch (cc) {
	case 0x1:
		return sink ? TYPEC_CC_RP_DEF : TYPEC_CC_RA;
	case 0x2:
		return sink ? TYPEC_CC_RP_1_5 : TYPEC_CC_RD;
	case 0x3:
		if (sink)
			return TYPEC_CC_RP_3_0;
		/* fall through */
	case 0x0:
	default:
		return TYPEC_CC_OPEN;
	}
}

static int tcpci_get_cc(struct tcpc_dev *tcpc,
			enum typec_cc_status *cc1, enum typec_cc_status *cc2)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_CC_STATUS, &reg);
	if (ret < 0)
		return ret;

	*cc1 = tcpci_to_typec_cc((reg >> TCPC_CC_STATUS_CC1_SHIFT) &
				 TCPC_CC_STATUS_CC1_MASK,
				 reg & TCPC_CC_STATUS_TERM);
	*cc2 = tcpci_to_typec_cc((reg >> TCPC_CC_STATUS_CC2_SHIFT) &
				 TCPC_CC_STATUS_CC2_MASK,
				 reg & TCPC_CC_STATUS_TERM);

	return 0;
}

static int tcpci_set_polarity(struct tcpc_dev *tcpc,
			      enum typec_cc_polarity polarity)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	int ret;

	ret = regmap_write(tcpci->regmap, TCPC_TCPC_CTRL,
			   (polarity == TYPEC_POLARITY_CC2) ?
			   TCPC_TCPC_CTRL_ORIENTATION : 0);
	if (ret < 0)
		return ret;

	return 0;
}


static int tcpci_set_bist_test_mode(struct tcpc_dev *tcpc,
			      enum bist_test_mode mode, enum typec_cc_polarity polarity)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_TCPC_CTRL, &reg);

	ret |= regmap_write(tcpci->regmap, TCPC_TCPC_CTRL, (mode == BIST_ENABLED) ?
			   reg | TCPC_TCPC_CTRL_BIST_TEST_MODE : reg & ~TCPC_TCPC_CTRL_BIST_TEST_MODE);

	return ret;
}

static int tcpci_set_vconn(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);

	ret |= regmap_write(tcpci->regmap, TCPC_POWER_CTRL,
			   enable ? reg | TCPC_POWER_CTRL_VCONN_ENABLE : reg & ~TCPC_POWER_CTRL_VCONN_ENABLE);
	if (ret < 0)
		return ret;

	return 0;
}

static int tcpci_set_roles(struct tcpc_dev *tcpc, bool attached,
			   enum typec_role role, enum typec_data_role data)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	reg = PD_REV20 << TCPC_MSG_HDR_INFO_REV_SHIFT;
	if (role == TYPEC_SOURCE)
		reg |= TCPC_MSG_HDR_INFO_PWR_ROLE;
	if (data == TYPEC_HOST)
		reg |= TCPC_MSG_HDR_INFO_DATA_ROLE;
	ret = regmap_write(tcpci->regmap, TCPC_MSG_HDR_INFO, reg);
	if (ret < 0)
		return ret;
	return 0;
}

static int tcpci_set_pd_rx(struct tcpc_dev *tcpc, bool enable)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg = 0;
	int ret, i;

	if (enable)
		reg = TCPC_RX_DETECT_SOP | TCPC_RX_DETECT_SOP_PRIME | TCPC_RX_DETECT_HARD_RESET;

	//print("Writing TCPC_RX_DETECT = 0x%02X", reg);

	ret = regmap_write(tcpci->regmap, TCPC_RX_DETECT, reg);

	if (ret < 0)
		return ret;

	if (enable) //restore alert mask after hard reset
	{
		//mdelay(100); //JRF wait for RX to power up
		reg = TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_FAILED |
			TCPC_ALERT_TX_DISCARDED | TCPC_ALERT_RX_STATUS | TCPC_ALERT_FAULT |
			TCPC_ALERT_RX_HARD_RST | TCPC_ALERT_CC_STATUS | TCPC_ALERT_VENDOR;
		if (tcpci->controls_vbus)
			reg |= TCPC_ALERT_POWER_STATUS;

		return tcpci_write16(tcpci, TCPC_ALERT_MASK, reg);

		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tcpci_get_vbus(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret;

	ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
	if (ret < 0)
		return ret;

	//print("TCPCI: TCPC_POWER_STATUS = 0x%02x", reg);

	return !!(reg & TCPC_POWER_STATUS_VBUS_PRES);
}

void set_rp(struct tcpci *tcpci, uint32_t rp_current)
{
	unsigned int reg;
	regmap_read(tcpci->regmap, TCPC_ROLE_CTRL, &reg);

	switch (rp_current) {

	case 1500:
		reg = (reg & ~TCPC_ROLE_CTRL_RP_VAL_MASK) | (TCPC_ROLE_CTRL_RP_VAL_1_5 << TCPC_ROLE_CTRL_RP_VAL_SHIFT);
		break;
	case 3000:
		reg = (reg & ~TCPC_ROLE_CTRL_RP_VAL_MASK) | (TCPC_ROLE_CTRL_RP_VAL_3_0 << TCPC_ROLE_CTRL_RP_VAL_SHIFT);
		break;
	default:
		reg = (reg & ~TCPC_ROLE_CTRL_RP_VAL_MASK) | (TCPC_ROLE_CTRL_RP_VAL_DEF << TCPC_ROLE_CTRL_RP_VAL_SHIFT);
	}
	regmap_write(tcpci->regmap, TCPC_ROLE_CTRL, reg);

}

void set_ilim(struct tcpci *tcpci, uint32_t curent)
{
	uint16_t ilim = curent / 25; // Convert to 25mA units
    	regmap_write(tcpci->regmap, TCPC_VBUS_ILIM_SET, ilim);
}

u8 buck_boost_setup_reg = 0x74;
u8 general_setup_reg = 0x00;


void set_slp(struct tcpci *tcpci, u32 voltage)
{

	static u8 last_slp = 0xFF;

	// Find slope compensation
	u8 new_slp;
	u16 vbus_alarm_hi, vbus_alarm_lo;

	u16 vbus_alarm_hysteresis = 200;
	if (voltage < 6000) {
		new_slp = VOUT_SLP_5V;
		vbus_alarm_lo = 0;
		vbus_alarm_hi = (6000+vbus_alarm_hysteresis)/25;
	}
	else if (voltage >= 6000 && voltage < 10000) {
		new_slp = VOUT_SLP_9V;
		vbus_alarm_lo = (6000-vbus_alarm_hysteresis)/25;
		vbus_alarm_hi = (10000+vbus_alarm_hysteresis)/25;
	}
	else if (voltage >= 10000 && voltage < 16000) {
		new_slp = VOUT_SLP_15V;
		vbus_alarm_lo = (10000-vbus_alarm_hysteresis)/25;
		vbus_alarm_hi = (16000+vbus_alarm_hysteresis)/25;
	}
	else if (voltage >= 16000) {
		new_slp = VOUT_SLP_20V;
		vbus_alarm_lo = (16000-vbus_alarm_hysteresis)/25;
		vbus_alarm_hi = (25000+vbus_alarm_hysteresis)/25;
	}

#ifdef DYNAMIC_SLP
	// Set vbus alarms
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_L, vbus_alarm_hi & 0xFF);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_H, (vbus_alarm_hi>>8) & 0x03);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_L, vbus_alarm_lo & 0xFF);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_H, (vbus_alarm_lo>>8) & 0x03);

	// Enable vbus alarms
	u16 reg16;
	tcpci_read16(tcpci, TCPC_ALERT_MASK, &reg16);
	tcpci_write16(tcpci, TCPC_ALERT_MASK, reg16 | TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_V_ALARM_HI);
#endif
	// Set SLP
	if (new_slp != last_slp) {
		buck_boost_setup_reg = buck_boost_setup_reg & ~TCPC_BUCK_BOOST_SETUP_SLP_MASK;
        	buck_boost_setup_reg |= new_slp << TCPC_BUCK_BOOST_SETUP_SLP_SHIFT;
		regmap_write(tcpci->regmap, TCPC_BUCK_BOOST_SETUP, buck_boost_setup_reg);
	}
	last_slp = new_slp;
}

void enable_vbus_alarms(struct tcpci *tcpci, u32 alarm_lo_mv, u32 alarm_hi_mv)
{
    u16 vbus_alarm_hi, vbus_alarm_lo;

	// Set vbus alarms
	vbus_alarm_lo = (alarm_lo_mv)/25;
	vbus_alarm_hi = (alarm_hi_mv)/25;
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_L, vbus_alarm_hi & 0xFF);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_H, (vbus_alarm_hi>>8) & 0x03);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_L, vbus_alarm_lo & 0xFF);
	regmap_write(tcpci->regmap, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_H, (vbus_alarm_lo>>8) & 0x03);
	// Enable vbus alarms
	u16 reg16;
	tcpci_read16(tcpci, TCPC_ALERT_MASK, &reg16);
	tcpci_write16(tcpci, TCPC_ALERT_MASK, reg16 | TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_V_ALARM_HI);
	// Enable voltage alarms power status reporting
	unsigned int reg;
	regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
	regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg & ~TCPC_POWER_CTRL_VOLT_ALRMS_EN);
}

void disable_vbus_alarms(struct tcpci *tcpci)
{
	// Mask vbus alarm interrupt
	u16 reg16;
	tcpci_read16(tcpci, TCPC_ALERT_MASK, &reg16);
	tcpci_write16(tcpci, TCPC_ALERT_MASK, reg16 & ~(TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_V_ALARM_HI));

    unsigned int reg = 0, reg_verify = 0;
	volatile int i = 0;
	while (1)	{
		// Disable voltage alarms power status reporting
		regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
        reg |= TCPC_POWER_CTRL_VOLT_ALRMS_EN;
		regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg);
		// The POWER_CONTROL register update is blocked in an ongoing Type-A fault event until it clears
		regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg_verify);
		if ((reg & 0x000000FF) == (reg_verify & 0x000000FF)) {
			break; // write and readback match
		}
		if (i++ == 100) {
			break; // write attempts timed out
		}
	}
	// Clear vbus alarm interrupt bit
	tcpci_write16(tcpci, TCPC_ALERT, (TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_V_ALARM_HI));
}

static int tcpci_set_vbus(struct tcpc_dev *tcpc, bool source, bool sink)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned int reg;
	int ret, i;

	// Mask interrupts so set_vbus is not interrupted
	u16 tcpc_alert_mask;
	tcpci_read16(tcpci, TCPC_ALERT_MASK, &tcpc_alert_mask);
	tcpci_write16(tcpci, TCPC_ALERT_MASK, 0x0000);
    
    // Ensure vbus alarms are disabled
	regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
	regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg | TCPC_POWER_CTRL_VOLT_ALRMS_EN);

	/* Disable both source and sink first before enabling anything */

	if (!source) {
		//disable_vbus_alarms(tcpci);

		////printk("Reinit PDOs to defaults");
		for(i=0;i<NUM_PDOS;i++) {
			src_pdo[i] = src_pdo_3A[i];
			////printk("   src_pdo[%d] = 0x%08x",i,src_pdo[i]);
		}

		////printk("Writing COMMAND = DISABLE_SRC_VBUS");

		ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);

		if (reg & TCPC_POWER_STATUS_SRC_VBUS)
		{
			// Hard reset sequence

			// DVS down to 3.3V (SPT3 fix for capacitor DA)
			uint16_t vbus_code;
			if (USE_VBUS_HIRES_MODE) {
				vbus_code = 330; // 0x14A
			}
			else {
				vbus_code = 165; // 0xA5
			}
			ret = regmap_write(tcpci->regmap, TCPC_VBUS_NONDEFAULT_TARGET_L, vbus_code & 0x00FF);
			ret = regmap_write(tcpci->regmap, TCPC_VBUS_NONDEFAULT_TARGET_H, vbus_code >> 8);
			ret = regmap_write(tcpci->regmap, TCPC_COMMAND, TCPC_CMD_SRC_VBUS_HIGH);

			mdelay(250);

			//Disable VBUS
			ret |= regmap_write(tcpci->regmap, TCPC_COMMAND, TCPC_CMD_DISABLE_SRC_VBUS);

			//Force Discharge
			ret |= regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
			ret |= regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg | TCPC_POWER_CTRL_FORCE_DSH);

			mdelay(100);
		}
		else {
			// This is a disconnect, wait for discharge
			mdelay(250);
		}

		//Disable Auto Discharge and Force Discharge
		ret |= regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
		ret |= regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg & ~(TCPC_POWER_CTRL_AUTO_DSH | TCPC_POWER_CTRL_FORCE_DSH));

		//AE11: ret |= regmap_write(tcpci->regmap, 0x80, 0x00); // 2.4A / 5V (default)

		////printk("TCPCI: Force Discharge = 1");

		if (ret < 0)
			return ret;
	}

	if (!sink) {
		// Do not send DisbleSinkVbus as we are a source only.
		/*
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_DISABLE_SINK_VBUS);
		if (ret < 0)
			return ret;
		*/
	}

	if (source) {

		// #1: JRF, Must set up discharge before turning on VBUS
		ret |= regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);

		ret |= regmap_write(tcpci->regmap, TCPC_POWER_CTRL, (reg & ~TCPC_POWER_CTRL_FORCE_DSH) | TCPC_POWER_CTRL_AUTO_DSH);

		////printk("TCPCI: Force Discharge = 0");

		// Unset CL bit
		ret |= regmap_write(tcpci->regmap, TCPC_GENERAL_SETUP, general_setup_reg & ~TCPC_GENERAL_SETUP_CL_EN); // Unset CL bit
        
		// Set Rp
		set_rp(tcpci, DEFAULT_RP_CURRENT);

		// Set ILIM
		set_ilim(tcpci, DEFAULT_RP_CURRENT + ILIM_HEADROOM);

		// Set SLP
		set_slp(tcpci, 5000);

		// UA01 set default voltage
        //printf("SrcVbusDef\n");
		regmap_write(tcpci->regmap, TCPC_COMMAND, TCPC_CMD_SRC_VBUS_DEFAULT);

		if (ret < 0)
			return ret;
	}

	if (sink) {
		ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
				   TCPC_CMD_SINK_VBUS);
		if (ret < 0)
			return ret;
	}
	// Enable alarms
	tcpci_write16(tcpci, TCPC_ALERT_MASK, tcpc_alert_mask);

	return 0;
}

static int tcpci_set_vbus_hv(struct tcpc_dev *tcpc, u32 voltage, u32 op_curr, u8 pdo_type)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);

	unsigned int reg;
	int ret;
	static u8 last_pdo_type = 0xFF;
	//static u8 last_slp = 0xFF;


	// Calculate UA01 vbus code with offset
	uint16_t vbus_code;
	if (USE_VBUS_HIRES_MODE) {
		vbus_code = (voltage/10) - ((304*voltage)/100000 - 10);
	}
	else {
		vbus_code = (voltage/20) - ((305*voltage)/200000 - 5);
	}

    ret = regmap_write(tcpci->regmap, TCPC_VBUS_NONDEFAULT_TARGET_L, vbus_code & 0x00FF);
    ret = regmap_write(tcpci->regmap, TCPC_VBUS_NONDEFAULT_TARGET_H, vbus_code >> 8);

	// UA01 Set current
	if (pdo_type == PDO_TYPE_FIXED) {
		set_ilim(tcpci, op_curr + ILIM_HEADROOM); // Add additional ILIM if this is a fixed PDO
	}
	else {
        set_ilim(tcpci, op_curr + 25); //MB addition
	}

	// Set CL bit
	if (pdo_type != last_pdo_type)
	{
		if (pdo_type == PDO_TYPE_APDO) {
			// Set CL bit
			ret = regmap_write(tcpci->regmap, TCPC_GENERAL_SETUP, general_setup_reg | TCPC_GENERAL_SETUP_CL_EN);
			// SPT7 vPpsShutdown
			enable_vbus_alarms(tcpci, 2950, 25000);
		}
		else {
			// Unset CL bit
			ret = regmap_write(tcpci->regmap, TCPC_GENERAL_SETUP, general_setup_reg & ~TCPC_GENERAL_SETUP_CL_EN);
			// Disable vbus alarms
			disable_vbus_alarms(tcpci);
		}
	}
	last_pdo_type = pdo_type;

	// Write UA01 command:
    //printf("SrcVbusHv\n");
	ret = regmap_write(tcpci->regmap, TCPC_COMMAND, TCPC_CMD_SRC_VBUS_HIGH);

	// Set slope compensation
	set_slp(tcpci, voltage);

	if (ret < 0)
		return ret;

	return 0;

}

// Cable discovery is handled by a the pd_transmit function
static int tcpci_cable_disc(struct tcpc_dev *tcpc)
{
	return -1;
}

extern unsigned int tcpm_negotiated_rev;

static int tcpci_pd_transmit(struct tcpc_dev *tcpc,
			     enum tcpm_transmit_type type,
			     const struct pd_message *msg)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	struct tcpm_port *port = tcpci->port;
	u16 header = msg ? le16_to_cpu(msg->header) : 0;
	unsigned int reg, cnt;
	int ret, i;

	u8 transmit_buffer[32];

	cnt = msg ? pd_header_cnt(header) * 4 : 0; // cnt is number of data objects * 4

	transmit_buffer[0] = cnt + 2; // TCPC_TX_BYTE_CNT

	transmit_buffer[1] = (header & 0x00FF); // TCPC_TX_HDR

	transmit_buffer[2] = ((header >> 8) & 0x00FF); // TCPC_TX_HDR

	memcpy(&transmit_buffer[3],&msg->payload,cnt); // TCPC_TRANSMIT

	ret = regmap_raw_write(tcpci->regmap, TCPC_TX_DATA, &transmit_buffer, (cnt+3));

	//for (i=0;i<(cnt+3);i++)
	   ////print("TCPC_TRANSMIT[%d] = 0x%02x", i, transmit_buffer[i]);

	//if (type == 0x5)
	//	//print("TCPCI: Transmit Hard Reset");

	////print("tcpm_negotiated_rev = %d",tcpm_negotiated_rev);
	if (tcpm_negotiated_rev < PD_REV30)
		reg = ((PD_RETRY_COUNT+1) << TCPC_TRANSMIT_RETRY_SHIFT) | (type << TCPC_TRANSMIT_TYPE_SHIFT);
	else
		reg = (PD_RETRY_COUNT << TCPC_TRANSMIT_RETRY_SHIFT) | (type << TCPC_TRANSMIT_TYPE_SHIFT);

	ret = regmap_write(tcpci->regmap, TCPC_TRANSMIT, reg);
	if (ret < 0)
		return ret;

	return 0;
}


static int tcpci_init(struct tcpc_dev *tcpc)
{
	struct tcpci *tcpci = tcpc_to_tcpci(tcpc);
	unsigned long timeout = get_ms_clk() + 2000; /* XXX */
	unsigned int reg;
	int ret;

	while (time_before_eq(get_ms_clk(), timeout)) {
		ret = regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
		if (ret < 0)
			return ret;
		if (!(reg & TCPC_POWER_STATUS_UNINIT))
			break;
		usleep_range(10000, 20000);
	}
	if (time_after(get_ms_clk(), timeout))
		return -ETIMEDOUT;

	/* Handle vendor init */
	/*
	if (tcpci->data->init) {
		ret = tcpci->data->init(tcpci, tcpci->data);
		if (ret < 0)
			return ret;
	}
	*/
	/* Clear all events */
	//ret = tcpci_write16(tcpci, TCPC_ALERT, 0xffff);
	//if (ret < 0)
	//	return ret;

	if (tcpci->controls_vbus) {
		reg = TCPC_POWER_STATUS_VBUS_PRES;
    }
	else {
		reg = 0;
    }
	ret = regmap_write(tcpci->regmap, TCPC_POWER_STATUS_MASK, reg);
	if (ret < 0)
		return ret;

	/* Enable Vbus detection */
	ret = regmap_write(tcpci->regmap, TCPC_COMMAND,
			   TCPC_CMD_ENABLE_VBUS_DETECT);
	if (ret < 0)
		return ret;

	// Disable Alerts First
	tcpci_write16(tcpci, TCPC_ALERT_MASK, 0);

	// Enable ADC
	regmap_read(tcpci->regmap, TCPC_POWER_CTRL, &reg);
	regmap_write(tcpci->regmap, TCPC_POWER_CTRL, reg & ~TCPC_POWER_CTRL_VBUS_VOLT_MON_EN);

	////printk("Writing VENDOR_STATUS_MASK");
	reg = 0xFF;
	ret = regmap_write(tcpci->regmap, TCPC_VENDOR_STATUS_MASK, TCPC_VENDOR_STATUS_MASK_SHIELDING | TCPC_VENDOR_STATUS_MASK_OMF_TRANS);

	general_setup_reg = 0x00 | (AUTO_CDP_DCP_MODE & TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK);
	// 11-bit resolution mode (and CL disabled on startup)
	if (USE_VBUS_HIRES_MODE)
		general_setup_reg = general_setup_reg | TCPC_GENERAL_SETUP_VBUS_HIRES;

	//Set VIN UV limit to 4.5A
	ret = regmap_write(tcpci->regmap, TCPC_IN_THRESH, IN_UV_THRESH);

	// Set cable comp GAIN
	ret = regmap_write(tcpci->regmap, TCPC_CABLE_COMP_CONTROL, CABLE_COMP_GAIN);

	// BUCK_BOOST_SETUP
	buck_boost_setup_reg = 0x74; // BUCK_BOOST_SETUP default
	buck_boost_setup_reg = (buck_boost_setup_reg & ~TCPC_BUCK_BOOST_SETUP_SS_SEL) | SS_SEL;
	buck_boost_setup_reg = (buck_boost_setup_reg & ~TCPC_BUCK_BOOST_SETUP_FSW_MASK) | (FSW << TCPC_BUCK_BOOST_SETUP_FSW_SHIFT);
	buck_boost_setup_reg = (buck_boost_setup_reg & ~TCPC_BUCK_BOOST_SETUP_SLP_MASK) | (VOUT_SLP_5V << TCPC_BUCK_BOOST_SETUP_SLP_SHIFT);
	ret = regmap_write(tcpci->regmap, TCPC_BUCK_BOOST_SETUP, buck_boost_setup_reg);

	// Disable UA01 internal OVP and OCP
	//regmap_write(tcpci->regmap, TCPC_FAULT_CTRL, 0x06);

	// Set VBUS STOP discharge threshold to 0.5V
	tcpci_write16(tcpci, TCPC_VBUS_STOP_DISCHARGE_THRESH, 0x0000);
    
    // Set VCONN OCP and UV
	regmap_write(tcpci->regmap, TCPC_VCONN_THRESH, (VCONN_OCPL_SEL << TCPC_VCONN_OCPL_SEL_SHIFT) | (VCONN_IN_UV_THRESH << TCPC_VCONN_IN_UV_THRESH_SHIFT));

	// Enable alerts
	reg = TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_FAILED |
		TCPC_ALERT_TX_DISCARDED | TCPC_ALERT_RX_STATUS | TCPC_ALERT_FAULT |
		TCPC_ALERT_RX_HARD_RST | TCPC_ALERT_CC_STATUS | TCPC_ALERT_VENDOR;
	if (tcpci->controls_vbus)
		reg |= TCPC_ALERT_POWER_STATUS;
	return tcpci_write16(tcpci, TCPC_ALERT_MASK, reg);
}

u8 store[32];
irqreturn_t tcpci_irq(struct tcpci *tcpci)
{
	u16 status;

	tcpci_read16(tcpci, TCPC_ALERT, &status);
    //printf("ALERT = 0x%04x\n", status);

	if (status & TCPC_ALERT_CC_STATUS) {
		tcpm_cc_change(tcpci->port);
    }

	if (status & TCPC_ALERT_POWER_STATUS) {
		unsigned int reg;

		//regmap_read(tcpci->regmap, TCPC_POWER_STATUS_MASK, &reg);
		//printk("TCPC_POWER_STATUS_MASK = 0x%02x", reg);
        //regmap_read(tcpci->regmap, TCPC_POWER_STATUS, &reg);
		//printf("TCPC_POWER_STATUS = 0x%02x\n", reg);        

		//if (reg != 0xff)
			tcpm_vbus_change(tcpci->port);
	}

	if (status & TCPC_ALERT_FAULT) {
		unsigned int reg;

		regmap_read(tcpci->regmap, TCPC_FAULT_STATUS, &reg);
		regmap_write(tcpci->regmap, TCPC_FAULT_STATUS, reg);

		//printk("TCPCI: TCPC_ALERT_FAULT = 0x%02x",reg);

		if (reg & TCPC_ALL_REG_RST) {
			//printk("TCPC: POR RESET Detected");
			tcpm_tcpc_reset(tcpci->port);
		}

		//if (reg & 0x80)  		printk("\nAE11 TCPC FAULT (%s) detected\n", "ALL_REG_RST");
		//if (reg & 0x40)  		printk("\nAE11 TCPC FAULT (%s) detected\n", "FORC_OFF_VBUS");
		//if (reg & 0x20)  		printk("\nAE11 TCPC FAULT (%s) detected\n", "AUTO_DISCH_FAIL");
		//if (reg & 0x10)  		printk("\nAE11 TCPC FAULT (%s) detected\n", "FORCE_DISH_FAIL");
		//if (reg & TCPC_FAULT_VBUS_OCP)  printk("\nAE11 TCPC FAULT (%s) detected\n", "VBUS_OCP");
		//if (reg & TCPC_FAULT_VBUS_OVP)  printk("\nAE11 TCPC FAULT (%s) detected\n", "VBUS_OVP");
		//if (reg & TCPC_FAULT_VCONN_OCP) printk("\nAE11 TCPC FAULT (%s) detected\n", "VCONN_OCP");
		//if (reg & TCPC_FAULT_I2C_ERR)   printk("\nAE11 TCPC FAULT (%s) detected\n", "I2C_ERR");

	}

	if (status & TCPC_ALERT_VENDOR) {

		unsigned int fault;
		regmap_read(tcpci->regmap, TCPC_VENDOR_STATUS, &fault);
		regmap_write(tcpci->regmap, TCPC_VENDOR_STATUS, fault); // clear faults
		//printk("TCPCI: TCPC_ALERT_VENDOR");

		if (fault & TCPC_VENDOR_STATUS_SHIELDING) {
			//printk("\nAE11 Vendor FAULT (%s) detected\n", "SHIELDING");
		}

		if (fault & TCPC_VENDOR_STATUS_OMF_TRANS) {
            unsigned int reg_ilim;
			//printk("\nAE11 Vendor FAULT (%s) detected\n", "OMF_TRANS");
			unsigned int fault_auto_shield_status;
			// Read the CL/CV status
			regmap_read(tcpci->regmap, TCPC_AUTO_SHIELD_STATUS1, &fault_auto_shield_status);

			if (fault_auto_shield_status & TCPC_AUTO_SHIELD_STATUS1_CL_CV) {
				//printk("\nCL MODE");
				tcpm_pd_set_cl_cv_status(tcpci->port, 1);
			}
			else {
				//printk("\nCV MODE");
				tcpm_pd_set_cl_cv_status(tcpci->port, 0);
			}
		}
	}

#ifdef DYNAMIC_SLP
	if ((status & TCPC_ALERT_V_ALARM_LO) || (status & TCPC_ALERT_V_ALARM_HI)) {
		unsigned int vbus_lo, vbus_hi;
		regmap_read(tcpci->regmap, TCPC_VBUS_VOLTAGE_L, &vbus_lo);
		regmap_read(tcpci->regmap, TCPC_VBUS_VOLTAGE_H, &vbus_hi);
		u32 vbus = vbus_lo | ((vbus_hi & 0x03) << 8);
		vbus *= 25; // VBUS_VOLTAGE result is 25mV LSB
		set_slp(tcpci, vbus);
	}
#endif

	// Handle vPpsShutdown event
	if (status & TCPC_ALERT_V_ALARM_LO) {
		disable_vbus_alarms(tcpci);
		tcpm_pd_hard_reset(tcpci->port);
	}

	/*
	 * Clear alert status for everything except RX_STATUS, which shouldn't
	 * be cleared until we have successfully retrieved message.
	 */
    if (status & ~TCPC_ALERT_RX_STATUS) {
		////printk("Clearing Alerts by writing = 0x%04x", status & ~TCPC_ALERT_RX_STATUS);
		tcpci_write16(tcpci, TCPC_ALERT, status & ~TCPC_ALERT_RX_STATUS);
    }

	if (status & TCPC_ALERT_RX_STATUS) {
		struct pd_message msg;
		u8 cnt, type; // JRF: Fix size
		u16 header;
		enum pd_data_msg_type msg_type;
		u32 bist_request, cable_vdo;
		unsigned int reg;
		int ret, i;

		//u8 store[32];
        //printf("ALERT_RX_STATUS\n");

		regmap_raw_read(tcpci->regmap, TCPC_RX_BYTE_CNT, &store, 32);

		cnt = store[0];
		type = store[1];
		header = store[2] + (store[3] << 8);

		//printf("cnt = %d, type = 0x%02x, header = 0x%04x\n", cnt, type, header);
        
        // Verify that a valid RX_BYTE_CNT was read before doing memory operations
        if (cnt < 3) {
            //printf("ERROR\n");
            tcpci_write16(tcpci, TCPC_ALERT, TCPC_ALERT_RX_STATUS);
            return IRQ_HANDLED;
        }
        
		msg.header = cpu_to_le16(header);

		memcpy(&msg.payload,&store[4],(cnt-3));

		//for(i=0;i<(cnt-3)/4;i++)
		//	//printk("msg.payload[%d] = 0x%08x",i,msg.payload[i]);

		msg_type = pd_header_type_le(msg.header);

		bist_request = le32_to_cpu(msg.payload[0]);

        //printf("msg_type = %d, bist_request = 0x%08x, BDO_MASK() = 0x%08x\n", msg_type, bist_request, BDO_MODE_MASK(bist_request));
        //printf("bist_request >> 16 = 0x%08x\n", BDO_MODE_MASK(bist_request)>>16);
		if (type == TCPC_TX_SOP_PRIME)
		{
			cable_vdo = le32_to_cpu(msg.payload[4]);

			//printf("RX: SOP_PRIME (cable_vdo) = 0x%08X\n", msg.payload[4]);

			if ((cable_vdo & 0x00000060) >> 5 == 0x1) {
				////printk(" Cable = 3A");
				for(i=0;i<NUM_PDOS;i++) {
					src_pdo[i] = src_pdo_3A[i];
					////printk("   src_pdo[%d] = 0x%08x",i,src_pdo[i]);
				}
			}

			if ((cable_vdo & 0x00000060) >> 5 == 0x2) {
				////printk(" Cable = 5A");
				for(i=0;i<NUM_PDOS;i++) {
					src_pdo[i] = src_pdo_5A[i];
					////printk("   src_pdo[%d] = 0x%08x",i,src_pdo[i]);
				}
			}
		}
		else if ((cnt > 3) && (msg_type == PD_DATA_BIST) && (BDO_MODE_MASK(bist_request) == BDO_MODE_TESTDATA))
		{
			ret = regmap_raw_read(tcpci->regmap, TCPC_TCPC_CTRL, &reg, sizeof(u8)); // JRF: Read the Frame Type second
			ret |= regmap_write(tcpci->regmap, TCPC_TCPC_CTRL, reg | TCPC_TCPC_CTRL_BIST_TEST_MODE);
		}
		else
		{
			tcpm_pd_receive(tcpci->port, &msg);
		}
		/* Read complete, clear RX status alert bit */

        ////printk("Clearing TCPC_ALERT_RX_STATUS by writing = 0x%04x", TCPC_ALERT_RX_STATUS);
		tcpci_write16(tcpci, TCPC_ALERT, TCPC_ALERT_RX_STATUS);
	}

	if (status & TCPC_ALERT_RX_HARD_RST) {
		unsigned int reg;
        // Clear BIST Mode
		regmap_raw_read(tcpci->regmap, TCPC_TCPC_CTRL, &reg, sizeof(u8)); 
		regmap_write(tcpci->regmap, TCPC_TCPC_CTRL, reg & (~TCPC_TCPC_CTRL_BIST_TEST_MODE));
        
        // POWER_STATUS_MASK register needs to be reinitialized after hard reset
        if (tcpci->controls_vbus) {
            reg = TCPC_POWER_STATUS_VBUS_PRES;
        }
        else {
            reg = 0;
        }
        regmap_write(tcpci->regmap, TCPC_POWER_STATUS_MASK, reg);
    
		tcpm_pd_hard_reset(tcpci->port);
		////printk("TCPCI: TCPC_ALERT_RX_HARD_RST");
	}

	if (status & TCPC_ALERT_TX_SUCCESS) {
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_SUCCESS);
		////printk("TCPCI: TCPC_ALERT_TX_SUCCESS");
	} else if (status & TCPC_ALERT_TX_DISCARDED) {
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_DISCARDED);
		////printk("TCPCI: TCPC_ALERT_TX_DISCARDED");
	} else if (status & TCPC_ALERT_TX_FAILED) {
		tcpm_pd_transmit_complete(tcpci->port, TCPC_TX_FAILED);
		////printk("TCPCI: TCPC_ALERT_TX_FAILED");
	}

	return IRQ_HANDLED;
}

static struct tcpc_config tcpci_tcpc_config = {
	.src_pdo = src_pdo,
	.nr_src_pdo = ARRAY_SIZE(src_pdo),
	.snk_pdo = snk_pdo,
	.nr_snk_pdo = ARRAY_SIZE(snk_pdo),

	.type = TYPEC_PORT_SRC,
	.data = TYPEC_PORT_DFP,
	.default_role = TYPEC_SOURCE,

	.alt_modes = 0,
};

static int tcpci_parse_config(struct tcpci *tcpci)
{
	tcpci->controls_vbus = true; /* XXX */

	/* TODO: Populate struct tcpc_config from ACPI/device-tree */
	tcpci->tcpc.config = &tcpci_tcpc_config;

	return 0;
}

struct tcpci *tcpci_register_port(struct regmap *port_info)
{
	struct tcpci *tcpci;
	int err;

  tcpci = malloc(sizeof(*tcpci));
	if (!tcpci)
		return ERR_PTR(-ENOMEM);

	tcpci->regmap = port_info;

	tcpci->tcpc.init = tcpci_init;
	tcpci->tcpc.get_vbus = tcpci_get_vbus;
	tcpci->tcpc.set_vbus = tcpci_set_vbus;
	tcpci->tcpc.set_vbus_hv = tcpci_set_vbus_hv;
	tcpci->tcpc.set_cc = tcpci_set_cc;
	tcpci->tcpc.get_cc = tcpci_get_cc;
	tcpci->tcpc.set_polarity = tcpci_set_polarity;
	tcpci->tcpc.set_bist_test_mode = tcpci_set_bist_test_mode;
	tcpci->tcpc.set_vconn = tcpci_set_vconn;
	//tcpci->tcpc.start_drp_toggling = tcpci_start_drp_toggling;

	tcpci->tcpc.set_pd_rx = tcpci_set_pd_rx;
	tcpci->tcpc.set_roles = tcpci_set_roles;
	tcpci->tcpc.cable_disc = tcpci_cable_disc;
	tcpci->tcpc.pd_transmit = tcpci_pd_transmit;

	err = tcpci_parse_config(tcpci);
	if (err < 0)
		return ERR_PTR(err);

	tcpci->port = tcpm_register_port(&tcpci->tcpc);

	return tcpci;
}

void mx_switch_init(struct tcpci *tcpci)
{
	int err;
    
//	tcpci->tcpc.set_vbus = tcpci_set_vbus;
//	tcpci->tcpc.set_vbus_hv = tcpci_set_vbus_hv;

	err = tcpci_parse_config(tcpci);
	if (err < 0)
		return ERR_PTR(err);

    if (tcpci->port)
    {
        free(tcpci->port);
        tcpci->port = NULL;
    }
    
	tcpci->port = tcpm_register_port(&tcpci->tcpc);

}

void mx_switch_init2(struct regmap *port_info, struct tcpci *tcpci, uint16_t cnt)
{
	unsigned int reg1, reg2;
    if(cnt == 100) {
        regmap_read(port_info, TCPC_GENERAL_SETUP, &reg1);
        regmap_write(port_info, TCPC_GENERAL_SETUP, ((reg1 & ~TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK) | AUTO_CDP_DCP_MODE));
        general_setup_reg = ((general_setup_reg & ~TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK) | AUTO_CDP_DCP_MODE);

//        mx_pd_hard_reset_value(tcpci->port);
    }
    else if(cnt == 10){
        tcpm_pd_hard_reset(tcpci->port);
    }
    else {  }
    
    
//    mdelay(100);
    
    
//	regmap_read(port_info, TCPC_TRANSMIT, &reg2);
//    regmap_write(port_info, TCPC_TRANSMIT, ((reg2 & ~TCPC_TRANSMIT_TYPE_MASK) | TCPC_TX_HARD_RESET));
//    
//    regmap_read(port_info, TCPC_ALERT_MASK, &reg2);
//    regmap_write(port_info, TCPC_ALERT_MASK, (reg2 | 0x08));
    
}

void tcpci_switch_init(struct tcpci *tcpci)
{
    unsigned int reg;
    regmap_read(tcpci->regmap, TCPC_GENERAL_SETUP, &reg);
    regmap_write(tcpci->regmap, TCPC_GENERAL_SETUP, ((reg & ~TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK) | AUTO_CDP_DCP_MODE));
    general_setup_reg = ((general_setup_reg & ~TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK) | AUTO_CDP_DCP_MODE);
    
    //mx_pd_hard_reset_value(tcpci->port);
    //tcpm_pd_hard_reset(tcpci->port);
    
    tcpm_pd_disable(tcpci->port);

}
