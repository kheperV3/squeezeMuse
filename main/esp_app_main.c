/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "platform_esp32.h"
#include "led.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "nvs_utilities.h"
#include "http_server.h"
#include "trace.h"
#include "wifi_manager.h"
#include "squeezelite-ota.h"
#include <math.h>
#include "config.h"
#include "audio_controls.h"
#include "telnet.h"
#include "driver/i2c.h"


static const char certs_namespace[] = "certificates";
static const char certs_key[] = "blob";
static const char certs_version[] = "version";

EventGroupHandle_t wifi_event_group;

bool bypass_wifi_manager=false;
const int CONNECTED_BIT = BIT0;
#define JOIN_TIMEOUT_MS (10000)
#define LOCAL_MAC_SIZE 20
static const char TAG[] = "esp_app_main";
#define DEFAULT_HOST_NAME "squeezelite"
char * fwurl = NULL;

static bool bWifiConnected=false;
extern const uint8_t server_cert_pem_start[] asm("_binary_github_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_github_pem_end");

// as an exception _init function don't need include
extern void services_init(void);
extern void	display_init(char *welcome);

/* brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event */
void cb_connection_got_ip(void *pvParameter){
	static ip4_addr_t ip;
	tcpip_adapter_ip_info_t ipInfo; 

	tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
	if (ip.addr && ipInfo.ip.addr != ip.addr) {
		ESP_LOGW(TAG, "IP change, need to reboot");
		if(!wait_for_commit()){
			ESP_LOGW(TAG,"Unable to commit configuration. ");
		}
		esp_restart();
	}
	ip.addr = ipInfo.ip.addr;
	ESP_LOGI(TAG, "I have a connection!");
	xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
	bWifiConnected=true;
	led_unpush(LED_GREEN);
}
void cb_connection_sta_disconnected(void *pvParameter){
	led_blink_pushed(LED_GREEN, 250, 250);
	bWifiConnected=false;
	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
}
bool wait_for_wifi(){
	bool connected=(xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT)!=0;
	if(!connected){
		ESP_LOGD(TAG,"Waiting for WiFi...");
	    connected = (xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
	                                   pdFALSE, pdTRUE, JOIN_TIMEOUT_MS / portTICK_PERIOD_MS)& CONNECTED_BIT)!=0;
	    if(!connected){
	    	ESP_LOGW(TAG,"wifi timeout.");
	    }
	    else
	    {
	    	ESP_LOGI(TAG,"WiFi Connected!");
	    }
	}
    return connected;
}

char * process_ota_url(){
    ESP_LOGI(TAG,"Checking for update url");
    char * fwurl=config_alloc_get(NVS_TYPE_STR, "fwurl");
	if(fwurl!=NULL)
	{
		ESP_LOGD(TAG,"Deleting nvs entry for Firmware URL %s", fwurl);
		config_delete_key("fwurl");
	}
	return fwurl;
}

esp_log_level_t  get_log_level_from_char(char * level){
	if(!strcasecmp(level, "NONE"    )) { return ESP_LOG_NONE  ;}
	if(!strcasecmp(level, "ERROR"   )) { return ESP_LOG_ERROR ;}
	if(!strcasecmp(level, "WARN"    )) { return ESP_LOG_WARN  ;}
	if(!strcasecmp(level, "INFO"    )) { return ESP_LOG_INFO  ;}
	if(!strcasecmp(level, "DEBUG"   )) { return ESP_LOG_DEBUG ;}
	if(!strcasecmp(level, "VERBOSE" )) { return ESP_LOG_VERBOSE;}
	return ESP_LOG_WARN;
}
void set_log_level(char * tag, char * level){
	esp_log_level_set(tag, get_log_level_from_char(level));
}
esp_err_t update_certificates(){
//	server_cert_pem_start
//	server_cert_pem_end

	nvs_handle handle;
	esp_err_t esp_err;
    esp_app_desc_t running_app_info;

	ESP_LOGI(TAG,   "About to check if certificates need to be updated in flash");
	esp_err = nvs_open_from_partition(settings_partition, certs_namespace, NVS_READWRITE, &handle);
	if (esp_err != ESP_OK) {
		ESP_LOGE(TAG,  "Unable to open name namespace %s. Error %s", certs_namespace, esp_err_to_name(esp_err));
		return esp_err;
	}

	const esp_partition_t *running = esp_ota_get_running_partition();
	if(running->subtype !=ESP_PARTITION_SUBTYPE_APP_FACTORY ){
		ESP_LOGI(TAG, "Running partition [%s] type %d subtype %d (offset 0x%08x)", running->label, running->type, running->subtype, running->address);

	}

	if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
		ESP_LOGI(TAG, "Running version: %s", running_app_info.version);
	}


	size_t len=0;
	char *str=NULL;
	bool changed=false;
	if ( (esp_err= nvs_get_str(handle, certs_version, NULL, &len)) == ESP_OK) {
		str=(char *)malloc(len);
		if ( (esp_err = nvs_get_str(handle,  certs_version, str, &len)) == ESP_OK) {
			printf("String associated with key '%s' is %s \n", certs_version, str);
		}
	}
	if(str!=NULL){
		if(strcmp((char *)running_app_info.version,(char *)str )){
			// Versions are different
			ESP_LOGW(TAG,"Found a different software version. Updating certificates");
			changed=true;
		}
		free(str);
	}
	else {
		ESP_LOGW(TAG,"No certificate found. Adding certificates");
		changed=true;
	}

	if(changed){

		esp_err = nvs_set_blob(handle, certs_key, server_cert_pem_start, (server_cert_pem_end-server_cert_pem_start));
		if(esp_err!=ESP_OK){
			ESP_LOGE(TAG, "Failed to store certificate data: %s", esp_err_to_name(esp_err));
		}
		else {
			ESP_LOGI(TAG,"Updated stored https certificates");
			esp_err = nvs_set_str(handle,  certs_version, running_app_info.version);
			if(esp_err!=ESP_OK){
				ESP_LOGE(TAG, "Failed to store app version: %s", esp_err_to_name(esp_err));
			}
			else {
				esp_err = nvs_commit(handle);
				if(esp_err!=ESP_OK){
					ESP_LOGE(TAG, "Failed to commit certificate changes: %s", esp_err_to_name(esp_err));
				}
			}
		}
	}

	nvs_close(handle);
	return ESP_OK;
}
const char * get_certificate(){
	nvs_handle handle;
	esp_err_t esp_err;
	char *blob =NULL;
//
	ESP_LOGD(TAG,  "Fetching certificate.");
	esp_err = nvs_open_from_partition(settings_partition, certs_namespace, NVS_READONLY, &handle);
	if(esp_err == ESP_OK){
        size_t len;
        esp_err = nvs_get_blob(handle, certs_key, NULL, &len);
        if( esp_err == ESP_OK) {
            blob = (char *)malloc(len);
            esp_err = nvs_get_blob(handle, certs_key, blob, &len);
            if ( esp_err  == ESP_OK) {
                printf("Blob associated with key '%s' is %d bytes long: \n", certs_key, len);
            }
        }
        else{
        	ESP_LOGE(TAG,  "Unable to get the existing blob from namespace %s. [%s]", certs_namespace, esp_err_to_name(esp_err));
        }
        nvs_close(handle);
	}
	else{
		ESP_LOGE(TAG,  "Unable to open name namespace %s. [%s]", certs_namespace, esp_err_to_name(esp_err));
	}
	return blob;
}

#define DEFAULT_NAME_WITH_MAC(var,defval) char var[strlen(defval)+sizeof(macStr)]; strcpy(var,defval); strcat(var,macStr)
void register_default_nvs(){
	uint8_t mac[6];
	char macStr[LOCAL_MAC_SIZE+1];
	char default_command_line[strlen(CONFIG_DEFAULT_COMMAND_LINE)+sizeof(macStr)];

	esp_read_mac((uint8_t *)&mac, ESP_MAC_WIFI_STA);
	snprintf(macStr, LOCAL_MAC_SIZE-1,"-%x%x%x", mac[3], mac[4], mac[5]);

	DEFAULT_NAME_WITH_MAC(default_bt_name,"Muse");
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_name", default_bt_name);
	config_set_default(NVS_TYPE_STR, "bt_name", default_bt_name, 0);



	DEFAULT_NAME_WITH_MAC(default_host_name,"Muse");
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "host_name", default_host_name);
	config_set_default(NVS_TYPE_STR, "host_name", default_host_name, 0);
	


	DEFAULT_NAME_WITH_MAC(default_airplay_name,"Muse-Airplay");
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "airplay_name",default_airplay_name);
	config_set_default(NVS_TYPE_STR, "airplay_name",default_airplay_name , 0);


	DEFAULT_NAME_WITH_MAC(default_ap_name,"Muse");
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_ssid", default_ap_name);
	config_set_default(NVS_TYPE_STR, "ap_ssid",default_ap_name , 0);



	strncpy(default_command_line, "squeezelite -o I2S -b 500:2000 -d all=info -C 30 -W",sizeof(default_command_line)-1);
	strncat(default_command_line, " -n ",sizeof(default_command_line)-1);
	strncat(default_command_line, default_host_name,sizeof(default_command_line)-1);
       

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "autoexec", "1");
	config_set_default(NVS_TYPE_STR,"autoexec","1", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "autoexec1",default_command_line);
	config_set_default(NVS_TYPE_STR,"autoexec1",default_command_line,0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "a2dp_sink_name", CONFIG_A2DP_SINK_NAME);
	config_set_default(NVS_TYPE_STR, "a2dp_sink_name", "SMSL BT4.2", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bt_sink_pin", STR(CONFIG_BT_SINK_PIN));
	config_set_default(NVS_TYPE_STR, "bt_sink_pin", "1234", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "release_url", "https://github.com/sle118/squeezelite-esp32/releases");
	//config_set_default(NVS_TYPE_STR, "release_url","https://github.com/sle118/squeezelite-esp32/releases" , 0);
	config_set_default(NVS_TYPE_STR, "release_url", "https://github.com/sle118/squeezelite-esp32/releases", 0);


	ESP_LOGD(TAG,"Registering default value for key %s, value %s","ap_ip_address","192.168.4.1" );
	config_set_default(NVS_TYPE_STR, "ap_ip_address","192.168.4.1" , 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_ip_gateway", "192.168.4.1");
	config_set_default(NVS_TYPE_STR, "ap_ip_gateway","192.168.4.1" , 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s","ap_ip_netmask","255.255.255.0" );
	config_set_default(NVS_TYPE_STR, "ap_ip_netmask","255.255.255.0" , 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_channel", "1");
	config_set_default(NVS_TYPE_STR, "ap_channel","1" , 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ap_pwd", "musemuse");
	config_set_default(NVS_TYPE_STR, "ap_pwd", "musemuse", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "airplay_port", "5000");
	config_set_default(NVS_TYPE_STR, "airplay_port", "5000", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "a2dp_dev_name", "Muse");
	config_set_default(NVS_TYPE_STR, "a2dp_dev_name", "Muse", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "bypass_wm", "0");
	config_set_default(NVS_TYPE_STR, "bypass_wm", "0", 0);

	ESP_LOGD(TAG,"Registering default Audio control board type %s, value ","actrls_config");
	config_set_default(NVS_TYPE_STR, "actrls_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "lms_ctrls_raw");
	config_set_default(NVS_TYPE_STR, "lms_ctrls_raw", "n", 0);
	
	ESP_LOGD(TAG,"Registering default Audio control board type %s, value %s", "rotary_config", CONFIG_ROTARY_ENCODER);
	config_set_default(NVS_TYPE_STR, "rotary_config", "", 0);

	//char number_buffer[101] = {};
	//snprintf(number_buffer,sizeof(number_buffer)-1,"%u",OTA_FLASH_ERASE_BLOCK);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_erase_blk", "249856");
	//config_set_default(NVS_TYPE_STR, "ota_erase_blk", number_buffer, 0);
	config_set_default(NVS_TYPE_STR, "ota_erase_blk", "249856", 0);


	//snprintf(number_buffer,sizeof(number_buffer)-1,"%u",OTA_STACK_SIZE);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_stack", "10240");
	//config_set_default(NVS_TYPE_STR, "ota_stack", number_buffer, 0);
	config_set_default(NVS_TYPE_STR, "ota_stack", "10240", 0);


	//snprintf(number_buffer,sizeof(number_buffer)-1,"%d",OTA_TASK_PRIOTITY);
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "ota_prio", "6");
	//config_set_default(NVS_TYPE_STR, "ota_prio", number_buffer, 0);
	config_set_default(NVS_TYPE_STR, "ota_prio", "6", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "enable_bt_sink", "1");
	//config_set_default(NVS_TYPE_STR, "enable_bt_sink", STR(CONFIG_BT_SINK), 0);
	config_set_default(NVS_TYPE_STR, "enable_bt_sink", "1", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "enable_airplay", "1");
	//config_set_default(NVS_TYPE_STR, "enable_airplay", STR(CONFIG_AIRPLAY_SINK), 0);
	config_set_default(NVS_TYPE_STR, "enable_airplay", "1", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "display_config", "");
	//config_set_default(NVS_TYPE_STR, "display_config", CONFIG_DISPLAY_CONFIG, 0);
	config_set_default(NVS_TYPE_STR, "display_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "i2c_config", "");
	//config_set_default(NVS_TYPE_STR, "i2c_config", CONFIG_I2C_CONFIG, 0);
	config_set_default(NVS_TYPE_STR, "i2c_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "spi_config", "");
	//config_set_default(NVS_TYPE_STR, "spi_config", CONFIG_SPI_CONFIG, 0);
	config_set_default(NVS_TYPE_STR, "spi_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "set_GPIO", "");
	//config_set_default(NVS_TYPE_STR, "set_GPIO", CONFIG_SET_GPIO, 0);
	config_set_default(NVS_TYPE_STR, "set_GPIO", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "led_brightness");
	config_set_default(NVS_TYPE_STR, "led_brightness", "", 0);
	
        ESP_LOGD(TAG,"Registering default value for key %s", "spdif_config");
	config_set_default(NVS_TYPE_STR, "spdif_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "dac_config", "bck=5,ws=25,do=26,sda=18,scl=23,i2c=16,model=I2S");
	config_set_default(NVS_TYPE_STR, "dac_config", "bck=5,ws=25,do=26,sda=18,scl=23,model=I2S", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "dac_controlset", "{\"init\":....");
/*
	config_set_default(NVS_TYPE_STR, "dac_controlset", "{\"init\": [{\"reg\": 0,\"val\": 128 },{ \"reg\": 0, \"val\": 0 },{ \"reg\": 25,\"val\": 4 },{ \"reg\": 1, \"val\": 80 }, { \"reg\": 2, \"val\": 243}, { \"reg\": 8, \"val\": 0 }, { \"reg\": 4, \"val\": 192 },{ \"reg\": 0, \"val\": 18 }, { \"reg\": 1, \"val\": 0 }, { \"reg\": 23, \"val\": 24 }, { \"reg\": 24, \"val\": 2 },{ \"reg\": 38, \"val\": 9 }, { \"reg\": 39, \"val\": 128 }, { \"reg\": 42, \"val\": 128	},{ \"reg\": 43, \"val\": 128 }, { \"reg\": 45, \"val\": 0 }, { \"reg\": 27, \"val\": 0 }, { \"reg\": 26, \"val\": 0 },{ \"reg\": 29, \"val\": 28 },{ \"reg\": 4, \"val\": 48},{ \"reg\": 25, \"val\": 0 }, { \"reg\": 46, \"val\": 33 }, { \"reg\": 47, \"val\": 33 },{ \"reg\": 2, \"val\": 0 }]}", 0);
*/
        config_set_default(NVS_TYPE_STR, "dac_controlset", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "jack_mutes_amp", "n");
	config_set_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "bat_config");
	config_set_default(NVS_TYPE_STR, "bat_config", "", 0);
			
	ESP_LOGD(TAG,"Registering default value for key %s", "metadata_config");
	config_set_default(NVS_TYPE_STR, "metadata_config", "", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s", "telnet_enable");
	config_set_default(NVS_TYPE_STR, "telnet_enable", "", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "telnet_buffer", "40000");
	config_set_default(NVS_TYPE_STR, "telnet_buffer", "40000", 0);

	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "telnet_block", "500");
	config_set_default(NVS_TYPE_STR, "telnet_block", "500", 0);
	
	ESP_LOGD(TAG,"Registering default value for key %s, value %s", "stats", "n");
	config_set_default(NVS_TYPE_STR, "stats", "n", 0);
	
	ESP_LOGD(TAG,"Done setting default values in nvs.");
}
///////////////////////////////////////////////////////////////////////
// Write ES8388 register (Muse board)
///////////////////////////////////////////////////////////////////////
#define ES8388_ADDR 0x10

void ES8388_Write_Reg(uint8_t reg, uint8_t val) {
	esp_err_t ret;
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, (ES8388_ADDR<< 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
	i2c_master_write_byte(cmd, val, I2C_MASTER_NACK);
	
	i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(0, cmd, 100 / portTICK_RATE_MS);
        i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C write failed");
	}	
    return ;
}

void app_main()
{
	char * fwurl = NULL;
	ESP_LOGI(TAG,"Starting app_main");
	initialize_nvs();
	ESP_LOGI(TAG,"Setting up telnet.");
	init_telnet(); // align on 32 bits boundaries

	ESP_LOGI(TAG,"Setting up config subsystem.");
	config_init();
/////////////////////////////////////////////////////////////////
// hardware inits for MUSE board
//
/////////////////////////////////////////////////////////////////
#define PA GPIO_NUM_21
#define SDA 18
#define SCL 23

// configure i2c
	i2c_config_t i2c_config = {
			.mode = I2C_MODE_MASTER,
			.sda_io_num = SDA,
			.sda_pullup_en = GPIO_PULLUP_ENABLE,
			.scl_io_num = SCL,
			.scl_pullup_en = GPIO_PULLUP_ENABLE,
			.master.clk_speed = 250000,
		};
	
	i2c_param_config(0, &i2c_config);
	i2c_driver_install(0, I2C_MODE_MASTER, false, false, false);

// CLK_OUT1 ==> MCLK
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
        WRITE_PERI_REG(PIN_CTRL, READ_PERI_REG(PIN_CTRL)& 0xFFFFFFF0);

//amplifier validation
	gpio_reset_pin(PA);
        gpio_set_direction(PA, GPIO_MODE_OUTPUT);
	gpio_set_level(PA, 1);

///////////// init ES8388
//
/////////////////////////////////////////// 
// reset 
ES8388_Write_Reg(0, 0x80); 
ES8388_Write_Reg(0, 0x00); 
// mute
ES8388_Write_Reg(25, 0x04);
ES8388_Write_Reg(1, 0x50); 
//powerup
ES8388_Write_Reg(2, 0x00);
// slave mode
ES8388_Write_Reg(8, 0x00);
// DAC powerdown
ES8388_Write_Reg(4, 0xC0);
// vmidsel/500k ADC/DAC idem
ES8388_Write_Reg(0, 0x12);

ES8388_Write_Reg(1, 0x00);
// i2s 16 bits
ES8388_Write_Reg(23, 0x18);
// sample freq 256
ES8388_Write_Reg(24, 0x02);
// LIN2/RIN2 for mixer
ES8388_Write_Reg(38, 0x09);
// left DAC to left mixer
ES8388_Write_Reg(39, 0x90);
// right DAC to right mixer
ES8388_Write_Reg(42, 0x90);
// DACLRC ADCLRC idem
ES8388_Write_Reg(43, 0x80);
ES8388_Write_Reg(45, 0x00);
// DAC volume max
ES8388_Write_Reg(27, 0x00);
ES8388_Write_Reg(26, 0x00);

ES8388_Write_Reg(2 , 0xF0);
ES8388_Write_Reg(2 , 0x00);
ES8388_Write_Reg(29, 0x1C);
// DAC power-up LOUT1/ROUT1 enabled
ES8388_Write_Reg(4, 0x30);
// unmute
ES8388_Write_Reg(25, 0x00);
// max volume
ES8388_Write_Reg(46, 0x21);
ES8388_Write_Reg(47, 0x21);

////////////////////////////////////////////////////////////////////


	ESP_LOGD(TAG,"Creating event group for wifi");
	wifi_event_group = xEventGroupCreate();
	ESP_LOGD(TAG,"Clearing CONNECTED_BIT from wifi group");
	xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
	

	ESP_LOGI(TAG,"Registering default values");
	register_default_nvs();

	ESP_LOGD(TAG,"Configuring services");
	services_init();

	ESP_LOGD(TAG,"Initializing display");	
	display_init("SqueezeESP32");

#if !RECOVERY_APPLICATION
	ESP_LOGI(TAG,"Checking if certificates need to be updated");
	update_certificates();
#endif

	ESP_LOGD(TAG,"Getting firmware OTA URL (if any)");
	fwurl = process_ota_url();

	ESP_LOGD(TAG,"Getting value for WM bypass, nvs 'bypass_wm'");
	char * bypass_wm = config_alloc_get_default(NVS_TYPE_STR, "bypass_wm", "0", 0);
	if(bypass_wm==NULL)
	{
		ESP_LOGE(TAG, "Unable to retrieve the Wifi Manager bypass flag");
		bypass_wifi_manager = false;
	}
	else {
		bypass_wifi_manager=(strcmp(bypass_wm,"1")==0 ||strcasecmp(bypass_wm,"y")==0);
	}

	ESP_LOGD(TAG,"Getting audio control mapping ");
	char *actrls_config = config_alloc_get_default(NVS_TYPE_STR, "actrls_config", NULL, 0);
	if (actrls_init(actrls_config) == ESP_OK) {
		ESP_LOGD(TAG,"Initializing audio control buttons type %s", actrls_config);	
	} else {
		ESP_LOGD(TAG,"No audio control buttons");
	}
	if (actrls_config) free(actrls_config);

	/* start the wifi manager */
	ESP_LOGD(TAG,"Blinking led");
	led_blink_pushed(LED_GREEN, 250, 250);

	if(bypass_wifi_manager){
		ESP_LOGW(TAG,"\n\nwifi manager is disabled. Please use wifi commands to connect to your wifi access point.\n\n");
	}
	else {
		ESP_LOGW(TAG,"\n\nwifi manager is ENABLED. Starting...\n\n");
		wifi_manager_start();
		wifi_manager_set_callback(EVENT_STA_GOT_IP, &cb_connection_got_ip);
		wifi_manager_set_callback(EVENT_STA_DISCONNECTED, &cb_connection_sta_disconnected);
		/* Start the telnet service after we are certain that the network stack has been properly initialized.
		 * This can be either after we're started the AP mode, or after we've started the STA mode  */
		wifi_manager_set_callback(ORDER_START_AP, &start_telnet);
		wifi_manager_set_callback(ORDER_CONNECT_STA, &start_telnet);
	}
	console_start();
	if(fwurl && strlen(fwurl)>0){
#if RECOVERY_APPLICATION
		while(!bWifiConnected){
			wait_for_wifi();
			taskYIELD();
		}
		ESP_LOGI(TAG,"Updating firmware from link: %s",fwurl);
		start_ota(fwurl);
#else
		ESP_LOGE(TAG,"Restarted to application partition. We're not going to perform OTA!");
#endif
		free(fwurl);


	}
}
