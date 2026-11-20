#include "core/Anim.h"
#include <cmath>

namespace ui {

static double gNow = 0;
static bool gReduce = false;

double now() { return gNow; }
void setNow(double t) { gNow = t; }
void setReduceMotion(bool on) { gReduce = on; }
bool reduceMotion() { return gReduce; }

float cubicBezier(float x1, float y1, float x2, float y2, float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    auto bx = [&](float s) { float u = 1 - s; return 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; };
    auto by = [&](float s) { float u = 1 - s; return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s; };
    float lo = 0, hi = 1, s = t;
    for (int i = 0; i < 30; i++) {
        s = (lo + hi) * .5f;
        if (bx(s) < t) lo = s; else hi = s;
    }
    return by(s);
}

float easeLinear(float t) { return t; }
float easeCss(float t) { return cubicBezier(.25f, .1f, .25f, 1.f, t); }
float easeInOut(float t) { return cubicBezier(.42f, 0.f, .58f, 1.f, t); }
float easeOut(float t) { return cubicBezier(.2f, .8f, .2f, 1.f, t); }
float easeArt(float t) { return cubicBezier(.2f, .7f, .2f, 1.f, t); }

float Tween::progress() const {
    if (dur <= 0) return now() >= start + delay ? 1.f : 0.f;
    double p = (now() - start - delay) / dur;
    return (float)std::fmin(1.0, std::fmax(0.0, p));
}

float Tween::value() const {
    return from + (to - from) * ease(progress());
}

bool Tween::done() const { return progress() >= 1.f; }

void Tween::set(float target, float duration, Ease e, float delayS) {
    if (target == to && dur > 0 && !done()) return;
    restart(value(), target, duration, e, delayS);
}

void Tween::restart(float fromV, float target, float duration, Ease e, float delayS) {
    from = fromV;
    to = target;
    start = now();
    dur = gReduce ? 0.f : duration;
    delay = gReduce ? 0.f : delayS;
    ease = e;
}

void Tween::snap(float v) {
    from = to = v;
    dur = 0;
    delay = 0;
    start = now();
}

}
