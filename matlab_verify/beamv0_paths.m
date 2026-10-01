function beamv0_paths()
% beamv0_paths - put everything BeamV0 needs on the MATLAB path.
%
% Three roots are required and none of them is obvious:
%
%   BeamV0\                 loadDefaultSys.m does `what('BeamV0')` to find
%                           ..\DefaultSubjectV0\defaultSubjectMNIV1.mat, so
%                           the folder NAMED BeamV0 must itself be on the
%                           path -- adding only GUIMatlab makes startup fail
%                           with "Unable to find 'C:\DefaultSubjectV0\...'".
%   BeamV0\GUIMatlab\       the app, its callbacks, and nifti_utils.
%   BeamV0\BEAMANALYSIS\    getTxRxSignalAmplitude, reached from startupFcn
%                           via initializeCorrectionValues.
%
%   beamv0_paths; app = BeamV0;

    root = fullfile(fileparts(mfilename('fullpath')), '..', '..', 'BeamV0');
    if ~isfolder(root)
        error('beamv0_paths:notFound', 'BeamV0 not found at %s', root);
    end
    addpath(root);
    addpath(genpath(fullfile(root, 'GUIMatlab')));
    addpath(genpath(fullfile(root, 'BEAMANALYSIS')));
end
