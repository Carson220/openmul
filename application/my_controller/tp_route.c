#include "tp_route.h"
#include "heap.h"
#include "ARP.h"

extern tp_sw * tp_graph;
extern tp_swdpid_glabolkey * key_table;

rt_node * rt_find_node(uint64_t key, rt_node * rt_set)
{
    rt_node *s = NULL;
    HASH_FIND(hh, rt_set, &key, sizeof(uint64_t), s);
    return s;
}

rt_node * rt_add_node(uint64_t key, tp_sw *prev, tp_link *node, rt_node * rt_set)
{
    rt_node *s = NULL;

    c_log_debug("rt_find_node");
    if(rt_find_node(key, rt_set))return NULL;

    c_log_debug("start add node to rt_set");
    s = malloc(sizeof(rt_node));
    memset(s, 0, sizeof(rt_node));
    s->key = key;
    s->prev = prev;
    s->node = node;
    HASH_ADD(hh, rt_set, key, sizeof(uint64_t), s);

    return s;
}

int rt_del_node(uint64_t key, rt_node * rt_set)
{
    rt_node *s = NULL;

    s = rt_find_node(key, rt_set);
    if(!s)return 0;
    HASH_DEL(rt_set, s);
    free(s);

    return 1;
}

void rt_distory(rt_node * rt_set)
{
    rt_node * s, * tmp;

    HASH_ITER(hh, rt_set, s, tmp) 
    {
        HASH_DEL(rt_set, s);
        free(s);
    }
}

void rt_set_ip_flow_path(uint32_t src_ip, uint32_t dst_ip, rt_node * rt_visited_set)
{
    rt_node *s ;
    tp_sw * dst_node = tp_find_sw(arp_find_key(dst_ip)->sw_key);
    //uint32_t src_port = arp_find_key(src_ip)->port_no;
    uint32_t outport = arp_find_key(dst_ip)->port_no;
    struct flow fl;
    struct flow mask;
    mul_act_mdata_t mdata;

    s = rt_find_node(dst_node->key, rt_visited_set);

    do{
        //流表下发
        memset(&fl, 0, sizeof(fl));
        memset(&mdata, 0, sizeof(mdata));
        of_mask_set_dc_all(&mask);
    
        fl.ip.nw_src = src_ip;
        of_mask_set_nw_src(&mask, 32);
        fl.ip.nw_dst = dst_ip;
        of_mask_set_nw_dst(&mask, 32);

        mul_app_act_alloc(&mdata);
        mul_app_act_set_ctors(&mdata, dst_node->sw_dpid);
        mul_app_action_output(&mdata, outport); 

        mul_app_send_flow_add(MUL_TR_SERVICE_NAME, NULL, dst_node->sw_dpid, &fl, &mask,
                            (uint32_t)-1,
                            mdata.act_base, mul_app_act_len(&mdata),
                            0, 0, C_FL_PRIO_FWD, C_FL_ENT_LOCAL);
        outport = (s->node != NULL)?s->node->port_h:0;
        dst_node = s->prev;
        s = rt_find_node(dst_node->key, rt_visited_set);
    }while(outport);
    mul_app_act_free(&mdata);
}

int tp_rt_ip(uint32_t nw_src, uint32_t nw_dst)
{
    tp_sw * src_node = tp_find_sw(arp_find_key(nw_src)->sw_key);
    tp_sw * dst_node = tp_find_sw(arp_find_key(nw_dst)->sw_key);
    tp_sw * start_node = src_node;
    tp_link * adj_node;
    heap rt_minheap;
    rt_node rt_visited_set;
    uint64_t * delay_get;
    uint64_t * sw_visiting_key;
    uint64_t delay_set = 0;

    if(!src_node || !dst_node) return 0;

    c_log_debug("heap_create!");
    heap_create(&rt_minheap, 0, NULL);
    c_log_debug("rt_set add src_node!");
    rt_add_node(src_node->key, src_node, NULL, &rt_visited_set);
    c_log_debug("heap insert src_node!");
    heap_insert(&rt_minheap, (void*)&(src_node->key), (void*)&delay_set);

    while(heap_size(&rt_minheap))
    {
        c_log_debug("get the min_delay node from heap");
        heap_delmin(&rt_minheap, (void*)&sw_visiting_key, (void*)&delay_get);
        c_log_debug("get the sw node from tp_graph");
        start_node = tp_find_sw(*sw_visiting_key);
        c_log_debug("start traverse the sw links");
        adj_node = start_node->list_link;//找到一个点开始遍历
        if(adj_node == NULL) continue;
        while(adj_node)//遍历邻接点
        {
            if(adj_node->key == dst_node->key)
            {
                //找到了，下发流表
                c_log_debug("find the dst and than issue the flow table");
                c_log_debug("add the dst sw to rt_visited_set");
                rt_add_node(adj_node->key, start_node, adj_node, &rt_visited_set);
                c_log_debug("issue the flow table");
                rt_set_ip_flow_path(nw_src, nw_dst, &rt_visited_set);
                c_log_debug("free the malloc and return 1");
                rt_distory(&rt_visited_set);
                heap_destroy(&rt_minheap);
                return 1;
            }
            if(!rt_find_node(adj_node->key, &rt_visited_set))//没有被遍历过
            {
                c_log_debug("calculate the sum of delay");
                delay_set = *delay_get + adj_node->delay;
                c_log_debug("add the adj sw to heap");
                heap_insert(&rt_minheap, (void*)&adj_node->key, (void*)&delay_set);//添加要遍历的点
                c_log_debug("add the adj sw to rt_visited_set");
                rt_add_node(adj_node->key, start_node, adj_node, &rt_visited_set);//添加已经遍历的点
            }
            adj_node = adj_node->next;
        }
    }

    c_log_debug("free the malloc and return 0");
    rt_distory(&rt_visited_set);
    heap_destroy(&rt_minheap);
    return 0;
}
