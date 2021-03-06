/*
Copyright 2018 Massdrop Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "arm_atsam_protocol.h"

#include <string.h>

volatile clk_t system_clks;
volatile uint64_t ms_clk;

volatile uint8_t us_delay_done;

const uint32_t sercom_apbbase[] = {(uint32_t)SERCOM0,(uint32_t)SERCOM1,(uint32_t)SERCOM2,(uint32_t)SERCOM3,(uint32_t)SERCOM4,(uint32_t)SERCOM5};
const uint8_t sercom_pchan[] = {7, 8, 23, 24, 34, 35};

void CLK_oscctrl_init(void)
{
    Oscctrl *posctrl = OSCCTRL;
    Gclk *pgclk = GCLK;

    //default setup on por
    system_clks.freq_dfll = FREQ_DFLL_DEFAULT;
    system_clks.freq_gclk[0] = system_clks.freq_dfll;

    //configure and startup 16MHz xosc0
    posctrl->XOSCCTRL[0].bit.STARTUP = 7;
    posctrl->XOSCCTRL[0].bit.ENALC = 1;
    posctrl->XOSCCTRL[0].bit.IMULT = 5;
    posctrl->XOSCCTRL[0].bit.IPTAT = 3;
    posctrl->XOSCCTRL[0].bit.ONDEMAND = 0;
    posctrl->XOSCCTRL[0].bit.XTALEN = 1;
    posctrl->XOSCCTRL[0].bit.ENABLE = 1;
    while (posctrl->STATUS.bit.XOSCRDY0==0) {}  //wait for xosc0 stable and ready
    system_clks.freq_xosc0 = FREQ_XOSC0;

    //configure and startup DPLL0
    posctrl->Dpll[0].DPLLCTRLB.bit.REFCLK = 2;              //select XOSC0 (16MHz)
    posctrl->Dpll[0].DPLLCTRLB.bit.DIV = 7;                 //16 MHz -> 1 MHz
    posctrl->Dpll[0].DPLLRATIO.bit.LDR = PLL_RATIO;         //48 MHz
    while (posctrl->Dpll[0].DPLLSYNCBUSY.bit.DPLLRATIO) {}
    posctrl->Dpll[0].DPLLCTRLA.bit.ONDEMAND = 0;
    posctrl->Dpll[0].DPLLCTRLA.bit.ENABLE = 1;
    while (posctrl->Dpll[0].DPLLSYNCBUSY.bit.ENABLE) {}
    while (posctrl->Dpll[0].DPLLSTATUS.bit.CLKRDY == 0) {}   //wait for CLKRDY
    while (posctrl->Dpll[0].DPLLSTATUS.bit.LOCK == 0) {}     // and LOCK
    system_clks.freq_dpll[0] = (system_clks.freq_xosc0 / 2 / (posctrl->Dpll[0].DPLLCTRLB.bit.DIV + 1)) * (posctrl->Dpll[0].DPLLRATIO.bit.LDR + 1);

    //change gclk0 to DPLL0
    pgclk->GENCTRL[GEN_DPLL0].bit.SRC = GCLK_SOURCE_DPLL0;
    while (pgclk->SYNCBUSY.bit.GENCTRL0) {}
    system_clks.freq_gclk[0] = system_clks.freq_dpll[0];
}

//configure for 1MHz (1 usec timebase)
//call CLK_set_gclk_freq(GEN_TC45, FREQ_TC45_DEFAULT);
uint32_t CLK_set_gclk_freq(uint8_t gclkn, uint32_t freq)
{
    Gclk *pgclk = GCLK;

    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.SRC = GCLK_SOURCE_DPLL0;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.DIV = (uint8_t)(system_clks.freq_dpll[0] / freq);
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.DIVSEL = 0;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.GENEN = 1;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    system_clks.freq_gclk[gclkn] = system_clks.freq_dpll[0] / pgclk->GENCTRL[gclkn].bit.DIV;
    return system_clks.freq_gclk[gclkn];
}

void CLK_init_osc(void)
{
    uint8_t gclkn = GEN_OSC0;
    Gclk *pgclk = GCLK;

    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.SRC = GCLK_SOURCE_XOSC0;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.DIV = 1;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.DIVSEL = 0;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    pgclk->GENCTRL[gclkn].bit.GENEN = 1;
    while (pgclk->SYNCBUSY.vec.GENCTRL) {}
    system_clks.freq_gclk[gclkn] = system_clks.freq_xosc0;
}

void CLK_reset_time(void)
{
    Tc *ptc4 = TC4;
    Tc *ptc0 = TC0;

    ms_clk = 0;

    //stop counters
    ptc4->COUNT16.CTRLA.bit.ENABLE = 0;
    while (ptc4->COUNT16.SYNCBUSY.bit.ENABLE) {}
    ptc0->COUNT32.CTRLA.bit.ENABLE = 0;
    while (ptc0->COUNT32.SYNCBUSY.bit.ENABLE) {}
    //zero counters
    ptc4->COUNT16.COUNT.reg = 0;
    while (ptc4->COUNT16.SYNCBUSY.bit.COUNT) {}
    ptc0->COUNT32.COUNT.reg = 0;
    while (ptc0->COUNT32.SYNCBUSY.bit.COUNT) {}
    //start counters
    ptc0->COUNT32.CTRLA.bit.ENABLE = 1;
    while (ptc0->COUNT32.SYNCBUSY.bit.ENABLE) {}
    ptc4->COUNT16.CTRLA.bit.ENABLE = 1;
    while (ptc4->COUNT16.SYNCBUSY.bit.ENABLE) {}
}

void TC4_Handler()
{
    if (TC4->COUNT16.INTFLAG.bit.MC0)
    {
        TC4->COUNT16.INTFLAG.reg = TC_INTENCLR_MC0;
        ms_clk++;
    }
}

void TC5_Handler()
{
    if (TC5->COUNT16.INTFLAG.bit.MC0)
    {
        TC5->COUNT16.INTFLAG.reg = TC_INTENCLR_MC0;
        us_delay_done = 1;
        TC5->COUNT16.CTRLA.bit.ENABLE = 0;
        while (TC5->COUNT16.SYNCBUSY.bit.ENABLE) {}
    }
}

uint32_t CLK_enable_timebase(void)
{
    Gclk *pgclk = GCLK;
    Mclk *pmclk = MCLK;
    Tc *ptc4 = TC4;
    Tc *ptc5 = TC5;
    Tc *ptc0 = TC0;
    Evsys *pevsys = EVSYS;

    //gclk2  highspeed time base
    CLK_set_gclk_freq(GEN_TC45, FREQ_TC45_DEFAULT);
    CLK_init_osc();

    //unmask TC4, sourcegclk2 to TC4
    pmclk->APBCMASK.bit.TC4_ = 1;
    pgclk->PCHCTRL[TC4_GCLK_ID].bit.GEN = GEN_TC45;
    pgclk->PCHCTRL[TC4_GCLK_ID].bit.CHEN = 1;

    //unmask TC5 sourcegclk2 to TC5
    pmclk->APBCMASK.bit.TC5_ = 1;
    pgclk->PCHCTRL[TC5_GCLK_ID].bit.GEN = GEN_TC45;
    pgclk->PCHCTRL[TC5_GCLK_ID].bit.CHEN = 1;

    //configure TC4
    ptc4->COUNT16.CTRLA.bit.ENABLE = 0;
    while (ptc4->COUNT16.SYNCBUSY.bit.ENABLE) {}
    ptc4->COUNT16.CTRLA.bit.SWRST = 1;
    while (ptc4->COUNT16.SYNCBUSY.bit.SWRST) {}
    while (ptc4->COUNT16.CTRLA.bit.SWRST) {}

    //CTRLA defaults
    //CTRLB as default, counting up
    ptc4->COUNT16.CTRLBCLR.reg = 5;
    while (ptc4->COUNT16.SYNCBUSY.bit.CTRLB) {}
    ptc4->COUNT16.CC[0].reg = 999;
    while (ptc4->COUNT16.SYNCBUSY.bit.CC0) {}
    //ptc4->COUNT16.DBGCTRL.bit.DBGRUN = 1;

    //wave mode
    ptc4->COUNT16.WAVE.bit.WAVEGEN = 1; //MFRQ match frequency mode, toggle each CC match
    //generate event for next stage
    ptc4->COUNT16.EVCTRL.bit.MCEO0 = 1;

    NVIC_EnableIRQ(TC4_IRQn);
    ptc4->COUNT16.INTENSET.bit.MC0 = 1;

    //configure TC5
    ptc5->COUNT16.CTRLA.bit.ENABLE = 0;
    while (ptc5->COUNT16.SYNCBUSY.bit.ENABLE) {}
    ptc5->COUNT16.CTRLA.bit.SWRST = 1;
    while (ptc5->COUNT16.SYNCBUSY.bit.SWRST) {}
    while (ptc5->COUNT16.CTRLA.bit.SWRST) {}

    //CTRLA defaults
    //CTRLB as default, counting up
    ptc5->COUNT16.CTRLBCLR.reg = 5;
    while (ptc5->COUNT16.SYNCBUSY.bit.CTRLB) {}
    //ptc5->COUNT16.DBGCTRL.bit.DBGRUN = 1;

    //wave mode
    ptc5->COUNT16.WAVE.bit.WAVEGEN = 1; //MFRQ match frequency mode, toggle each CC match
    //generate event for next stage
    ptc5->COUNT16.EVCTRL.bit.MCEO0 = 1;

    NVIC_EnableIRQ(TC5_IRQn);
    ptc5->COUNT16.INTENSET.bit.MC0 = 1;

    //unmask TC0,1, sourcegclk2 to TC0,1
    pmclk->APBAMASK.bit.TC0_ = 1;
    pgclk->PCHCTRL[TC0_GCLK_ID].bit.GEN = GEN_TC45;
    pgclk->PCHCTRL[TC0_GCLK_ID].bit.CHEN = 1;

    pmclk->APBAMASK.bit.TC1_ = 1;
    pgclk->PCHCTRL[TC1_GCLK_ID].bit.GEN = GEN_TC45;
    pgclk->PCHCTRL[TC1_GCLK_ID].bit.CHEN = 1;

    //configure TC0
    ptc0->COUNT32.CTRLA.bit.ENABLE = 0;
    while (ptc0->COUNT32.SYNCBUSY.bit.ENABLE) {}
    ptc0->COUNT32.CTRLA.bit.SWRST = 1;
    while (ptc0->COUNT32.SYNCBUSY.bit.SWRST) {}
    while (ptc0->COUNT32.CTRLA.bit.SWRST) {}
    //CTRLA as default
    ptc0->COUNT32.CTRLA.bit.MODE = 2; //32 bit mode
    ptc0->COUNT32.EVCTRL.bit.TCEI = 1; //enable incoming events
    ptc0->COUNT32.EVCTRL.bit.EVACT = 2 ; //count events

    //configure event system
    pmclk->APBBMASK.bit.EVSYS_ = 1;
    pgclk->PCHCTRL[EVSYS_GCLK_ID_0].bit.GEN = GEN_TC45;
    pgclk->PCHCTRL[EVSYS_GCLK_ID_0].bit.CHEN = 1;
    pevsys->USER[44].reg = EVSYS_ID_USER_PORT_EV_0;                               //TC0 will get event channel 0
    pevsys->Channel[0].CHANNEL.bit.EDGSEL = EVSYS_CHANNEL_EDGSEL_RISING_EDGE_Val; //Rising edge
    pevsys->Channel[0].CHANNEL.bit.PATH = EVSYS_CHANNEL_PATH_SYNCHRONOUS_Val;     //Synchronous
    pevsys->Channel[0].CHANNEL.bit.EVGEN = EVSYS_ID_GEN_TC4_MCX_0;                //TC4 MC0

    CLK_reset_time();

    ADC0_clock_init();

    return 0;
}

uint32_t CLK_get_ms(void)
{
    return ms_clk;
}

void CLK_delay_us(uint16_t usec)
{
    us_delay_done = 0;

    if (TC5->COUNT16.CTRLA.bit.ENABLE)
    {
        TC5->COUNT16.CTRLA.bit.ENABLE = 0;
        while (TC5->COUNT16.SYNCBUSY.bit.ENABLE) {}
    }

    if (usec < 10) usec = 0;
    else usec -= 10;

    TC5->COUNT16.CC[0].reg = usec;
    while (TC5->COUNT16.SYNCBUSY.bit.CC0) {}

    TC5->COUNT16.CTRLA.bit.ENABLE = 1;
    while (TC5->COUNT16.SYNCBUSY.bit.ENABLE) {}

    while (!us_delay_done) {}
}

void CLK_delay_ms(uint64_t msec)
{
    msec += CLK_get_ms();
    while (msec > CLK_get_ms()) {}
}

void clk_enable_sercom_apbmask(int sercomn)
{
    Mclk *pmclk = MCLK;
    switch (sercomn)
    {
        case 0:
            pmclk->APBAMASK.bit.SERCOM0_ = 1;
            break;
        case 1:
            pmclk->APBAMASK.bit.SERCOM1_ = 1;
            break;
        case 2:
            pmclk->APBBMASK.bit.SERCOM2_ = 1;
            break;
        case 3:
            pmclk->APBBMASK.bit.SERCOM3_ = 1;
            break;
        default:
            break;
    }
}

//call CLK_oscctrl_init first
//call CLK_set_spi_freq(CHAN_SERCOM_SPI, FREQ_SPI_DEFAULT);
uint32_t CLK_set_spi_freq(uint8_t sercomn, uint32_t freq)
{
    Gclk *pgclk = GCLK;
    Sercom *psercom = (Sercom *)sercom_apbbase[sercomn];
    clk_enable_sercom_apbmask(sercomn);

    //all gclk0 for now
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.GEN = 0;
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.CHEN = 1;

    psercom->I2CM.CTRLA.bit.SWRST = 1;
    while (psercom->I2CM.SYNCBUSY.bit.SWRST) {}
    while (psercom->I2CM.CTRLA.bit.SWRST) {}

    psercom->SPI.BAUD.reg = (uint8_t) (system_clks.freq_gclk[0]/2/freq-1);
    system_clks.freq_spi = system_clks.freq_dfll/2/(psercom->SPI.BAUD.reg+1);
    system_clks.freq_sercom[sercomn] = system_clks.freq_spi;

    return system_clks.freq_spi;
}

//call CLK_oscctrl_init first
//call CLK_set_i2c0_freq(CHAN_SERCOM_I2C0, FREQ_I2C0_DEFAULT);
uint32_t CLK_set_i2c0_freq(uint8_t sercomn, uint32_t freq)
{
    Gclk *pgclk = GCLK;
    Sercom *psercom = (Sercom *)sercom_apbbase[sercomn];
    clk_enable_sercom_apbmask(sercomn);

    //all gclk0 for now
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.GEN = 0;
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.CHEN = 1;

    psercom->I2CM.CTRLA.bit.SWRST = 1;
    while (psercom->I2CM.SYNCBUSY.bit.SWRST) {}
    while (psercom->I2CM.CTRLA.bit.SWRST) {}

    psercom->I2CM.BAUD.bit.BAUD = (uint8_t) (system_clks.freq_gclk[0]/2/freq-1);
    system_clks.freq_i2c0 = system_clks.freq_dfll/2/(psercom->I2CM.BAUD.bit.BAUD+1);
    system_clks.freq_sercom[sercomn] = system_clks.freq_i2c0;

    return system_clks.freq_i2c0;
}

//call CLK_oscctrl_init first
//call CLK_set_i2c1_freq(CHAN_SERCOM_I2C1, FREQ_I2C1_DEFAULT);
uint32_t CLK_set_i2c1_freq(uint8_t sercomn, uint32_t freq)
{
    Gclk *pgclk = GCLK;
    Sercom *psercom = (Sercom *)sercom_apbbase[sercomn];
    clk_enable_sercom_apbmask(sercomn);

    //all gclk0 for now
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.GEN = 0;
    pgclk->PCHCTRL[sercom_pchan[sercomn]].bit.CHEN = 1;

    psercom->I2CM.CTRLA.bit.SWRST = 1;
    while (psercom->I2CM.SYNCBUSY.bit.SWRST) {}
    while (psercom->I2CM.CTRLA.bit.SWRST) {}

    psercom->I2CM.BAUD.bit.BAUD = (uint8_t) (system_clks.freq_gclk[0]/2/freq-10);
    system_clks.freq_i2c1 = system_clks.freq_dfll/2/(psercom->I2CM.BAUD.bit.BAUD+1);
    system_clks.freq_sercom[sercomn] = system_clks.freq_i2c1;

    return system_clks.freq_i2c1;
}

void CLK_init(void)
{
    memset((void *)&system_clks,0,sizeof(system_clks));

    CLK_oscctrl_init();
    CLK_enable_timebase();
}

