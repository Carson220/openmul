#include "tp_route_redis.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "db_wr.h"

extern tp_sw * tp_graph;
extern tp_swdpid_glabolkey * key_table;

struct node node_sw[MAXNUM];// sw-sw
int16_t map_sw[MAXNUM]={0,}; // hash map
int32_t node_sw_num = 0;

int tp_create(void)
{
    char cmd[CMD_MAX_LENGHT] = {0};
    uint8_t port1, port2; // port id
    uint32_t sw1, sw2; // sw key id
    uint64_t port, delay;
    redisContext *context;
    redisReply *reply;
    struct edge *link_sw, tmp;

    /*组装Redis命令*/
    snprintf(cmd, CMD_MAX_LENGHT, "hgetall link_delay");
    // for(int i=0;cmd[i]!='\0';i++)
    // 	printf("%c",cmd[i]);
    // printf("\n");

    /*连接redis*/
    context = redisConnect(REDIS_SERVER_IP, REDIS_SERVER_PORT);
    if (context->err)
    {
        redisFree(context);
        printf("%d connect redis server failure:%s\n", __LINE__, context->errstr);
        return ret;
    }
    printf("connect redis server success\n");

    /*执行redis命令*/
    reply = (redisReply *)redisCommand(context, cmd);
    if (NULL == reply)
    {
        printf("%d execute command:%s failure\n", __LINE__, cmd);
        redisFree(context);
        return ret;
    }

    // 输出查询结果 
    // printf("%d,%lu\n",reply->type,reply->elements);
    printf("element num = %lu\n",reply->elements);
    int i = 0;
    for(i=0;i < reply->elements;i++)
    {
        if(i%2 ==0)// port
        {
            printf("%s:",reply->element[i]->str);
            port = atoi(reply->element[i]->str);
            
            sw1 = (uint32_t)((port & 0xffffff0000000000) >> 32);
            sw2 = (uint32_t)(port & 0x00000000ffffff00);
            port1 = (uint8_t)((port & 0x000000ff00000000) >> 32);
            port2 = (uint8_t)(port & 0x00000000000000ff);

            if(map_sw[sw1]==0)
            {
                node_sw_num++;
                map_sw[sw1] = node_sw_num;
                node_sw[map_sw[sw1]].node_id = sw1;
                node_sw[map_sw[sw1]].dist = INF;
                node_sw[map_sw[sw1]].flag = false;
                node_sw[map_sw[sw1]].rt_pre = 0;
            }
            if(map_sw[sw2]==0)
            {
                node_sw_num++;
                map_sw[sw2] = node_sw_num;
                node_sw[map_sw[sw2]].node_id = sw2;
                node_sw[map_sw[sw2]].dist = INF;
                node_sw[map_sw[sw2]].flag = false;
                node_sw[map_sw[sw2]].rt_pre = 0;
            }
        }
        else// delay
        {
            printf("%s\n",reply->element[i]->str);
            delay = atoi(reply->element[i]->str);

            // construct sw<->sw topo
            // you can skip "for" to save time
            tmp = node_sw[map_sw[sw1]].next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw2 && delay < tmp->delay)
                {
                    tmp->delay = delay;
                    tmp->port1 = port1;
                    tmp->port2 = port2;
                    break;
                }
                tmp = tmp->next;
            }
            if(tmp == NULL)
            {
                link_sw = (struct edge*)malloc(sizeof(struct edge));
                link_sw->node_id = sw2;
                link_sw->delay = delay;
                link_sw->port1 = port1;
                link_sw->port2 = port2;
                link_sw->next = node_sw[map_sw[sw1]].next ;
                node_sw[map_sw[sw1]].next = link_sw;
            }

            tmp = node_sw[map_sw[sw2]].next;
            for(; tmp != NULL; )// update sw-sw min delay
            {
                if(tmp->node_id == sw1 && delay < tmp->delay)
                {
                    tmp->delay = delay;
                    tmp->port1 = port2;
                    tmp->port2 = port1;
                    break;
                }
                tmp = tmp->next;
            }
            if(tmp == NULL)
            {
                link_sw = (struct edge*)malloc(sizeof(struct edge));
                link_sw->node_id = sw1;
                link_sw->delay = delay;
                link_sw->port1 = port2;
                link_sw->port2 = port1;
                link_sw->next = node_sw[map_sw[sw2]].next ;
                node_sw[map_sw[sw2]].next = link_sw;
            }
        }
    }

    freeReplyObject(reply);
    redisFree(context);
    return 0;
}

int tp_rt_redis_ip(uint32_t nw_src, uint32_t nw_dst)
{
    // create topo by redis
    tp_create();

    // get start and end info
    uint32_t port_start, port_end;
    uint32_t sw_start, sw_end;
    port_start = Get_Pc_Sw_Port(nw_src);  
    port_end = Get_Pc_Sw_Port(nw_dst);
    sw_start = (port_start & 0xffffff00);
    sw_end = (port_end & 0xffffff00);

    // calculate route sw-sw
    struct edge *tmp = node_sw[map_sw[sw_start]].next;
    for(; tmp != NULL; )//从起点开始 
	{
        node_sw[map_sw[tmp->node_id]].dist = tmp->delay;
        node_sw[sw_start].next = node_sw[sw_start].next->next;
	}
    node_sw[map_sw[sw_start]].dist = 0;
    node_sw[map_sw[sw_start]].flag = 1;

	uint64_t min_node, min_dist;
    int32_t pre_node = map_sw[sw_start];
    int32_t i, j;
    for(i = 0;i < node_sw_num;i ++)// 循环N次
    {
        min_node = 0;
        min_dst = INF;
        for(j = 1; i <= node_sw_num; j ++)//寻找下一个固定标号
        {
            if(!node_sw[j].flag && node_sw[j].dist < min_dst)
            {
                min_dst = node_sw[j].dist;
                min_node = j;
            }
        }
        if(min_node == 0) break;// 找不到最短路径 

        node_sw[min_node].flag = 1;// 改为固定标号
        node_sw[min_node].rt_pre = pre_node;
        pre_node = min_node;
        if(min_node == map_sw[sw_end]) break;// 找到最短路径

        tmp = node_sw[min_node].next;
        for(; tmp != NULL; )// 更新相邻节点标号值 
        {
            if(!node_sw[map_sw[tmp->node_id]].flag && min_dst + tmp->delay < node_sw[map_sw[tmp->node_id]].dist)
            {
                node_sw[map_sw[tmp->node_id]].dist = min_dst + tmp->delay;
            }
        }

    }
	if(min_node == 0)
    {
        printf("no path\n");
        return 0;
    }
	else 
        printf("ip route delay: %lu us\n",node_sw[map_sw[sw_end]].dist);

    // set flow-table
    uint32_t outsw = sw_end;
    struct edge *tmp;
    struct edge *head, next;
    tp_sw * dst_node = tp_find_sw(outsw);
    uint32_t outport = port_end;
    struct flow fl;
    struct flow mask;
    mul_act_mdata_t mdata;

    do{
        if(dst_node != NULL) // store in local
        {
            //流表下发
            memset(&fl, 0, sizeof(fl));
            memset(&mdata, 0, sizeof(mdata));
            of_mask_set_dc_all(&mask);
        
            fl.ip.nw_src = nw_src;
            of_mask_set_nw_src(&mask, 32);
            fl.ip.nw_dst = nw_dst;
            of_mask_set_nw_dst(&mask, 32);

            mul_app_act_alloc(&mdata);
            mul_app_act_set_ctors(&mdata, dst_node->sw_dpid);
            mul_app_action_output(&mdata, outport); 

            mul_app_send_flow_add(MY_CONTROLLER_APP_NAME, NULL, dst_node->sw_dpid, &fl, &mask,
                                (uint32_t)-1, mdata.act_base, mul_app_act_len(&mdata),
                                0, 0, C_FL_PRIO_FWD, C_FL_ENT_NOCACHE);
        }

        if(node_sw[map_sw[outsw]].rt_pre != 0)
        {
            tmp = node_sw[map_sw[outsw]].next;
            for(; tmp != NULL; )
            {
                if(tmp->node_id == node_sw[map_sw[outsw]].rt_pre)
                {
                    outport = tmp->port2;
                }
                tmp = tmp->next;
            }
            
            outsw = node_sw[node_sw[map_sw[outsw]].rt_pre].node_id
            dest_node = tp_find_sw(outsw);
        }
        else
            break;
        
    }while(1);
    mul_app_act_free(&mdata);

    // free topo (map/next)
    for(i = 1; i <= node_sw_num; i ++)
    {
        head = node_sw[i].next;
        next = head->next;
        for(; head != NULL; )
        {
            free(head);
            head = next;
            next = head->next;
        }
    }
    memset(map_sw, 0, sizeof(map_sw));
    node_sw_num = 0;
    return 1;
}