function verify_registration(workDir, outPath)
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

% --- end-to-end registration: the two buttons an operator presses ---
% Same six measured fiducials (mm) and same array as apps/parity_registration,
% taken from a real BeamAI session on BEAM MRIs/F040/T1_MRI. This reproduces
% registerArrayToFiducials.m and the MRI-Based branch of
% registerCurrentTransducerPostion.m inline, because both take the live `app`
% object; every line below is the source's own, with app.* reads replaced by
% the locals they would have returned.
% Read from the same checked-in fixture apps/parity_registration reads, so the
% comparison has one source of truth for its input rather than two sets of
% literals that can drift apart. Ordered by NAME against the array's own
% marker list, not by row position.
fixturePath = fullfile(thisDir, '..', 'testdata', 'beamai_fiducials_F040_T1_MRI.csv');
nominalArray = setArrayFiducialMarkers(defineArrayData(buildRect(90)));
mriFiducials = readFiducialFixture(fixturePath, {nominalArray.fiducialMarkers.name});

originArrayData = defineArrayData(buildRect(90));
originArrayData = setArrayFiducialMarkers(originArrayData);

% registerArrayToFiducials.m
N = numel(originArrayData.fiducialMarkers);
arrayFiducials = zeros(3, N);
for i = 1:N
    arrayFiducials(:,i) = reshape(originArrayData.fiducialMarkers(i).position, [], 1) * 1000;
end
affineMatrix = getAffineMatrixFromRegistration(arrayFiducials, mriFiducials, 1);
fittedArrayData = applyAffineToArrayData(affineMatrix, originArrayData);
writeVec(fid, 'e2e_centreAfterFitMm', ...
    mean(fittedArrayData.arrayTotal.rect(17:19,:), 2)' * 1000);
for i = 1:numel(fittedArrayData.fiducialMarkers)
    writeVec(fid, ['e2e_fittedFid_' char(fittedArrayData.fiducialMarkers(i).name)], ...
        fittedArrayData.fiducialMarkers(i).position);
end

% registerCurrentTransducerPostion.m, MRIFiducialsButton branch.
% Its basis comes from app.FiducialROIs -- the measured points -- so build
% that list here from the same millimetre values.
measuredROIs = struct('name', {}, 'position', {});
for i = 1:N
    measuredROIs(i).name = originArrayData.fiducialMarkers(i).name;
    measuredROIs(i).position = mriFiducials(:,i)';
end
[~, Xvector, Yvector, Zvector] = getTranslationMatrixFromTransducerFiducials([], measuredROIs);
if Zvector(3) < 0
    Zvector = -1 * Zvector;
end
Mbasis = [reshape(Xvector,[],1)'; reshape(Yvector,[],1)'; reshape(Zvector,[],1)']';

verticalDelta = 10;
horizontalDelta = 7.5;
dH = 3 - 1;   % slider readings, matching apps/parity_registration
dV = 3 - 1;
dY = -dH * horizontalDelta / 1000;
dZ =  dV * verticalDelta / 1000;
regdXYZ = Mbasis * reshape([0, dY, dZ], [], 1);
translationMat = eye(4);
translationMat(1:3,4) = reshape(regdXYZ, [], 1);
lockedArrayData = applyAffineToArrayData(translationMat, fittedArrayData);
writeVec(fid, 'e2e_centreAfterLockMm', ...
    mean(lockedArrayData.arrayTotal.rect(17:19,:), 2)' * 1000);

% --- end-to-end on the REAL transducer geometry ----------------------
% Same 160-element array apps/parity_registration loads, run through
% BeamV0's own registerArrayToFiducials.m / registerCurrentTransducerPostion.m.
geomPath = fullfile(thisDir, '..', '..', 'DefaultSubjectV0', 'defaultSubjectArrayRect.csv');
realRect = readmatrix(geomPath);
realArray = defineArrayData(realRect);
% defineArrayData.m sets BOTH halves to the whole array; setArrayFiducialMarkers.m
% centres each marker on array(designation).rect, so the halves must be real or the
% six nominal fiducials come out ~90 mm off in LR. BeamV0's sys .mat ships the
% halves; rebuild them here from the midline (verified identical to the .mat).
midlineX = mean(realRect(17,:));
isRight = realRect(17,:) >= midlineX;
halfR = defineArrayStruct(realRect(:, isRight), realArray.arrayTotal.frequency, realArray.arrayTotal.elementDimensions);
halfL = defineArrayStruct(realRect(:,~isRight), realArray.arrayTotal.frequency, realArray.arrayTotal.elementDimensions);
halfR.elementMapping = 1; halfL.elementMapping = 2;
realArray = struct('arrayTotal', realArray.arrayTotal, 'array', [halfR, halfL]);
realArray = setArrayFiducialMarkers(realArray);
nElements = size(realArray.arrayTotal.rect, 2);
fprintf(fid, 'real_nElements,%d\n', nElements);

% registerArrayToFiducials.m
Nr = numel(realArray.fiducialMarkers);
realArrayFiducials = zeros(3, Nr);
for i = 1:Nr
    realArrayFiducials(:,i) = reshape(realArray.fiducialMarkers(i).position, [], 1) * 1000;
end
realAffine = getAffineMatrixFromRegistration(realArrayFiducials, mriFiducials, 1);
realFit = applyAffineToArrayData(realAffine, realArray);
writeVec(fid, 'real_centreAfterFitMm', mean(realFit.arrayTotal.rect(17:19,:), 2)' * 1000);
for i = 1:numel(realFit.fiducialMarkers)
    writeVec(fid, ['real_fittedFid_' char(realFit.fiducialMarkers(i).name)], ...
        realFit.fiducialMarkers(i).position);
end

% registerCurrentTransducerPostion.m, MRIFiducialsButton branch
realMeasured = struct('name', {}, 'position', {});
for i = 1:Nr
    realMeasured(i).name = realArray.fiducialMarkers(i).name;
    realMeasured(i).position = mriFiducials(:,i)';
end
[~, rXv, rYv, rZv] = getTranslationMatrixFromTransducerFiducials([], realMeasured);
if rZv(3) < 0
    rZv = -1 * rZv;
end
rM = [reshape(rXv,[],1)'; reshape(rYv,[],1)'; reshape(rZv,[],1)']';
rRegdXYZ = rM * reshape([0, dY, dZ], [], 1);
rTranslation = eye(4);
rTranslation(1:3,4) = reshape(rRegdXYZ, [], 1);
realLock = applyAffineToArrayData(rTranslation, realFit);
writeVec(fid, 'real_centreAfterLockMm', mean(realLock.arrayTotal.rect(17:19,:), 2)' * 1000);

for c = 0:nElements-1
    for k = 0:2
        fprintf(fid, 'real_fitElem_%d_%d,%.17g\n',  c, k, realFit.arrayTotal.rect(17+k,  c+1));
        fprintf(fid, 'real_lockElem_%d_%d,%.17g\n', c, k, realLock.arrayTotal.rect(17+k, c+1));
    end
end

writeRectCsv(fullfile(workDir, 'beamv0_rect_fit.csv'),  realFit.arrayTotal.rect);
writeRectCsv(fullfile(workDir, 'beamv0_rect_lock.csv'), realLock.arrayTotal.rect);

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
function writeRectCsv(path, m)
f = fopen(path, 'w');
if f < 0, error('cannot open %s', path); end
c = onCleanup(@() fclose(f)); %#ok<NASGU>
fmt = [repmat('%.17g,', 1, size(m,2)-1) '%.17g\n'];
fprintf(f, fmt, m');
end

function fiducials = readFiducialFixture(path, orderNames)
% Reads testdata/beamai_fiducials_F040_T1_MRI.csv into a 3xN matrix (mm),
% ordered to match orderNames by name. Errors rather than falling back: a
% parity check that silently registers from a partial input is worse than one
% that stops.
if ~isfile(path)
    error('verify_registration:missingFixture', 'cannot open fiducial fixture: %s', path);
end
names = strings(0,1);
coords = zeros(0,3);
f = fopen(path, 'r');
c = onCleanup(@() fclose(f)); %#ok<NASGU>
while true
    line = fgetl(f);
    if ~ischar(line), break; end
    line = strtrim(line);
    if isempty(line) || startsWith(line, '#') || startsWith(line, 'name,')
        continue;
    end
    parts = strsplit(line, ',');
    if numel(parts) ~= 4
        error('verify_registration:badRow', 'expected name,x,y,z in %s: %s', path, line);
    end
    names(end+1,1) = string(strtrim(parts{1})); %#ok<AGROW>
    coords(end+1,:) = [str2double(parts{2}), str2double(parts{3}), str2double(parts{4})]; %#ok<AGROW>
end
fiducials = zeros(3, numel(orderNames));
for i = 1:numel(orderNames)
    match = find(names == string(orderNames{i}), 1);
    if isempty(match)
        error('verify_registration:noMatch', '%s has no row for fiducial %s', path, orderNames{i});
    end
    fiducials(:,i) = coords(match,:)';
end
end
