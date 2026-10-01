function centreMm = beamv0_register_from_beamai(app, csvPath, sliderValue)
% beamv0_register_from_beamai - drive the live BeamV0 GUI to the end of
% registration using BeamAI's measured fiducials, and leave the result on
% screen.
%
% Run this with BeamV0 already open and the SAME MRI loaded (File > Load MRI
% > "select folder" > the leaf DICOM series folder). It does exactly what an
% operator does:
%
%   1. drops BeamAI's six fiducials into app.FiducialROIs
%   2. selects the MRI-Based registration mode
%   3. presses "Register To MRI Fiducials"
%   4. sets all four lock-position sliders
%   5. presses "Register Arrays to Current Position"
%
% Each step redraws the MRI panes, so the registered array is visible in the
% GUI when it returns -- this is the app doing the work, not a simulation of
% it.
%
%   app = BeamV0;                       % or leave out, it will find a running one
%   % ... File > Load MRI ...
%   beamv0_register_from_beamai([], 'C:\...\beamai_fiducials.csv', 3)
%
% Returns the registered array centre in millimetres, which is the number to
% compare against BeamAI's.

    if nargin < 1 || isempty(app), app = findRunningBeamV0(); end
    if nargin < 2 || isempty(csvPath)
        csvPath = fullfile(fileparts(mfilename('fullpath')), '..', 'testdata', ...
                           'beamai_fiducials_F040_T1_MRI.csv');
    end
    if nargin < 3 || isempty(sliderValue), sliderValue = 3; end

    if ~isfield(app.sys, 'arrayData') || isempty(app.sys.arrayData)
        error('beamv0_register_from_beamai:noMri', ...
              'Load an MRI first (File > Load MRI), then re-run.');
    end

    % 1. the six measured points
    load_beamai_fiducials(app, csvPath);

    % 2. MRI-Based mode -- registerCurrentTransducerPostion branches on this
    app.MRIFiducialsButton.Value = true;

    % 3. "Register To MRI Fiducials" (RegisterToMRIFiducialsButtonPushed)
    registerArrayToFiducials(app);
    app.sys.frame.MRIRegistrationComplete = 1;
    app.sys.frame.CurrentRegistrationComplete = 0;
    setRegistrationCheck(app);
    drawMrImages(app);
    fitCentre = mean(app.sys.arrayData.arrayTotal.rect(17:19,:), 2)' * 1000;
    fprintf('after Register To MRI Fiducials: [%.6f %.6f %.6f] mm\n', fitCentre);

    % 4. lock-position sliders. BeamV0 reads the RIGHT pair and only warns
    %    when the two sides disagree, so set all four and keep them equal.
    app.RightHorizontalPositionYSlider.Value = sliderValue;
    app.RightVerticalPositionZSlider.Value   = sliderValue;
    app.LeftHorizontalPositionYSlider.Value  = sliderValue;
    app.LeftVerticalPositionZSlider.Value    = sliderValue;

    % 5. "Register Arrays to Current Position"
    %    (RegisterArraystoCurrentPositionButtonPushed). Note this re-runs
    %    registerArrayToFiducials internally before applying the offset.
    registerCurrentTransducerPostion(app);
    app.sys.frame.CurrentRegistrationComplete = 1;
    setRegistrationCheck(app);
    drawMrImages(app);

    centreMm = mean(app.sys.arrayData.arrayTotal.rect(17:19,:), 2)' * 1000;
    fprintf('after Register Arrays to Current Position (sliders %g): [%.6f %.6f %.6f] mm\n', ...
            sliderValue, centreMm);
end

function app = findRunningBeamV0()
    figs = findall(groot, 'Type', 'figure');
    for i = 1:numel(figs)
        if isprop(figs(i), 'RunningAppInstance')
            candidate = figs(i).RunningAppInstance;
            if ~isempty(candidate) && isa(candidate, 'BeamV0')
                app = candidate;
                return;
            end
        end
    end
    error('beamv0_register_from_beamai:notRunning', ...
          'No running BeamV0 found. Start it with:  app = BeamV0;');
end
