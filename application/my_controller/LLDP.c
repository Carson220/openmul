#include "LLDP.h"
#include "tp_graph.h"
#include <sys/time.h>
#include "db_wr.h"

extern tp_sw * tp_graph;
extern tp_swdpid_glabolkey * key_table;

uint64_t lldp_get_timeval(void)
{
    struct timeval t;
    gettimeofday(&t, 0);
    //c_log_debug("get time sec: %ld, usec: %ld", t.tv_sec, t.tv_usec);
    return t.tv_sec*1000000 + t.tv_usec;//us
}

/**
 * lldp_create_packet -
 *
 * Generates LLDP_PACKET with source switch id/port into specified buffer
 * src_addr:sw_port MAC address
 * srcID: sw_dpid
 */
void lldp_create_packet(void *src_addr, uint32_t srcId, uint32_t srcPort, 
                        lldp_pkt_t *buffer, uint8_t user_type)
{
    //nearest bridge
    uint8_t dest_addr[OFP_ETH_ALEN] = {0x01,0x80,0xc2,0x00,0x00,0x0e};

    //data link information
    memcpy(buffer->eth_head.eth_dst,dest_addr,OFP_ETH_ALEN);
    memcpy(buffer->eth_head.eth_src,src_addr,OFP_ETH_ALEN);
    buffer->eth_head.eth_type = htons(0x88cc);
    
    //the switch information (sw_dpid)
    buffer->chassis_tlv_type = LLDP_CHASSIS_ID_TLV;
    buffer->chassis_tlv_length = sizeof(uint32_t) + 1;
    buffer->chassis_tlv_subtype = LLDP_CHASSIS_ID_LOCALLY_ASSIGNED;
    buffer->chassis_tlv_id = htonl(srcId);
    // c_log_debug("srcId:%x", srcId);

    //the switch outport
    buffer->port_tlv_type = LLDP_PORT_ID_TLV;
    buffer->port_tlv_length = sizeof(uint32_t) + 1;
    buffer->port_tlv_subtype = LLDP_PORT_ID_LOCALLY_ASSIGNED;
    buffer->port_tlv_id = htons(srcPort);

    //lldp pkt ttl set
    buffer->ttl_tlv_type = LLDP_TTL_TLV;
    buffer->ttl_tlv_length = 2;
    buffer->ttl_tlv_ttl = htons(LLDP_DEFAULT_TTL);

    //lldp user data(include time stamp)
    buffer->user_tlv_type = LLDP_USERE_LINK_DATA;
    buffer->user_tlv_length = sizeof(uint64_t)+sizeof(uint8_t);
    buffer->user_tlv_data_type = user_type;
    buffer->user_tlv_data_timeval = htonll(lldp_get_timeval());

    //lldp pkt end
    buffer->end_of_lldpdu_tlv_type = LLDP_END_OF_LLDPDU_TLV;
    buffer->end_of_lldpdu_tlv_length = 0;
}

void lldp_measure_delay_ctos(uint64_t sw_dpid)
{
    uint8_t src_addr[OFP_ETH_ALEN] = {0x02, 0x42, 0xf7, 0x6d, 0x95, 0x69};
    lldp_pkt_t buffer;
    // c_log_debug("start to make a lldp packet!");
    lldp_create_packet(src_addr, tp_get_sw_glabol_id(sw_dpid), OF_NO_PORT, &buffer, USER_TLV_DATA_TYPE_CTOS);
    // c_log_debug("create a lldp packet success!");
    lldp_send_packet(sw_dpid, &buffer, 1, 0);
    // c_log_debug("send a ctos lldp packet!!");
}

//process the lldp packet
void lldp_proc(mul_switch_t *sw, struct flow *fl, uint32_t inport, uint32_t buffer_id, \
              uint8_t *raw, size_t pkt_len)
{
    uint64_t now_timeval, delay, delay_tmp;
    uint32_t sw1_key = tp_get_sw_glabol_id(sw->dpid);
    tp_sw * sw1 = tp_find_sw(sw1_key);
    lldp_pkt_t * lldp = (lldp_pkt_t*)raw;
    uint32_t sw2_key = ntohl(lldp->chassis_tlv_id);
    tp_sw * sw2 = tp_find_sw(sw2_key);
    tp_link * link_n1;
    tp_link * link_n2;
    uint16_t cid;
    uint8_t sid;
    uint64_t sw2_delay;

    // c_log_debug("sw2_key:%x", sw2_key);
    now_timeval = lldp_get_timeval();

    switch (lldp->user_tlv_data_type)
    {
    case USER_TLV_DATA_TYPE_CTOS:
        sw1->delay += (now_timeval-ntohll(lldp->user_tlv_data_timeval))/2;
        if(sw1->delay_measure_times != 0)
            sw1->delay /= 2;
        sw1->delay_measure_times += 1;
        c_log_debug("lldp_ctos_pkt from s%x and %dth average delay: %lu us", \
            sw1->key, sw1->delay_measure_times, sw1->delay);
        // write in redis
        cid = (uint16_t)((sw1->key & 0xffff0000) >> 16);
        sid = (uint8_t)((sw1->key & 0xffffff00) >> 8);
        Set_Sw_Delay(cid, sid, sw1->delay); 

        //flood lldp and measure the delay five times
        if(sw1->delay_measure_times == DELAY_MEASURE_TIMES)
        {
            lldp_flood(sw1);
        }else
        {
            lldp_measure_delay_ctos(sw1->sw_dpid);
        }
        break;
    case USER_TLV_DATA_TYPE_STOS:
        c_log_debug("lldp_stos_pkt between s%x and s%x", sw1->key, sw2->key);
        if(sw2)
        {//in the same area
            //add edge between the sw_node.
            tp_add_link(sw1->key, inport, sw2->key, ntohs(lldp->port_tlv_id));
            link_n1 = __tp_get_link_in_head(sw1->list_link, sw2_key);
            link_n2 = __tp_get_link_in_head(sw2->list_link, sw1_key);
            link_n1->delay_measure_times += 1;
            link_n2->delay_measure_times += 1;
            
            // if(link_n1->delay_measure_times == 1)break;
            // else if(link_n1->delay_measure_times <= DELAY_MEASURE_TIMES)lldp_flood(sw1);
            if(link_n1->delay_measure_times < DELAY_MEASURE_TIMES)lldp_flood(sw1);

            delay_tmp = now_timeval-ntohll(lldp->user_tlv_data_timeval);
            c_log_debug("%dth all delay: %lu us, sw%x_delay:%lu us, sw%x_delay:%lu us", \
                link_n1->delay_measure_times, delay_tmp, sw1->key, sw1->delay, sw2->key, sw2->delay);
            delay_tmp -= (sw1->delay + sw2->delay);
            c_log_debug("%dth sw%x <-> sw%x link delay: %lu us", \
                link_n1->delay_measure_times, sw1->key, sw2->key, delay_tmp);
            // c_log_debug("get last time link delay: %llu us", link_n1->delay);
            if(link_n1->delay)delay = (link_n1->delay + delay_tmp)/2;
            else delay = delay_tmp;
            c_log_debug("average link delay: %lu us", delay);
            link_n1->delay = delay;
            link_n2->delay = delay;

            // write in redis      
            Set_Link_Delay(sw1->key + link_n1->port_h, sw2->key + link_n1->port_n, delay);
            Set_Link_Delay(sw2->key + link_n2->port_h, sw1->key + link_n2->port_n, delay);
        }
        else//LLDP from other area
        {
            //add edge between the sw_node.
            tp_add_link(sw1->key, inport, sw2->key, ntohs(lldp->port_tlv_id));

            if(!__tp_get_link_in_head(sw1->list_link, sw2_key))
            {
                link_n1 = malloc(sizeof(tp_link));
                memset(link_n1, 0, sizeof(tp_link));
                link_n1->key = sw2_key;
                link_n1->port_h = (inport & 0x000000ff);
                link_n1->port_n = (ntohs(lldp->port_tlv_id) & 0x000000ff);
                __tp_head_add_link(sw1, link_n1);
            }

            link_n1 = __tp_get_link_in_head(sw1->list_link, sw2_key);
            link_n1->delay_measure_times += 1;
            if(link_n1->delay_measure_times < DELAY_MEASURE_TIMES)lldp_flood(sw1);

            delay_tmp = now_timeval-ntohll(lldp->user_tlv_data_timeval);
            cid = (uint16_t)((sw2->key & 0xffff0000) >> 16);
            sid = (uint8_t)((sw2->key & 0x0000ff00) >> 8);
            sw2_delay = Get_Sw_Delay(cid, sid);
            c_log_debug("%dth all delay: %lu us, sw%x_delay:%lu us, sw%x_delay:%lu us", \
                link_n1->delay_measure_times, delay_tmp, sw1->key, sw1->delay, sw2->key, sw2_delay);
            delay_tmp -= (sw1->delay + sw2_delay);
            c_log_debug("%dth sw%x <-> sw%x link delay: %lu us", \
                link_n1->delay_measure_times, sw1->key, sw2->key, delay_tmp);
            // c_log_debug("get last time link delay: %llu us", link_n1->delay);
            if(link_n1->delay)delay = (link_n1->delay + delay_tmp)/2;
            else delay = delay_tmp;
            c_log_debug("average link delay: %lu us", delay);
            link_n1->delay = delay;

            // write in redis      
            Set_Link_Delay(sw1->key + link_n1->port_h, sw2_key + link_n1->port_n, delay);
            Set_Link_Delay(sw2_key + link_n1->port_n, sw1->key + link_n1->port_h, delay);
        }
        
        break;
    default:
        c_log_debug("ERROR LLDP TYPE");
        break;
    }
}

void lldp_send_packet(uint64_t sw_dpid, lldp_pkt_t *buffer, uint32_t inport, uint32_t outport)
{
    struct of_pkt_out_params  parms;
    struct mul_act_mdata      mdata;
    int ret = -1;

    memset(&parms, 0, sizeof(parms));
    mul_app_act_alloc(&mdata);
    mdata.only_acts = true;
    mul_app_act_set_ctors(&mdata, sw_dpid);
    ret = mul_app_action_output(&mdata, outport);
    if(ret == 0) c_log_debug("\n\n\nset action output sucess!\n\n\n");
    else c_log_debug("\n\n\nset action output fail!\n\n\n");
    parms.buffer_id = -1;
    parms.in_port = inport;
    parms.action_list = mdata.act_base;
    parms.action_len = mul_app_act_len(&mdata);
    parms.data_len = sizeof(lldp_pkt_t);
    parms.data = buffer;
    mul_app_send_pkt_out(NULL, sw_dpid, &parms);
    mul_app_act_free(&mdata);
}

void lldp_flood(tp_sw *sw)
{
    //uint64_t sw_srcdpid = sw->dpid;
    lldp_pkt_t buffer;
    tp_sw_port * port_list = sw->list_port;
    
    //create the lldp packet due to get the adj
    //lldp_create_packet(port_infor.hw_addr, sw_srcdpid, port_infor.port_no, &buffer);
    while(port_list)
    {
        lldp_create_packet(port_list->dl_hw_addr, sw->key, port_list->port_no, &buffer,\
                           USER_TLV_DATA_TYPE_STOS);
        lldp_send_packet(sw->sw_dpid, &buffer, OF_NO_PORT, port_list->port_no);
        port_list = port_list->next;
    }    
}