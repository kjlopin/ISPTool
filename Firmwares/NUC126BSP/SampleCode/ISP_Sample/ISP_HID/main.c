/******************************************************************************
 * @file     main.c
 * @brief
 *           Demonstrate how to transfer ISP Command between USB device and PC through USB HID interface.
 *           A windows tool is also included in this sample code to connect with USB device.
 *
 * @note
 * Copyright (C) 2016 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#include <stdio.h>
#include "NUC126.h"
#include "hid_transfer.h"
#include "targetdev.h"

#define PLLCTL_SETTING  CLK_PLLCTL_144MHz_HXT
#define PLL_CLOCK       144000000

/* If crystal-less is enabled, system won't use any crystal as clock source
   If using crystal-less, system will be 48MHz, otherwise, system is 72MHz
*/
#define CRYSTAL_LESS    1
#define HIRC48_AUTO_TRIM    0x412   /* Use USB SOF to fine tune HIRC 48MHz */

void SYS_Init(void)
{
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init System Clock                                                                                       */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Enable HIRC clock (Internal RC 22.1184MHz) */
    CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk;

    /* Waiting for HIRC clock ready */
    while (!(CLK->STATUS & CLK_STATUS_HIRCSTB_Msk));

    /* Select HCLK clock source as HIRC and and HCLK clock divider as 1 */
    CLK->CLKSEL0 = (CLK->CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) | CLK_CLKSEL0_HCLKSEL_HIRC;
    CLK->CLKDIV0 = (CLK->CLKDIV0 & ~CLK_CLKDIV0_HCLKDIV_Msk) | CLK_CLKDIV0_HCLK(1);
#ifndef CRYSTAL_LESS
    /* Enable HXT clock (external XTAL 12MHz) */
    CLK->PWRCTL |= CLK_PWRCTL_HXTEN_Msk;

    /* Waiting for HXT clock ready */
    while (!(CLK->STATUS & CLK_STATUS_HXTSTB_Msk));

    /* Enable PLL and Set PLL frequency */
    CLK->PLLCTL = PLLCTL_SETTING;

    while (!(CLK->STATUS & CLK_STATUS_PLLSTB_Msk));

    CLK->CLKSEL0 = (CLK->CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) | CLK_CLKSEL0_HCLKSEL_PLL;
    /* Select HCLK clock divider as 2 */
    CLK->CLKDIV0 = (CLK->CLKDIV0 & ~CLK_CLKDIV0_HCLKDIV_Msk) | CLK_CLKDIV0_HCLK(2);
    CLK->CLKSEL3 |= CLK_CLKSEL3_USBDSEL_PLL;
    CLK->CLKDIV0 &= ~CLK_CLKDIV0_USBDIV_Msk;
    CLK->CLKDIV0 |= CLK_CLKDIV0_USB(3);
    PllClock        = PLL_CLOCK;            // PLL
    SystemCoreClock = PLL_CLOCK / 2;        // HCLK
    CyclesPerUs     = SystemCoreClock / 1000000;  // For SYS_SysTickDelay()
#else
    CLK->PWRCTL |= CLK_PWRCTL_HIRC48EN_Msk;

    while (!(CLK->STATUS & CLK_STATUS_HIRC48STB_Msk));

    CLK->CLKSEL0 = (CLK->CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) | CLK_CLKSEL0_HCLKSEL_HIRC48;
    CLK->CLKSEL3 &= ~CLK_CLKSEL3_USBDSEL_Msk;
    CLK->CLKDIV0 &= ~CLK_CLKDIV0_USBDIV_Msk;
    CLK->CLKDIV0 |= CLK_CLKDIV0_USB(1);
    SystemCoreClock = 48000000;        // HCLK
    CyclesPerUs     = SystemCoreClock / 1000000;  // For SYS_SysTickDelay()
#endif
    /* Enable module clock */
    CLK->APBCLK0 |= CLK_APBCLK0_USBDCKEN_Msk;
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Main Function                                                                                          */
/*---------------------------------------------------------------------------------------------------------*/
int32_t main(void)
{
    /* Unlock write-protected registers */
    SYS_UnlockReg();
    WDT->CTL &= ~(WDT_CTL_WDTEN_Msk | WDT_CTL_ICEDEBUG_Msk);
    WDT->CTL |= (WDT_TIMEOUT_2POW18 | WDT_CTL_RSTEN_Msk);
    /* Init system and multi-funcition I/O */
    SYS_Init();
    FMC->ISPCTL |= FMC_ISPCTL_ISPEN_Msk;	// (1ul << 0)
    g_apromSize = GetApromSize();
    GetDataFlashInfo(&g_dataFlashAddr, &g_dataFlashSize);

    while (DetectPin == 0) {
        /* Open USB controller */
        USBD_Open(&gsInfo, HID_ClassRequest, NULL);
        /*Init Endpoint configuration for HID */
        HID_Init();
        /* Start USB device */
        USBD_Start();
#ifdef CRYSTAL_LESS
        /* Waiting for SOF before USB clock auto trim */
        USBD->INTSTS = USBD_INTSTS_SOFIF_Msk;

        while ((USBD->INTSTS & USBD_INTSTS_SOFIF_Msk) == 0);

        /* Enable USB clock trim function */
        SYS->IRCTCTL1 = HIRC48_AUTO_TRIM;
#endif
        /* Enable USB device interrupt */
        NVIC_EnableIRQ(USBD_IRQn);

        while (DetectPin == 0) {
#ifdef CRYSTAL_LESS

            /* Re-start auto trim when any error found */
            if (SYS->IRCTISTS & (SYS_IRCTISTS_CLKERRIF1_Msk | SYS_IRCTISTS_TFAILIF1_Msk)) {
                /* USB clock trim fail. Just retry */
                SYS->IRCTCTL1 = 0;  /* Disable Auto Trim */
                SYS->IRCTISTS = SYS_IRCTISTS_CLKERRIF1_Msk | SYS_IRCTISTS_TFAILIF1_Msk;
                /* Waiting for SOF before USB clock auto trim */
                USBD->INTSTS = USBD_INTSTS_SOFIF_Msk;

                while ((USBD->INTSTS & USBD_INTSTS_SOFIF_Msk) == 0);

                SYS->IRCTCTL1 = HIRC48_AUTO_TRIM; /* Re-enable Auto Trim */
            }

#endif

            if (bUsbDataReady == TRUE) {
                WDT->CTL &= ~(WDT_CTL_WDTEN_Msk | WDT_CTL_ICEDEBUG_Msk);
                WDT->CTL |= (WDT_TIMEOUT_2POW18 | WDT_CTL_RSTEN_Msk);
                ParseCmd((uint8_t *)usb_rcvbuf, EP3_MAX_PKT_SIZE);
                EP2_Handler();
                bUsbDataReady = FALSE;
            }
        }

        goto _APROM;
    }

    SysTick->LOAD = 300000 * CyclesPerUs;
    SysTick->VAL  = (0x00);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    /* Waiting for down-count to zero */
    while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0);

_APROM:
    outpw(&SYS->RSTSTS, 3);//clear bit
    outpw(&FMC->ISPCTL, inpw(&FMC->ISPCTL) & 0xFFFFFFFC);
    outpw(&SCB->AIRCR, (V6M_AIRCR_VECTKEY_DATA | V6M_AIRCR_SYSRESETREQ));

    /* Trap the CPU */
    while (1);
}


/*** (C) COPYRIGHT 2016 Nuvoton Technology Corp. ***/

