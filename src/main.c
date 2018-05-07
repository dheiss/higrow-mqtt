#include "mgos.h"
#include "mgos_wifi.h"
#include "mgos_mqtt.h"
#include "mgos_dht.h"
#include "mgos_adc.h"

#define DHTTYPE         DHT11 // DHT 22  (AM2302), AM2321
#define MQTT_BUFFER_LEN 100
#define uS_TO_SEC       1000000
#define SLEEP_TIMER     60 // automatic sleep after 60sec

static struct mgos_dht *s_dht = NULL;
static int sleep_timer_countdown = 0;
static mgos_timer_id read_sensor_timer_id, led_timer_id;

static void read_sensor_cb(void *arg) {
  float t = mgos_dht_get_temp(s_dht);
  float h = mgos_dht_get_humidity(s_dht);
  int   w = mgos_adc_read(mgos_sys_config_get_pins_soil());
  char topic[MQTT_BUFFER_LEN], message[MQTT_BUFFER_LEN];
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

  if (res && (mgos_sys_config_get_higrow_sleep() > 0)) { 
    sleep_timer_countdown = SLEEP_TIMER;
  }

  (void) arg;
}


static void led_timer_cb(void *arg) {
  bool val = mgos_gpio_toggle(mgos_sys_config_get_pins_led());
  (void) arg;
}


static void sleep_timer_cb(void *arg) {
  sleep_timer_countdown++;
  if (sleep_timer_countdown >= SLEEP_TIMER) {
    if (sleep_timer_countdown == SLEEP_TIMER) {
      LOG(LL_INFO, ("No succesful connection, go to sleep for %d sec...", mgos_sys_config_get_higrow_sleep()));
    } else {
      LOG(LL_INFO, ("Successful MQTT publish, go to sleep for %d sec...", mgos_sys_config_get_higrow_sleep()));
    }
    mgos_wifi_disconnect();
    esp_deep_sleep(mgos_sys_config_get_higrow_sleep()*uS_TO_SEC);
  }
  (void) arg;
}


static void network_state_cb(int ev, void *evd, void *arg) {
  if (led_timer_id) mgos_clear_timer(led_timer_id);
  switch (ev) {
    case MGOS_NET_EV_DISCONNECTED:
      LOG(LL_INFO, ("%s", "Net disconnected"));
      mgos_clear_timer(read_sensor_timer_id);
      mgos_gpio_write(mgos_sys_config_get_pins_led(), true);
      break;
    case MGOS_NET_EV_CONNECTING:
      led_timer_id = mgos_set_timer(500, MGOS_TIMER_REPEAT, led_timer_cb, NULL);
      LOG(LL_INFO, ("%s", "Net connecting..."));
      break;
    case MGOS_NET_EV_CONNECTED:
      led_timer_id = mgos_set_timer(50, MGOS_TIMER_REPEAT, led_timer_cb, NULL);
      LOG(LL_INFO, ("%s", "Net connected"));
      break;
    case MGOS_NET_EV_IP_ACQUIRED:
      mgos_gpio_write(mgos_sys_config_get_pins_led(), false);
      read_sensor_timer_id = mgos_set_timer(2000 /* ms */, true /* repeat */, read_sensor_cb, NULL);  
      LOG(LL_INFO, ("%s", "Net got IP address"));
      break;
  }

  (void) evd;
  (void) arg;
}


enum mgos_app_init_result mgos_app_init(void) {
  LOG(LL_INFO, ("Wakeup cause: %d", esp_sleep_get_wakeup_cause()));
  mgos_gpio_set_mode(mgos_sys_config_get_pins_led(), MGOS_GPIO_MODE_OUTPUT);

  if ((s_dht = mgos_dht_create(mgos_sys_config_get_pins_dht(), DHTTYPE)) == NULL) 
    return MGOS_APP_INIT_ERROR;
  if (! mgos_adc_enable(mgos_sys_config_get_pins_soil())) 
    return MGOS_APP_INIT_ERROR;

  mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, network_state_cb, NULL);
  mgos_set_timer(1000, MGOS_TIMER_REPEAT, sleep_timer_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
