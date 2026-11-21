#include "analog.h"

#include <algorithm>
#include <cmath>

namespace ambika {
namespace plugin {

namespace {

const double kPi = 3.14159265358979323846;

// Lowest cutoff, chosen so that CV 255 lands near 20 kHz (12.66 * 2^(255/24)).
const double kMinCutoffHz = 12.66;

// Smoothing time constants of the CV lines.
const double kCvTimeConstant = 0.0015;
const double kVcaTimeConstant = 0.001;

inline float SoftClip(float x) {
  // Rational tanh approximation, exact at 0 and saturating to +/-1.
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  const float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

}  // namespace

void AnalogVoice::Init(double sample_rate, int oversampling) {
  sample_rate_ = sample_rate;
  oversampling_ = std::max(1, oversampling);
  const double native_rate = sample_rate_ / oversampling_;
  cv_coefficient_ = static_cast<float>(
      1.0 - std::exp(-1.0 / (kCvTimeConstant * native_rate)));
  vca_coefficient_ = static_cast<float>(
      1.0 - std::exp(-1.0 / (kVcaTimeConstant * native_rate)));
  dc_coefficient_ = static_cast<float>(std::exp(-2.0 * kPi * 8.0 / sample_rate_));
  // One control block (40 native samples) is one Surge coefficient block.
  surge_.Init(sample_rate_, 40 * oversampling_);
  Reset();
}

void AnalogVoice::Reset() {
  dac_value_ = 0.0f;
  crush_counter_ = 0;
  cutoff_cv_ = 255.0f;
  resonance_cv_ = 0.0f;
  vca_cv_ = 0.0f;
  s1_ = s2_ = 0.0f;
  dc_x_ = dc_y_ = 0.0f;
  surge_.Reset();
  last_model_ = 0;
  coefficient_cutoff_cv_ = coefficient_resonance_cv_ = -1.0f;
}

double AnalogVoice::CutoffToHz(double cutoff_cv) {
  return kMinCutoffHz * std::exp2(cutoff_cv / 24.0);
}

void AnalogVoice::Process(const uint8_t* samples, int num_samples,
                          uint8_t cutoff, uint8_t resonance, uint8_t mode,
                          uint8_t vca, uint8_t crush, uint8_t filter_model,
                          uint8_t filter_variant, float* out) {
  if (crush == 0) {
    crush = 1;
  }
  if (filter_model != last_model_) {
    // Switching between filter engines: clear both so nothing rings over.
    s1_ = s2_ = 0.0f;
    surge_.Reset();
    last_model_ = filter_model;
  }
  if (filter_model != 0 && num_samples * oversampling_ <= 256) {
    ProcessSurge(samples, num_samples, cutoff, resonance, vca, crush,
                 filter_model, filter_variant, out);
    return;
  }
  const double max_cutoff = 0.45 * sample_rate_;
  const float cutoff_target = cutoff;
  const float resonance_target = resonance;
  const float vca_target = vca;

  for (int i = 0; i < num_samples; ++i) {
    // DAC: the voicecard only refreshes the DAC every `crush` samples.
    if (++crush_counter_ >= crush) {
      crush_counter_ = 0;
      dac_value_ = (static_cast<float>(samples[i]) - 128.0f) * (1.0f / 128.0f);
    }

    // CV smoothing, once per native sample.
    cutoff_cv_ += cv_coefficient_ * (cutoff_target - cutoff_cv_);
    resonance_cv_ += cv_coefficient_ * (resonance_target - resonance_cv_);
    vca_cv_ += vca_coefficient_ * (vca_target - vca_cv_);

    if (cutoff_cv_ != coefficient_cutoff_cv_ || resonance_cv_ != coefficient_resonance_cv_) {
      coefficient_cutoff_cv_ = cutoff_cv_;
      coefficient_resonance_cv_ = resonance_cv_;
      const double fc = std::min(CutoffToHz(cutoff_cv_), max_cutoff);
      g_ = static_cast<float>(std::tan(kPi * fc / sample_rate_));
      const float r = resonance_cv_ * (1.0f / 255.0f);
      k_ = 1.41f - 1.39f * r;
      denominator_ = 1.0f / (1.0f + g_ * (k_ + g_));
    }
    const float g = g_;
    const float k = k_;
    const float denominator = denominator_;
    const float gain = vca_cv_ * (1.0f / 255.0f);

    for (int j = 0; j < oversampling_; ++j) {
      const float x = dac_value_;
      const float hp = (x - (k + g) * s1_ - s2_) * denominator;
      const float v1 = g * hp;
      const float bp = v1 + s1_;
      s1_ = SoftClip(bp + v1);
      const float v2 = g * bp;
      const float lp = v2 + s2_;
      s2_ = lp + v2;

      float y;
      switch (mode) {
        case 1: y = bp; break;   // FILTER_MODE_BP
        case 2: y = hp; break;   // FILTER_MODE_HP
        case 3: y = lp + hp; break;  // FILTER_MODE_NOTCH (not exposed by the UI)
        default: y = lp; break;  // FILTER_MODE_LP
      }
      y *= gain;

      // DC blocker.
      const float dc = y - dc_x_ + dc_coefficient_ * dc_y_;
      dc_x_ = y;
      dc_y_ = dc;
      *out++ += dc;
    }
  }
}

void AnalogVoice::ProcessSurge(const uint8_t* samples, int num_samples,
                               uint8_t cutoff, uint8_t resonance, uint8_t vca,
                               uint8_t crush, uint8_t model, uint8_t variant,
                               float* out) {
  // Same DAC as the SVF path, held over the oversampled block.
  const int total = num_samples * oversampling_;
  for (int i = 0; i < num_samples; ++i) {
    if (++crush_counter_ >= crush) {
      crush_counter_ = 0;
      dac_value_ = (static_cast<float>(samples[i]) - 128.0f) * (1.0f / 128.0f);
    }
    for (int j = 0; j < oversampling_; ++j) {
      block_[i * oversampling_ + j] = dac_value_;
    }
  }

  // Cutoff and resonance CVs, smoothed per block (Surge interpolates the
  // coefficients across the block).
  const float block_coefficient =
      1.0f - std::pow(1.0f - cv_coefficient_, static_cast<float>(num_samples));
  cutoff_cv_ += block_coefficient * (static_cast<float>(cutoff) - cutoff_cv_);
  resonance_cv_ += block_coefficient * (static_cast<float>(resonance) - resonance_cv_);
  surge_.SetParameters(model, variant, CutoffToHz(cutoff_cv_),
                       resonance_cv_ * (1.0f / 255.0f));
  surge_.Process(block_, total);

  // VCA and output coupling, per sample as in the SVF path.
  const float vca_target = vca;
  for (int i = 0; i < num_samples; ++i) {
    vca_cv_ += vca_coefficient_ * (vca_target - vca_cv_);
    const float gain = vca_cv_ * (1.0f / 255.0f);
    for (int j = 0; j < oversampling_; ++j) {
      const float y = block_[i * oversampling_ + j] * gain;
      const float dc = y - dc_x_ + dc_coefficient_ * dc_y_;
      dc_x_ = y;
      dc_y_ = dc;
      *out++ += dc;
    }
  }
}

}  // namespace plugin
}  // namespace ambika
