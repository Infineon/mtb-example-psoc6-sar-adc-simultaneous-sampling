/******************************************************************************
* File Name:   main.c
*
* Description: In this code example, two SAR ADCs are configured to sample
*              external signals simultaneously. In the firmware, product of the
*              results are calculated and sent to UART Terminal. The scaled
*              value of product is loaded into DAC.
*
* Related Document: See README.md
*
*
*******************************************************************************
* (c) 2020, Cypress Semiconductor Corporation. All rights reserved.
*******************************************************************************
* This software, including source code, documentation and related materials
* ("Software"), is owned by Cypress Semiconductor Corporation or one of its
* subsidiaries ("Cypress") and is protected by and subject to worldwide patent
* protection (United States and foreign), United States copyright laws and
* international treaty provisions. Therefore, you may use this Software only
* as provided in the license agreement accompanying the software package from
* which you obtained this Software ("EULA").
*
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software source
* code solely for use in connection with Cypress's integrated circuit products.
* Any reproduction, modification, translation, compilation, or representation
* of this Software except as specified above is prohibited without the express
* written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer of such
* system or application assumes all risk of such use and in doing so agrees to
* indemnify Cypress against all liability.
*******************************************************************************/

#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
********************************************************************************/
/*
 * Scaling factor is to reduce the product of inputs to range of 0V - 3.3V
 * The values to be set in CTDAC next value register is from 0 to 4095
 * The maximum product of two inputs can be 3.3V*3.3V = 10.89V
 * So 4095 represents 10.89V; 1V is represented by 372
 * Since the output on Analog pin ranges from 0 to 3.3V, the output measured
 * on the pin is to be multiplied with 3.3 to get the correct result.
 *
 */
#define SCALING_FACTOR 372

/* TCPWM Counter 0 */
#define TCPWM_CNT_NUM   (0UL)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
/* Analog Initialization Function */
void init_analog_resources(void);

/* SAR0 Interrupt Handler */
void sar0_interrupt(void);

/* SAR1 Interrupt Handler */
void sar1_interrupt(void);

/* SAR0 interrupt configuration structure */
/* Source is set to SAR0 and Priority as 7 */
const cy_stc_sysint_t SAR0_IRQ_cfg = {
    .intrSrc = (IRQn_Type) pass_interrupt_sar_0_IRQn,
    .intrPriority = 7UL
};

/* SAR1 interrupt configuration structure */
/* Source is set to SAR1 and Priority as 7 */
const cy_stc_sysint_t SAR1_IRQ_cfg = {
    .intrSrc = (IRQn_Type) pass_interrupt_sar_1_IRQn,
    .intrPriority = 7UL
};

/* Flags to check End-Of-Scan interrupt from SAR0 and SAR1 */
static bool sar0_isr_set = false;
static bool sar1_isr_set = false;
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  System entrance point. This function performs initial setup, initializes
*  analog component - SAR ADC Block and CTDAC, it samples the input voltages,
*  calculates its product, displays the result on UART Terminal and outputs the
*  scaled value on P9.2 analog output pin.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    /* Variable to capture return value of functions */
    cy_rslt_t result;

    /* Variable to hold data retrieved from SAR result register */
    int16_t sar_result0 = 0, sar_result1 = 0;
    float32_t resultV_0 = 0, resultV_1 = 0;
    float32_t product_result = 0;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize the debug UART */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                     CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Print message */

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("-----------------------------------------------------------\r\n");
    printf("PSoC 6 MCU: Simultaneous Sampling SAR ADCs \r\n");
    printf("-----------------------------------------------------------\r\n\n");
    printf("Provide input voltages at pin P10.0 and P10.2 and observe \r\n");
    printf("the scaled product of inputs on pin P9.2.\r\n\n");

    /* Initialize analog resources */
    init_analog_resources();

    /* Enable IRQ */
    __enable_irq();

    /* Start the TCPWM Timer */
    Cy_TCPWM_TriggerStart_Single(TCPWM0, TCPWM_CNT_NUM);

    for (;;)
    {
        /* Wait till printf completes the UART transfer */
        while(cyhal_uart_is_tx_active(&cy_retarget_io_uart_obj) == true);

        /* Sleep until both SAR conversions are complete */
        while(!(sar0_isr_set & sar1_isr_set))
        {
             Cy_SysPm_CpuEnterSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
        }

        /* Clear the flags */
        sar0_isr_set = false;
        sar1_isr_set = false;
        
        /* Retrieve value from SAR result register */
        sar_result0 = Cy_SAR_GetResult16(SAR0, 0 );
        sar_result1 = Cy_SAR_GetResult16(SAR1, 0 );

        /* Convert data retrieved from SAR to Volts */
        resultV_0 = Cy_SAR_CountsTo_Volts(SAR0, 0, sar_result0);
        resultV_1 = Cy_SAR_CountsTo_Volts(SAR1, 0, sar_result1);

        /* Product of the result obtained */
        product_result = resultV_0 * resultV_1;
        /* Scale the result of the product for range 0V to 3.3V and output to pin*/
        Cy_CTDAC_SetValue(CTDAC0, (int)(product_result*SCALING_FACTOR));

        /* Print the inputs and the result */
        printf("SAR0 input: %.2fV \t SAR1 input: %.2fV\r\n", resultV_0, resultV_1);

    }
}

/*******************************************************************************
* Function Name: init_analog_resources
********************************************************************************
* Summary:
* This function initializes analog components - CTBM, SAR ADC and CTDAC
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void init_analog_resources(void)
{
    /* Variable to capture return value of functions */
    cy_rslt_t result;

    /* Initialize AREF */
    result = Cy_SysAnalog_Init(&pass_0_aref_0_config);
    if (CY_SYSANALOG_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Enable AREF */
    Cy_SysAnalog_Enable();

    /* Initialize common resources for SAR ADCs. */
    /* Common resources include simultaneous trigger parameters, scan count
       and power up delay. This is configured in the device configurator. */
    result = Cy_SAR_CommonInit(PASS, &pass_0_saradc_0_config);
    if (CY_SAR_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Initialize SAR0 and SAR1 */
    result = Cy_SAR_Init(SAR0, &pass_0_saradc_0_sar_0_config );
    if (CY_SAR_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    result = Cy_SAR_Init(SAR1, &pass_0_saradc_0_sar_1_config );
    if (CY_SAR_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Enable SAR block */
    Cy_SAR_Enable(SAR0);
    Cy_SAR_Enable(SAR1);

    Cy_SAR_SetInterruptMask(SAR0, CY_SAR_INTR);
    Cy_SAR_SetInterruptMask(SAR1, CY_SAR_INTR);

    (void)Cy_SysInt_Init(&SAR0_IRQ_cfg, sar0_interrupt);
    (void)Cy_SysInt_Init(&SAR1_IRQ_cfg, sar1_interrupt);

    /* Enable the SAR interrupts */
    NVIC_EnableIRQ(SAR0_IRQ_cfg.intrSrc);
    NVIC_EnableIRQ(SAR1_IRQ_cfg.intrSrc);

    /* Enable OpAmp for buffered output of CTDAC */
    /* The routing from CTDAC to CTBM is configured using the design.modus file */
    result = Cy_CTB_OpampInit(CTBM0, CY_CTB_OPAMP_0, &pass_0_ctb_0_oa_0_config);
    if(CY_CTB_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Initialize DAC block*/
    result = Cy_CTDAC_Init(CTDAC0, &pass_0_ctdac_0_config);
    if(CY_CTDAC_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Enable OpAmp and CTDAC */
    Cy_CTDAC_Enable(CTDAC0);
    Cy_CTB_Enable(CTBM0);

    /* Initialize TCPWM Counter */
    result = Cy_TCPWM_Counter_Init(TCPWM0, TCPWM_CNT_NUM, &tcpwm_0_group_0_cnt_0_config);
    if(CY_TCPWM_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    /* Enable the initialized counter */
    Cy_TCPWM_Counter_Enable(TCPWM0, TCPWM_CNT_NUM);
}

/*******************************************************************************
* Function Name: sar0_interrupt
********************************************************************************
* Summary:
* This function is the handler for SAR0 interrupt
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void sar0_interrupt(void)
{
    /* Check if End-Of-Scan trigger has occurred. If yes, set sar0_isr_set flag to true  */
    if (Cy_SAR_GetInterruptStatus(SAR0) & CY_SAR_INTR_EOS)
    {
        sar0_isr_set = true;
    }

    /* Clear the interrupts */
    Cy_SAR_ClearInterrupt(SAR0, CY_SAR_INTR);
}

/*******************************************************************************
* Function Name: sar1_interrupt
********************************************************************************
* Summary:
* This function is the handler for SAR1 interrupt
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void sar1_interrupt(void)
{
    /* Check if End-Of-Scan trigger has occurred. If yes, set sar1_isr_set flag to true  */
    if (Cy_SAR_GetInterruptStatus(SAR1) & CY_SAR_INTR_EOS)
    {
        sar1_isr_set = true;
    }

    /* Clear the interrupts */
    Cy_SAR_ClearInterrupt(SAR1, CY_SAR_INTR);
}
/* [] END OF FILE */
