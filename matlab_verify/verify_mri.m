function verify_mri(workDir, outPath) %#ok<INUSD>
% Runs the real BeamV0 MRI/ coordinate-math functions
% (get_ras_xform_fromHdr, getRASAxisVectorsFromNifti, getSliceImage) on the
% same synthetic headers + volume as apps/parity_mri, matching the labels in
% parity_mri_cpp.csv. Driven by matlab_verify/compare_parity.ps1.

thisDir = fileparts(mfilename('fullpath'));
beam = fullfile(thisDir, '..', '..', 'BeamV0', 'GUIMatlab', 'BEAM');
addpath(fullfile(beam, 'MRI'));

fid = fopen(outPath, 'w');
if fid < 0, error('cannot open %s', outPath); end
cleanupObj = onCleanup(@() fclose(fid)); %#ok<NASGU>

% --- synthetic headers (must match apps/parity_mri/main.cpp) ---
hq = struct();
hq.hist.qform_code = 1;
hq.hist.quatern_b = 0.1;
hq.hist.quatern_c = -0.2;
hq.hist.quatern_d = 0.3;
hq.hist.qoffset_x = -90;
hq.hist.qoffset_y = -126;
hq.hist.qoffset_z = -72;
hq.hist.sform_code = 0;
hq.dime.pixdim = [1.0 0.9 1.1 1.3 0 0 0 0];
hq.dime.dim = [3 4 5 6 0 0 0 0];

hqn = hq;
hqn.dime.pixdim(1) = -1.0;   % qfac

hs = struct();
hs.hist.qform_code = 0;
hs.hist.sform_code = 1;
hs.hist.srow_x = [0.9  0.02 -0.01 -90];
hs.hist.srow_y = [-0.02 1.1  0.03 -126];
hs.hist.srow_z = [0.01 -0.03 1.3  -72];
hs.dime.pixdim = [1.0 0.9 1.1 1.3 0 0 0 0];
hs.dime.dim = [3 4 5 6 0 0 0 0];

writeXform(fid, 'qform', get_ras_xform_fromHdr(hq));
writeAxes(fid, 'qaxes', hq);
writeXform(fid, 'qformNeg', get_ras_xform_fromHdr(hqn));
writeXform(fid, 'sform', get_ras_xform_fromHdr(hs));
writeAxes(fid, 'saxes', hs);

% --- synthetic ramp volume 4 x 5 x 6 ---
img = zeros(4, 5, 6);
for ii = 1:4
    for jj = 1:5
        for kk = 1:6
            img(ii, jj, kk) = (ii-1)*10000 + (jj-1)*100 + (kk-1);
        end
    end
end
writeSlice(fid, 'sagital', getSliceImage(img, 2, 'sagital'));
writeSlice(fid, 'coronal', getSliceImage(img, 3, 'coronal'));
writeSlice(fid, 'axial',   getSliceImage(img, 4, 'axial'));

end

function writeXform(fid, prefix, x)
for i = 0:2
    for j = 0:3
        fprintf(fid, '%s_r%dc%d,%.17g\n', prefix, i, j, x(i+1, j+1));
    end
end
end

function writeAxes(fid, prefix, hdr)
[dimLR, dimAP, dimIS] = getRASAxisVectorsFromNifti(hdr);
fprintf(fid, '%s_LR_first,%.17g\n', prefix, dimLR(1));
fprintf(fid, '%s_LR_last,%.17g\n',  prefix, dimLR(end));
fprintf(fid, '%s_AP_first,%.17g\n', prefix, dimAP(1));
fprintf(fid, '%s_AP_last,%.17g\n',  prefix, dimAP(end));
fprintf(fid, '%s_IS_first,%.17g\n', prefix, dimIS(1));
fprintf(fid, '%s_IS_last,%.17g\n',  prefix, dimIS(end));
end

function writeSlice(fid, prefix, s)
fprintf(fid, '%s_rows,%d\n', prefix, size(s, 1));
fprintf(fid, '%s_cols,%d\n', prefix, size(s, 2));
fprintf(fid, '%s_00,%.17g\n',       prefix, s(1, 1));
fprintf(fid, '%s_0last,%.17g\n',    prefix, s(1, end));
fprintf(fid, '%s_last0,%.17g\n',    prefix, s(end, 1));
fprintf(fid, '%s_lastlast,%.17g\n', prefix, s(end, end));
fprintf(fid, '%s_11,%.17g\n',       prefix, s(2, 2));
end
