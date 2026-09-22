#pragma once

#include <Eigen/Core>

namespace beam::correction {

// abs(hilbert(x)) -- the analytic-signal envelope, computed the same way
// MATLAB's hilbert() does (FFT, zero the negative frequencies, double the
// positive ones, inverse FFT). Uses Eigen's bundled kiss-fft backend.
Eigen::VectorXd analyticEnvelope(const Eigen::VectorXd& x);

// Port of BeamV0/GUIMatlab/BEAM/Correction/filterTransmitSignal.m.
// Finds the transmit burst (first run of >=5 consecutive envelope samples
// above threshold 5), blanks +/- 175 samples around its start, and returns
// the blanked waveform plus startSample (1-based, already advanced by
// +175 as the source does). If the blank window would run off either end
// the waveform is left unblanked (the source's bare try/catch).
struct FilterTransmitResult {
    Eigen::VectorXd filtWave;
    int startSample = 0;  // 1-based, matching the MATLAB return
};
FilterTransmitResult filterTransmitSignal(const Eigen::VectorXd& wv);

// Port of BeamV0/GUIMatlab/BEAM/Correction/getTxRxSignalAmplitude.m.
// Crops the signal to x(20:end-200) (1-based), then:
//   peak2peak = median of the abs-signal peaks taller than
//               (|min(y)| + max(y)) / 4   (MATLAB's mean([...])/2 with a
//               MinPeakHeight findpeaks) -- NaN if there are no such peaks
//               (MATLAB median([]))
//   rms       = sqrt(mean(y.^2))
//   envelope  = max(abs(hilbert(y)))
// Throws std::invalid_argument if the signal is <= 219 samples (the crop
// would be empty; MATLAB would error on the index range).
struct TxRxSignalAmplitude {
    double peak2peak = 0.0;
    double rms = 0.0;
    double envelope = 0.0;
};
TxRxSignalAmplitude txRxSignalAmplitude(const Eigen::VectorXd& x);

// Port of the computation in
// BeamV0/GUIMatlab/BEAM/Correction/getTransmissionAfterThroughTransmit.m,
// with the two received waveforms passed in directly instead of read off
// `app.sys.RTT(1)`:
//   xk = filterTransmitSignal(chk); ak = txRxSignalAmplitude(xk);
//   amp = mean([sqrt(a0.envelope/1200), sqrt(a1.envelope/1200)])
//   rms = mean([sqrt(a0.rms/90),        sqrt(a1.rms/90)])
struct ThroughTransmitAmplitude {
    double amp = 0.0;
    double rms = 0.0;
};
ThroughTransmitAmplitude throughTransmitAmplitude(const Eigen::VectorXd& ch0rcv,
                                                   const Eigen::VectorXd& ch1rcv);

}  // namespace beam::correction
