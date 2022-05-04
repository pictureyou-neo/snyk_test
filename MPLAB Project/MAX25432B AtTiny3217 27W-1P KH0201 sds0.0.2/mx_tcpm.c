/*******************************************************************************
* Copyright (C) Maxim Integrated Products, Inc., All rights Reserved.
*
* This software is protected by copyright laws of the United States and
* of foreign countries. This material may also be protected by patent laws
* and technology transfer regulations of the United States and of foreign
* countries. This software is furnished under a license agreement and/or a
* nondisclosure agreement and may only be used or reproduced in accordance
* with the terms of those agreements. Dissemination of this information to
* any party or parties not specified in the license agreement and/or
* nondisclosure agreement is expressly prohibited.
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
* OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of Maxim Integrated
* Products, Inc. shall not be used except as stated in the Maxim Integrated
* Products, Inc. Branding Policy.
*
* The mere transfer of this software does not imply any licenses
* of trade secrets, proprietary technology, copyrights, patents,
* trademarks, maskwork rights, or any other form of intellectual
* property whatsoever. Maxim Integrated Products, Inc. retains all
* ownership rights.
*******************************************************************************
*/

#include "tcpm.h"
#include "tcpci.h"
#include "mx_tcpm.h"
#include "irqreturn.h"

extern u8 general_setup_reg;

// This struct from tcpm.h needs to be declared here so it can be used in this file.
// this is being used as a container for port specific information
struct regmap {
	uint8_t device_addr;
	uint8_t max_port_power;
};

// This struct from tcpm.c needs to be declared here so it can be used in this file.
extern struct tcpci {
	/*struct device *dev;*/ // mcm removing device

	struct tcpm_port *port;

	struct regmap *regmap;

	bool controls_vbus;

	struct tcpc_dev tcpc;
	struct tcpci_data *data;
};

// Function declarations needed for this module
struct tcpci *tcpci_register_port(struct regmap *port_info);
irqreturn_t tcpci_irq(struct tcpci *tcpci);
void tcpm_pd_event_handler_mx(struct tcpm_port *port);
void mx_switch_init(struct tcpci *tcpci);
void mx_switch_init2(struct regmap *port_info, struct tcpci *tcpci, uint16_t cnt);


static uint8_t tcpm_initialized = 0;

struct mx_tcpm_callbacks mxcb;

struct tcpci *tcpci0;

struct regmap port0; // tcpci info

void mx_tcpm_set_port_addr(uint8_t addr)
{
	port0.device_addr = addr;
}

void mx_tcpm_reinit(void)
{
//  	tcpci0 = tcpci_register_port(&port0);
//    mx_switch_init(tcpci0);
//    mx_switch_init2(&port0, tcpci0, cnt);
    tcpci_switch_init(tcpci0);
}

/**
 * mx_tcpm_init - Initialize the tcpm
 * struct mx_tcpm_callbacks _mxcb: callbacks to platform functions required by TCPM
 * uint8_t i2c_device_addr: 7-bit address of the device
 */
void mx_tcpm_init(struct mx_tcpm_callbacks _mxcb)
{
	mxcb.reg_read = _mxcb.reg_read;
	mxcb.reg_write = _mxcb.reg_write;

	//port0.device_addr = i2c_device_addr_1;
	//port0.max_port_power = 60;
	if (!tcpm_initialized) {
  	tcpci0 = tcpci_register_port(&port0);

	}
	//tcpci->port = tcpm_register_port(&tcpci->tcpc);
  tcpm_initialized = 1;
}

void mx_tcpm_irq()
{
    if (tcpm_initialized) {
        tcpci_irq(tcpci0);
	}
}

void mx_tcpm_increment_ms_clk()
{
	increment_ms_clk();
}

uint32_t mx_tcpm_get_ms_clk()
{
	return get_ms_clk();
}

void mx_tcpm_work()
{
  if (tcpm_initialized) {
    tcpm_pd_event_handler_mx(tcpci0->port);
	}
}

// These functions use the i2c callbacks that were provided by user
int mx_reg_read_bytes(const uint8_t i2c_device_addr, const uint8_t reg_addr, uint8_t reg_vals[], const uint8_t num_bytes)
{
  mxcb.reg_read(i2c_device_addr, reg_addr, reg_vals, num_bytes);
  return 0;
}
int mx_reg_write_bytes(const uint8_t i2c_device_addr, const uint8_t reg_addr, const uint8_t reg_vals[], const uint8_t num_bytes)
{
  mxcb.reg_write(i2c_device_addr, reg_addr, reg_vals, num_bytes);
  return 0;
}
