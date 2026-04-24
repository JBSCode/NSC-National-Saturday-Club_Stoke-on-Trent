// Memory-optimised UNO version.
// RGB strips only. No RGBW.
// Strip A: smooth sub-pixel blobs.
// Strip B: travelling palette/white gradient.
// Palette advances on each startup using EEPROM:
// Terracotta -> Forest -> Ocean -> Rainbow -> repeat.

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

#ifdef __AVR__
 #include <avr/pgmspace.h>
#endif

#include <math.h>

// ===================== STRIP A =====================
#define LED_PIN     A1
#define LED_COUNT   200
#define BRIGHTNESS  200

#define WHITE_LEN          30
#define WHITE_STEP_MS      50
#define WHITE_LEVEL        128
#define WHITE_SPAWN_MS     4500

#define COLOR_LEN          35
#define COLOR_STEP_MS      50
#define COLOR_LEVEL        255
#define COLOR_SPAWN_MS     2000

#define FEATHER_LEN        5
#define ARRIVE_FADE_MS     350

// 1 LED = 256 sub-steps
#define FP_SHIFT 8
#define FP       256L

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===================== STRIP B =====================
#define LED_PIN_B        A0
#define LED_COUNT_B      20
#define BRIGHTNESS_B     255
#define TRAVEL_PERIOD_MS 4000

Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

// ===================== PALETTES =====================
#define PAL_TERRACOTTA 0
#define PAL_FOREST     1
#define PAL_OCEAN      2
#define PAL_RAINBOW    3
#define PAL_COUNT      4
#define PAL_COLORS     4

#define EEPROM_PALETTE_ADDR 0

const uint8_t palettes[PAL_COUNT][PAL_COLORS][3] PROGMEM = {
  { // Terracotta
    {210,  90,  60},
    {170,  55,  32},
    {230, 115,  65},
    {130,  38,  25}
  },
  { // Forest - greener
    {  8, 120,  35},
    { 20, 180,  55},
    { 45, 220,  85},
    {  4,  75,  28}
  },
  { // Ocean
    {  0,  80, 150},
    {  0, 150, 190},
    { 25,  60, 210},
    {  0,  35,  90}
  },
  { // Rainbow
    {255,  40,  20},
    {255, 160,   0},
    { 30, 190,  70},
    { 40,  80, 255}
  }
};

uint8_t currentPalette = PAL_TERRACOTTA;

uint8_t getNextPalette() {
  uint8_t last = EEPROM.read(EEPROM_PALETTE_ADDR);

  // First use / garbage value protection
  if (last >= PAL_COUNT) {
    last = PAL_COUNT - 1;
  }

  uint8_t next = last + 1;
  if (next >= PAL_COUNT) next = 0;

  EEPROM.update(EEPROM_PALETTE_ADDR, next);
  return next;
}

void getPaletteColor(uint8_t palette, uint8_t index, uint8_t &r, uint8_t &g, uint8_t &b) {
  palette %= PAL_COUNT;
  index &= 3;

  r = pgm_read_byte(&palettes[palette][index][0]);
  g = pgm_read_byte(&palettes[palette][index][1]);
  b = pgm_read_byte(&palettes[palette][index][2]);
}

// ===================== HELPERS =====================
static inline uint8_t addClampU8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + b;
  return (s > 255) ? 255 : (uint8_t)s;
}

static inline uint8_t scaleU8(uint8_t v, uint8_t scale) {
  return (uint8_t)(((uint16_t)v * scale + 127) / 255);
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (uint8_t)(a + (b - a) * t + 0.5f);
}

static inline uint8_t featherIntensityFP(int32_t local, uint8_t len, uint8_t feather) {
  if (len <= 1) return 255;

  if (feather * 2 > len) feather = len / 2;
  if (feather == 0) return 255;

  int32_t lenFP = (int32_t)len * FP;
  int32_t featherFP = (int32_t)feather * FP;

  if (local < 0 || local >= lenFP) return 0;

  if (local < featherFP) {
    return (uint8_t)((local * 255L) / featherFP);
  }

  if (local >= lenFP - featherFP) {
    int32_t t = lenFP - local;
    return (uint8_t)((t * 255L) / featherFP);
  }

  return 255;
}

// ===================== BLOBS =====================
#define MAX_BLOBS 8

struct Blob {
  int32_t pos;        // fixed-point start position, LED * 256
  uint16_t lastMs;    // low 16 bits of millis
  uint16_t spawnMs;   // low 16 bits of millis
  uint8_t len;
  uint8_t feather;
  uint8_t level;
  uint8_t r, g, b;
  uint8_t stepMs;
  uint8_t active;
};

Blob blobs[MAX_BLOBS];

static inline uint16_t now16(uint32_t now) {
  return (uint16_t)now;
}

int8_t allocBlob() {
  for (uint8_t i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) return i;
  }
  return -1;
}

void spawnWhiteBlob(uint32_t now) {
  int8_t idx = allocBlob();
  if (idx < 0) return;

  Blob &b = blobs[idx];

  b.active = 1;
  b.len = WHITE_LEN;
  b.pos = -((int32_t)WHITE_LEN * FP);
  b.feather = FEATHER_LEN;
  b.level = WHITE_LEVEL;
  b.r = WHITE_LEVEL;
  b.g = WHITE_LEVEL;
  b.b = WHITE_LEVEL;
  b.stepMs = WHITE_STEP_MS;
  b.lastMs = now16(now);
  b.spawnMs = now16(now);
}

void spawnColorBlob(uint32_t now) {
  int8_t idx = allocBlob();
  if (idx < 0) return;

  Blob &b = blobs[idx];

  uint8_t r, g, bl;
  getPaletteColor(currentPalette, random(0, 4), r, g, bl);

  b.active = 1;
  b.len = COLOR_LEN;
  b.pos = -((int32_t)COLOR_LEN * FP);
  b.feather = FEATHER_LEN;
  b.level = COLOR_LEVEL;
  b.r = r;
  b.g = g;
  b.b = bl;
  b.stepMs = COLOR_STEP_MS;
  b.lastMs = now16(now);
  b.spawnMs = now16(now);
}

void updateBlobs(uint32_t now) {
  uint16_t n = now16(now);

  for (uint8_t i = 0; i < MAX_BLOBS; i++) {
    Blob &b = blobs[i];
    if (!b.active) continue;

    uint16_t elapsed = n - b.lastMs;
    if (elapsed == 0) continue;

    b.lastMs = n;

    // stepMs = time to move 1 LED
    b.pos += ((int32_t)elapsed * FP) / b.stepMs;

    if (b.pos >= ((int32_t)LED_COUNT * FP)) {
      b.active = 0;
    }
  }
}

// No RGB accumulator arrays.
// For each pixel, calculate combined colour directly.
void renderAndPushStripA(uint32_t now) {
  uint16_t n = now16(now);

  for (uint16_t x = 0; x < LED_COUNT; x++) {
    uint8_t outR = 0;
    uint8_t outG = 0;
    uint8_t outB = 0;

    int32_t pixelPos = (int32_t)x * FP;

    for (uint8_t bi = 0; bi < MAX_BLOBS; bi++) {
      Blob &b = blobs[bi];
      if (!b.active) continue;

      int32_t local = pixelPos - b.pos;
      uint8_t kFeather = featherIntensityFP(local, b.len, b.feather);
      if (kFeather == 0) continue;

      uint16_t age = n - b.spawnMs;
      uint8_t arriveK = (age >= ARRIVE_FADE_MS)
        ? 255
        : (uint8_t)(((uint32_t)age * 255UL) / ARRIVE_FADE_MS);

      uint8_t k = scaleU8(kFeather, arriveK);
      uint8_t a = scaleU8(b.level, k);

      outR = addClampU8(outR, scaleU8(b.r, a));
      outG = addClampU8(outG, scaleU8(b.g, a));
      outB = addClampU8(outB, scaleU8(b.b, a));
    }

    strip.setPixelColor(x, strip.Color(outR, outG, outB));
  }

  strip.show();
}

// ===================== STRIP B =====================
void updateStripB_Travel(uint32_t now) {
  float t = (float)(now % TRAVEL_PERIOD_MS) / (float)TRAVEL_PERIOD_MS;
  const float cyclesAcrossStrip = 1.0f;
  uint8_t whiteRGB = WHITE_LEVEL;

  for (uint8_t i = 0; i < LED_COUNT_B; i++) {
    float u = t + cyclesAcrossStrip * ((float)i / (float)LED_COUNT_B);
    u = u - floorf(u);

    float seg = u * 4.0f;
    uint8_t s = (uint8_t)seg;
    float f = seg - (float)s;

    uint8_t r0 = 0, g0 = 0, b0 = 0;
    uint8_t r1 = 0, g1 = 0, b1 = 0;

    uint8_t pr, pg, pb;
    getPaletteColor(currentPalette, i & 3, pr, pg, pb);

    switch (s) {
      case 0:
        r1 = whiteRGB;
        g1 = whiteRGB;
        b1 = whiteRGB;
        break;

      case 1:
        r0 = whiteRGB;
        g0 = whiteRGB;
        b0 = whiteRGB;
        break;

      case 2:
        r1 = pr;
        g1 = pg;
        b1 = pb;
        break;

      default:
        r0 = pr;
        g0 = pg;
        b0 = pb;
        break;
    }

    stripB.setPixelColor(i,
      lerpU8(r0, r1, f),
      lerpU8(g0, g1, f),
      lerpU8(b0, b1, f)
    );
  }

  stripB.show();
}

// ===================== MAIN =====================
uint32_t lastSpawnWhite = 0;
uint32_t lastSpawnColor = 0;

void setup() {
  currentPalette = getNextPalette();

  // Still seed random for colour selection within the active palette.
  randomSeed(analogRead(A5) ^ micros());

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  stripB.begin();
  stripB.setBrightness(BRIGHTNESS_B);
  stripB.show();

  for (uint8_t i = 0; i < MAX_BLOBS; i++) {
    blobs[i].active = 0;
  }

  uint32_t now = millis();
  lastSpawnWhite = now;
  lastSpawnColor = now;

  spawnWhiteBlob(now);
  spawnColorBlob(now);
}

void loop() {
  uint32_t now = millis();

  if ((uint32_t)(now - lastSpawnWhite) >= WHITE_SPAWN_MS) {
    lastSpawnWhite = now;
    spawnWhiteBlob(now);
  }

  if ((uint32_t)(now - lastSpawnColor) >= COLOR_SPAWN_MS) {
    lastSpawnColor = now;
    spawnColorBlob(now);
  }

  updateBlobs(now);
  renderAndPushStripA(now);
  updateStripB_Travel(now);
}