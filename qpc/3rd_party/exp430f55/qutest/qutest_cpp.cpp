//============================================================================
// Product: QUTEST port for the MSP-EXP430F5529LP board
// Last updated for version 7.2.0
// Last updated on  2022-12-15
//
//                    Q u a n t u m  L e a P s
//                    ------------------------
//                    Modern Embedded Software
//
// Copyright (C) 2005 Quantum Leaps. All rights reserved.
//
// This program is open source software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alternatively, this program may be distributed and modified under the
// terms of Quantum Leaps commercial licenses, which expressly supersede
// the GNU General Public License and are specifically designed for
// licensees interested in retaining the proprietary status of their code.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <www.gnu.org/licenses>.
//
// Contact information:
// <www.state-machine.com/licensing>
// <info@state-machine.com>
//============================================================================
#ifndef Q_SPY
    #error "Q_SPY must be defined to compile qutest_port.cpp"
#endif // Q_SPY

#define QP_IMPL        // this is QP implementation
#include "qf_port.hpp" // QF port
#include "qs_port.hpp" // QS port
#include "qs_pkg.hpp"  // QS package-scope interface
#include "qassert.h"   // QP embedded systems-friendly assertions

#include <msp430f5529.h>  // MSP430 variant used
// add other drivers if necessary...

//Q_DEFINE_THIS_MODULE("qutest_port")

using namespace QP;

// UART1 pins TX:P4.4, RX:P4.5
#define TXD     (1U << 4)
#define RXD     (1U << 5)

// Local-scope objects -------------------------------------------------------
// 1MHz clock setting, see BSP_init()
#define BSP_MCK     1000000U
#define BSP_SMCLK   1000000U

#define LED1        (1U << 0)
#define LED2        (1U << 7)

#define BTN_S1      (1U << 1)

//............................................................................
extern "C" {
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
    __interrupt void USCI_A1_ISR(void); // prototype
    #pragma vector=USCI_A1_VECTOR
    __interrupt void USCI_A1_ISR(void)
#elif defined(__GNUC__)
    __attribute__ ((interrupt(USCI_A1_VECTOR)))
    void USCI_A1_ISR(void)
#else
    #error MSP430 compiler not supported!
#endif
{
    if (UCA1IV == 2) {
        uint16_t b = UCA1RXBUF;
        QP::QS::rxPut(b);
    }
}

} // extern "C"

// QS callbacks ==============================================================
bool QS::onStartup(void const *arg) {
    static uint8_t qsBuf[256];  // buffer for QS; RAM is tight!
    static uint8_t qsRxBuf[80]; // buffer for QS receive channel
    //uint16_t tmp;

    WDTCTL = WDTPW | WDTHOLD; // stop watchdog timer

    // leave the MCK and SMCLK at default DCO setting

    P1DIR |= LED1;  // set LED1 pin to output
    P4DIR |= LED2;  // set LED2 pin to output

    initBuf  (qsBuf,   sizeof(qsBuf));
    rxInitBuf(qsRxBuf, sizeof(qsRxBuf));

    // USCI setup code...
    P4SEL |= (RXD | TXD);  // select the UART function for the pins
    UCA1CTL1 |= UCSWRST;   // reset USCI state machine
    UCA1CTL1 |= UCSSEL_2;  // choose the SMCLK clock
#if 1
    UCA1BR0 = 6; // 1MHz 9600 (see User's Guide)
    UCA1BR1 = 0; // 1MHz 9600
    // modulation UCBRSx=0, UCBRFx=0, oversampling
    UCA1MCTL = UCBRS_0 | UCBRF_13 | UCOS16;
#else
    UCA1BR0 = 9; // 1MHz 115200 (see User's Guide)
    UCA1BR1 = 0; // 1MHz 115200
    UCA1MCTL |= UCBRS_1 | UCBRF_0; // modulation UCBRSx=1, UCBRFx=0
#endif
    UCA1CTL1 &= ~UCSWRST;  // initialize USCI state machine
    UCA1IE |= UCRXIE;      // Enable USCI_A1 RX interrupt

    return true; // return success
}
//............................................................................
void QS::onCleanup(void) {
    // wait as long as the UART is busy
    while ((UCA1STAT & UCBUSY) != 0U) { // TX busy?
    }
    // delay before returning to allow all produced QS bytes to be received
    for (std::uint32_t volatile dly_ctr = 1000U; dly_ctr > 0U; --dly_ctr) {
    }
}
//............................................................................
void QS::onFlush(void) {
    uint16_t b;
    while ((b = getByte()) != QS_EOD) { // next QS byte available?
        while ((UCA1STAT & UCBUSY) != 0U) { // TX busy?
        }
        UCA1TXBUF = static_cast<uint8_t>(b);
    }
}
//............................................................................
// callback function to reset the target (to be implemented in the BSP)
void QS::onReset(void) {
    // write invalid password to WDT: cause a password-validation RESET
    WDTCTL = 0xDEAD;
}
//............................................................................
void QS::onTestLoop() {

    __enable_interrupt();  // IMPORTANT: enable global interrupts

    rxPriv_.inTestLoop = true;
    while (rxPriv_.inTestLoop) {

        // toggle the User LED on and then off, see NOTE01
        P4OUT |=  LED2;  // turn LED2 on
        P4OUT &= ~LED2;  // turn LED2 off

        rxParse();  // parse all the received bytes

        if ((UCA1STAT & UCBUSY) == 0U) { // TX NOT busy?

            uint16_t b;

            QF_INT_DISABLE();
            b = getByte();
            QF_INT_ENABLE();

            if (b != QS_EOD) {
                UCA1TXBUF = static_cast<uint8_t>(b);
            }
        }
    }
    // set inTestLoop to true in case calls to QS_onTestLoop() nest,
    // which can happen through the calls to QS_TEST_PAUSE().
    rxPriv_.inTestLoop = true;
}
