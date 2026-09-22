#pragma once

namespace beam::safety {

// The acoustic timing/amplitude fields the Safety functions read out of
// each sonication (from setStimParamsFromApp / the stim-param table). All
// times in seconds, amplitude in MPa, frequency in MHz.
struct SonicationSafetyParams {
    double amplitudeMPa = 0.0;
    double centerFrequencyMHz = 0.0;
    double pd = 0.0;  // pulse duration
    double pi = 0.0;  // pulse interval
    double bd = 0.0;  // burst duration
    double bi = 0.0;  // burst interval
    double startTime = 0.0;
    double endTime = 0.0;
};

// Spatial-peak pulse-average intensity (W/cm^2) from pressure amplitude:
//   I = (amp*1e6)^2 / (2 * rho * c) / 100^2
// checkSonicationSafety.m uses rho = 1040; getISPTAFromStimParams.m uses
// rho = 1046 -- a genuine inconsistency in the source. `density` is a
// parameter here so both call sites can be reproduced exactly; the
// convenience overload uses checkSonicationSafety.m's 1040.
double isppa(double amplitudeMPa, double density);
double isppa(double amplitudeMPa);  // density = 1040, c = 1546

// Port of getMechanicalIndex.m: MI = amplitude / sqrt(centerFrequencyMHz).
double mechanicalIndex(double amplitudeMPa, double centerFrequencyMHz);

// Port of the fully-computable part of getISPTAFromStimParams.m.
// IsptaAll additionally needs setBurstEventsFromStimParams (Verasonics,
// deferred), so it is not returned here; these two are:
//   numPulseTransmits = floor(BD / PI)
//   onOverOff         = numPulseTransmits * PD / BD
//   IsptaBurstDuration = I * onOverOff
//   IsptaBurstInterval = I * onOverOff * BD / BI
// (I from getISPTAFromStimParams.m's rho = 1046.)
struct IsptaResult {
    double isptaBurstDuration = 0.0;
    double isptaBurstInterval = 0.0;
};
IsptaResult isptaFromParams(const SonicationSafetyParams& p);

}  // namespace beam::safety
