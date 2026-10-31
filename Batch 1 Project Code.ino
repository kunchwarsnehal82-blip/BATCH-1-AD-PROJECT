#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED pins
#define OLED_SDA 8
#define OLED_SCL 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1
);

// INMP441 microphone pins
#define I2S_PORT I2S_NUM_0
#define I2S_BCLK 4
#define I2S_WS   5
#define I2S_DIN  6

#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512

// Adjust after checking Serial Monitor readings
#define SOUND_THRESHOLD 500

int32_t audioBuffer[BUFFER_SIZE];

uint32_t audioLevel = 0;
bool soundDetected = false;

unsigned long soundUntil = 0;
unsigned long lastDisplayUpdate = 0;

void showStatus(const char *status) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("VOICE ACTIVATOR");
  display.println("----------------");

  display.setCursor(0, 22);
  display.print("Status: ");
  display.println(status);

  display.setCursor(0, 42);
  display.print("Audio level: ");
  display.println(audioLevel);

  display.display();
}

bool startMicrophone() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(
      I2S_MODE_MASTER | I2S_MODE_RX
    ),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN
  };

  if (i2s_driver_install(
        I2S_PORT, &config, 0, nullptr) != ESP_OK) {
    return false;
  }

  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Start OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found!");
    while (true) {
      delay(1000);
    }
  }

  showStatus("STARTING");

  // Start microphone
  if (!startMicrophone()) {
    showStatus("MIC ERROR");
    Serial.println("I2S setup failed!");
    while (true) {
      delay(1000);
    }
  }

  showStatus("LISTENING");

  Serial.println("Voice activator ready.");
  Serial.println("Speak near the microphone.");
}

void loop() {
  size_t bytesRead = 0;

  esp_err_t result = i2s_read(
    I2S_PORT,
    audioBuffer,
    sizeof(audioBuffer),
    &bytesRead,
    portMAX_DELAY
  );

  if (result != ESP_OK || bytesRead == 0) {
    return;
  }

  int samples = bytesRead / sizeof(int32_t);
  uint64_t energy = 0;

  for (int i = 0; i < samples; i++) {
    // Convert the microphone's 24-bit
    // signal in a 32-bit I2S slot.
    int32_t sample = audioBuffer[i] >> 14;

    energy += (uint64_t)(
      (int64_t)sample * sample
    );
  }

  if (samples > 0) {
    audioLevel = (uint32_t)sqrt(
      (double)energy / samples
    );
  }

  // Simple sound-activity detection
  if (audioLevel > SOUND_THRESHOLD) {
    soundDetected = true;
    soundUntil = millis() + 1000;

    Serial.print("Sound detected: ");
    Serial.println(audioLevel);
  }

  if (millis() > soundUntil) {
    soundDetected = false;
  }

  // Refresh OLED every 200 ms
  if (millis() - lastDisplayUpdate >= 200) {
    lastDisplayUpdate = millis();

    if (soundDetected) {
      showStatus("SOUND DETECTED");
    } else {
      showStatus("LISTENING...");
    }

    Serial.print("Audio level: ");
    Serial.println(audioLevel);
  }
}
