function verify_correction(workDir, outPath)
% Runs the real BeamV0 Correction/ functions on the synthetic waveforms +
% rect written by apps/parity_correction, matching the labels in
% parity_correction_cpp.csv. Driven by matlab_verify/compare_parity.ps1.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'Arrays'));
addpath(fullfile(beam, 'Util'));
addpath(fullfile(beam, 'Correction'));
addpath(fullfile(beam, 'Correction', 'localizeArrays'));
addpath(fullfile(thisDir, '..', '..', 'BeamV0', 'BEAMANALYSIS', 'analysisUtil'));

wv = readmatrix(fullfile(workDir, 'parity_corr_wv.csv'));   % nEl x nSamp
rect = readmatrix(fullfile(workDir, 'parity_corr_rect.csv'));

fid = fopen(outPath, 'w');
if fid < 0, error('cannot open %s', outPath); end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

s1 = wv(1, :)';
s2 = wv(4, :)';

% --- xcorr ---
[xc, lags] = xcorr(s1, s2);
fprintf(fid, 'xcorr_len,%d\n', numel(xc));
fprintf(fid, 'xcorr_mid,%.17g\n', xc(lags == 0));
[~, mi] = max(xc);
fprintf(fid, 'xcorr_argmax_lag,%d\n', lags(mi));

cc = corrcoef(s1, s2);
fprintf(fid, 'corrcoef_s1s2,%.17g\n', cc(1, 2));
withNan = [1; 2; NaN; 4];
fprintf(fid, 'nanmean,%.17g\n', mean(withNan(~isnan(withNan))));

% --- xcorrS1ToS2 / corrSpeedUp ---
[d12, ccx] = xcorrS1ToS2(s1, s2, [-30, 30]);
fprintf(fid, 'xcorrS1ToS2_d12,%d\n', d12);
fprintf(fid, 'xcorrS1ToS2_cc,%.17g\n', ccx);
[speedupsa, ccsu] = corrSpeedUp(s1, s2);
fprintf(fid, 'corrSpeedUp_speedupsa,%d\n', speedupsa);
fprintf(fid, 'corrSpeedUp_cc,%.17g\n', ccsu);

% --- computeMaxCorrelationDelays ---
[delaysI, maxChannel, ccd] = computeMaxCorrelationDelays(wv);
fprintf(fid, 'compMaxDelays_maxChannel,%d\n', maxChannel);
fprintf(fid, 'compMaxDelays_delay_e5,%d\n', delaysI(6));
fprintf(fid, 'compMaxDelays_delay_e20,%d\n', delaysI(21));
fprintf(fid, 'compMaxDelays_cc_e5,%.17g\n', ccd(6));

% --- hilbert envelope / filterTransmitSignal ---
env = abs(hilbert(s1));
fprintf(fid, 'envelope_700,%.17g\n', env(701));
fprintf(fid, 'envelope_650,%.17g\n', env(651));
[filtWave, startSample] = filterTransmitSignal(s1);
fprintf(fid, 'filterTransmit_startSample,%d\n', startSample);
fprintf(fid, 'filterTransmit_wv_10,%.17g\n', filtWave(11));
fprintf(fid, 'filterTransmit_wv_700,%.17g\n', filtWave(701));

% --- shiftAndSumWaveforms ---
smallWv = [1, 2, 3, 4; 10, 20, 30, 40];
[waveformSum, ~] = shiftAndSumWaveforms(smallWv, [1, -1], [2, 0.5]);
for k = 0:3
    fprintf(fid, 'shiftSum_%d,%.17g\n', k, waveformSum(k + 1));
end

% --- setAdjustedAttValues / calculateAttenuation ---
adj = setAdjustedAttValues([2, 5, 0.5, Inf], 10, 1);
for k = 0:3
    if isinf(adj(k + 1))
        fprintf(fid, 'adjAtt_%d,%.17g\n', k, 1e300);
    else
        fprintf(fid, 'adjAtt_%d,%.17g\n', k, adj(k + 1));
    end
end
fprintf(fid, 'calcAtten,%.17g\n', calculateAttenuation(s1, s2));

% --- findPeakNegativeVoltage ---
fprintf(fid, 'fpnv_lowN,%.17g\n', findPeakNegativeVoltage(s1, 1));
fprintf(fid, 'fpnv_10,%.17g\n', findPeakNegativeVoltage(s1, 10));

% --- defineTxRxScanParams ---
scanParams = defineTxRxScanParams();
fprintf(fid, 'scan_desiredDepth,%.17g\n', scanParams.desiredDepth);
fprintf(fid, 'scan_sampleRateHz,%.17g\n', scanParams.sampleRateHz);
fprintf(fid, 'scan_voltageAmplitude,%.17g\n', scanParams.voltageAmplitude);
fprintf(fid, 'scan_mapping_count,%d\n', numel(scanParams.arrayToVSXMapping));

% --- getReceiveElementsUnderAngle ---
arrayData = defineArrayData(rect);
fprintf(fid, 'recvAngle_norm90,%d\n', numel(getReceiveElementsUnderAngle(arrayData, 1, 'norm', 90.001)));
fprintf(fid, 'recvAngle_explicit,%d\n', numel(getReceiveElementsUnderAngle(arrayData, 1, [0, 0, 500], 90.001)));

% --- getRecieveWaveformFromSerial.m: butter/filter ---
fsB = 1316800;
[B, A] = butter(2, [200000 400000] / (fsB / 2));
for k = 0:4
    fprintf(fid, 'butter_b_%d,%.17g\n', k, B(k + 1));
end
for k = 0:4
    fprintf(fid, 'butter_a_%d,%.17g\n', k, A(k + 1));
end
filtered = filter(B, A, s1);
fprintf(fid, 'filter_s1_0,%.17g\n', filtered(1));
fprintf(fid, 'filter_s1_500,%.17g\n', filtered(501));
fprintf(fid, 'filter_s1_1199,%.17g\n', filtered(1200));

% localize/distance and nonlinRelativeDistanceFun are covered by unit tests
% (Localize.*) and are trivial arithmetic -- not repeated here (the MATLAB
% nonlinRelativeDistanceFun signature needs a full `array` struct that's
% awkward to fake).

end
