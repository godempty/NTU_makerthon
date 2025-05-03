#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

const char* ssid = "MakeNTU2025-A-2.4G";
const char* password = "MakeNTU2025";
const char* uploadAPI = "http://10.10.32.126:1145/update";
const char* fetchAPI = "http://10.10.32.126:1145/get_voices";

#define I2S_WS 15
#define I2S_SCK 2
#define I2S_SD 13

#define SD_CS 5

#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define BUFFER_SIZE 64

File audioFile;
bool recording = false;
int dataSize = 0;
const char* sendAudioPath = "/tosend.wav";
const int chunkSize = 3000;
long fileSize = 0;

const char* recvAudioPath = "/receive.wav";

unsigned long lastRun = 0;
const unsigned long fetchPeriod = 10000;

String CLIENT_NAME = "114514";
String MAC_ADDR = "c8:f0:9e:ea:c9:ec";

WiFiClientSecure client;

void writeWAVHeader(File file, int sampleRate, int bitsPerSample, int numChannels, int dataSize) {
  file.seek(0);
  file.write((byte*)&"RIFF", 4);
  uint32_t fileSize = dataSize + 36;
  file.write((byte*)&fileSize, 4);
  file.write((byte*)&"WAVEfmt ", 8);
  uint32_t subChunk1Size = 16;
  uint16_t audioFormat = 1;
  file.write((byte*)&subChunk1Size, 4);
  file.write((byte*)&audioFormat, 2);
  file.write((byte*)&numChannels, 2);
  file.write((byte*)&sampleRate, 4);
  uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
  file.write((byte*)&byteRate, 4);
  uint16_t blockAlign = numChannels * bitsPerSample / 8;
  file.write((byte*)&blockAlign, 2);
  file.write((byte*)&bitsPerSample, 2);
  file.write((byte*)&"data", 4);
  file.write((byte*)&dataSize, 4);
}

void startRecording() {
  audioFile = SD.open(sendAudioPath, FILE_WRITE);
  if (!audioFile) {
    Serial.println("Failed to create file!");
    return;
  }
  writeWAVHeader(audioFile, SAMPLE_RATE, BITS_PER_SAMPLE, 1, 0);
  Serial.println("Recording started...");
  recording = true;
  dataSize = 0;
}

void stopRecording() {
  if (recording) {
    Serial.println("Recording finished!");
    audioFile.seek(4);
    fileSize = dataSize + 36;
    audioFile.write((byte*)&fileSize, 4);
    audioFile.seek(40);
    audioFile.write((byte*)&dataSize, 4);
    audioFile.close();
    Serial.println("File saved!");
    recording = false;
  }
}

void sendWavFile() {
  File file = SD.open(sendAudioPath, "r");
  if (!file) {
    Serial.println("Failed to open file!");
    return;
  }

  long fileSize = file.size();
  long bytesUploaded = 0;
  const int chunkSize = 4410; // 可依需求調整

  String jsonLeft = "{";
  jsonLeft += "\"board_id\":\"esp32_1\",";
  jsonLeft += "\"name\":\"ESP32\",";
  jsonLeft += "\"file\":\"";
  String jsonRight = "\"}";

  long base64Len = ((fileSize + 2) / 3) * 4;
  long contentLen = jsonLeft.length() + base64Len + jsonRight.length();

  WiFiClient client;

  if (!client.connect(server, port)) {
    Serial.println("Connection failed!");
    file.close();
    return;
  }  

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(server));
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(contentLen);
  client.println("Connection: close");
  client.println();
  client.print(jsonLeft);

  uint8_t buffer[chunkSize];
  while (bytesUploaded < fileSize) {
    size_t bytesToRead = min((long)chunkSize, fileSize - bytesUploaded);
    size_t bytesRead = file.read(buffer, bytesToRead);
    if (bytesRead == 0) {
      Serial.println("Failed to read chunk from file!");
      break;
    }
    String encodedData = base64::encode((char*)buffer, bytesRead);
    client.print(encodedData);
    bytesUploaded += bytesRead;
  }
  file.close();
  client.print(jsonRight);
  Serial.println(readResponse());
  client.stop();
}

String readResponse() {
  String response = "";
  long startTime = millis();

  while (millis() - startTime < 10000) {  // Timeout after 5s
    while (client.available()) {
      char c = client.read();
      response += c;

      if (response.length() > 4096) {  // Prevent excessive memory use
        Serial.println("Response too large! Truncating...");
        return response;
      }
    }
    if (response.length() > 0) break;
  }

  return response;
}

String parseJson(String response) {
  int jsonStart = response.indexOf("{");
  if (jsonStart == -1) {
    Serial.println("Invalid JSON response.");
    return "";
  }

  String jsonData = response.substring(jsonStart);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, jsonData);

  if (error) {
    Serial.print("JSON Parsing failed: ");
    Serial.println(error.f_str());
    return "";
  }

  // Extract text fields
  JsonArray candidates = doc["candidates"];
  for (JsonObject candidate : candidates) {
    JsonArray parts = candidate["content"]["parts"];
    for (JsonObject part : parts) {
      const char* text = part["text"];
      if (text) {
        Serial.println("Extracted Text: ");
        Serial.println(text);
        return text;
      }
    }
  }
  return "";
}


bool fetchWavFile() {
  Serial.printf("Fetching WAV file from %s\n", fetchAPI);

  HTTPClient http;
  http.begin(fetchAPI);

  int httpCode = http.GET();

  if (httpCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      int contentLength = http.getSize();
      if (contentLength <= 0) {
        Serial.println("Error: Content length is invalid");
        http.end();
        return false;
      }
      Serial.printf("Content size: %d bytes\n", contentLength);

      File file = SD.open(recvAudioPath, FILE_WRITE);
      if (!file) {
        Serial.printf("Failed to open file %s for writing\n", recvAudioPath);
        http.end();
        return false;
      }

      WiFiClient* stream = http.getStreamPtr();

      uint8_t buffer[BUFFER_SIZE];
      size_t bytesRead = 0;
      size_t totalRead = 0;
      unsigned long downloadStart = millis();

      while (http.connected() && (totalRead < contentLength)) {
        size_t available = stream->available();
        if (available) {
          size_t bytesToRead = min(available, (size_t)BUFFER_SIZE);
          bytesRead = stream->readBytes(buffer, bytesToRead);

          if (bytesRead > 0) {
            file.write(buffer, bytesRead);
            totalRead += bytesRead;

            if (totalRead % 10240 == 0) {
              int percentage = (totalRead * 100) / contentLength;
              Serial.printf("Downloaded: %d bytes (%d%%)\n", totalRead, percentage);
            }
          }
        }
        delay(1);
      }

      file.close();

      unsigned long downloadTime = millis() - downloadStart;
      float downloadSpeed = (float)totalRead / (downloadTime / 1000.0);
      Serial.printf("Download complete: %d bytes in %d ms (%.2f KB/s)\n",
                    totalRead, downloadTime, downloadSpeed / 1024.0);

      if (totalRead != contentLength) {
        Serial.printf("Warning: Downloaded %d bytes, expected %d bytes\n",
                      totalRead, contentLength);
      }

      http.end();
      return true;
    } else {
      Serial.printf("Error: Server returned code %d\n", httpCode);
      http.end();
      return false;
    }
  } else {
    Serial.printf("Error on HTTP request: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed");
    return;
  }
  Serial.println("SD card initialized.");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  HTTPClient http;
  Serial.println(" CONNECTED");
  Serial.println(WiFi.localIP());
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://10.10.32.126:1145/update");
    http.addHeader("Content-Type", "application/json");
    String json = "{\"board_id\":\"esp32_1\",\"name\":\"ESP32\",\"file\":\"testdata\"}";
    int httpResponseCode = http.POST(json);
    String response = http.getString();
    http.end();
  }
}

void loop() {
  /*
  unsigned long now = millis();
  if (now - lastRun >= fetchPeriod) {
    lastRun = now;
    fetchWavFile();
  }
  */

  if (!recording) {
    startRecording();
  } else if (dataSize >= 441000) {  //end recording every 1000 bytes
    stopRecording();
    delay(1000);
    sendWavFile();
  }

  if (recording) {
    int16_t sampleBuffer[BUFFER_SIZE];
    size_t bytesRead;
    i2s_read(I2S_NUM_0, sampleBuffer, BUFFER_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    audioFile.write((byte*)sampleBuffer, bytesRead);
    dataSize += bytesRead;
  }
}