#pragma once
#include <math.h>

/// Eases a displayed value toward the latest telemetry sample.
///
/// Telemetry arrives at 20-30 Hz, the panel refreshes faster: drawing every
/// sample as-is makes needles and the horizon move in visible steps. Instead the
/// widget keeps a target and moves its displayed value toward it exponentially,
/// which hides the sample rate and the odd late frame without overshooting.
///
/// `period` > 0 marks a circular quantity (e.g. 3600 for heading in tenths of a
/// degree): the value then takes the short way round and stays in [0, period).
class SmoothedValue {
public:
  explicit SmoothedValue(float period = 0.0f) : _period(period) {}

  /// Set a new target. `snap` jumps straight to it (first sample, screenshots).
  void setTarget(float target, bool snap = false) {
    _target = target;
    if (snap || !_initialised) {
      _value = target;
      _initialised = true;
    }
  }

  /// Advance by `dtMs` with time constant `tauMs`.
  /// Returns true if the displayed value changed.
  bool step(float dtMs, float tauMs) {
    float diff = delta();
    if (diff == 0.0f) return false;
    float k = tauMs > 0.0f ? 1.0f - expf(-dtMs / tauMs) : 1.0f;
    float next = diff * k;
    // Close enough: land exactly so the widget can stop redrawing
    if (fabsf(diff - next) < SETTLE) next = diff;
    _value = wrap(_value + next);
    return true;
  }

  float value() const { return _value; }
  float target() const { return _target; }
  bool settled() const { return delta() == 0.0f; }

private:
  static constexpr float SETTLE = 0.2f;  // in wire units (tenths)
  float _period;
  float _target = 0.0f;
  float _value = 0.0f;
  bool _initialised = false;

  float delta() const {
    float d = _target - _value;
    if (_period > 0.0f) {
      d = fmodf(d, _period);
      if (d > _period / 2) d -= _period;
      if (d < -_period / 2) d += _period;
    }
    return d;
  }

  float wrap(float v) const {
    if (_period <= 0.0f) return v;
    v = fmodf(v, _period);
    return v < 0.0f ? v + _period : v;
  }
};
