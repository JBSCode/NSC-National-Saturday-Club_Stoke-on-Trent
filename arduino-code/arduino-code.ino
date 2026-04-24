#include <Adafruit_NeoPixel.h>

#define PIN_A A0
#define PIN_B A1
#define LED_COUNT 200
#define BRIGHTNESS 170

// 0 = very slow, 100 = fast
int speedPC = 20;

#define MAX_BLOBS 3
#define FP 16   // fixed point scale: 1 pixel = 16 units

Adafruit_NeoPixel stripA(LED_COUNT, PIN_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(LED_COUNT, PIN_B, NEO_GRB + NEO_KHZ800);

struct Blob {
  byte active;
  int pos;          // pixel * 16
  int speed;        // sub-pixel speed
  byte len;         // pixels
  byte level;
  unsigned long next;
};

Blob a[MAX_BLOBS];
Blob b[MAX_BLOBS];

const byte pal[4][3] = {
  {210, 82, 45},
  {170, 55, 32},
  {230, 115, 65},
  {130, 38, 25}
};

byte clampAdd(byte x, int y) {
  int v = x + y;
  if (v > 255) return 255;
  if (v < 0) return 0;
  return v;
}

// dist16 and len16 are both fixed-point
byte feather16(int dist16, int len16) {
  if (dist16 < 0) dist16 = -dist16;
  if (dist16 >= len16) return 0;

  int x = 255 - ((long)dist16 * 255L / len16);

  // smoother falloff than linear, still cheap
  return (byte)((long)x * x / 255L);
}

void spawn(Blob blobs[]) {
  for (byte i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) {
      blobs[i].active = 1;
      blobs[i].pos = -random(20, 80) * FP;
      blobs[i].len = random(24, 52);
      blobs[i].level = random(90, 180);

      // Slower + smoother at low speeds.
      // At speedPC 0 it still creeps very slowly.
      int base = map(speedPC, 0, 100, 1, 14);
      blobs[i].speed = base + random(0, max(2, base / 2));

      blobs[i].next = 0;
      return;
    }
  }
}

void updateBlobs(Blob blobs[], unsigned long now) {
  for (byte i = 0; i < MAX_BLOBS; i++) {
    if (blobs[i].active) {
      blobs[i].pos += blobs[i].speed;

      if ((blobs[i].pos / FP) > LED_COUNT + blobs[i].len) {
        blobs[i].active = 0;
        blobs[i].next = now + random(1200, 6000);
      }
    } else {
      if (blobs[i].next && now > blobs[i].next) {
        spawn(blobs);
        blobs[i].next = 0;
      }
    }
  }
}

void drawStrip(Adafruit_NeoPixel &s, Blob blobs[], byte palOffset) {
  for (int px = 0; px < LED_COUNT; px++) {
    byte r = 2;
    byte g = 1;
    byte bl = 0;

    int px16 = px * FP;

    for (byte j = 0; j < MAX_BLOBS; j++) {
      if (!blobs[j].active) continue;

      int dist16 = px16 - blobs[j].pos;
      int len16 = blobs[j].len * FP;

      byte f = feather16(dist16, len16);
      if (f == 0) continue;

      byte pi = (j + palOffset) & 3;
      int aLevel = ((int)f * blobs[j].level) >> 8;

      r  = clampAdd(r,  ((int)pal[pi][0] * aLevel) >> 8);
      g  = clampAdd(g,  ((int)pal[pi][1] * aLevel) >> 8);
      bl = clampAdd(bl, ((int)pal[pi][2] * aLevel) >> 8);
    }

    s.setPixelColor(px, s.Color(r, g, bl));
  }

  s.show();
}

void setup() {
  randomSeed(analogRead(A5));

  stripA.begin();
  stripB.begin();

  stripA.setBrightness(BRIGHTNESS);
  stripB.setBrightness(BRIGHTNESS);

  stripA.show();
  stripB.show();

  unsigned long now = millis();

  for (byte i = 0; i < MAX_BLOBS; i++) {
    a[i].active = 0;
    b[i].active = 0;
    a[i].next = now + random(200, 2500);
    b[i].next = now + random(800, 4000);
  }

  spawn(a);
  spawn(b);
}

void loop() {
  unsigned long now = millis();

  updateBlobs(a, now);
  updateBlobs(b, now + 777);

  drawStrip(stripA, a, 0);
  drawStrip(stripB, b, 2);

  // Lower delay = smoother motion, but more CPU use.
  // 10-14 is usually a good UNO range.
  delay(12);
}