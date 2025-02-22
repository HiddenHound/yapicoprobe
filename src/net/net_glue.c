/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


//----------------------------------------------------------------------------------------------------------------------
//
// Stuff to glue lwIP and TinyUSB together.
// Code might be confusing because it is trying hard to call everything in the right context.
// It is assumed that net_glue is running in a FreeRTOS environment.
//

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "picoprobe_config.h"

#include "lwip/tcpip.h"
#include "dhserver.h"

#include "tusb.h"
#include "device/usbd_pvt.h"             // for usbd_defer_func
#include "tinyusb/net_device.h"


/// lwIP context
static struct netif netif_data;

/// Buffer for lwIP <- TinyUSB transmission
static uint8_t  rcv_buff[CFG_TUD_NET_MTU + 10];    // MTU plus some margin
static uint16_t rcv_buff_len = 0;

/// Buffer for lwIP -> TinyUSB transmission
static uint8_t  xmt_buff[CFG_TUD_NET_MTU + 10];    // MTU plus some margin
static uint16_t xmt_buff_len = 0;

#ifndef OPT_NET_192_168
    #define OPT_NET_192_168   14
#endif

/* network parameters of this MCU */
static const ip4_addr_t ipaddr  = IPADDR4_INIT_BYTES(192, 168, OPT_NET_192_168, 1);
static const ip4_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

/* database IP addresses that can be offered to the host; this must be in RAM to store assigned MAC addresses */
static dhcp_entry_t entries[] =
{
    /* mac ip address                          lease time */
    {
        .mac   = {0},
        .addr  = IPADDR4_INIT_BYTES(192, 168, OPT_NET_192_168, 2),
        .lease = 24 * 60 * 60
    },
};

static const dhcp_config_t dhcp_config =
{
    .router    = IPADDR4_INIT_BYTES(0, 0, 0, 0),    // router address (if any)
    .port      = 67,                                // listen port
    .dns       = IPADDR4_INIT_BYTES(0, 0, 0, 0),    // dns server
    .domain    = NULL,                              // dns suffix: specify NULL, otherwise /etc/resolv.conf will be changed
    .num_entry = TU_ARRAY_SIZE(entries),            // num entry
    .entries   = entries                            // entries
};



void tud_network_init_cb(void)
/**
 * initialize any network state back to the beginning
 */
{
    rcv_buff_len = 0;
    xmt_buff_len = 0;
}   // tud_network_init_cb



static void context_tinyusb_tud_network_recv_renew(void *param)
/**
 * Reenable reception logic in TinyUSB.
 *
 * Context: TinyUSB
 */
{
    tud_network_recv_renew();
}   // context_tinyusb_tud_network_recv_renew



static void net_glue_usb_to_lwip(void *ptr)
/**
 * Handle any packet received by tud_network_recv_cb()
 *
 * Context: lwIP
 */
{
    //printf("net_glue_usb_to_lwip\n");

    if (rcv_buff_len != 0) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, rcv_buff_len, PBUF_POOL);

        if (p) {
            memcpy(p->payload, rcv_buff, rcv_buff_len);
            ethernet_input(p, &netif_data);
            pbuf_free(p);
            rcv_buff_len = 0;
            usbd_defer_func(context_tinyusb_tud_network_recv_renew, NULL, false);
        }
    }
}   // net_glue_usb_to_lwip



bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
/**
 * Copy buffer (host ->) TinyUSB -> lwIP (-> application)
 *
 * Context: TinyUSB
 *
 * \return false if the packet buffer was not accepted
 */
{
    //printf("tud_network_recv_cb(%p,%u)\n", src, size);

    if (rcv_buff_len != 0)
        return false;

    if (size != 0) {
        assert(size < sizeof(rcv_buff));
        memcpy(rcv_buff, src, size);
        rcv_buff_len = size;
        tcpip_callback_with_block(net_glue_usb_to_lwip, NULL, 0);
    }
    return true;
}   // tud_network_recv_cb



uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
/**
 * This does the actual copy operation into a TinyUSB buffer.
 * Called by tud_network_xmit().
 *
 * (application ->) lwIP -> TinyUSB (-> host)
 *
 * Context: TinyUSB
 *
 * \return number of bytes copied
 */
{
    //printf("!!!!!!!!!!!!!!tud_network_xmit_cb(%p,%p,%u)\n", dst, ref, arg);

    uint16_t r = xmt_buff_len;
    memcpy(dst, xmt_buff, xmt_buff_len);
    xmt_buff_len = 0;
    return r;
}   // tud_network_xmit_cb



static void context_tinyusb_linkoutput(void *param)
/**
 * Put \a xmt_buff into TinyUSB (if possible).
 *
 * Context: TinyUSB
 */
{
    if ( !tud_network_can_xmit(xmt_buff_len)) {
        //printf("context_tinyusb_linkoutput: sleep\n");
        vTaskDelay(pdMS_TO_TICKS(5));
        usbd_defer_func(context_tinyusb_linkoutput, NULL, false);
    }
    else {
        tud_network_xmit(xmt_buff, 0);
    }
}   // context_tinyusb_linkoutput



static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
/**
 * called by lwIP to transmit data to TinyUSB
 *
 * Context: lwIP
 */
{
    if ( !tud_ready()) {
        return ERR_USE;
    }

    if (xmt_buff_len != 0) {
        return ERR_USE;
    }

    // copy data into temp buffer
    xmt_buff_len = pbuf_copy_partial(p, xmt_buff, p->tot_len, 0);
    assert(xmt_buff_len < sizeof(xmt_buff));

    usbd_defer_func(context_tinyusb_linkoutput, NULL, false);

    return ERR_OK;
}   // linkoutput_fn



static err_t ip4_output_fn(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    return etharp_output(netif, p, addr);
}   // ip4_output_fn



#if LWIP_IPV6
static err_t ip6_output_fn(struct netif *netif, struct pbuf *p, const ip6_addr_t *addr)
{
    return ethip6_output(netif, p, addr);
}   // ip6_output_fn
#endif



static err_t netif_init_cb(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'E';
    netif->name[1] = 'X';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
#if LWIP_IPV6
    netif->output_ip6 = ip6_output_fn;
#endif
    return ERR_OK;
}   // netif_init_cb



void net_glue_init(void)
{
    struct netif *netif = &netif_data;

    tcpip_init(NULL, NULL);

    /* the lwip virtual MAC address must be different from the host's; to ensure this, we toggle the LSbit */
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;     // don't know what for, but everybody is doing it...

    netif = netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ip_input);
#if LWIP_IPV6
    netif_create_ip6_linklocal_address(netif, 1);
#endif
    netif_set_default(netif);

    while ( !netif_is_up(netif))
        ;

    while (dhserv_init(&dhcp_config) != ERR_OK)
        ;
}   // net_glue_init
