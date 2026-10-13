function dump_rois(matPath)
    d = load(matPath, 'sys');
    r = d.sys.frame.FiducialROIs;
    fprintf('%s\n', matPath);
    fprintf('  numel(FiducialROIs) = %d\n', numel(r));
    for i = 1:numel(r)
        fprintf('  %d  %-10s  %12.6f %12.6f %12.6f\n', i, r(i).name, r(i).position);
    end
end
