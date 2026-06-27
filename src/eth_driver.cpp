// Ethernet driver — generic 802.3 PHY (OUI=0x0121C6, addr=1)
// Extracted from 06_ethernet_dhcp. Do NOT use named PHY drivers (LAN8720 etc).
#include "eth_driver.h"
#include <Arduino.h>
#include <string.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy_802_3.h"
#include "esp_eth_phy.h"

static volatile bool s_got_ip = false;
static char s_ip_str[20];

static void on_eth_event(void *, esp_event_base_t, int32_t id, void *) {
    if      (id == ETHERNET_EVENT_START)        Serial.println("[ETH] Started");
    else if (id == ETHERNET_EVENT_CONNECTED)    Serial.println("[ETH] Link up");
    else if (id == ETHERNET_EVENT_DISCONNECTED) Serial.println("[ETH] Link down");
}

static void on_got_ip(void *, esp_event_base_t, int32_t, void *data) {
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
    Serial.printf("[ETH] IP: %s\n", s_ip_str);
    s_got_ip = true;
}

static esp_eth_phy_t *new_generic_802_3_phy(const eth_phy_config_t *config) {
    phy_802_3_t *phy = (phy_802_3_t *)calloc(1, sizeof(phy_802_3_t));
    if (!phy) return NULL;
    if (esp_eth_phy_802_3_obj_config_init(phy, config) != ESP_OK) { free(phy); return NULL; }
    return &phy->parent;
}

void eth_driver_init(void) {
    // Arduino core already calls these; ignore ESP_ERR_INVALID_STATE
    esp_netif_init();
    esp_err_t el = esp_event_loop_create_default();
    if (el != ESP_OK && el != ESP_ERR_INVALID_STATE) {
        Serial.printf("[ETH] event loop err: %s\n", esp_err_to_name(el));
        return;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    if (!eth_netif) { Serial.println("[ETH] netif create failed"); return; }

    // MAC — use memset+field to avoid C++ designated-initializer ordering bug
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config;
    memset(&emac_config, 0, sizeof(emac_config));
    emac_config.smi_gpio.mdc_num                         = 31;
    emac_config.smi_gpio.mdio_num                        = 52;
    emac_config.interface                                = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode             = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio             = (emac_rmii_clock_gpio_t)50;
    emac_config.dma_burst_len                            = ETH_DMA_BURST_LEN_32;
    emac_config.intr_priority                            = 0;
    emac_config.mdc_freq_hz                              = 0;
    emac_config.emac_dataif_gpio.rmii.tx_en_num          = 49;
    emac_config.emac_dataif_gpio.rmii.txd0_num           = 34;
    emac_config.emac_dataif_gpio.rmii.txd1_num           = 35;
    emac_config.emac_dataif_gpio.rmii.crs_dv_num         = 28;
    emac_config.emac_dataif_gpio.rmii.rxd0_num           = 29;
    emac_config.emac_dataif_gpio.rmii.rxd1_num           = 30;
    emac_config.clock_config_out_in.rmii.clock_mode      = EMAC_CLK_EXT_IN;
    emac_config.clock_config_out_in.rmii.clock_gpio      = (emac_rmii_clock_gpio_t)-1;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!mac) { Serial.println("[ETH] MAC failed"); return; }

    // PHY — generic 802.3, addr=1 confirmed by MDIO scan
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr       = 1;
    phy_config.reset_gpio_num = -1;
    esp_eth_phy_t *phy = new_generic_802_3_phy(&phy_config);
    if (!phy) { Serial.println("[ETH] PHY failed"); return; }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    if (esp_eth_driver_install(&eth_config, &eth_handle) != ESP_OK) {
        Serial.println("[ETH] driver install failed");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    Serial.println("[ETH] Started, waiting for DHCP...");
}

bool eth_driver_got_ip(void) { return s_got_ip; }
const char *eth_driver_ip_str(void) { return s_ip_str; }
