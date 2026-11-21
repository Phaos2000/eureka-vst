#include "resampler.h"

#include <algorithm>
#include <cmath>

namespace ambika {
namespace plugin {

namespace {

const double kPi = 3.14159265358979323846;
const double kKaiserBeta = 9.0;

double BesselI0(double x) {
  double sum = 1.0;
  double term = 1.0;
  const double half = x * 0.5;
  for (int k = 1; k < 64; ++k) {
    term *= (half / k) * (half / k);
    sum += term;
    if (term < sum * 1e-12) {
      break;
    }
  }
  return sum;
}

}  // namespace

void Resampler::Init(double input_rate, double output_rate) {
  step_ = input_rate / output_rate;
  // 0.9 of the lower Nyquist frequency leaves room for the transition band.
  cutoff_ = std::min(1.0, output_rate / input_rate) * 0.9;
  half_width_ = static_cast<int>(std::ceil(kZeroCrossings / cutoff_));

  // Kernel table over t = 0 .. half_width_ input samples (symmetric).
  const int size = half_width_ * kTableResolution + 2;
  table_.assign(size, 0.0f);
  const double i0_beta = BesselI0(kKaiserBeta);
  for (int i = 0; i < size; ++i) {
    const double t = static_cast<double>(i) / kTableResolution;
    if (t >= half_width_) {
      table_[i] = 0.0f;
      continue;
    }
    const double x = cutoff_ * t;
    const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
    const double w = t / half_width_;
    const double window = BesselI0(kKaiserBeta * std::sqrt(1.0 - w * w)) / i0_beta;
    table_[i] = static_cast<float>(cutoff_ * sinc * window);
  }
  buffer_.assign(kBufferSize, 0.0f);
  Reset();
}

void Resampler::Reset() {
  std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  // Start as if 2 * half_width_ zeros had already been written, so the first
  // output is centred half_width_ samples in the past.
  written_ = 2 * half_width_;
  position_ = static_cast<double>(half_width_);
}

void Resampler::Push(const float* samples, int count) {
  for (int i = 0; i < count; ++i) {
    buffer_[static_cast<size_t>(written_ & (kBufferSize - 1))] = samples[i];
    ++written_;
  }
}

bool Resampler::CanPop() const {
  const int64_t base = static_cast<int64_t>(std::floor(position_));
  return base + half_width_ < written_;
}

float Resampler::Kernel(double t) const {
  t = std::fabs(t);
  const double index = t * kTableResolution;
  const int i = static_cast<int>(index);
  if (i + 1 >= static_cast<int>(table_.size())) {
    return 0.0f;
  }
  const float frac = static_cast<float>(index - i);
  return table_[i] + frac * (table_[i + 1] - table_[i]);
}

float Resampler::Pop() {
  const int64_t base = static_cast<int64_t>(std::floor(position_));
  const double frac = position_ - static_cast<double>(base);
  float sum = 0.0f;
  for (int k = -half_width_ + 1; k <= half_width_; ++k) {
    const int64_t index = base + k;
    const float x = buffer_[static_cast<size_t>(index & (kBufferSize - 1))];
    sum += x * Kernel(static_cast<double>(k) - frac);
  }
  Advance();
  return sum;
}

void Resampler::PopStereo(Resampler& other, float* out, float* other_out) {
  const int64_t base = static_cast<int64_t>(std::floor(position_));
  const double frac = position_ - static_cast<double>(base);
  float sum = 0.0f;
  float other_sum = 0.0f;
  for (int k = -half_width_ + 1; k <= half_width_; ++k) {
    const size_t index = static_cast<size_t>((base + k) & (kBufferSize - 1));
    const float weight = Kernel(static_cast<double>(k) - frac);
    sum += buffer_[index] * weight;
    other_sum += other.buffer_[index] * weight;
  }
  Advance();
  other.Advance();
  *out = sum;
  *other_out = other_sum;
}

void Resampler::Advance() {
  position_ += step_;
  // Keep the numbers small: rebase both counters together, by a multiple of
  // the ring size so that buffer indices are unchanged.
  if (position_ > 1e9) {
    const int64_t shift = (static_cast<int64_t>(position_) - 4 * half_width_) &
        ~static_cast<int64_t>(kBufferSize - 1);
    position_ -= static_cast<double>(shift);
    written_ -= shift;
  }
}

}  // namespace plugin
}  // namespace ambika
