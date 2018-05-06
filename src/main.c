#include "mgos.h"
#include "mgos_wifi.h"
#include "mgos_mqtt.h"
#include "mgos_dht.h"
#include "mgos_adc.h"

#define DHTTYPE DHT11 // DHT 22  (AM2302), AM2321
#define DHTPIN  22
#define SOILPIN 32
#define LEDPIN  1

static struct mgos_dht *s_dht = NULL;

static void read_sensor_cb(void *arg) {
  float t = mgos_dht_get_temp(s_dht);
  float h = mgos_dht_get_humidity(s_dht);
  int   w = mgos_adc_read(SOILPIN);
  char topic[100], message[100];
  struct json_out out = JSON_OUT_BUF(message, sizeof(message));

  if (isnan(h) || isnan(t)) {
    LOG(LL_INFO, ("Failed to read data from sensor"));
    return;
  }
  
  snprintf(topic, sizeof(topic), "home/%s/result",
           mgos_sys_config_get_device_id());
  json_printf(&out, "{temp: %.1f, humidity: %.1f, soil: %d}", t, h, w);
  bool res = mgos_mqtt_pub(topic, message, strlen(message), 1, false);
  LOG(LL_INFO, ("%s %s, mqtt-pub: %s", topic, message, res ? "ok" : "err"));

  if (res) { 
    mgos_wifi_disconnect();
    esp_deep_sleep(mgos_sys_config_get_higrow_sleep()*60000000);
  }

  (void) arg;
}


static void led_timer_cb(void *arg) {
  bool val = mgos_gpio_toggle(mgos_sys_config_get_pins_led());
  (void) arg;
}


static void network_state_cb(int ev, void *evd, void *arg) {
  static mgos_timer_id read_sensor_timer_id, led_timer_id;

  switch (ev) {
    case MGOS_NET_EV_DISCONNECTED:
      LOG(LL_INFO, ("%s", "Net disconnected"));
      mgos_clear_timer(read_sensor_timer_id);
      mgos_clear_timer(led_timer_id);
      mgos_gpio_write(mgos_sys_config_get_pins_led(), false);
      break;
    case MGOS_NET_EV_CONNECTING:
      led_timer_id = mgos_set_timer(500, MGOS_TIMER_REPEAT, led_timer_cb, NULL);
      LOG(LL_INFO, ("%s", "Net connecting..."));
      break;
    case MGOS_NET_EV_CONNECTED:
      led_timer_id = mgos_set_timer(100, MGOS_TIMER_REPEAT, led_timer_cb, NULL);
      LOG(LL_INFO, ("%s", "Net connected"));
      break;
    case MGOS_NET_EV_IP_ACQUIRED:
      read_sensor_timer_id = mgos_set_timer(2000 /* ms */, true /* repeat */, read_sensor_cb, NULL);  
      mgos_clear_timer(led_timer_id);
      mgos_gpio_write(mgos_sys_config_get_pins_led(), true);
      LOG(LL_INFO, ("%s", "Net got IP address"));
      break;
  }

  (void) evd;
  (void) arg;
}


enum mgos_app_init_result mgos_app_init(void) {
  LOG(LL_INFO, ("Wakeup cause: %d", esp_sleep_get_wakeup_cause()));
  
  if ((s_dht = mgos_dht_create(DHTPIN, DHTTYPE)) == NULL) return MGOS_APP_INIT_ERROR;
  if (! mgos_adc_enable(SOILPIN)) return MGOS_APP_INIT_ERROR;

  mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, network_state_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
