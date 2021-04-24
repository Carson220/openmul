/*
 *  my_controller.c: my_controller application for MUL Controller 
 *  Copyright (C) 2012, Dipjyoti Saikia <dipjyoti.saikia@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "config.h"
#include "mul_common.h"
#include "mul_vty.h"
#include "my_controller.h"
#include "LLDP.h"
#include "tp_graph.h"
#include "tp_route.h"
#include "ARP.h"

struct event *my_controller_timer;
struct mul_app_client_cb my_controller_app_cbs;

extern arp_hash_table_t * arp_table;//arp hash table handler
extern tp_sw * tp_graph;//topo hash handler

/**
 * my_controller_install_dfl_flows -
 * Installs default flows on a switch
 *
 * @dpid : Switch's datapath-id
 * @return : void
 */
static void
my_controller_install_dfl_flows(uint64_t dpid)
{
    struct flow fl;
    struct flow mask;
    //struct mul_act_mdata mdata; 
    mul_act_mdata_t mdata;

    memset(&fl, 0, sizeof(fl));
    of_mask_set_dc_all(&mask);

    /* Clear all entries for this switch */
    mul_app_send_flow_del(MY_CONTROLLER_APP_NAME, NULL, dpid, &fl,
                          &mask, OFPP_NONE, 0, C_FL_ENT_NOCACHE, OFPG_ANY);

    /* Zero DST MAC Drop */
    of_mask_set_dl_dst(&mask); 
    mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dpid, &fl, &mask,
                          MY_CONTROLLER_UNK_BUFFER_ID, NULL, 0, 0, 0, 
                          C_FL_PRIO_DRP, C_FL_ENT_NOCACHE);  

    /* Zero SRC MAC Drop */
    of_mask_set_dc_all(&mask);
    of_mask_set_dl_src(&mask); 
    mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dpid, &fl, &mask, 
                          MY_CONTROLLER_UNK_BUFFER_ID, NULL, 0, 0, 0,  
                          C_FL_PRIO_DRP, C_FL_ENT_NOCACHE);

    /* Broadcast SRC MAC Drop */
    memset(&fl.dl_src, 0xff, OFP_ETH_ALEN);
    mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dpid, &fl, &mask,
                          MY_CONTROLLER_UNK_BUFFER_ID, NULL, 0, 0, 0,
                          C_FL_PRIO_DRP, C_FL_ENT_NOCACHE);

    /* Send any unknown flow to app */
    memset(&fl, 0, sizeof(fl));
    of_mask_set_dc_all(&mask);
    mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dpid, &fl, &mask,
                          MY_CONTROLLER_UNK_BUFFER_ID, NULL, 0, 0, 0,
                          C_FL_PRIO_DRP, C_FL_ENT_LOCAL);

    /* Add Flow to receive LLDP Packet type */
    memset(&fl, 0, sizeof(fl));
    
    fl.dl_type = htons(0x88cc);
    of_mask_set_dl_type(&mask);

    mul_app_act_alloc(&mdata);
    mul_app_act_set_ctors(&mdata, dpid);
    mul_app_action_output(&mdata, 0); /* Send to controller */

    mul_app_send_flow_add(MUL_TR_SERVICE_NAME, NULL, dpid, &fl, &mask,
                          (uint32_t)-1,
                          mdata.act_base, mul_app_act_len(&mdata),
                          0, 0, C_FL_PRIO_EXM, C_FL_ENT_LOCAL);
    mul_app_act_free(&mdata);
}


/**
 * my_controller_sw_add -
 * Switch join event notifier
 * 
 * @sw : Switch arg passed by infra layer
 * @return : void
 */
static void 
my_controller_sw_add(mul_switch_t *sw)
{
    /* Add few default flows in this switch */
    my_controller_install_dfl_flows(sw->dpid);

    //topo Add a sw node to the topo
    tp_add_sw(sw->dpid, tp_graph);
    //topo add the port information
    tp_add_sw_port(sw, tp_graph);

    //写入数据库，新加了一个交换机，且由该控制器控制

    c_log_debug("switch dpid 0x%ldx joined network", (uint64_t)(sw->dpid));
}

/**
 * my_controller_sw_del -
 * Switch delete event notifier
 *
 * @sw : Switch arg passed by infra layer
 * @return : void
 */
static void
my_controller_sw_del(mul_switch_t *sw)
{
    tp_delete_sw(sw->dpid, tp_graph);
    c_log_debug("switch dpid 0x%ldx left network", (uint64_t)(sw->dpid));
}

/**
 * my_controller_packet_in -
 * my_controller app's packet-in notifier call-back
 *
 * @sw : switch argument passed by infra layer (read-only)
 * @fl : Flow associated with the packet-in
 * @inport : in-port that this packet-in was received
 * @raw : Raw packet data pointer
 * @pkt_len : Packet length
 * 
 * @return : void
 */
static void 
my_controller_packet_in(mul_switch_t *sw UNUSED,
                struct flow *fl UNUSED,
                uint32_t inport UNUSED,
                uint32_t buffer_id UNUSED,
                uint8_t *raw UNUSED,
                size_t pkt_len UNUSED)
{
    //uint32_t                    oport = OF_ALL_PORTS;
    struct of_pkt_out_params    parms;
    struct mul_act_mdata mdata;
    uint16_t type;

    memset(&parms, 0, sizeof(parms));

    /* Check packet validity */
    if (is_zero_ether_addr(fl->dl_src) || 
        is_zero_ether_addr(fl->dl_dst) ||
        is_multicast_ether_addr(fl->dl_src) || 
        is_broadcast_ether_addr(fl->dl_src)) {
        //c_log_err("%s: Invalid src/dst mac addr", FN);
        return;
    }

    if (buffer_id != MY_CONTROLLER_UNK_BUFFER_ID) {
        pkt_len = 0;
    }

    /* check ether type. common-libs/mul-lib/include/packets.h 104 row*/
    type = ntohs(fl->dl_type);
    c_log_info("my_controller app - DL TYPE %dx", type);
    switch (type){
    case ETH_TYPE_LLDP:
        //LLDP 0x88cc
        lldp_proc(sw, fl, inport, buffer_id, raw, pkt_len);
        c_log_info("LLDP packet-in from network");
        break;
    case ETH_TYPE_IP:
        //IP 0x0800
        c_log_info("IP packet-in from network");
        mul_app_act_alloc(&mdata);
        mdata.only_acts = true;
        mul_app_act_set_ctors(&mdata, sw->dpid);
        mul_app_action_output(&mdata, OF_ALL_PORTS);
        parms.buffer_id = buffer_id;
        parms.in_port = inport;
        parms.action_list = mdata.act_base;
        parms.action_len = mul_app_act_len(&mdata);
        parms.data_len = pkt_len;
        parms.data = raw;
        mul_app_send_pkt_out(NULL, sw->dpid, &parms);
        mul_app_act_free(&mdata);
        break;
    case ETH_TYPE_ARP:
        //ARP 0x0806
        c_log_info("ARP packet-in from network");
        arp_proc(sw, fl, inport, buffer_id, raw, pkt_len);
        break;
    case ETH_TYPE_IPV6:
        //IPv6 0x86dd
        //c_log_info("IPv6 packet-in from network");
        break;
    default:
        c_log_debug("%s: ethertype 0x%hx not recognized ", FN, fl->dl_type);
        return;
    }
}

/**
 * my_controller_core_closed -
 * mul-core connection drop notifier
 */
static void
my_controller_core_closed(void)
{
    c_log_info("%s: ", FN);

    /* Nothing to do */
    return;
}

/**
 * my_controller_core_reconn -
 * mul-core reconnection notifier
 */
static void
my_controller_core_reconn(void)
{
    c_log_info("%s: ", FN);

    /* 
     * Once core connection has been re-established,
     * we need to re-register the app
     */
    mul_register_app_cb(NULL,                 /* Application specific arg */
                        MY_CONTROLLER_APP_NAME,       /* Application Name */
                        C_APP_ALL_SW,         /* Send any switch's notification */
                        C_APP_ALL_EVENTS,     /* Send all event notification per switch */
                        0,                    /* If any specific dpid filtering is requested */
                        NULL,                 /* List of specific dpids for filtering events */
                        &my_controller_app_cbs);      /* Event notifier call-backs */
}


/* Network event callbacks */
struct mul_app_client_cb my_controller_app_cbs = {
    .switch_priv_alloc = NULL,
    .switch_priv_free = NULL,
    .switch_add_cb =  my_controller_sw_add,         /* Switch add notifier */
    .switch_del_cb = my_controller_sw_del,          /* Switch delete notifier */
    .switch_priv_port_alloc = NULL,
    .switch_priv_port_free = NULL,
    .switch_port_add_cb = NULL,
    .switch_port_del_cb = NULL,
    .switch_port_link_chg = NULL,
    .switch_port_adm_chg = NULL,
    .switch_packet_in = my_controller_packet_in,    /* Packet-in notifier */ 
    .core_conn_closed = my_controller_core_closed,  /* Core connection drop notifier */
    .core_conn_reconn = my_controller_core_reconn   /* Core connection join notifier */
};  

/**
 * my_controller_timer_event -
 * Timer running at specified interval 
 * 
 * @fd : File descriptor used internally for scheduling event
 * @event : Event type
 * @arg : Any application specific arg
 */
static void
my_controller_timer_event(evutil_socket_t fd UNUSED,
                  short event UNUSED,
                  void *arg UNUSED)
{
    struct timeval tv = { 1 , 0 }; /* Timer set to run every one second */

    /* Any housekeeping activity */

    evtimer_add(my_controller_timer, &tv);
}  

/**
 * my_controller_module_init -
 * my_controller application's main entry point
 * 
 * @base_arg: Pointer to the event base used to schedule IO events
 * @return : void
 */
void
my_controller_module_init(void *base_arg)
{
    struct event_base *base = base_arg;
    struct timeval tv = { 1, 0 };

    c_log_debug("%s", FN);

    /* Fire up a timer to do any housekeeping work for this application */
    my_controller_timer = evtimer_new(base, my_controller_timer_event, NULL); 
    evtimer_add(my_controller_timer, &tv);

    mul_register_app_cb(NULL,                 /* Application specific arg */
                        MY_CONTROLLER_APP_NAME,       /* Application Name */ 
                        C_APP_ALL_SW,         /* Send any switch's notification */
                        C_APP_ALL_EVENTS,     /* Send all event notification per switch */
                        0,                    /* If any specific dpid filtering is requested */
                        NULL,                 /* List of specific dpids for filtering events */
                        &my_controller_app_cbs);      /* Event notifier call-backs */

    return;
}

/**
 * my_controller_module_vty_init -
 * my_controller Application's vty entry point. If we want any private cli
 * commands. then we register them here
 *
 * @arg : Pointer to the event base(mostly left unused)
 */
void
my_controller_module_vty_init(void *arg UNUSED)
{
    c_log_debug("%s:", FN);
}

module_init(my_controller_module_init);
module_vty_init(my_controller_module_vty_init);