function beamv0_sweep(csvDir, labels, outCsv, notch)
% beamv0_sweep - run BeamV0's own registration math over several datasets.
%
% For each label it reads BeamAI's detected fiducials from
% <csvDir>/beamai_fiducials_<label>.csv and reproduces, with BeamV0's own
% functions, what the operator gets from
%
%   "Register To MRI Fiducials"            -> FIT row
%   "Register Arrays to Current Position"  -> LOCK row  (both sliders at notch)
%
% This is registerArrayToFiducials.m and registerCurrentTransducerPostion.m's
% MRIFiducialsButton branch with the GUI calls (drawTransducersOnMRI,
% setBackupMRIandArrayData, drawMrImages, the protocolTables stamp) left out --
% none of them touch arrayData.
%
% No MRI is loaded: the fit maps originArrayData's own fiducials onto the
% measured ones, so a rigid translation of originArrayData cancels out and
% the MRI-centred placement cannot change the answer.

    if nargin < 4 || isempty(notch), notch = 3; end
    beamv0_paths();

    defaults = load(fullfile(fileparts(mfilename('fullpath')), '..', '..', ...
                             'DefaultSubjectV0', 'defaultSubjectMNIV1.mat'));
    originArrayData = setArrayFiducialMarkers(defaults.sys.originArrayData);

    fid = fopen(outCsv, 'w');
    closer = onCleanup(@() fclose(fid));

    for k = 1:numel(labels)
        label = labels{k};
        csvPath = fullfile(csvDir, sprintf('beamai_fiducials_%s.csv', label));
        try
            FiducialROIs = readFiducials(csvPath, originArrayData);

            % --- registerArrayToFiducials.m ---
            N = numel(originArrayData.fiducialMarkers);
            mriFiducials = zeros(3, N);
            arrayFiducials = zeros(3, N);
            for i = 1:N
                mriFiducials(:, i) = reshape(FiducialROIs(i).position, [], 1);
                arrayFiducials(:, i) = reshape(originArrayData.fiducialMarkers(i).position, [], 1) * 1000;
            end
            affineMatrix = getAffineMatrixFromRegistration(arrayFiducials, mriFiducials, 1);
            fitted = applyAffineToArrayData(affineMatrix, originArrayData);
            fitCentre = mean(fitted.arrayTotal.rect(17:19, :), 2)' * 1000;
            fprintf(fid, 'FIT,%s,%.12f,%.12f,%.12f\n', label, fitCentre);

            % --- registerCurrentTransducerPostion.m, MRIFiducialsButton branch ---
            dH = notch - 1;
            dV = notch - 1;
            dXYZ = [0, -dH * 7.5 / 1000, dV * 10 / 1000];
            [~, Xvector, Yvector, Zvector] = ...
                getTranslationMatrixFromTransducerFiducials([], FiducialROIs);
            if Zvector(3) < 0, Zvector = -1 * Zvector; end
            M = [reshape(Xvector, [], 1)'; reshape(Yvector, [], 1)'; reshape(Zvector, [], 1)']';
            regdXYZ = M * reshape(dXYZ, [], 1);
            translationMat = eye(4);
            translationMat(1:3, 4) = reshape(regdXYZ, [], 1)';
            locked = applyAffineToArrayData(translationMat, fitted);
            lockCentre = mean(locked.arrayTotal.rect(17:19, :), 2)' * 1000;
            fprintf(fid, 'LOCK,%s,%.12f,%.12f,%.12f\n', label, lockCentre);
            fprintf('%s  fit [%9.4f %9.4f %9.4f]  lock [%9.4f %9.4f %9.4f]\n', ...
                    label, fitCentre, lockCentre);
        catch err
            fprintf(fid, 'FAIL,%s,%s\n', label, strrep(err.message, ',', ';'));
            fprintf('%s  FAILED: %s\n', label, err.message);
        end
    end
end

function FiducialROIs = readFiducials(csvPath, originArrayData)
% Pair by name, in originArrayData's own marker order -- the order
% registerArrayToFiducials indexes both lists by.
    text = readlines(csvPath);
    names = {};
    positions = [];
    for i = 1:numel(text)
        line = strtrim(text(i));
        if line == "" || startsWith(line, '#') || startsWith(line, 'name,'), continue; end
        parts = split(line, ',');
        names{end+1} = char(parts(1)); %#ok<AGROW>
        positions(end+1, :) = [str2double(parts(2)), str2double(parts(3)), str2double(parts(4))]; %#ok<AGROW>
    end
    FiducialROIs = struct('name', {}, 'position', {}, 'group', {});
    for i = 1:numel(originArrayData.fiducialMarkers)
        wanted = originArrayData.fiducialMarkers(i).name;
        hit = find(strcmp(names, wanted), 1);
        if isempty(hit)
            error('beamv0_sweep:missingFiducial', 'no %s in %s', wanted, csvPath);
        end
        FiducialROIs(i).name = wanted;
        FiducialROIs(i).position = positions(hit, :);
        FiducialROIs(i).group = "Transducer";
    end
end
