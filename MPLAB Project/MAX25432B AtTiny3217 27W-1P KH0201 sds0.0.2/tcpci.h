/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2015-2017 Google, Inc
 *
 * USB Type-C Port Controller Interface.
 */

/*
 * Modified 2021 by Maxim Integrated for MAX25432
 */


#ifndef __USB_TCPCI_H
#define __USB_TCPCI_H

#define TCPC_VENDOR_ID  0x0
#define TCPC_PRODUCT_ID 0x2
#define TCPC_BCD_DEV    0x4
#define TCPC_TC_REV     0x6
#define TCPC_PD_REV     0x8
#define TCPC_PD_INT_REV	0xa

#define TCPC_ALERT                  0x10
#define TCPC_ALERT_VENDOR           BIT(15)
#define TCPC_ALERT_VBUS_DISCNCT     BIT(11)
#define TCPC_ALERT_RX_BUF_OVF       BIT(10)
#define TCPC_ALERT_FAULT            BIT(9)
#define TCPC_ALERT_V_ALARM_LO       BIT(8)
#define TCPC_ALERT_V_ALARM_HI       BIT(7)
#define TCPC_ALERT_TX_SUCCESS       BIT(6)
#define TCPC_ALERT_TX_DISCARDED     BIT(5)
#define TCPC_ALERT_TX_FAILED        BIT(4)
#define TCPC_ALERT_RX_HARD_RST      BIT(3)
#define TCPC_ALERT_RX_STATUS        BIT(2)
#define TCPC_ALERT_POWER_STATUS     BIT(1)
#define TCPC_ALERT_CC_STATUS        BIT(0)

#define TCPC_ALERT_MASK           0x12
#define TCPC_POWER_STATUS_MASK    0x14
#define TCPC_FAULT_STATUS_MASK    0x15
#define TCPC_CONFIG_STD_OUTPUT    0x18

#define TCPC_TCPC_CTRL                  0x19
#define TCPC_TCPC_CTRL_BIST_TEST_MODE   BIT(1)
#define TCPC_TCPC_CTRL_ORIENTATION      BIT(0)

#define TCPC_ROLE_CTRL                  0x1a
#define TCPC_ROLE_CTRL_DRP              BIT(6)
#define TCPC_ROLE_CTRL_RP_VAL_SHIFT     4
#define TCPC_ROLE_CTRL_RP_VAL_MASK      0x30
#define TCPC_ROLE_CTRL_RP_VAL_DEF       0x0
#define TCPC_ROLE_CTRL_RP_VAL_1_5       0x1
#define TCPC_ROLE_CTRL_RP_VAL_3_0       0x2
#define TCPC_ROLE_CTRL_CC2_SHIFT        2
#define TCPC_ROLE_CTRL_CC2_MASK         0x3
#define TCPC_ROLE_CTRL_CC1_SHIFT        0
#define TCPC_ROLE_CTRL_CC1_MASK         0x3
#define TCPC_ROLE_CTRL_CC_RA            0x0
#define TCPC_ROLE_CTRL_CC_RP            0x1
#define TCPC_ROLE_CTRL_CC_RD            0x2
#define TCPC_ROLE_CTRL_CC_OPEN          0x3

#define TCPC_FAULT_CTRL                 0x1b

#define TCPC_POWER_CTRL                   0x1c
#define TCPC_POWER_CTRL_VBUS_VOLT_MON_EN  BIT(6)
#define TCPC_POWER_CTRL_VOLT_ALRMS_EN     BIT(5)
#define TCPC_POWER_CTRL_AUTO_DSH          BIT(4)
#define TCPC_POWER_CTRL_FORCE_DSH         BIT(2)
#define TCPC_POWER_CTRL_VCONN_ENABLE      BIT(0)


#define TCPC_CC_STATUS                  0x1d
#define TCPC_CC_STATUS_TOGGLING         BIT(5)
#define TCPC_CC_STATUS_TERM             BIT(4)
#define TCPC_CC_STATUS_CC2_SHIFT        2
#define TCPC_CC_STATUS_CC2_MASK         0x3
#define TCPC_CC_STATUS_CC1_SHIFT        0
#define TCPC_CC_STATUS_CC1_MASK         0x3

#define TCPC_POWER_STATUS               0x1e
#define TCPC_POWER_STATUS_UNINIT        BIT(6)
#define TCPC_POWER_STATUS_SRC_NONDEF    BIT(5)
#define TCPC_POWER_STATUS_SRC_VBUS      BIT(4)
#define TCPC_POWER_STATUS_VBUS_DET      BIT(3)
#define TCPC_POWER_STATUS_VBUS_PRES     BIT(2)

#define TCPC_FAULT_STATUS               0x1f
#define TCPC_ALL_REG_RST                BIT(7)
#define TCPC_FAULT_VBUS_OCP             BIT(3)
#define TCPC_FAULT_VBUS_OVP             BIT(2)
#define TCPC_FAULT_VCONN_OCP            BIT(1)
#define TCPC_FAULT_I2C_ERR              BIT(0)

#define TCPC_COMMAND                    0x23
#define TCPC_CMD_WAKE_I2C               0x11
#define TCPC_CMD_DISABLE_VBUS_DETECT    0x22
#define TCPC_CMD_ENABLE_VBUS_DETECT     0x33
#define TCPC_CMD_DISABLE_SINK_VBUS      0x44
#define TCPC_CMD_SINK_VBUS              0x55
#define TCPC_CMD_DISABLE_SRC_VBUS       0x66
#define TCPC_CMD_SRC_VBUS_DEFAULT       0x77
#define TCPC_CMD_SRC_VBUS_HIGH          0x88
#define TCPC_CMD_LOOK4CONNECTION        0x99
#define TCPC_CMD_RXONEMORE              0xAA
#define TCPC_CMD_I2C_IDLE               0xFF

#define TCPC_DEV_CAP_1                  0x24
#define TCPC_DEV_CAP_2                  0x26
#define TCPC_STD_INPUT_CAP              0x28
#define TCPC_STD_OUTPUT_CAP             0x29

#define TCPC_MSG_HDR_INFO               0x2e
#define TCPC_MSG_HDR_INFO_DATA_ROLE     BIT(3)
#define TCPC_MSG_HDR_INFO_PWR_ROLE      BIT(0)
#define TCPC_MSG_HDR_INFO_REV_SHIFT     1
#define TCPC_MSG_HDR_INFO_REV_MASK      0x3

#define TCPC_RX_DETECT                  0x2f
#define TCPC_RX_DETECT_HARD_RESET       BIT(5)
#define TCPC_RX_DETECT_SOP_PRIME        BIT(1)
#define TCPC_RX_DETECT_SOP              BIT(0)

#define TCPC_RX_BYTE_CNT                0x30
#define TCPC_RX_BUF_FRAME_TYPE          0x30
#define TCPC_RX_HDR                     0x30
#define TCPC_RX_DATA                    0x30 /* through 0x4f */

#define TCPC_TRANSMIT                   0x50
#define TCPC_TRANSMIT_RETRY_SHIFT       4
#define TCPC_TRANSMIT_RETRY_MASK        0x3
#define TCPC_TRANSMIT_TYPE_SHIFT        0
#define TCPC_TRANSMIT_TYPE_MASK         0x7

#define TCPC_TX_BYTE_CNT                0x51
#define TCPC_TX_HDR                     0x51
#define TCPC_TX_DATA                    0x51 /* through 0x6f */

#define TCPC_VBUS_VOLTAGE                 0x70
#define TCPC_VBUS_SINK_DISCONNECT_THRESH  0x72
#define TCPC_VBUS_STOP_DISCHARGE_THRESH   0x74
#define TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_L  0x76
#define TCPC_VBUS_VOLTAGE_ALARM_HI_CFG_H  0x77
#define TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_L  0x78
#define TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_H  0x79

#define TCPC_VBUS_NONDEFAULT_TARGET_L     0x7A
#define TCPC_VBUS_NONDEFAULT_TARGET_H     0x7B
#define TCPC_VBUS_CURRENT                 0x80
#define TCPC_CABLE_COMP_CONTROL           0x81
#define TCPC_VBUS_ILIM_SET                0x82

#define TCPC_BUCK_BOOST_SETUP             0x83
#define TCPC_BUCK_BOOST_SETUP_SS_SEL      0x03
#define TCPC_BUCK_BOOST_SETUP_SYNC_DIR    0x04
#define TCPC_BUCK_BOOST_SETUP_FSW_SHIFT   3
#define TCPC_BUCK_BOOST_SETUP_FSW_MASK    0x18
#define TCPC_BUCK_BOOST_SETUP_SLP_SHIFT   5
#define TCPC_BUCK_BOOST_SETUP_SLP_MASK    0xE0

#define TCPC_WATCHDOG_SETUP               0x84
#define TCPC_AUTO_SHIELD_SETUP            0x85

#define TCPC_GENERAL_SETUP                         0x86
#define TCPC_GENERAL_SETUP_VBUS_HIRES              0x40
#define TCPC_GENERAL_SETUP_EXT_BIAS_SEL            0x10
#define TCPC_GENERAL_SETUP_CL_EN                   0x04
#define TCPC_GENERAL_SETUP_AUTO_CDP_DCP_MODE_MASK  0x3

#define TCPC_IN_THRESH                      0x87

#define TCPC_VCONN_THRESH                   0x88
#define TCPC_VCONN_OCPL_SEL_SHIFT           4
#define TCPC_VCONN_OCPL_SEL_MASK            0xF0
#define TCPC_VCONN_IN_UV_THRESH_SHIFT       0
#define TCPC_VCONN_IN_UV_THRESH_MASK        0x07

#define TCPC_VBUS_THRESH                      0x89

#define TCPC_VENDOR_STATUS_MASK               0x8A
#define TCPC_VENDOR_STATUS_MASK_SHIELDING     BIT(7)
#define TCPC_VENDOR_STATUS_MASK_OMF_TRANS     BIT(0)

#define TCPC_VENDOR_STATUS                    0x8B
#define TCPC_VENDOR_STATUS_SHIELDING          BIT(7)
#define TCPC_VENDOR_STATUS_OMF_TRANS          BIT(0)

#define TCPC_AUTO_SHIELD_STATUS0              0x8C
#define TCPC_AUTO_SHIELD_STATUS0_VCONN_IN_UV  BIT(3)
#define TCPC_AUTO_SHIELD_STATUS0_IN_OC        BIT(2)
#define TCPC_AUTO_SHIELD_STATUS0_VDD_USB_UV   BIT(1)
#define TCPC_AUTO_SHIELD_STATUS0_VBUS_UV      BIT(0)

#define TCPC_AUTO_SHIELD_STATUS1              0x8D
#define TCPC_AUTO_SHIELD_STATUS1_VBUS_SHT_GND	BIT(4)
#define TCPC_AUTO_SHIELD_STATUS1_CL_CV        BIT(7)

//***********  MAX25432 Options **********

#define DEFAULT_RP_CURRENT  3000 /* mA */

#define USE_VBUS_HIRES_MODE 1
#define IN_UV_THRESH 0x00 /* 4.5V */
#define CABLE_COMP_GAIN 0x05

//#define VOUT_ILIM_5V   0x1  /* 4A */
//#define VOUT_ILIM_9V   0x1  /* 4A */
//#define VOUT_ILIM_15V  0x1  /* 4A */
//#define VOUT_ILIM_20V  0x3  /* 6A */

// BUCK_BOOST_SETUP compile options
//#define SS_SEL    0b00    /* SS disabled */
#define SS_SEL      0b11    /* 9% */
//#define FSW       0b11    /* 2.2MHz */
#define FSW         0b10    /*400kHz default*/
#define VOUT_SLP_5V     0b011  /* 400mV */
#define VOUT_SLP_9V     0b011  /* 400mV */
#define VOUT_SLP_15V    0b011  /* 400mV */
#define VOUT_SLP_20V    0b011  /* 400mV */

//#define VOUT_SLP_5V     0b010  /* 300mV */
//#define VOUT_SLP_9V     0b010  /* 300mV */
//#define VOUT_SLP_15V    0b101  /* 600mV */
//#define VOUT_SLP_20V    0b111  /* 800mV */

#define ILIM_HEADROOM     200 /* Extra ILIM margin */

//#define AUTO_CDP_DCP_MODE 0b01 /* Auto-CDP */ 

#define VCONN_OCPL_SEL      0x0 /* 50mA */ 
//#define VCONN_OCPL_SEL      0x6 /* 350mA (default) */ 
#define VCONN_IN_UV_THRESH  0x3 /* 3.05V (default) */

struct regmap;
struct tcpci;

struct tcpci *tcpci_register_port(struct regmap *port_info /*struct device *dev, struct tcpci_data *data*/);
void tcpci_unregister_port(struct tcpci *tcpci);
void mx_switch_init(struct tcpci *tcpci);
void mx_switch_init2(struct regmap *port_info, struct tcpci *tcpci, uint16_t cnt);

#endif
