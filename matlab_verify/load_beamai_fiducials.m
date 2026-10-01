function positions = load_beamai_fiducials(app, csvPath)
% load_beamai_fiducials - drive BeamV0 from BeamAI's measured fiducials.
%
% BeamAI writes beamai_fiducials.csv when "Confirm all located fiducials" is
% pressed: the six measured points in millimetres, RAS, at 17 significant
% digits so an IEEE double round-trips exactly. Reading that file into
% app.FiducialROIs gives BeamV0 bit-identical input to what BeamAI itself
% registers with, so any difference in the result afterwards is a real
% difference between the two implementations rather than a copying artefact.
%
% Usage, with BeamV0 already open and the same MRI loaded:
%
%     load_beamai_fiducials(app, 'C:\...\beamai_fiducials.csv');
%     registerArrayToFiducials(app);                       % "Register To MRI Fiducials"
%     format long
%     mean(app.sys.arrayData.arrayTotal.rect(17:19,:), 2)' * 1000   % array centre
%
% then set the lock-position sliders to the same values used in BeamAI and
% press "Register Arrays to Current Position" (or call
% registerCurrentTransducerPostion(app)) and read the centre again.
%
% Matching is by fiducial NAME, not row order, so it stays correct even if
% the two applications ever build their marker lists in a different order.
% Errors if any of BeamV0's fiducials is missing from the file.

    if nargin < 2 || isempty(csvPath)
        % The tracked fixture, not the app's volatile working-directory export.
        csvPath = fullfile(fileparts(mfilename('fullpath')), '..', 'testdata', ...
                           'beamai_fiducials_F040_T1_MRI.csv');
    end
    if ~isfile(csvPath)
        error('load_beamai_fiducials:missingFile', 'No such file: %s', csvPath);
    end

    % Read name,x,y,z rows; '#' comment lines and the header are skipped.
    names = strings(0, 1);
    coords = zeros(0, 3);
    fid = fopen(csvPath, 'r');
    cleanup = onCleanup(@() fclose(fid));
    while true
        line = fgetl(fid);
        if ~ischar(line), break; end
        line = strtrim(line);
        if isempty(line) || startsWith(line, '#') || startsWith(line, 'name,')
            continue;
        end
        parts = strsplit(line, ',');
        if numel(parts) ~= 4
            error('load_beamai_fiducials:badRow', 'Expected name,x,y,z but found: %s', line);
        end
        names(end+1, 1) = string(strtrim(parts{1})); %#ok<AGROW>
        coords(end+1, :) = [str2double(parts{2}), str2double(parts{3}), str2double(parts{4})]; %#ok<AGROW>
    end

    if any(~isfinite(coords(:)))
        error('load_beamai_fiducials:badNumber', 'Non-finite coordinate in %s', csvPath);
    end

    % Assign by name into the app's own fiducial list.
    positions = zeros(numel(app.FiducialROIs), 3);
    for i = 1:numel(app.FiducialROIs)
        target = string(app.FiducialROIs(i).name);
        match = find(names == target, 1);
        if isempty(match)
            error('load_beamai_fiducials:noMatch', ...
                  'BeamV0 fiducial "%s" is not present in %s', target, csvPath);
        end
        app.FiducialROIs(i).position = coords(match, :);
        positions(i, :) = coords(match, :);
    end

    % Redraw so the ROIs land on the imported points.
    try
        drawMrImages(app);
    catch
        % Display refresh is best-effort; the positions above are what matter.
    end

    fprintf('Loaded %d fiducials from %s into app.FiducialROIs:\n', size(positions, 1), csvPath);
    for i = 1:numel(app.FiducialROIs)
        fprintf('  %-10s % .12f  % .12f  % .12f\n', app.FiducialROIs(i).name, positions(i, :));
    end
end
