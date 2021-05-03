#include "ARP.h"
#include "tp_graph.h"
#include "db_wr.h"

extern tp_swdpid_glabolkey * key_table;
arp_hash_table_t * arp_table = NULL;
extern tp_sw * tp_graph;
extern uint32_t controller_area;

void arp_add_key(uint32_t key_ip, uint8_t dl_hw_addr[ETH_ADDR_LEN]) 
{
    arp_hash_table_t *s;

    s = malloc(sizeof(arp_hash_table_t));
    s->id = key_ip;
    memcpy(s->dl_hw_addr, dl_hw_addr, ETH_ADDR_LEN);
    // c_log_debug("src s->dl_hw_addr learn mac %x%x%x%x%x%x", s->dl_hw_addr[0],s->dl_hw_addr[1],s->dl_hw_addr[2],s->dl_hw_addr[3],s->dl_hw_addr[4],s->dl_hw_addr[5]);
    // c_log_debug("src dl_hw_addr learn mac %x%x%x%x%x%x", dl_hw_addr[0],dl_hw_addr[1],dl_hw_addr[2],dl_hw_addr[3],dl_hw_addr[4],dl_hw_addr[5]);
    HASH_ADD_INT(arp_table, id, s);  /* id: name of key field */
}

arp_hash_table_t* arp_find_key(uint32_t key_ip)
{
    arp_hash_table_t* s = NULL;
    HASH_FIND_INT(arp_table, &key_ip, s);
    return s;
}

void arp_delete_key(uint32_t key_ip)
{
    arp_hash_table_t* s = NULL;
    s = arp_find_key(key_ip);
    HASH_DEL(arp_table, s);  /* user: pointer to deletee */
    free(s);             /* optional; it's up to you! */
}

void arp_distory(void)
{
    arp_hash_table_t * s, * tmp;

    HASH_ITER(hh, arp_table, s, tmp) {
      HASH_DEL(arp_table, s);
      free(s);
    }

    arp_table = NULL;
}

int src_mac_is_valid(uint8_t src_mac[ETH_ADDR_LEN])
{
    uint8_t ctrl_mac[OFP_ETH_ALEN] = {0x02, 0x42, 0xf7, 0x6d, 0x93, 0x67};
    if(memcmp(src_mac, ctrl_mac, OFP_ETH_ALEN) == 0)// same -> invalid
        return 0;
    return 1;
}

void arp_learn(struct arp_eth_header *arp_req, uint64_t sw_dpid, uint32_t port)
{
    arp_hash_table_t * tmp;
    uint32_t pc_sw_port;
    // c_log_debug("src learn mac %x%x%x%x%x%x", arp_req->ar_sha[0],arp_req->ar_sha[1],arp_req->ar_sha[2],arp_req->ar_sha[3],arp_req->ar_sha[4],arp_req->ar_sha[5]);
    arp_add_key(arp_req->ar_spa, arp_req->ar_sha);
    tmp = arp_find_key(arp_req->ar_spa);
    tmp->sw_key = tp_get_sw_glabol_id(sw_dpid);
    tmp->port_no = port;
    // c_log_debug("src learn end mac %x%x%x%x%x%x", tmp->dl_hw_addr[0], tmp->dl_hw_addr[1], tmp->dl_hw_addr[2], tmp->dl_hw_addr[3], tmp->dl_hw_addr[4], tmp->dl_hw_addr[5]);

    //write in redis database
    pc_sw_port = tmp->sw_key + (port & 0x000000ff);
    Set_Pc_Sw_Port(arp_req->ar_spa, pc_sw_port);
}


void arp_proc(mul_switch_t *sw, struct flow *fl, uint32_t inport, uint32_t buffer_id, \
              uint8_t *raw, size_t pkt_len)
{
    struct arp_eth_header     *arp;
    struct eth_header         *eth;
    struct of_pkt_out_params  parms;
    struct mul_act_mdata      mdata;
    arp_hash_table_t * s = NULL;
    uint8_t src_addr[OFP_ETH_ALEN] = {0x02, 0x42, 0xf7, 0x6d, 0x93, 0x67};
    uint8_t dst_addr[OFP_ETH_ALEN] = {0x03, 0x42, 0xf7, 0x6d, 0x93, 0x67};
    tp_sw * sw_tmp1, * sw_tmp2;
    uint32_t pc_sw_port;

    eth = (struct eth_header*)raw;
    arp = (void *)(raw + sizeof(struct eth_header)  +
                   (fl->dl_vlan ? VLAN_HEADER_LEN : 0));

    // c_log_info("my_controller app - ARP SRC IP KEY %d!", arp->ar_spa);
    // 源学习之前需要判断源MAC是否有效
    if(src_mac_is_valid(arp->ar_sha))
    {
        s = arp_find_key(arp->ar_spa);
        if(s)
        {
            arp_delete_key(arp->ar_spa);
        }
        arp_learn(arp, sw->dpid, inport);
    }

    memset(&parms, 0, sizeof(parms));
    mul_app_act_alloc(&mdata);
    mdata.only_acts = true;
    if(htons(arp->ar_op) == 1){
        // arp request
        // s = arp_find_key(arp->ar_tpa);
        // 收到arp request查询数据库有注册信息，则回复固定的目标MAC，否则洪泛arp request
        pc_sw_port = Get_Pc_Sw_Port(arp->ar_tpa);
        // if(s)
        if(pc_sw_port != -1)
        {
            // arp cache reply
            c_log_debug("\n\n\nARP Cache reply!\n\n\n");
            mul_app_act_set_ctors(&mdata, sw->dpid);
            mul_app_action_output(&mdata, inport);
            parms.buffer_id = buffer_id;
            parms.in_port = OF_NO_PORT;
            parms.action_list = mdata.act_base;
            parms.action_len = mul_app_act_len(&mdata);
            parms.data_len = sizeof(struct eth_header) + sizeof(struct arp_eth_header);
            // reply fixed mac(false)
            // parms.data = get_proxy_arp_reply(arp, s->dl_hw_addr);
            parms.data = get_proxy_arp_reply(arp, dst_addr);
            mul_app_send_pkt_out(NULL, sw->dpid, &parms);
            mul_app_act_free(&mdata);
        }else
        {
            // STP flood c2s
            c_log_debug("\n\n\nARP Flood!\n\n\n");
            mul_app_act_set_ctors(&mdata, sw->dpid);
            mul_app_action_output(&mdata, OF_ALL_PORTS); 
            parms.buffer_id = buffer_id;
            parms.in_port =OF_NO_PORT;
            parms.action_list = mdata.act_base;
            parms.action_len = mul_app_act_len(&mdata);
            parms.data_len = pkt_len;
            memcpy(arp->ar_sha, src_addr, OFP_ETH_ALEN);
            memcpy(eth->eth_src, src_addr, OFP_ETH_ALEN);
            parms.data = raw;
            HASH_ITER(hh, tp_graph, sw_tmp1, sw_tmp2) 
            {
                if((sw_tmp1->key & 0xffff0000) == controller_area)
                {
                    mul_app_send_pkt_out(NULL, sw_tmp1->sw_dpid, &parms);
                    //c_log_debug("Send a arp req pkt to sw %x", sw_tmp1->sw_dpid);
                }
            }
            mul_app_act_free(&mdata);
        }
    }else
    {
        // arp reply route trans
        // 收到arp reply只进行源学习，不再回复目标MAC

        c_log_debug("\n\n\nARP host Reply!\n\n\n");
        // s = arp_find_key(arp->ar_tpa);
        // memcpy(arp->ar_tha, s->dl_hw_addr, OFP_ETH_ALEN);
        // c_log_debug("dst mac %x%x%x%x%x%x", arp->ar_tha[0],arp->ar_tha[1],arp->ar_tha[2],arp->ar_tha[3],arp->ar_tha[4],arp->ar_tha[5]);
        // memcpy(eth->eth_dst, s->dl_hw_addr, OFP_ETH_ALEN);
        // mul_app_act_set_ctors(&mdata, tp_find_sw(s->sw_key)->sw_dpid);
        // mul_app_action_output(&mdata, s->port_no);
        // parms.buffer_id = buffer_id;
        // parms.in_port = OF_NO_PORT;
        // parms.action_list = mdata.act_base;
        // parms.action_len = mul_app_act_len(&mdata);
        // parms.data_len = pkt_len;
        // parms.data = raw;
        // mul_app_send_pkt_out(NULL, tp_find_sw(s->sw_key)->sw_dpid, &parms);
        // mul_app_act_free(&mdata);
    }
}

void * get_proxy_arp_reply(struct arp_eth_header *arp_req, uint8_t fab_mac[ETH_ADDR_LEN])
{
    uint8_t               *out_pkt;
    struct eth_header     *eth;
    struct arp_eth_header *arp_reply;

    out_pkt = malloc(sizeof(struct arp_eth_header) +
                         sizeof(struct eth_header));

    eth = (struct eth_header *)out_pkt;
    arp_reply = (struct arp_eth_header *)(eth + 1);
    
    memcpy(eth->eth_dst, arp_req->ar_sha, ETH_ADDR_LEN);
    memcpy(eth->eth_src, fab_mac, ETH_ADDR_LEN);
    eth->eth_type = htons(ETH_TYPE_ARP);

    arp_reply->ar_hrd = htons(ARP_HRD_ETHERNET);
    arp_reply->ar_pro = htons(ARP_PRO_IP); 
    arp_reply->ar_pln = IP_ADDR_LEN;
    arp_reply->ar_hln = ETH_ADDR_LEN;
    arp_reply->ar_op = htons(ARP_OP_REPLY);

    memcpy(arp_reply->ar_sha, fab_mac, ETH_ADDR_LEN);
    arp_reply->ar_spa = arp_req->ar_tpa;
    memcpy(arp_reply->ar_tha, arp_req->ar_sha, ETH_ADDR_LEN);
    arp_reply->ar_tpa = arp_req->ar_spa;

    return out_pkt; 
}
