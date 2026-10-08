function export_treatment_tables(outDir)
% export_treatment_tables - dumps BeamV0's authoritative Target List and
% protocol tables so BeamAI can be built from them and checked against them.
%
%   beamv0_paths; app = BeamV0;          % then load an MRI if you want app state
%   export_treatment_tables             % writes to BeamAI/testdata/matlab_export
%
% Writes, from the shipped default subject:
%   targets.csv              Target List names, in protocolTables index order
%   stim_<NN>_<name>.csv     sys.protocolTables(NN).stimParamTableData
%   protocol_<name>.csv      sys.treatmentProtocolTables(i).blank.Data
%
% The Target List order matters: setProtocolTableWithStimParamTable.m keys
% protocolTables by the list position, so position is the only link between a
% stim table and a target name.

    if nargin < 1
        thisDir = fileparts(mfilename('fullpath'));
        outDir = fullfile(thisDir, '..', 'testdata', 'matlab_export');
    end
    if ~isfolder(outDir), mkdir(outDir); end

    beamv0_paths();
    sys = loadDefaultSys();

    % --- Target List names -------------------------------------------------
    % Prefer a running app, whose list is what the operator actually sees.
    names = {};
    apps = findall(groot, 'Type', 'figure');
    for k = 1:numel(apps)
        if isprop(apps(k), 'RunningAppInstance') && ...
                isprop(apps(k).RunningAppInstance, 'TargetListListBox')
            names = apps(k).RunningAppInstance.TargetListListBox.Items;
            fprintf('Target List read from the running app (%d items).\n', numel(names));
            break
        end
    end
    if isempty(names) && isfield(sys, 'arrayData') && ...
            isfield(sys.arrayData, 'fiducialMarkers')
        fprintf('No running app found; falling back to protocolTables indices only.\n');
    end

    nStim = 0;
    if isfield(sys, 'protocolTables'), nStim = numel(sys.protocolTables); end
    fprintf('sys.protocolTables: %d entries\n', nStim);

    fid = fopen(fullfile(outDir, 'targets.csv'), 'w');
    fprintf(fid, 'index,name\n');
    for i = 1:nStim
        if i <= numel(names)
            nm = names{i};
        else
            nm = sprintf('target%02d', i);
        end
        fprintf(fid, '%d,%s\n', i, nm);
    end
    fclose(fid);

    % --- per-target stim parameters ---------------------------------------
    for i = 1:nStim
        if i <= numel(names)
            nm = matlab.lang.makeValidName(names{i});
        else
            nm = sprintf('target%02d', i);
        end
        data = sys.protocolTables(i).stimParamTableData;
        writetable(data, fullfile(outDir, sprintf('stim_%02d_%s.csv', i, nm)));
    end

    % --- protocol schedules ------------------------------------------------
    nProto = 0;
    if isfield(sys, 'treatmentProtocolTables')
        nProto = numel(sys.treatmentProtocolTables);
    end
    fprintf('sys.treatmentProtocolTables: %d entries\n', nProto);
    for i = 1:nProto
        nm = matlab.lang.makeValidName(sys.treatmentProtocolTables(i).name);
        writetable(sys.treatmentProtocolTables(i).blank.Data, ...
                   fullfile(outDir, sprintf('protocol_%s.csv', nm)));
    end

    fprintf('\nWrote %d stim tables and %d protocol tables to\n  %s\n', ...
            nStim, nProto, outDir);
end
