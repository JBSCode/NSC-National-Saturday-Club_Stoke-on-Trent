// Two moving blobs with feathered head+tail:
// - White blob uses W channel at WHITE_LEVEL
// - Terracotta blob uses RGB scaled by TERRA_LEVEL
// Both move independently and can overlap (add + clamp)

#include <Adafruit_NeoPixel.h>

#define LED_PIN     A0
#define LED_COUNT   200
#define BRIGHTNESS  200

// ---------- WHITE BLOB ----------
#define WHITE_LEN          10     // blob length
#define WHITE_STEP_MS      5     // speed (ms per step)
#define WHITE_LEVEL        255    // peak W (0..255)

// ---------- TERRACOTTA BLOB ----------
#define TERRA_LEN          15     // blob length
#define TERRA_STEP_MS      10     // speed (ms per step)
#define TERRA_LEVEL        255    // peak brightness scaling (0..255)

// Terracotta base color (tweak to taste)
#define TERRA_R            210
#define TERRA_G            90
#define TERRA_B            60

// ---------- FEATHER ----------
#define FEATHER_LEN        5      // fade pixels at EACH end (0..min(LEN/2))

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

// Positions + timers
int whiteHead = 0;
int terraHead = 0;
uint32_t lastWhiteMs = 0;
uint32_t lastTerraMs = 0;

// Per-pixel accumulation (so blobs can overlap nicely)
uint8_t accR[LED_COUNT];
uint8_t accG[LED_COUNT];
uint8_t accB[LED_COUNT];
uint8_t accW[LED_COUNT];

static inline uint8_t addClampU8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + (uint16_t)b;
  return (s > 255) ? 255 : (uint8_t)s;
}

static inline uint8_t scaleU8(uint8_t v, uint8_t scale /*0..255*/) {
  // (v * scale) / 255 with rounding
  return (uint8_t)(((uint16_t)v * (uint16_t)scale + 127) / 255);
}

void clearAccumulators() {
  for (int i = 0; i < LED_COUNT; i++) {
    accR[i] = accG[i] = accB[i] = accW[i] = 0;
  }
}

// Returns per-pixel intensity (0..255) for position i in a feathered block of length len
// Feather is applied on BOTH ends. Middle is full strength.
// If feather*2 > len, feather is clamped to len/2.
static inline uint8_t featherIntensity(int i, int len, int feather) {
  if (len <= 1) return 255;

  if (feather < 0) feather = 0;
  if (feather * 2 > len) feather = len / 2;

  int flatLen = len - 2 * feather;

  if (feather == 0) return 255;

  if (i < feather) {
    // fade-in: (i+1)/feather -> 0..1 (first LED not fully off)
    return (uint8_t)((uint32_t)255 * (uint32_t)(i + 1) / (uint32_t)feather);
  }

  if (i >= feather + flatLen) {
    // fade-out: (len-i)/feather -> 1..0 (last LED not fully off)
    int t = len - i; // 1..feather
    return (uint8_t)((uint32_t)255 * (uint32_t)t / (uint32_t)feather);
  }

  // middle
  return 255;
}

void drawFeatherBlobW(int head, int len, uint8_t peakW, int feather) {
  if (len < 1) return;
  if (len > LED_COUNT) len = LED_COUNT;

  for (int i = 0; i < len; i++) {
    int idx = (head + i) % LED_COUNT;

    uint8_t k = featherIntensity(i, len, feather);     // 0..255
    uint8_t w = scaleU8(peakW, k);                     // peakW scaled by feather

    accW[idx] = addClampU8(accW[idx], w);
  }
}

void drawFeatherBlobRGB(int head, int len, uint8_t peakScale, int feather,
                        uint8_t baseR, uint8_t baseG, uint8_t baseB) {
  if (len < 1) return;
  if (len > LED_COUNT) len = LED_COUNT;

  for (int i = 0; i < len; i++) {
    int idx = (head + i) % LED_COUNT;

    uint8_t k = featherIntensity(i, len, feather);     // 0..255
    uint8_t a = scaleU8(peakScale, k);                 // 0..peakScale

    uint8_t r = scaleU8(baseR, a);
    uint8_t g = scaleU8(baseG, a);
    uint8_t b = scaleU8(baseB, a);

    accR[idx] = addClampU8(accR[idx], r);
    accG[idx] = addClampU8(accG[idx], g);
    accB[idx] = addClampU8(accB[idx], b);
  }
}

void pushToStrip() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(accR[i], accG[i], accB[i], accW[i]));
  }
  strip.show();
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  whiteHead = 0;
  terraHead = LED_COUNT / 2;
}

void loop() {
  uint32_t now = millis();

  // Update positions independently
  if ((uint32_t)(now - lastWhiteMs) >= WHITE_STEP_MS) {
    lastWhiteMs = now;
    whiteHead++;
    if (whiteHead >= LED_COUNT) whiteHead = 0;
  }

  if ((uint32_t)(now - lastTerraMs) >= TERRA_STEP_MS) {
    lastTerraMs = now;
    terraHead++;
    if (terraHead >= LED_COUNT) terraHead = 0;
  }

  // Render both blobs every loop
  clearAccumulators();

  // Use same FEATHER_LEN for both; you can split into WHITE_FEATHER / TERRA_FEATHER if you want
  drawFeatherBlobW(whiteHead, WHITE_LEN, WHITE_LEVEL, FEATHER_LEN);
  drawFeatherBlobRGB(terraHead, TERRA_LEN, TERRA_LEVEL, FEATHER_LEN, TERRA_R, TERRA_G, TERRA_B);

  pushToStrip();
}