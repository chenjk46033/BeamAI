function verify_arrays(workDir, outPath)
% Runs the real BeamV0 Arrays/ and Util/ functions on the synthetic rect
% written by apps/parity_arrays (<workDir>/parity_rect.csv), and writes a
% "label,value" CSV matching parity_arrays_cpp.csv. Driven by
% matlab_verify/compare_parity.ps1; see matlab_verify/README.md.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'Arrays'));
addpath(fullfile(beam, 'Util'));

rect = readmatrix(fullfile(workDir, 'parity_rect.csv'));
assert(isequal(size(rect), [19, 100]), 'expected 19x100 rect, got %dx%d', size(rect,1), size(rect,2));

fid = fopen(outPath, 'w');
if fid < 0
    error('could not open %s', outPath);
end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

% --- affine matrices ---
zr = zRotAffineMatrix(pi/6);
fprintf(fid, 'zrot_00,%.17g\n', zr(1,1));
fprintf(fid, 'zrot_10,%.17g\n', zr(2,1));
tr = defineTranslateAffineMatrix([0.001;0.002;0.003]);
fprintf(fid, 'translate_03,%.17g\n', tr(1,4));
fprintf(fid, 'translate_13,%.17g\n', tr(2,4));

moved = applyAffineToRect(zr, rect);
writeVec(fid, 'applyaffine_elem1_center', moved(17:19,1));
writeVec(fid, 'applyaffine_elem50_corner1', moved(2:4,50));

% --- defineArrayStruct ---
array = defineArrayStruct(rect, 150000, [0.06, 0.06]);
for idx = [1, 50, 100]
    p = sprintf('elem%d', idx);
    writeVec(fid, [p '_pos'], array.element(idx).position);
    writeVec(fid, [p '_normal'], array.element(idx).normalVector);
    fprintf(fid, '%s_opposing,%d\n', p, array.element(idx).opposingElement);
    fprintf(fid, '%s_receive_count,%d\n', p, numel(array.element(idx).receiveElements));
end

positions = getElementPositionsFromArrayStruct(array);
writeVec(fid, 'positions_row1', positions(1,:)');
writeVec(fid, 'positions_rowN', positions(100,:)');

writeVec(fid, 'normal_3points', normalVectorFrom3Points([0.01;0.02;0.03], [0.05;0.01;0.02], [0.02;0.06;0.01]));
writeVec(fid, 'calc_rect_normal', calculateRectNormalVector(rect, 1));

corners = reshape(rect(2:13,1), 3, 4);
samples = spatiallySampleElement(corners, 0.0001);
fprintf(fid, 'spatial_sample_count,%d\n', size(samples,1));
if size(samples,1) > 0
    writeVec(fid, 'spatial_sample_first', samples(1,:)');
end

[oppEl, recvEls] = getReceiveElements(rect, 1, 1490/150000, 0.06);
fprintf(fid, 'getreceive_opposing,%d\n', oppEl);
fprintf(fid, 'getreceive_count,%d\n', numel(recvEls));

% --- defineArrayData / getOpposingElements ---
arrayData = defineArrayData(rect);
opp1 = getOpposingElements(arrayData, 1);
fprintf(fid, 'opposing1_count,%d\n', numel(opp1));
fprintf(fid, 'opposing1_first,%d\n', opp1(1));
fprintf(fid, 'opposing1_last,%d\n', opp1(end));

% --- defineArrayTxElements / arrayElementsToVSXElements ---
[txElements, ~] = defineArrayTxElements('firstThenSecond');
fprintf(fid, 'tx_rows,%d\n', size(txElements,1));
fprintf(fid, 'tx_row0_first,%d\n', txElements(1,1));
fprintf(fid, 'tx_row0_last,%d\n', txElements(1,end));
fprintf(fid, 'tx_row1_first,%d\n', txElements(2,1));
fprintf(fid, 'tx_row1_last,%d\n', txElements(2,end));
vsx = arrayElementsToVSXElements([1, 126, 127]);
fprintf(fid, 'vsx_0,%d\n', vsx(1));
fprintf(fid, 'vsx_1,%d\n', vsx(2));
fprintf(fid, 'vsx_2,%d\n', vsx(3));

% --- util ---
writeVec(fid, 'add_vectors', addVectors([1;2;3], [10;20;30]));
fprintf(fid, 'angle_between,%.17g\n', angleBetweenTwoVectors([1;2;2], [2;3;6]));
fprintf(fid, 'vector_range,%.17g\n', vectorRange([1;5;-3;2;8]));
fprintf(fid, 'distance_point_to_line,%.17g\n', distancePointToLine([1;0;0], [0;0;0], [0.5;0.3;0.4]));
cyc = convertDelaysToCycles([1e-6, 2e-6, 3e-6], 650000);
fprintf(fid, 'cycles_0,%.17g\n', cyc(1));
fprintf(fid, 'cycles_2,%.17g\n', cyc(3));

% --- util/camera ---
rmat = [0.936293, -0.289629, 0.198669; 0.312992, 0.944703, -0.0978434; -0.159345, 0.153792, 0.975170];
pts = [0.1, 0.2, 0.3; -0.05, 0.15, 0.4];
local = cam2targetSpace(pts, rmat, [0.01; 0.02; 0.03]);
writeVec(fid, 'cam2target_row0', local(1,:)');
writeVec(fid, 'cam2target_row1', local(2,:)');

px = mm2pixel([12.5, 33.0], 0.5, [1.0, 2.0]);
fprintf(fid, 'mm2pixel_0,%.17g\n', px(1));
fprintf(fid, 'mm2pixel_1,%.17g\n', px(2));

K = [800, 0, 320; 0, 800, 240; 0, 0, 1];
pix = [320, 240; 400, 300];
hit = mm3Dpnp(rmat, [0; 0; 0.5], pix, K, 0);
writeVec(fid, 'mm3dpnp_row0', hit(1,:)');
writeVec(fid, 'mm3dpnp_row1', hit(2,:)');

writeVec(fid, 'rotm2eul', rotm2eul_simple(rmat));

end

function writeVec(fid, label, v)
    fprintf(fid, '%s_x,%.17g\n', label, v(1));
    fprintf(fid, '%s_y,%.17g\n', label, v(2));
    fprintf(fid, '%s_z,%.17g\n', label, v(3));
end
