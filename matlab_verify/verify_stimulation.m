function verify_stimulation(workDir, outPath)
% Runs the real BeamV0 Stimulation/ functions on the synthetic array +
% parameters written by apps/parity_stimulation, matching the labels in
% parity_stimulation_cpp.csv. Driven by matlab_verify/compare_parity.ps1.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'Arrays'));
addpath(fullfile(beam, 'Util'));
addpath(fullfile(beam, 'Stimulation'));
addpath(fullfile(beam, 'Stimulation', 'GeneralSonication'));

rect = readmatrix(fullfile(workDir, 'parity_stim_rect.csv'));   % 19 x nEl

fid = fopen(outPath, 'w');
if fid < 0, error('cannot open %s', outPath); end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

nEl = size(rect, 2);
array = defineArrayStruct(rect, 150000, [0.06 0.06]);
freqsMHz = (0.6 + 0.001 * (0:nEl-1))';

% --- focusArrayAtPoint ---
[delaysT, delaysTRaw, mi] = focusArrayAtPoint(array, [0.06, 0.01, 0.15], 1500);
fprintf(fid, 'focus_delaysT_0,%.17g\n', delaysT(1));
fprintf(fid, 'focus_delaysT_last,%.17g\n', delaysT(nEl));
fprintf(fid, 'focus_delaysTRaw_0,%.17g\n', delaysTRaw(1));
fprintf(fid, 'focus_mi,%d\n', mi);

% --- calculateMultifrequencySuperpositionDelays ---
wd = calculateMultifrequencySuperpositionDelays(freqsMHz .* 1e6, 0);
fprintf(fid, 'mfsdelays_0,%.17g\n', wd(1));
fprintf(fid, 'mfsdelays_last,%.17g\n', wd(nEl));

% --- getApodFromAtt ---
[V, apods] = getApodFromAtt(10, [1 2 0.5 3 1.2 5]', 1.5);
fprintf(fid, 'apod_V,%.17g\n', V);
fprintf(fid, 'apod_0,%.17g\n', apods(1));
fprintf(fid, 'apod_5,%.17g\n', apods(6));

% --- defineStimFreqs ---
f650 = defineStimFreqs('650', 8);
fhigh = defineStimFreqs('high', 8);
fprintf(fid, 'stimfreqs_650_0,%.17g\n', f650(1));
fprintf(fid, 'stimfreqs_high_7,%.17g\n', fhigh(8));

% --- interp1 ---
ix = [0 1 2 3]; iy = [0 10 30 60];
fprintf(fid, 'interp1_1p5,%.17g\n', interp1(ix, iy, 1.5));
fprintf(fid, 'interp1_2p5,%.17g\n', interp1(ix, iy, 2.5));

% --- pressureToDutyCycleGivenTransmission ---
calDuty = [40 45 56 75 80 90 97.5];
calPressure = [0.75 1.12 1.72 2.95 3.1 3.72 3.92];
fprintf(fid, 'duty_0p65_0p2,%.17g\n', ...
    pressureToDutyCycleGivenTransmission(0.65, 0.2, calDuty, calPressure));

% --- getPauseIntervals ---
pi_ = getPauseIntervals(1.0, 0.3);
fprintf(fid, 'pause_count,%d\n', numel(pi_));
fprintf(fid, 'pause_last,%.17g\n', pi_(end));

% --- defineStimParams ---
txElements = 1:30;
txElementsArray = 1:30;
rxElements = [1 2 3];
positions = [10, 0, 150];
apodsFull = ones(nEl, 1);
correctionDelays = zeros(nEl, 1);
sp = defineStimParams(array, 1500, txElements, txElementsArray, rxElements, ...
    positions, freqsMHz, apodsFull, correctionDelays);
fprintf(fid, 'stimparams_count,%d\n', numel(sp));
fprintf(fid, 'stimparams_centerFreqMHz,%.17g\n', sp(1).centerFrequencyMHz);
fprintf(fid, 'stimparams_delaysCycle_0,%.17g\n', sp(1).delaysCycle(1));
fprintf(fid, 'stimparams_delaysCycle_last,%.17g\n', sp(1).delaysCycle(30));
fprintf(fid, 'stimparams_delaysSeconds_0,%.17g\n', sp(1).delaysSeconds(1));
fprintf(fid, 'stimparams_delaysSteering_0,%.17g\n', sp(1).delaysSteering(1));

% --- getTxAndBurstEvents ---
stimParams = struct('startTime', 0.5, 'BI', 1.0, 'BD', 0.4, 'PI', 0.1, 'PD', 0.05);
[burstEvents, txEvents] = getTxAndBurstEvents(stimParams, 2, {[0.1 0.2]}, 3, 2);
fprintf(fid, 'events_burst_count,%d\n', numel(burstEvents));
fprintf(fid, 'events_tx_count,%d\n', numel(txEvents));
fprintf(fid, 'events_burst1_timeOn,%.17g\n', burstEvents(2).timeOn);
fprintf(fid, 'events_burst1_timeOff,%.17g\n', burstEvents(2).timeOff);
fprintf(fid, 'events_txlast_timeOn,%.17g\n', txEvents(end).timeOn);
fprintf(fid, 'events_txlast_timeOff,%.17g\n', txEvents(end).timeOff);

end
