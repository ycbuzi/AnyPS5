#pragma once

namespace ui {

using Ease = float (*)(float);

float cubicBezier(float x1, float y1, float x2, float y2, float t);
float easeLinear(float t);
float easeCss(float t);
float easeInOut(float t);
float easeOut(float t);
float easeArt(float t);

double now();
void setNow(double t);
void setReduceMotion(bool on);
bool reduceMotion();

struct Tween {
    float from = 0, to = 0;
    double start = 0;
    float dur = 0, delay = 0;
    Ease ease = easeOut;

    float value() const;
    float progress() const;
    bool done() const;
    void set(float target, float duration, Ease e = easeOut, float delayS = 0);
    void restart(float fromV, float target, float duration, Ease e = easeOut, float delayS = 0);
    void snap(float v);
};

}
