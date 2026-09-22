function verify_safety(workDir, outPath) %#ok<INUSD>
% Safety-phase parity. BeamV0's checkSonicationSafety.m /
% getISPTAFromStimParams.m / getMechanicalIndex.m are GUI callbacks that
% take `app` and call Verasonics/GUI code, so they can't be invoked here.
% This script instead:
%   * evaluates the acoustic formulae copied *verbatim* from those source
%     files (line references below), diffing them against the C++ ports;
%   * calls getMaxSteeringRange / getMaxSonicationAmplitude, which are
%     directly callable (their `app` arg is unused).
% Matches the labels in parity_safety_cpp.csv. Driven by compare_parity.ps1.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'GUI', 'Safety'));

fid = fopen(outPath, 'w');
if fid < 0, error('cannot open %s', outPath); end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

% synthetic sonications (must match apps/parity_safety/main.cpp)
s1 = struct('amp', 2.5, 'cf', 0.65, 'pd', 0.02, 'pi', 0.05, 'bd', 0.3, 'bi', 1.0);
s2 = struct('amp', 1.8, 'cf', 0.70, 'pd', 0.10, 'pi', 0.08, 'bd', 0.5, 'bi', 0.4);
s3 = struct('amp', 3.0, 'cf', 0.65, 'pd', 0.04, 'pi', 0.05, 'bd', 0.4, 'bi', 0.5);

% --- ISPPA ---
% checkSonicationSafety.m:  Intensity = (t.Amplitude(i)*1e6)^2/(2*1040*1546)/(100^2)
isppa1040 = @(amp) (amp*1e6)^2/(2*1040*1546)/(100^2);
% getISPTAFromStimParams.m: Intensity = ((...Amplitude(i)*1e6)^2/(2*density*c))/(100^2), density=1046, c=1546
isppa1046 = @(amp) ((amp*1e6)^2/(2*1046*1546))/(100^2);
fprintf(fid, 'isppa_1040_s1,%.17g\n', isppa1040(s1.amp));
fprintf(fid, 'isppa_1040_s2,%.17g\n', isppa1040(s2.amp));
fprintf(fid, 'isppa_1046_s3,%.17g\n', isppa1046(s3.amp));

% --- mechanical index ---
% getMechanicalIndex.m: MI(i) = stimParams(i).Amplitude/sqrt(stimParams(i).centerFrequencyMHz)
mi = @(amp, cf) amp/sqrt(cf);
fprintf(fid, 'mi_s1,%.17g\n', mi(s1.amp, s1.cf));
fprintf(fid, 'mi_s2,%.17g\n', mi(s2.amp, s2.cf));

% --- ISPTA (burst-duration / burst-interval) ---
% getISPTAFromStimParams.m:
%   numPulseTransmits = floor(BD/PI)
%   pulseOnOverOffTimePerBurstDuration = numPulseTransmits*PD/BD
%   IsptaBurstDuration(i) = Intensity*pulseOnOverOffTimePerBurstDuration
%   IsptaBurstInterval(i) = Intensity*pulseOnOverOffTimePerBurstDuration*BD/BI
intensity3 = isppa1046(s3.amp);
numPulseTransmits = floor(s3.bd/s3.pi);
onOverOff = numPulseTransmits*s3.pd/s3.bd;
fprintf(fid, 'isptaBurstDuration_s3,%.17g\n', intensity3*onOverOff);
fprintf(fid, 'isptaBurstInterval_s3,%.17g\n', intensity3*onOverOff*s3.bd/s3.bi);

% --- limits (directly callable) ---
steer = getMaxSteeringRange([]);
for a = 0:2
    for b = 0:1
        fprintf(fid, 'maxSteer_%d%d,%.17g\n', a, b, steer(a+1, b+1));
    end
end
fprintf(fid, 'maxAmp,%.17g\n', getMaxSonicationAmplitude([]));

end
