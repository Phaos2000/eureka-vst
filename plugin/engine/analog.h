// Analog back end of one voicecard: DAC, VCF and VCA.
//
// This is not Ambika firmware. On the hardware these stages are analog
// circuits driven by the voicecard microcontroller:
// - the 8-bit sample goes to a 12-bit DAC, refreshed every `crush` samples;
// - cutoff and resonance are PWM outputs smoothed by RC filters into CVs;
// - the VCA level is written to the DAC's second channel once per block.
//
// The filter here is a model of the 2-pole state-variable filter card
// (SSM2164 SVF): a TPT state-variable filter with soft saturation in the
// resonance path. It covers the three modes the firmware exposes (LP, BP, HP).
// It is a musical approximation, not a circuit-level simulation.

#ifndef AMBIKA_PLUGIN_ANALOG_H_
#define AMBIKA_PLUGIN_ANALOG_H_

#include <stdint.h>

#include "surge_filter.h"

namespace ambika {
namespace plugin {

class AnalogVoice {
 public:
  // sample_rate is the rate at which Process() runs (oversample x native).
  void Init(double sample_rate, int oversampling);

  // Converts one control block of 8-bit voicecard samples into
  // num_samples * oversampling analog samples, added to out.
  // filter_model 0 is the SVF model below; 1.. selects a Surge XT filter
  // type (see SurgeFilter), with filter_variant as its subtype.
  void Process(const uint8_t* samples, int num_samples,
               uint8_t cutoff, uint8_t resonance, uint8_t mode,
               uint8_t vca, uint8_t crush, uint8_t filter_model,
               uint8_t filter_variant, float* out);

  void Reset();

 private:
  void ProcessSurge(const uint8_t* samples, int num_samples, uint8_t cutoff,
                    uint8_t resonance, uint8_t vca, uint8_t crush, uint8_t model,
                    uint8_t variant, float* out);

 public:

  // Cutoff frequency for an 8-bit cutoff CV. 2 steps per semitone, so the
  // firmware's note tracking (pitch added to cutoff) tracks the keyboard.
  static double CutoffToHz(double cutoff_cv);

 private:
  double sample_rate_ = 78431.0;
  int oversampling_ = 2;

  // DAC state.
  float dac_value_ = 0.0f;
  uint8_t crush_counter_ = 0;

  // Smoothed control voltages (0..255 scale).
  float cutoff_cv_ = 255.0f;
  float resonance_cv_ = 0.0f;
  float vca_cv_ = 0.0f;
  float cv_coefficient_ = 0.0f;
  float vca_coefficient_ = 0.0f;

  // Filter state.
  float s1_ = 0.0f;
  float s2_ = 0.0f;

  // SVF coefficients for the CVs they were computed from. The smoothed CVs
  // settle on a fixed value, so tan() and exp2() run only while they move.
  float coefficient_cutoff_cv_ = -1.0f;
  float coefficient_resonance_cv_ = -1.0f;
  float g_ = 0.0f;
  float k_ = 0.0f;
  float denominator_ = 0.0f;

  // Surge filter path. block_ holds one oversampled block before the VCA.
  SurgeFilter surge_;
  float block_[256];
  uint8_t last_model_ = 0;

  // DC blocker on the output (the hardware is AC-coupled).
  float dc_x_ = 0.0f;
  float dc_y_ = 0.0f;
  float dc_coefficient_ = 0.0f;
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_ANALOG_H_
