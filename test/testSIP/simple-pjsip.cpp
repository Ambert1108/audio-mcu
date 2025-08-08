#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>

int main() {
  pj_caching_pool cp;
  pjsip_endpoint* endpt;
  pj_status_t status;

  // 初始化基础库
  pj_init();
  pj_caching_pool_init(&cp, NULL, 0);

  // 创建 SIP Endpoint（核心信令引擎）
  
  status = pjsip_endpoint_create(&cp.factory, "endpt_name", &endpt);

  // 创建 UDP 传输
  pjsip_transport* udp_transport;
  pjsip_transport_config cfg;
  pjsip_transport_config_default(&cfg);
  status = pjsip_udp_transport_start(endpt, &cfg, 5060, &udp_transport);

  // 注册自定义模块处理 SIP 消息
  pjsip_module mod_msg = {
      .on_rx_request = &on_rx_request, // 回调函数处理请求
      .name = { "MsgHandler", 10 }
  };
  pjsip_endpoint_register_module(endpt, &mod_msg);

  // 发送 SIP MESSAGE
  pjsip_tx_data* tdata;
  pjsip_method method = { PJSIP_OTHER_METHOD, { "MESSAGE", 7 } };
  pj_str_t target = pj_str("sip:user@example.com");
  pjsip_endpt_create_request(endpt, &method, &target, NULL, NULL, NULL, -1, NULL, &tdata);
  pjsip_endpt_send_request(endpt, tdata, -1, NULL, NULL);

  // 事件循环（非阻塞）
  while (1) {
    pj_time_val timeout = { 0, 10 };
    pjsip_endpt_handle_events(endpt, &timeout);
  }
  return 0;
}

// 处理收到的 SIP 请求（如 MESSAGE）
static pj_bool_t on_rx_request(pjsip_rx_data* rdata) {
  if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, pjsip_get_message_method())) {
    pjsip_tx_data* tdata;
    pjsip_endpt_create_response(endpt, rdata, 200, NULL, &tdata);
    pjsip_endpt_send_response(endpt, tdata);
    return PJ_TRUE; // 已处理
  }
  return PJ_FALSE;
}