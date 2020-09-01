#include "Arduino.h"
#include "esp_camera.h"
#include "soc/soc.h"  
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include "SD_MMC.h"
#include "FS.h" 

#include <EEPROM.h>

#define EEPROM_SIZE 512 //full size of ESP32 EEPROM (512 bytes)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
 
RTC_DATA_ATTR int bootCount = 0;
 
int pictureNumber = 0;
int pictureMultiplier = 1;

/* INSTRUCTIONS
   - Select "AI Thinker ESP32-CAM" under Tools -> Board
     * Requires installation of ESP32 add-on
   - Connect IO-0 to GND
   - Flash the program, you will need an external serial programmer (FTDI or another Arduino board)
   - When the terminal says "Connecting ...___...___" press the RESET button on the board.
     *(Only applies if the device wasn't powered on with GPIO 0 grounded already!)
   - Disconnect IO-0 and reset/power cycle
 */
  
void setup() {
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable the brownout detector
  Serial.begin(115200); //set baud rate
  Serial.setDebugOutput(true); //ESP will print current state at boot

  //clear EEPROM, uncomment below on first flash or when you want to reset pic number.
  //re-comment and flash again after clearing the EEPROM.
  
//  EEPROM.begin(EEPROM_SIZE);
//  for (int i = 0; i < EEPROM_SIZE; i++) {
//    EEPROM.write(i, 0);
//    }
//  EEPROM.end();

  pinMode(4, INPUT); //remove the camera flash RTC hold while the program runs
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);
 
  Serial.println("Initializing SD card...");

  if(!SD_MMC.begin("/sdcard", true)){
    Serial.println("SD Card Mount Failed!");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD card inserted!");
    return;
  }

  camera_setup();
  
  camera_fb_t * fb = NULL;
 
  fb = esp_camera_fb_get();  //Take a picture
  if(!fb) {
    Serial.println("Failed to take a picture!");
    return;
  }
  //Initialize the EEPROM
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;
  pictureMultiplier = EEPROM.read(1);
  
  if(pictureNumber == 256){ //Increase maximum number of pictures to 65,025 before rolling over
    pictureMultiplier++;
    pictureNumber = 1;  
  }
 
  //Picture storage path on SD card
  String path = "/pic" + String(pictureNumber+(pictureMultiplier*255)) +".jpg";
 
  fs::FS &fs = SD_MMC;
  Serial.printf("Pic location: %s\n", path.c_str()); //print the file location to Serial
 
  File file = fs.open(path.c_str(), FILE_WRITE); //open the path with the added file number
  if(!file){
    Serial.println("Cannot open file!");
  }
  else {
    file.write(fb->buf, fb->len); // write the image to the file at the path
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber); //store the current number in EEPROM to remember on next boot
    EEPROM.write(1, pictureMultiplier); //store the current multiplier ...
    EEPROM.commit();
  }
  file.close(); //done with writing to file
  esp_camera_fb_return(fb); //done with camera
  
  pinMode(4, OUTPUT); //The flash glows after using the SD card, need to turn off!
  digitalWrite(4, LOW);
  rtc_gpio_hold_en(GPIO_NUM_4); //Hold the LOW state in deep sleep mode

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0); //re-enable PIR sensor input
 
  Serial.println("Entering Deep Sleep");
  esp_deep_sleep_start(); //enter deep sleep mode until next wakeup
} 
 
void loop() { //not used
 
}

bool camera_setup(){
  //camera configuration from Espressif example code
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){ //there will be PSRAM, but just in case
    config.frame_size = FRAMESIZE_UXGA; //The largest frame size, 1600x1200
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return 0;
  }

  sensor_t * s = esp_camera_sensor_get();

  if  (s == NULL) {
    Serial.println("Problem setting camera parameters");
    return 0;
  }
  
  //changing settings here to try and fix contrast issues
    //s->set_framesize(s, FRAMESIZE_UXGA);
    //s->set_quality(s, 10);
    s->set_brightness(s, -2);
    s->set_contrast(s, 1);
    s->set_saturation(s, 1);

    s->set_aec_value(s, 0);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 0);

    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    
    s->set_agc_gain(s, 1);
    s->set_gainceiling(s, (gainceiling_t)0);
    
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    
    s->set_dcw(s, 1);
    s->set_bpc(s, 0);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    s->set_colorbar(s, 0);

    delay(1000); //hopefully gives the camera time to adjust

  return 1;
}
