function render_registration_overlay(workDir, mriPath, outDir)
% render_registration_overlay - draw BeamV0's and BeamAI's registered array
% on the same MRI, from the rect CSVs the parity run writes.
%
% Both pipelines start from DefaultSubjectV0/defaultSubjectArrayRect.csv and
% the same six measured fiducials, so if the ports agree the two overlays
% coincide exactly: every red BeamAI cross sits inside a green BeamV0 circle.
%
% mriPath defaults to the checked-in fixture
% testdata/beamai_F040_T1_MRI.nii, so the figures reproduce from a clean
% checkout with no access to the original series. Pass the original DICOM
% folder instead to render from that directly -- both give the same geometry
% (the fixture was written from it, and its axes round-trip to 8e-6 mm, the
% float32 floor of the NIfTI-1 header format).
%
%   render_registration_overlay('..\build-ai\parity_registration')
%   render_registration_overlay(workDir, 'C:\...\BEAM MRIs\F040\T1_MRI', outDir)

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(genpath(fullfile(beam, 'MRI')));
addpath(genpath(fullfile(beam, '..', 'nifti_utils')));
addpath(fullfile(beam, 'Util'));

% Default to the ORIGINAL DICOM series, not testdata/beamai_F040_T1_MRI.nii.
% That fixture is oriented for BeamAI's reader, which pairs the header's axes
% with its own volume reordering; BeamV0's loadMRIRAS reports the opposite
% direction on all three axes for the same file, so rendering the fixture here
% would mirror the backdrop underneath correctly-placed array markers. See
% docs/known_gaps_mri.md -- the disagreement is real and unresolved, so this
% tool errors rather than guessing.
if nargin < 2 || isempty(mriPath)
    mriPath = ['C:\Users\jkche\dev\spire.us\BeamExampleDataDICOMandMatFiles\' ...
               'BEAM MRIs\F040\T1_MRI\DICOM\26060824\27380000'];
end
if ~isfolder(mriPath) && ~isfile(mriPath)
    error('render_registration_overlay:noMri', ...
          ['MRI not found: %s\nPass the F040 DICOM series folder explicitly. The ' ...
           'checked-in testdata/*.nii is oriented for BeamAI''s reader and would ' ...
           'render mirrored here (docs/known_gaps_mri.md).'], mriPath);
end
if nargin < 3 || isempty(outDir), outDir = workDir; end

% The six measured fiducials, read from the same checked-in fixture the parity
% run registers from. Drawn only as reference markers here, so row order does
% not matter -- but it is the same file, so the diamonds cannot disagree with
% the arrays they are plotted beside.
fixturePath = fullfile(thisDir, '..', 'testdata', 'beamai_fiducials_F040_T1_MRI.csv');
if ~isfile(fixturePath)
    error('render_registration_overlay:missingFixture', 'cannot open %s', fixturePath);
end
fixture = readcell(fixturePath, 'NumHeaderLines', 0, 'Delimiter', ',', 'CommentStyle', '#');
isDataRow = cellfun(@(v) ischar(v) || isstring(v), fixture(:,1)) & ...
            ~strcmp(string(fixture(:,1)), "name");
mriFiducials = cell2mat(fixture(isDataRow, 2:4));

fprintf('loading MRI: %s\n', mriPath);
[img, ~, dimLR, dimAP, dimIS] = loadMRIRAS(mriPath);
close all force;
fprintf('volume %dx%dx%d  LR[%.1f %.1f] AP[%.1f %.1f] IS[%.1f %.1f] mm\n', ...
    size(img,1), size(img,2), size(img,3), ...
    min(dimLR), max(dimLR), min(dimAP), max(dimAP), min(dimIS), max(dimIS));

stages = {'fit', 'lock'};
titles = {'Step 2 - Register array to MRI fiducials', ...
          'Step 3 - Register array to current lock position (sliders 3,3)'};

for s = 1:2
    v0 = readRect(fullfile(workDir, ['beamv0_rect_' stages{s} '.csv']));
    ai = readRect(fullfile(workDir, ['beamai_rect_' stages{s} '.csv']));
    cV0 = v0(17:19,:)' * 1000;   % element centres, mm, Nx3
    cAI = ai(17:19,:)' * 1000;

    d = max(abs(cV0(:) - cAI(:)));
    fprintf('%s: %d elements, max |BeamV0 - BeamAI| = %.3e mm\n', stages{s}, size(cV0,1), d);

    centre = mean(cV0, 1);
    fprintf('%s: array centre  BeamV0 [%.6f %.6f %.6f]  BeamAI [%.6f %.6f %.6f] mm\n', ...
        stages{s}, centre, mean(cAI,1));

    f = figure('Position', [50 50 1500 1100], 'Color', 'w', 'Visible', 'off');
    tl = tiledlayout(f, 2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');

    % --- axial: x=LR, y=AP, slice at the array centre's IS ---
    k = nearestIdx(dimIS, centre(3));
    nexttile; drawSlice(squeeze(img(:,:,k))', dimLR, dimAP);
    plotPair(cV0(:,1), cV0(:,2), cAI(:,1), cAI(:,2), mriFiducials(:,1), mriFiducials(:,2));
    title(sprintf('Axial  IS = %.1f mm', dimIS(k))); xlabel('LR (mm)'); ylabel('AP (mm)');

    % --- coronal: x=LR, y=IS, slice at the array centre's AP ---
    j = nearestIdx(dimAP, centre(2));
    nexttile; drawSlice(squeeze(img(:,j,:))', dimLR, dimIS);
    plotPair(cV0(:,1), cV0(:,3), cAI(:,1), cAI(:,3), mriFiducials(:,1), mriFiducials(:,3));
    title(sprintf('Coronal  AP = %.1f mm', dimAP(j))); xlabel('LR (mm)'); ylabel('IS (mm)');

    % --- sagittal: one panel only. The whole array's LR centre falls between
    % the two panels (mid-head), so slicing there would draw the elements over
    % brain tissue they sit nowhere near. Slice through the subject-left panel
    % and plot only that panel's elements and fiducials.
    midLR = mean(cV0(:,1));
    panel = cV0(:,1) < midLR;
    fidPanel = mriFiducials(:,1) < midLR;
    i = nearestIdx(dimLR, mean(cV0(panel,1)));
    nexttile; drawSlice(squeeze(img(i,:,:))', dimAP, dimIS);
    plotPair(cV0(panel,2), cV0(panel,3), cAI(panel,2), cAI(panel,3), ...
             mriFiducials(fidPanel,2), mriFiducials(fidPanel,3));
    title(sprintf('Sagittal  LR = %.1f mm  (subject-left panel)', dimLR(i)));
    xlabel('AP (mm)'); ylabel('IS (mm)');

    % --- 3D ---
    nexttile; hold on; grid on; box on;
    plot3(cV0(:,1), cV0(:,2), cV0(:,3), 'o', 'Color', [0 .7 0], 'MarkerSize', 7, 'LineWidth', 1.2);
    plot3(cAI(:,1), cAI(:,2), cAI(:,3), 'rx', 'MarkerSize', 6, 'LineWidth', 1.0);
    plot3(mriFiducials(:,1), mriFiducials(:,2), mriFiducials(:,3), 'd', ...
        'MarkerFaceColor', [1 .85 0], 'MarkerEdgeColor', 'k', 'MarkerSize', 10);
    xlabel('LR (mm)'); ylabel('AP (mm)'); zlabel('IS (mm)');
    title('3D - 160 element centres'); view(35, 20); axis equal;
    legend({'BeamV0 (MATLAB)', 'BeamAI (C++)', 'measured fiducials'}, 'Location', 'best');

    title(tl, sprintf('%s   |   max |BeamV0 - BeamAI| = %.2e mm', titles{s}, d), ...
        'FontWeight', 'bold', 'Interpreter', 'none');

    outPath = fullfile(outDir, ['registration_overlay_' stages{s} '.png']);
    exportgraphics(f, outPath, 'Resolution', 110);
    close(f);
    fprintf('wrote %s\n', outPath);
end
end

function m = readRect(path)
if ~isfile(path), error('missing %s -- run compare_parity.ps1 -Phase registration first', path); end
m = readmatrix(path);
end

function idx = nearestIdx(axisVec, value)
[~, idx] = min(abs(axisVec(:) - value));
end

function drawSlice(sliceImg, xAxis, yAxis)
imagesc(xAxis, yAxis, sliceImg); colormap(gca, gray); axis xy image; hold on;
end

function plotPair(xV0, yV0, xAI, yAI, xF, yF)
plot(xV0, yV0, 'o', 'Color', [0 .9 0], 'MarkerSize', 6, 'LineWidth', 1.1);
plot(xAI, yAI, 'rx', 'MarkerSize', 5, 'LineWidth', 0.9);
plot(xF, yF, 'd', 'MarkerFaceColor', [1 .85 0], 'MarkerEdgeColor', 'k', 'MarkerSize', 9);
end
