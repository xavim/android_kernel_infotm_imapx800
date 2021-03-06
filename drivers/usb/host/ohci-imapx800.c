#include <linux/platform_device.h>
#include <linux/clk.h>
#include <mach/usb-phy.h>
#include <mach/items.h>

#define IMAP_DEBUG 	0 

#if IMAP_DEBUG
#define IMAP_OHCI_DEBUG(debugs, ...)	printk(KERN_INFO debugs,  ##__VA_ARGS__)
#else
#define IMAP_OHCI_DEBUG(debugs, ...)	do{}while(0)
#endif

static struct clk *bus_clk;
static struct clk *ref_clk;
extern char otg_phy_state;
static struct usb_hcd *imap_ohci_hcd;

static void imapx800_start_hc(struct platform_device *pdev,struct usb_hcd *hcd)
{

	IMAP_OHCI_DEBUG("++imapx800_start_hc \r\n");

	clk_enable(bus_clk);

	IMAP_OHCI_DEBUG("--imapx800_start_hc \r\n");
}

static void imapx800_stop_hc(struct platform_device *pdev)
{
	IMAP_OHCI_DEBUG("++imapx800_stop_hc \r\n");
	
	clk_disable(bus_clk);
	
	IMAP_OHCI_DEBUG("--imapx800_stop_hc \r\n");
}



static void usb_hcd_imapx800_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	imapx800_stop_hc(pdev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start,hcd->rsrc_len);
	usb_put_hcd(hcd);
}

uint32_t imap_ohci_port_state(void)
{
	uint32_t temp;
	struct ohci_hcd *ohci = hcd_to_ohci(imap_ohci_hcd);

	temp = ohci_readl(ohci, &ohci->regs->roothub.portstatus[2]);
	return temp&0x1;	
}
EXPORT_SYMBOL(imap_ohci_port_state);


static int usb_hcd_imapx800_probe(const struct hc_driver *driver,struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct platform_data *info;
	int ret,irq;
	struct resource *r_mem;

	IMAP_OHCI_DEBUG("++usb_hcd_imapx800_probe\r\n");
	
	info = pdev->dev.platform_data;
	if(info)
		return -ENODEV;

	irq = platform_get_irq(pdev,0);
	if(irq < 0)
	{
		dev_err(&pdev->dev,"no resource of IORESOURCE_IRQ");
		return -ENXIO;
	}
	IMAP_OHCI_DEBUG("USB Resource IRQ is 0x%x \r\n",irq);

	r_mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(!r_mem)
	{
		dev_err(&pdev->dev,"no resource of IORESOURCE_MEM");
		return -ENXIO;
	}
	hcd = usb_create_hcd(driver,&pdev->dev, "imapx800");
	if(hcd == NULL)
	{
		dev_err(&pdev->dev,"usb_create_hcd failed! \r\n");
		return -ENOMEM;
	}
	imap_ohci_hcd = hcd;

	hcd->rsrc_start = r_mem->start;
	hcd->rsrc_len = resource_size(r_mem);
	IMAP_OHCI_DEBUG("USB Resource MEM is 0x%x \r\n",r_mem->start);
	IMAP_OHCI_DEBUG("USB Resource Len is 0x%x \r\n",hcd->rsrc_len);

	if(!request_mem_region(hcd->rsrc_start,hcd->rsrc_len,hcd_name))
	{
		dev_err(&pdev->dev,"request_mem_region failed");
		ret = -EBUSY;
		goto err_put;
	}

	bus_clk = clk_get(NULL, "usb ohci");
	if(IS_ERR(bus_clk)) {
		dev_err(&pdev->dev, "can't get usb bus clock\n");
		ret = PTR_ERR(bus_clk);
		goto err_mem;
	}

	ref_clk = clk_get(NULL, "usb-ref");
	if(IS_ERR(ref_clk)) {
		dev_err(&pdev->dev, "can't get phy ref clock\n");
		ret = PTR_ERR(ref_clk);
		goto err_clk;
	}

	hcd->regs = ioremap(hcd->rsrc_start,hcd->rsrc_len);
	if(!hcd->regs)
	{
		dev_err(&pdev->dev,"ioremap failed \r\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	IMAP_OHCI_DEBUG("USB Register Map Address 0x%x \r\n",hcd->regs);

	imapx800_start_hc(pdev,hcd);

	ohci_hcd_init(hcd_to_ohci(hcd));

	IMAP_OHCI_DEBUG("ohci_hcd_init ok! \r\n");
	ret = usb_add_hcd(hcd,irq,IRQF_DISABLED);
	if(ret != 0)
	{
		dev_err(&pdev->dev,"usb_add_hcd failed \r\n");
		goto err_ioremap;
	}
	IMAP_OHCI_DEBUG("--usb_hcd_imapx800_probe \r\n");

	return 0;

err_ioremap:
	imapx800_stop_hc(pdev);
	iounmap(hcd->regs);
	clk_put(ref_clk);

err_clk:
	clk_put(bus_clk);

err_mem:
	release_mem_region(hcd->rsrc_start,hcd->rsrc_len);

err_put:
	usb_put_hcd(hcd);

	return ret;
}


static int ohci_imapx800_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	IMAP_OHCI_DEBUG("++ohci_imapx800_start \r\n");
	if((ret = ohci_init(ohci))<0) 
	{
		return ret;
	}
	IMAP_OHCI_DEBUG("++ohci_init success \r\n");
	
	if((ret = ohci_run(ohci)) < 0) 
	{
		err("ohci can't start %s",hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}
	IMAP_OHCI_DEBUG("--ohci_imapx800_start \r\n");

	return 0;
}


static const struct hc_driver ohci_imapx800_hc_driver = {
	.description = hcd_name,
	.product_desc = "Infotm Ohci Controller",
	.hcd_priv_size = sizeof(struct ohci_hcd),
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	.start = ohci_imapx800_start,
	.stop = ohci_stop,
	.shutdown = ohci_shutdown,

	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,
	.get_frame_number = ohci_get_frame,
	
	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif	
	.start_port_reset = ohci_start_port_reset,
};

static int ohci_hcd_imapx800_drv_probe(struct platform_device *pdev)
{
        if(item_exist("usb.enable"))
        {
            if(item_integer("usb.enable", 0) == 0)
                return -ENODEV;
        }
	return usb_hcd_imapx800_probe(&ohci_imapx800_hc_driver,pdev);
}

static int ohci_hcd_imapx800_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_imapx800_remove(hcd,pdev);
	return 0;
}

#ifdef CONFIG_PM
static int ohci_hcd_imapx800_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	unsigned long flags;
	int rc;

	rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

bail:
	spin_unlock_irqrestore(&ohci->lock, flags);

	return rc;

}

static int ohci_hcd_imapx800_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	int clk_rate;

        clk_enable(bus_clk);
        clk_enable(ref_clk);
	clk_rate = clk_get_rate(ref_clk);
	printk("usb host ref_clock = %d\n", clk_rate);
        if(item_exist("otg.function"))
        {
            if(item_equal("otg.function", "usb", 0))
                otg_phy_config(clk_rate, 1, 0);
        }
	host_phy_config(clk_rate, 1);
    if(otg_phy_state)
        otg_phy_config(24000000,1,0);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);

	return 0;

}

static struct dev_pm_ops imapx800_ohci_pmops = {
	.suspend = ohci_hcd_imapx800_drv_suspend,
	.resume = ohci_hcd_imapx800_drv_resume,
};

#define IMAPX800_OHCI_PMOPS &imapx800_ohci_pmops

#else
#define IMAPX800_OHCI_PMOPS NULL
#endif

static struct platform_driver ohci_hcd_imapx800_driver = {
	.probe	= ohci_hcd_imapx800_drv_probe,
	.remove	= ohci_hcd_imapx800_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "imap-ohci",
		.pm     = IMAPX800_OHCI_PMOPS,
	},
};
