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

#include "mx_time.h"

static volatile uint32_t jiffies;

// These need to be defined in main
extern disable_periodic_timer_interrupt();
extern enable_periodic_timer_interrupt();

void increment_ms_clk()
{
	jiffies++;
}

// platform-independent method of avoiding invalid read of jiffies is 
// memory access is non-atomic and interrupt modifies the variable
// at the same time as the read
/*
uint32_t get_ms_clk() 
{
    disable_periodic_timer_interrupt();
    uint32_t temp = jiffies;
    enable_periodic_timer_interrupt();
    return temp;
}
*/
uint32_t get_ms_clk() 
{
    uint32_t volatile temp1 = jiffies;
    uint32_t volatile temp2 = jiffies;
    while (temp1 != temp2) {
        temp1 = jiffies;
        temp2 = jiffies;
    }
    return temp1;
}

void mdelay(uint32_t delay)
{
    if (delay < 1) {
        return;
    }
    uint32_t tnow = get_ms_clk();
	uint32_t timeout = tnow + delay;
    if (timeout < tnow){ // timeout overflowed
        while (timeout < get_ms_clk()) {
            // wait for clk to overflow
        }
    }
	while (1) {
        if (get_ms_clk() >= timeout) {
            break;
        }
    }
};
void usleep_range(uint32_t min, uint32_t max)
{
	uint32_t timeout = get_ms_clk() + min / 1000;
	while (get_ms_clk() < timeout) {}
};


