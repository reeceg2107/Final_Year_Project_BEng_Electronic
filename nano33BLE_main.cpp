#include <PDM.h>
#include "reeceg-project-1_inferencing.h"  

// Voice command settings
const char TARGET_COMMAND[] = "wake_up";
const float VOICE_THRESHOLD = 0.5f;  
const int ALERT_PIN = 2;

// Inference buffer structure
typedef struct {
  signed short *buffers[2];
  unsigned char buf_select;
  unsigned char buf_ready;
  unsigned int buf_count;
  unsigned int n_samples;
} inference_t;

static inference_t inference;
static bool record_ready = false;
static signed short *sampleBuffer;
static bool debug_nn = true;  // Enable debug output

// Counter to limit prints
static unsigned long iterationCount = 0;

// Function prototypes
static void pdm_data_ready_inference_callback(void);
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Edge Impulse Inferencing Demo - Debug Enabled");

  // Initialize the LED pin
  pinMode(ALERT_PIN, OUTPUT);
  digitalWrite(ALERT_PIN, LOW);

  // Initialize Edge Impulse classifier and microphone
  run_classifier_init();
  if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
    ei_printf("ERR: Could not allocate audio buffer\r\n");
    return;
  }
}

void loop() {
  // Wait for a full buffer of audio data to be ready, with timeout
  if (!microphone_inference_record()) {
    ei_printf("ERR: Failed to record audio...\n");
    delay(1000);
    return;
  }

  // Prepare the signal for classification
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = { 0 };

  // Run the classifier
  EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
  if (r != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\n", r);
    delay(1000);
    return;
  }

  iterationCount++;

  // Print debug info every 10 iterations
  if (iterationCount % 10 == 0 && debug_nn) {
    ei_printf("Predictions (DSP: %d ms, Classification: %d ms, Anomaly: %d ms):\n",
              result.timing.dsp, result.timing.classification, result.timing.anomaly);
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      ei_printf("  %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
    }
  }

  // Check for the target command
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    if (strcmp(result.classification[ix].label, TARGET_COMMAND) == 0 &&
        result.classification[ix].value > VOICE_THRESHOLD) {
      ei_printf("Command '%s' detected with confidence %.2f!\n",
                TARGET_COMMAND, result.classification[ix].value);
      digitalWrite(ALERT_PIN, HIGH);
      delay(200);
      digitalWrite(ALERT_PIN, LOW);
      
      // Stop further operation by halting in an infinite loop.
      ei_printf("Operation halted after detection.\n");
      while (true) {
        delay(1000);
      }
    }
  }
  
  // Add a short delay to ease processing load
  delay(100);
}

// Callback: called when new PDM data is available.
static void pdm_data_ready_inference_callback(void) {
  int bytesAvailable = PDM.available();
  int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);
  if (record_ready) {
    for (int i = 0; i < (bytesRead >> 1); i++) {
      inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];
      if (inference.buf_count >= inference.n_samples) {
        inference.buf_select ^= 1;
        inference.buf_count = 0;
        inference.buf_ready = 1;
      }
    }
  }
}

// Initialize the microphone: allocate buffers and start PDM.
static bool microphone_inference_start(uint32_t n_samples) {
  inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));
  if (!inference.buffers[0]) return false;
  inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));
  if (!inference.buffers[1]) {
    free(inference.buffers[0]);
    return false;
  }
  sampleBuffer = (signed short *)malloc((n_samples >> 1) * sizeof(signed short));
  if (!sampleBuffer) {
    free(inference.buffers[0]);
    free(inference.buffers[1]);
    return false;
  }
  inference.buf_select = 0;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;

  PDM.onReceive(&pdm_data_ready_inference_callback);
  PDM.setBufferSize((n_samples >> 1) * sizeof(int16_t));
  if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) {
    ei_printf("Failed to start PDM!\n");
    return false;
  }
  PDM.setGain(127);
  record_ready = true;
  return true;
}

// Wait until a full buffer of audio is available, with a timeout.
static bool microphone_inference_record(void) {
  unsigned long startTime = millis();
  while (inference.buf_ready == 0) {
    if (millis() - startTime > 5000) {
      ei_printf("Timeout waiting for audio buffer.\n");
      return false;
    }
    delay(1);
  }
  inference.buf_ready = 0;
  return true;
}

// Convert raw int16 audio samples to floats for classification.
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
  return 0;
}

// Optionally free allocated memory (if ever needed)
static void microphone_inference_end(void) {
  PDM.end();
  free(inference.buffers[0]);
  free(inference.buffers[1]);
  free(sampleBuffer);
}
