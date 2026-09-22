function verify_registration(workDir, outPath) %#ok<INUSD>
% Runs the real BeamV0 Registration/ functions on the same synthetic point
% sets / fiducials / array as apps/parity_registration, matching the labels
% in parity_registration_cpp.csv. Driven by matlab_verify/compare_parity.ps1.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'Arrays'));
addpath(fullfile(beam, 'Util'));
addpath(fullfile(beam, 'Registration'));
addpath(fullfile(beam, 'Registration', 'MRINeuroNav', 'TranslateArrayPosition'));

fid = fopen(outPath, 'w');
if fid < 0, error('cannot open %s', outPath); end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

deg = pi / 180;
R = rotZ(25*deg) * rotY(15*deg) * rotX(-10*deg);

A = [0.10  0.20  -0.15   0.05   0.30  -0.20;
     0.05 -0.10   0.20  -0.25   0.15   0.10;
     0.12  0.08   0.30   0.18  -0.05   0.22];
tTrue = [0.010; -0.005; 0.003];
B = R * A + tTrue;

% --- affineRegistration, unweighted ---
reg = affineRegistration(A, B);
writeMat(fid, 'reg_R', reg.R);
writeVec(fid, 'reg_t', reg.t);
writeVec(fid, 'reg_q', reg.q);

% --- affineRegistration, weighted ---
regW = affineRegistration(A, B, 'weights', [0.5,1,0.5,0.5,1,0.5]);
writeMat(fid, 'regW_R', regW.R);
writeVec(fid, 'regW_t', regW.t);

% --- getAffineMatrixFromRegistration ---
m0 = getAffineMatrixFromRegistration(A, B, false);
writeMat(fid, 'affMat', m0(1:3, :));
m1 = getAffineMatrixFromRegistration(A, B, true);
writeVec(fid, 'affMatMm_t', m1(1:3, 4));

% --- fiducial-basis math ---
fnames = {'LeftY1Z3','LeftY1Z1','LeftY4Z1','RightY1Z3','RightY1Z1','RightY4Z1'};
fpos = [-0.02 0.0 0.02; -0.02 0.0 0.0; -0.02 -0.0225 0.0;
         0.02 0.0 0.02;  0.02 0.0 0.0;  0.02 -0.0225 0.0];
FiducialROIs = struct('name', {}, 'position', {});
for i = 1:6
    FiducialROIs(i).name = fnames{i};
    FiducialROIs(i).position = (R * fpos(i,:)')';
end

[M, Xv, Yv, Zv] = getTranslationMatrixFromTransducerFiducials([], FiducialROIs);
writeMat(fid, 'transBasis', M);
writeVec(fid, 'transBasis_x', Xv);
writeVec(fid, 'transBasis_y', Yv);
writeVec(fid, 'transBasis_z', Zv);
writeVec(fid, 'fidByName_RightY1Z1', getFiducialPositionFromName('RightY1Z1', FiducialROIs));

% --- applyAffineMatrixToFiducialMarkers (per-point transform from
%     applyAffineMatrixToFrameData.m) ---
aff = eye(4);
aff(1:3,1:3) = R;
aff(1:3,4) = [0.1; -0.2; 0.3];
moved = zeros(6, 3);
for i = 1:6
    xyz = [reshape(FiducialROIs(i).position, [], 1); 1];
    p = aff * xyz;
    moved(i,:) = reshape(p(1:3), [], 1)';
end
writeVec(fid, 'movedFid0', moved(1,:));
writeVec(fid, 'movedFid5', moved(6,:));

% --- setArrayFiducialMarkers ---
rect = buildRect(90);
arrayData = defineArrayData(rect);
arrayData = setArrayFiducialMarkers(arrayData);
fprintf(fid, 'arrFids_count,%d\n', numel(arrayData.fiducialMarkers));
for i = 1:numel(arrayData.fiducialMarkers)
    writeVec(fid, ['arrFid_' char(arrayData.fiducialMarkers(i).name)], ...
        arrayData.fiducialMarkers(i).position);
end

end

function R = rotX(a)
R = [1 0 0; 0 cos(a) -sin(a); 0 sin(a) cos(a)];
end
function R = rotY(a)
R = [cos(a) 0 sin(a); 0 1 0; -sin(a) 0 cos(a)];
end
function R = rotZ(a)
R = [cos(a) -sin(a) 0; sin(a) cos(a) 0; 0 0 1];
end

function rect = buildRect(n)
h = 0.0003; spacing = 0.002;
rect = zeros(19, n);
for i = 1:n
    c = [i*spacing; 0.001*(i-1); 0];
    rect(1, i) = i;
    rect(2:4, i)   = c + [-h; -h; 0];
    rect(5:7, i)   = c + [ h; -h; 0];
    rect(8:10, i)  = c + [ h;  h; 0];
    rect(11:13, i) = c + [-h;  h; 0];
    rect(17:19, i) = c;
end
end

function writeMat(fid, prefix, m)
for r = 0:size(m,1)-1
    for c = 0:size(m,2)-1
        fprintf(fid, '%s_%d%d,%.17g\n', prefix, r, c, m(r+1, c+1));
    end
end
end

function writeVec(fid, prefix, x)
x = reshape(x, [], 1);
for r = 0:numel(x)-1
    fprintf(fid, '%s_%d,%.17g\n', prefix, r, x(r+1));
end
end
