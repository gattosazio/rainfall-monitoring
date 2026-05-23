#include "hibernation.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "../helpers/helpers.h"
#include "../gps/gps.h"
#include "../modem/modem.h"
#include "../domain/config.h"

void enterHibernation() {
  DEBUG_PRINTLN("\n Entering Hibernation Mode (Ultra Deep Sleep)...");
  delay(200);

  if (gpsStreaming) {
    SerialAT.println("AT+CGNSSTST=0");
    delay(500);
  }
  if (gpsEnabled) {
    SerialAT.println("AT+CGNSSPWR=0");
    delay(500);
  }

  digitalWrite(MODEM_POWER_ON, LOW);
  delay(100);

  rtc_gpio_init((gpio_num_t)Config::TIP_PIN);
  rtc_gpio_set_direction((gpio_num_t)Config::TIP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)Config::TIP_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)Config::TIP_PIN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)Config::TIP_PIN, 0);

  DEBUG_PRINTLN(" Wake-up source: Rain Gauge tip (with pull-up)");
  DEBUG_PRINTLN(" Entering hibernation...\n");
  delay(200);
  
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  
  esp_deep_sleep_start();
}
