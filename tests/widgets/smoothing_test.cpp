// Host-side test for lib/hud_widgets/Smoothing.h (display-side easing).
//
//   c++ -std=c++17 -Ilib/hud_widgets tests/widgets/smoothing_test.cpp -o smoothing_test && ./smoothing_test

#include <cmath>
#include <cstdio>

#include "Smoothing.h"

static int failures = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static void firstSampleSnaps() {
  SmoothedValue v;
  v.setTarget(123);
  CHECK(v.value() == 123);
  CHECK(v.settled());
  CHECK(!v.step(25, 45));
}

static void easesMonotonicallyAndSettles() {
  SmoothedValue v;
  v.setTarget(0);
  v.setTarget(100);
  float prev = v.value();
  int steps = 0;
  while (!v.settled() && steps < 1000) {
    CHECK(v.step(25, 45));
    CHECK(v.value() > prev);
    CHECK(v.value() <= 100);
    prev = v.value();
    steps++;
  }
  CHECK(v.value() == 100);
  CHECK(steps < 40);  // ~1 s at 40 fps
}

static void halfwayAfterOneTimeConstantOrSo() {
  SmoothedValue v;
  v.setTarget(0);
  v.setTarget(100);
  v.step(45, 45);
  CHECK(std::fabs(v.value() - 63.2f) < 0.5f);  // 1 - 1/e
}

static void headingTakesTheShortWayRound() {
  SmoothedValue v(3600);
  v.setTarget(3550);  // 355°
  v.setTarget(50);    //   5°: +10°, not -350°
  v.step(10, 45);
  CHECK(v.value() > 3550 || v.value() < 50);
  for (int i = 0; i < 100; i++) v.step(25, 45);
  CHECK(v.value() == 50);
  CHECK(v.value() >= 0 && v.value() < 3600);
}

static void snapOverridesEasing() {
  SmoothedValue v;
  v.setTarget(0);
  v.setTarget(500, true);
  CHECK(v.value() == 500);
}

int main() {
  firstSampleSnaps();
  easesMonotonicallyAndSettles();
  halfwayAfterOneTimeConstantOrSo();
  headingTakesTheShortWayRound();
  snapOverridesEasing();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("smoothing: all tests passed\n");
  return 0;
}
