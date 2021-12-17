/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-10-19     JasonHu      first version
 * 2021-11-12     JasonHu      fix bug that not intr on f133
 */

#include <rtthread.h>

#include <rtdbg.h>

#include "plic.h"
#include "rt_interrupt.h"
#include "io.h"
#include "encoding.h"

static void *c906_plic_regs = RT_NULL;

struct plic_handler
{
    rt_bool_t present;
    void *hart_base;
    void *enable_base;
};

rt_inline void plic_toggle(struct plic_handler *handler, int hwirq, int enable);
struct plic_handler c906_plic_handlers[C906_NR_CPUS];

rt_inline void plic_irq_toggle(int hwirq, int enable)
{
    int cpu = 0;

    /* set priority of interrupt, interrupt 0 is zero. */
    writel(enable, c906_plic_regs + PRIORITY_BASE + hwirq * PRIORITY_PER_ID);
    struct plic_handler *handler = &c906_plic_handlers[cpu];

    if (handler->present)
    {
        plic_toggle(handler, hwirq, enable);
    }
}

void plic_complete(int irqno)
{
    int cpu = 0;
    struct plic_handler *handler = &c906_plic_handlers[cpu];

    writel(irqno, handler->hart_base + CONTEXT_CLAIM);
}

void plic_disable_irq(int irqno)
{
    plic_irq_toggle(irqno, 0);
}

void plic_enable_irq(int irqno)
{
    plic_irq_toggle(irqno, 1);
}

/*
 * Handling an interrupt is a two-step process: first you claim the interrupt
 * by reading the claim register, then you complete the interrupt by writing
 * that source ID back to the same claim register.  This automatically enables
 * and disables the interrupt, so there's nothing else to do.
 */
void plic_handle_irq(void)
{
    int cpu = 0;
    unsigned int irq;

    struct plic_handler *handler = &c906_plic_handlers[cpu];
    void *claim = handler->hart_base + CONTEXT_CLAIM;

    if (c906_plic_regs == RT_NULL || !handler->present)
    {
        LOG_E("plic state not initialized.");
        return;
    }

    clear_csr(sie, SIE_SEIE);

    while ((irq = readl(claim)))
    {
        /* ID0 is diabled permantually from spec. */
        if (irq == 0)
        {
            LOG_E("irq no is zero.");
        }
        else
        {
            generic_handle_irq(irq);
        }
    }
    set_csr(sie, SIE_SEIE);
}

rt_inline void plic_toggle(struct plic_handler *handler, int hwirq, int enable)
{
    uint32_t  *reg = handler->enable_base + (hwirq / 32) * sizeof(uint32_t);
    uint32_t hwirq_mask = 1 << (hwirq % 32);

    if (enable)
    {
        writel(readl(reg) | hwirq_mask, reg);
    }
    else
    {
        writel(readl(reg) & ~hwirq_mask, reg);
    }
}

void plic_init(void)
{
    int nr_irqs;
    int nr_context;
    int i;
    unsigned long hwirq;
    int cpu = 0;

    if (c906_plic_regs)
    {
        return;
    }

    nr_context = C906_NR_CONTEXT;

    c906_plic_regs = (void *)C906_PLIC_PHY_ADDR;
    nr_irqs = C906_PLIC_NR_EXT_IRQS;

    for (i = 0; i < nr_context; i ++)
    {
        struct plic_handler *handler;
        uint32_t threshold = 0;

        cpu = 0;

        /* skip contexts other than supervisor external interrupt */
        if (i == 0)
        {
            continue;
        }

        // we always use CPU0 M-mode target register.
        handler = &c906_plic_handlers[cpu];
        if (handler->present)
        {
            threshold  = 0xffffffff;
            goto done;
        }

        handler->present = RT_TRUE;
        handler->hart_base = c906_plic_regs + CONTEXT_BASE + i * CONTEXT_PER_HART;
        handler->enable_base = c906_plic_regs + ENABLE_BASE + i * ENABLE_PER_HART;
done:
        /* priority must be > threshold to trigger an interrupt */
        writel(threshold, handler->hart_base + CONTEXT_THRESHOLD);
        for (hwirq = 1; hwirq <= nr_irqs; hwirq++)
        {
            plic_toggle(handler, hwirq, 0);
        }
    }

    /* Enable supervisor external interrupts. */
    set_csr(mie, MIP_MEIP);
}
