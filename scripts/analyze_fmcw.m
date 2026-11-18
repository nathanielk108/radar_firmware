% Offline FMCW range processing for the captures currently in data/.
% Run this script from MATLAB R2020a or newer. Base MATLAB is sufficient.
% Optional overrides before running, for example
%   fmcwOptions = struct('makePlots', false, 'quietRmsCodes', 200);
% Results remain in fmcwResults and are saved under build/fmcw_analysis/.
%
% ADC A and B are separate real receivers. B has the installed antenna.
% Firmware already removes the previous-conversion word. Retain sample zero.
% USB shots support incoherent power averaging. Coherent Doppler, AoA and
% mapping require timed trains, populated receivers and pose information.

repoRoot = fileparts(fileparts(mfilename('fullpath')));
cfg = struct('dataDir', fullfile(repoRoot, 'data'), ...
  'outputDir', fullfile(repoRoot, 'build', 'fmcw_analysis'), ...
  'channel', 'b', 'quietRmsCodes', 200, 'fftSize', 256, ...
  'removeLinearTrend', false, 'slopeScale', 1, 'rangeBiasM', 0, ...
  'minRangeM', 0.75, 'maxRangeM', 29, 'expectedGateM', 1, ...
  'cfarTraining', 6, 'cfarGuard', 2, 'cfarMinTraining', 6, ...
  'cfarNominalPfa', 0.01, 'backgroundContrastDb', 6, ...
  'maxPeaks', 8, 'makePlots', true, 'figureVisible', 'on');
if exist('fmcwOptions', 'var')
  fields = fieldnames(fmcwOptions);
  for k = 1:numel(fields)
    assert(isfield(cfg, fields{k}), 'Unknown option %s', fields{k});
    cfg.(fields{k}) = fmcwOptions.(fields{k});
  end
end
validate_config(cfg);
if ~isfolder(cfg.outputDir)
  mkdir(cfg.outputDir);
end

files = [dir(fullfile(cfg.dataDir, '*.log')); dir(fullfile(cfg.dataDir, '*.csv'))];
assert(~isempty(files), 'No .log or .csv files in %s', cfg.dataDir);
captureCells = cell(1, numel(files));
for k = 1:numel(files)
  capture = read_capture(fullfile(files(k).folder, files(k).name));
  capture.label = files(k).name;
  capture.expectedRangeM = ground_truth(capture.label);
  captureCells{k} = capture;
end
captures = [captureCells{:}];

% Run both selections. Quiet flags use unfiltered, mean-removed ADC codes.
% The filtered run describes the surviving acquisitions. The all-shot run
% preserves the measured capture reliability, including low-variation shots.
fmcwResults = struct('config', cfg, 'allShots', [], 'activeOnly', []);
selections = {'allShots', 'activeOnly'};
for selection = 1:numel(selections)
  mode = selections{selection};
  folder = fullfile(cfg.outputDir, mode);
  if ~isfolder(folder)
    mkdir(folder);
  end
  resultCells = cell(1, numel(captures));
  for k = 1:numel(captures)
    resultCells{k} = process_capture(captures(k), cfg, selection == 2);
  end
  results = [resultCells{:}];
  reference = find(strcmp({results.file}, 'rf_5m_empty.log'), 1);
  for k = 1:numel(results)
    if ~isempty(reference) && k ~= reference && compatible(results(k), results(reference))
      results(k).backgroundFile = results(reference).file;
      results(k).backgroundPower = results(reference).meanPower;
      results(k).contrastDb = power_db(results(k).meanPower ./ ...
        max(results(reference).meanPower, realmin('single')));
      % Signed power difference preserves decreases relative to the empty run.
      results(k).excessPower = results(k).meanPower - results(reference).meanPower;
    end
    results(k).peaks = peak_table(results(k), cfg);
  end
  summary = summary_table(results);
  knownTargets = target_table(results, cfg);
  shots = vertcat(results.shots);
  peaks = vertcat(results.peaks);
  writetable(summary, fullfile(folder, 'summary.csv'));
  writetable(shots, fullfile(folder, 'shots.csv'));
  writetable(peaks, fullfile(folder, 'peaks.csv'));
  writetable(knownTargets, fullfile(folder, 'known_targets.csv'));
  fmcwResults.(mode) = struct('captures', results, 'summary', summary, ...
    'shots', shots, 'peaks', peaks, 'knownTargets', knownTargets);
  fprintf('\n%s\n', mode);
  disp(summary(:, {'File', 'TotalShots', 'UsedShots', 'QuietShots', ...
    'StrongestRangeM', 'ResolutionM', 'NyquistRangeM'}));
  disp(knownTargets);
  if cfg.makePlots
    plot_run(results, cfg, folder, mode);
  end
end
comparison = compare_runs(fmcwResults.allShots.knownTargets, ...
  fmcwResults.activeOnly.knownTargets);
writetable(comparison, fullfile(cfg.outputDir, 'selection_comparison.csv'));
fmcwResults.selectionComparison = comparison;
save(fullfile(cfg.outputDir, 'fmcw_results.mat'), 'fmcwResults', 'captures', '-v7');
write_notes(fullfile(cfg.outputDir, 'analysis_notes.txt'), cfg);
fprintf('\nAll-shot versus active-only comparison\n');
disp(comparison);
fprintf('Saved results to %s\n', cfg.outputDir);

function validate_config(cfg)
  assert(ismember(lower(cfg.channel), {'a', 'b'}), 'channel must be a or b');
  assert(cfg.quietRmsCodes >= 0 && isfinite(cfg.quietRmsCodes), 'Invalid RMS cutoff');
  assert(ismember(cfg.fftSize, [32 64 128 256 512 1024 2048 4096]), ...
    'fftSize must be a supported CMSIS-DSP real FFT size');
  assert(cfg.slopeScale > 0 && isfinite(cfg.slopeScale), 'Invalid slope scale');
  assert(isfinite(cfg.rangeBiasM), 'Invalid range bias');
  assert(cfg.expectedGateM > 0 && cfg.minRangeM >= 0 && ...
    cfg.maxRangeM > cfg.minRangeM, 'Invalid range gates');
  assert(cfg.cfarNominalPfa > 0 && cfg.cfarNominalPfa < 1, 'Invalid nominal Pfa');
  values = [cfg.cfarTraining cfg.cfarGuard cfg.cfarMinTraining cfg.maxPeaks];
  assert(all(isfinite(values) & values == fix(values) & values >= 1), ...
    'CFAR cell counts and maxPeaks must be positive integers');
  assert(cfg.cfarMinTraining <= 2 * cfg.cfarTraining, 'Too few training cells');
end

function capture = read_capture(path)
  lines = regexp(fileread(path), '\r\n|\n|\r', 'split');
  metadata = struct();
  adc = [];
  shot = 0;
  line = 1;
  while line <= numel(lines)
    text = strtrim(lines{line});
    if ~startsWith(text, '# sample_rate_hz=')
      assert(isempty(regexp(text, '^[+-]?\d+,[+-]?\d+,[+-]?\d+$', 'once')), ...
        '%s line %d has a sample outside its capture block', path, line);
      line = line + 1;
      continue;
    end
    meta = struct();
    pairs = regexp(text, '([a-zA-Z_][a-zA-Z_0-9]*)=([-+0-9.eE]+)', 'tokens');
    for p = 1:numel(pairs)
      meta.(pairs{p}{1}) = str2double(pairs{p}{2});
    end
    required = {'sample_rate_hz', 'step_hz', 'step_rate_hz', 'samples'};
    for p = 1:numel(required)
      key = required{p};
      assert(isfield(meta, key) && isfinite(meta.(key)) && meta.(key) > 0, ...
        '%s line %d has invalid %s', path, line, key);
    end
    n = meta.samples;
    assert(n >= 8 && n <= 65535 && n == fix(n), '%s has invalid sample count', path);
    if ~isfield(meta, 'gain_mv')
      meta.gain_mv = NaN;
    end
    assert(line + n + 1 <= numel(lines), '%s has an incomplete capture', path);
    assert(strcmp(strtrim(lines{line + 1}), 'index,adc_a,adc_b'), ...
      '%s line %d has an unexpected CSV header', path, line + 1);
    codes = zeros(n, 2, 'int16');
    for row = 1:n
      rowText = strtrim(lines{line + row + 1});
      assert(~isempty(regexp(rowText, '^[+-]?\d+,[+-]?\d+,[+-]?\d+$', 'once')), ...
        '%s line %d has an invalid CSV row', path, line + row + 1);
      values = sscanf(rowText, '%f,%f,%f');
      assert(numel(values) == 3 && all(isfinite(values)) && ...
        all(values == fix(values)) && values(1) == row - 1 && ...
        all(values(2:3) >= -32768 & values(2:3) <= 32767), ...
        '%s line %d has invalid sample data', path, line + row + 1);
      codes(row, :) = int16(values(2:3));
    end
    if shot == 0
      metadata = meta;
      adc = zeros(n, 2, 0, 'int16');
    else
      same = [required {'gain_mv', 'start_hz', 'first_sample_offset_ticks', 'timer_hz'}];
      for p = 1:numel(same)
        key = same{p};
        assert(isfield(meta, key) == isfield(metadata, key), ...
          '%s changes metadata field %s between shots', path, key);
        if isfield(meta, key)
          assert(isequaln(meta.(key), metadata.(key)), ...
            '%s changes %s between shots', path, key);
        end
      end
    end
    shot = shot + 1;
    adc(:, :, shot) = codes; %#ok<AGROW>
    line = line + n + 2;
  end
  assert(shot > 0, '%s contains no supported single-chirp CSV blocks', path);
  capture = struct('path', path, 'metadata', metadata, 'adc', adc);
end

function range = ground_truth(name)
  % User-corrected log ranges. Legacy outdoor CSV distances are unconfirmed.
  range = NaN;
  switch name
    case 'rf_3m_target.log'
      range = 9 * 0.3048;
    case {'rf_5m_target.log', 'rf_5m_dual_target.log'}
      range = 15 * 0.3048;
    case 'rf_8m_target.log'
      range = 24 * 0.3048;
  end
end

function result = process_capture(capture, cfg, excludeQuiet)
  c = 299792458;
  meta = capture.metadata;
  n = meta.samples;
  assert(cfg.fftSize >= n, '%s exceeds fftSize. Increase that setting.', capture.label);
  channel = 1 + strcmpi(cfg.channel, 'b');
  raw = reshape(single(capture.adc(:, channel, :)), n, []);
  centered = raw - mean(raw, 1);
  rmsCodes = sqrt(mean(centered .* centered, 1));
  quiet = rmsCodes < cfg.quietRmsCodes;
  used = true(size(quiet));
  if excludeQuiet
    used = ~quiet;
  end
  shots = size(raw, 2);
  rail = sum(raw == -32768 | raw == 32767, 1);
  nearRail = mean(abs(raw) >= 0.99 * 32768, 1);
  crestDb = 20 * log10(max(abs(centered), [], 1) ./ max(rmsCodes, realmin('single')));
  if cfg.removeLinearTrend
    % Least-squares subtraction maps to simple sums on the MCU.
    time = single((0:n-1)' - (n-1)/2);
    centered = centered - time * ((time' * centered) / sum(time .* time));
  end
  normalized = centered / single(32768);
  index = single((0:n-1)');
  hannWindow = single(0.5) - single(0.5) * cos(single(2*pi) * index / single(n-1));
  blackmanWindow = single(0.42) - single(0.5) * cos(single(2*pi) * index / single(n-1)) + ...
    single(0.08) * cos(single(4*pi) * index / single(n-1));
  spectrum = fft_amplitude(normalized, hannWindow, cfg.fftSize);
  power = abs(spectrum).^2;
  % Use the smallest supported power-of-two FFT for detection. The larger
  % display FFT interpolates peaks. Both transforms map to CMSIS-DSP.
  detectionSize = max(32, 2^nextpow2(n));
  native = fft_amplitude(normalized, hannWindow, detectionSize);
  nativePower = abs(native).^2;
  [threshold, noise] = ca_cfar(nativePower, cfg);
  slope = meta.step_hz * meta.step_rate_hz * cfg.slopeScale;
  frequency = (0:cfg.fftSize/2)' * meta.sample_rate_hz / cfg.fftSize;
  range = c * frequency / (2 * slope) - cfg.rangeBiasM;
  nativeFrequency = (0:detectionSize/2)' * meta.sample_rate_hz / detectionSize;
  nativeRange = c * nativeFrequency / (2 * slope) - cfg.rangeBiasM;
  valid = nativeRange >= cfg.minRangeM & nativeRange <= cfg.maxRangeM;
  local = false(size(nativePower));
  local(2:end-1, :) = nativePower(2:end-1, :) > nativePower(1:end-2, :) & ...
    nativePower(2:end-1, :) >= nativePower(3:end, :);
  detections = nativePower > threshold & local & valid;
  strongestRange = NaN(shots, 1);
  for shot = 1:shots
    [~, bin] = strongest_peak(power(:, shot), range, cfg);
    if ~isnan(bin)
      [f, ~] = interpolate_peak(power(:, shot), bin, meta.sample_rate_hz / cfg.fftSize);
      strongestRange(shot) = c * f / (2 * slope) - cfg.rangeBiasM;
    end
  end
  file = repmat(string(capture.label), shots, 1);
  result = struct('file', capture.label, 'metadata', meta, 'channel', cfg.channel, ...
    'expectedRangeM', capture.expectedRangeM, 'slopeHzPerS', slope, ...
    'sampleSpanS', (n-1) / meta.sample_rate_hz, ...
    'bandwidthHz', slope * (n-1) / meta.sample_rate_hz, ...
    'resolutionM', c / (2 * slope * (n-1) / meta.sample_rate_hz), ...
    'nativeBinHz', meta.sample_rate_hz / detectionSize, ...
    'independentBinHz', meta.sample_rate_hz / n, ...
    'detectionFftSize', detectionSize, ...
    'nyquistRangeM', c * meta.sample_rate_hz / (4 * slope) - cfg.rangeBiasM, ...
    'hannEnbwHz', meta.sample_rate_hz * double(sum(hannWindow.^2) / sum(hannWindow)^2), ...
    'frequencyHz', frequency, 'rangeM', range, 'nativeRangeM', nativeRange, ...
    'rawCodes', raw, 'used', used, 'power', power, 'nativePower', nativePower, ...
    'detections', detections, 'threshold', threshold, 'noise', noise, ...
    'meanPower', selected_mean(power, used), 'medianPower', selected_median(power, used), ...
    'rectangularPower', selected_mean(abs(fft_amplitude(normalized, ones(n, 1, 'single'), cfg.fftSize)).^2, used), ...
    'blackmanPower', selected_mean(abs(fft_amplitude(normalized, blackmanWindow, cfg.fftSize)).^2, used), ...
    'meanThreshold', selected_mean(threshold, used), 'meanNoise', selected_mean(noise, used), ...
    'backgroundFile', '', 'backgroundPower', NaN(size(frequency)), ...
    'contrastDb', NaN(size(frequency)), 'excessPower', NaN(size(frequency)), 'peaks', table());
  result.shots = table(file, (1:shots)', used', quiet', double(mean(raw, 1))', ...
    double(rmsCodes)', double(rail)', double(nearRail)', double(crestDb)', strongestRange, ...
    'VariableNames', {'File', 'Shot', 'Used', 'Quiet', 'DcCodes', 'AcRmsCodes', ...
    'ClippedSamples', 'NearRailFraction', 'CrestDb', 'StrongestRangeM'});
end

function spectrum = fft_amplitude(input, window, nfft)
  transformed = fft(input .* window, nfft, 1) / sum(window);
  spectrum = transformed(1:floor(nfft/2)+1, :);
  lastInterior = size(spectrum, 1) - (mod(nfft, 2) == 0);
  spectrum(2:lastInterior, :) = 2 * spectrum(2:lastInterior, :);
end

function [threshold, noise] = ca_cfar(power, cfg)
  % CA-CFAR alpha assumes independent exponential single-shot powers.
  % Hann bins are correlated and courtyard clutter is structured, so the
  % configured Pfa is a nominal tuning parameter. Validate it empirically.
  % Edge cells use all available training cells above DC, with an explicit
  % minimum count. Each cell uses its actual training count for alpha.
  threshold = NaN(size(power), 'single');
  noise = NaN(size(power), 'single');
  last = size(power, 1);
  for bin = 2:last-1
    left = max(2, bin-cfg.cfarGuard-cfg.cfarTraining):bin-cfg.cfarGuard-1;
    right = bin+cfg.cfarGuard+1:min(last-1, bin+cfg.cfarGuard+cfg.cfarTraining);
    training = [left right];
    count = numel(training);
    if count < cfg.cfarMinTraining
      continue;
    end
    noise(bin, :) = mean(power(training, :), 1);
    alpha = count * (cfg.cfarNominalPfa^(-1/count) - 1);
    threshold(bin, :) = single(alpha) * noise(bin, :);
  end
end

function values = selected_mean(data, used)
  values = NaN(size(data, 1), 1, 'single');
  if any(used)
    values = mean(data(:, used), 2);
  end
end

function values = selected_median(data, used)
  values = NaN(size(data, 1), 1, 'single');
  if any(used)
    values = median(data(:, used), 2);
  end
end

function yes = compatible(a, b)
  keys = {'sample_rate_hz', 'step_hz', 'step_rate_hz', 'samples', 'gain_mv'};
  yes = any(a.used) && any(b.used) && isfinite(a.metadata.gain_mv) && ...
    isfinite(b.metadata.gain_mv);
  for k = 1:numel(keys)
    yes = yes && isequal(a.metadata.(keys{k}), b.metadata.(keys{k}));
  end
  % Older CSVs omit start-frequency/timing metadata. Comparisons of those
  % files use their documented shared firmware profile, recorded in notes.
  keys = {'start_hz', 'first_sample_offset_ticks', 'timer_hz', 'bandwidth_hz'};
  for k = 1:numel(keys)
    key = keys{k};
    yes = yes && isfield(a.metadata, key) == isfield(b.metadata, key);
    if isfield(a.metadata, key) && isfield(b.metadata, key)
      yes = yes && isequal(a.metadata.(key), b.metadata.(key));
    end
  end
end

function bins = local_peaks(power, range, cfg)
  bins = find(power(2:end-1) > power(1:end-2) & ...
    power(2:end-1) >= power(3:end)) + 1;
  bins = bins(range(bins) >= cfg.minRangeM & range(bins) <= cfg.maxRangeM);
  [~, order] = sort(power(bins), 'descend');
  bins = bins(order);
end

function [power, bin] = strongest_peak(values, range, cfg)
  bins = local_peaks(values, range, cfg);
  power = NaN;
  bin = NaN;
  if ~isempty(bins)
    bin = bins(1);
    power = values(bin);
  end
end

function [frequency, power] = interpolate_peak(values, bin, binHz)
  neighbors = log(max(double(values(bin-1:bin+1)), realmin));
  curvature = neighbors(1) - 2 * neighbors(2) + neighbors(3);
  offset = 0;
  if curvature < 0
    offset = max(-0.5, min(0.5, 0.5 * (neighbors(1) - neighbors(3)) / curvature));
  end
  frequency = (bin-1 + offset) * binHz;
  power = exp(neighbors(2) - 0.25 * (neighbors(1) - neighbors(3)) * offset);
end

function peaks = peak_table(result, cfg)
  bins = local_peaks(result.meanPower, result.rangeM, cfg);
  bins = bins(1:min(numel(bins), cfg.maxPeaks));
  count = numel(bins);
  frequency = NaN(count, 1);
  range = frequency;
  level = frequency;
  contrast = frequency;
  localRatio = frequency;
  hits = frequency;
  harmonic = zeros(count, 1);
  harmonicError = frequency;
  for k = 1:count
    bin = bins(k);
    [frequency(k), power] = interpolate_peak(result.meanPower, bin, ...
      result.metadata.sample_rate_hz / cfg.fftSize);
    range(k) = 299792458 * frequency(k) / (2 * result.slopeHzPerS) - cfg.rangeBiasM;
    level(k) = power_db(power);
    contrast(k) = interp1(result.frequencyHz, double(result.contrastDb), frequency(k));
    [~, nativeBin] = min(abs(result.nativeRangeM - range(k)));
    localRatio(k) = power_db(power / double(result.meanNoise(nativeBin)));
    gate = abs(result.nativeRangeM - range(k)) <= result.resolutionM;
    if any(result.used)
      hits(k) = mean(any(result.detections(gate, result.used), 1));
    end
    if isfinite(result.expectedRangeM)
      expected = 2 * result.slopeHzPerS * (result.expectedRangeM + cfg.rangeBiasM) / 299792458;
      multiple = round(frequency(k) / expected);
      if multiple >= 2 && multiple <= 8 && abs(frequency(k)-multiple*expected) < result.nativeBinHz
        harmonic(k) = multiple;
        harmonicError(k) = frequency(k) - multiple * expected;
      end
    end
  end
  peaks = table(repmat(string(result.file), count, 1), (1:count)', frequency, range, ...
    level, localRatio, contrast, hits, harmonic, harmonicError, ...
    'VariableNames', {'File', 'Rank', 'BeatHz', 'ApparentRangeM', 'PeakDbfs', ...
    'LocalPowerRatioDb', 'EmptyContrastDb', 'CfarHitFraction', ...
    'PossibleExpectedHarmonic', 'HarmonicErrorHz'});
end

function summary = summary_table(results)
  rows = cell(numel(results), 13);
  for k = 1:numel(results)
    r = results(k);
    strongest = NaN;
    if ~isempty(r.peaks)
      strongest = r.peaks.ApparentRangeM(1);
    end
    rows(k, :) = {string(r.file), height(r.shots), sum(r.used), sum(r.shots.Quiet), ...
      strongest, r.metadata.sample_rate_hz, r.metadata.gain_mv, r.bandwidthHz/1e6, ...
      r.resolutionM, r.nyquistRangeM, r.hannEnbwHz, ...
      sum(r.shots.ClippedSamples(r.used)), string(r.backgroundFile)};
  end
  summary = cell2table(rows, 'VariableNames', {'File', 'TotalShots', 'UsedShots', ...
    'QuietShots', 'StrongestRangeM', 'SampleRateHz', 'GainMv', 'SampledBandwidthMHz', ...
    'ResolutionM', 'NyquistRangeM', 'HannEnbwHz', 'ClippedSamples', 'BackgroundFile'});
end

function targets = target_table(results, cfg)
  selected = find(isfinite([results.expectedRangeM]));
  rows = cell(numel(selected), 15);
  for k = 1:numel(selected)
    r = results(selected(k));
    expectedHz = 2 * r.slopeHzPerS * (r.expectedRangeM + cfg.rangeBiasM) / 299792458;
    bins = local_peaks(r.meanPower, r.rangeM, cfg);
    bins = bins(abs(r.rangeM(bins) - r.expectedRangeM) <= cfg.expectedGateM);
    candidate = NaN;
    contrast = NaN;
    if ~isempty(bins)
      [f, ~] = interpolate_peak(r.meanPower, bins(1), r.metadata.sample_rate_hz / cfg.fftSize);
      candidate = 299792458 * f / (2 * r.slopeHzPerS) - cfg.rangeBiasM;
      contrast = interp1(r.frequencyHz, double(r.contrastDb), f);
    end
    gate = abs(r.nativeRangeM - r.expectedRangeM) <= cfg.expectedGateM;
    hits = NaN;
    if any(r.used) && any(gate)
      hits = mean(any(r.detections(gate, r.used), 1));
    end
    level = interp1(r.frequencyHz, double(r.meanPower), expectedHz);
    referenceLevel = interp1(r.frequencyHz, double(r.backgroundPower), expectedHz);
    expectedContrast = power_db(level / referenceLevel);
    strongest = NaN;
    if ~isempty(r.peaks)
      strongest = r.peaks.ApparentRangeM(1);
    end
    % This is a descriptive screening rule, with all conditions exported.
    supported = isfinite(candidate) && hits >= 0.5 && ...
      contrast >= cfg.backgroundContrastDb && ~any(r.shots.ClippedSamples(r.used));
    rows(k, :) = {string(r.file), r.expectedRangeM, expectedHz, sum(r.used), ...
      height(r.shots), sum(r.shots.Quiet), strongest, candidate, ...
      candidate-r.expectedRangeM, hits, expectedContrast, contrast, ...
      power_db(level), logical(supported), string(r.backgroundFile)};
  end
  targets = cell2table(rows, 'VariableNames', {'File', 'ExpectedRangeM', 'ExpectedBeatHz', ...
    'UsedShots', 'TotalShots', 'QuietShots', 'StrongestRangeM', 'ExpectedGatePeakM', ...
    'GateErrorM', 'ExpectedGateCfarHitFraction', 'ExpectedBeatEmptyContrastDb', ...
    'GatePeakEmptyContrastDb', 'ExpectedBeatDbfs', 'CandidateScreenPass', 'BackgroundFile'});
end

function comparison = compare_runs(all, active)
  comparison = table(all.File, all.ExpectedRangeM, all.TotalShots, active.UsedShots, ...
    all.ExpectedGateCfarHitFraction, active.ExpectedGateCfarHitFraction, ...
    all.ExpectedBeatEmptyContrastDb, active.ExpectedBeatEmptyContrastDb, ...
    all.ExpectedGatePeakM, active.ExpectedGatePeakM, ...
    'VariableNames', {'File', 'ExpectedRangeM', 'TotalShots', 'ActiveShots', ...
    'AllCfarHitFraction', 'ActiveCfarHitFraction', 'AllEmptyContrastDb', ...
    'ActiveEmptyContrastDb', 'AllGatePeakM', 'ActiveGatePeakM'});
end

function db = power_db(power)
  values = double(power);
  db = 10 * log10(max(values, realmin));
  db(isnan(values)) = NaN;
end

function plot_run(results, cfg, folder, mode)
  palette = [0 0.35 0.65; 0.85 0.25 0.12; 0.55 0.4 0; ...
    0.4 0.2 0.65; 0.1 0.5 0.2; 0.2 0.65 0.65; 0.65 0.12 0.5];
  logIndices = find(endsWith({results.file}, '.log'));
  legacyIndices = find(endsWith({results.file}, '.csv'));
  groups = {logIndices, legacyIndices};
  groupNames = {'rf_logs', 'legacy_csv'};
  for group = 1:2
    indices = groups{group};
    if isempty(indices)
      continue;
    end
    fig = figure('Name', [mode ' ' groupNames{group}], ...
      'Visible', cfg.figureVisible, 'Position', [50 50 1500 270*numel(indices)], ...
      'DefaultAxesColorOrder', palette);
    layout = tiledlayout(fig, numel(indices), 3, 'TileSpacing', 'compact');
    title(layout, [mode ' | channel ' upper(cfg.channel) ' | nominal-slope apparent ranges'], ...
      'Interpreter', 'none', 'Color', 'k');
    for k = indices
      r = results(k);
      timeUs = (0:r.metadata.samples-1)' / r.metadata.sample_rate_hz * 1e6;
      nexttile;
      hold on;
      if any(r.used)
        plot(timeUs, r.rawCodes(:, r.used));
      end
      if any(~r.used)
        plot(timeUs, r.rawCodes(:, ~r.used), '--', 'Color', [0.7 0.7 0.7]);
      end
      grid on;
      xlabel('Time within chirp (us)');
      ylabel('ADC codes');
      title(sprintf('%s | %d/%d used', r.file, sum(r.used), numel(r.used)), 'Interpreter', 'none');
      nexttile;
      plot(r.rangeM, power_db(r.meanPower), 'LineWidth', 1.2);
      hold on;
      plot(r.rangeM, power_db(r.medianPower), ':');
      plot(r.nativeRangeM, power_db(r.meanThreshold), '--');
      if isfinite(r.expectedRangeM)
        xline(r.expectedRangeM, 'k:', 'Known target', 'LabelVerticalAlignment', 'bottom');
      end
      grid on;
      xlim([0 min(cfg.maxRangeM, r.nyquistRangeM)]);
      ylim([-100 5]);
      xlabel('Apparent range (m)');
      ylabel('Amplitude (dBFS)');
      legend('Mean power', 'Median power', 'Mean per-shot CA threshold', 'Location', 'best');
      nexttile;
      imagesc(r.rangeM, 1:numel(r.used), power_db(r.power)');
      set(gca, 'YDir', 'normal');
      caxis([-90 0]);
      colorbar;
      xlim([0 min(cfg.maxRangeM, r.nyquistRangeM)]);
      xlabel('Apparent range (m)');
      ylabel('Independent shot');
      title('Every shot retained for diagnostics (dBFS)');
    end
    save_figure(fig, fullfile(folder, [groupNames{group} '.png']));
  end
  fig = figure('Name', [mode ' comparisons'], 'Visible', cfg.figureVisible, ...
    'Position', [80 80 1450 1000], 'DefaultAxesColorOrder', palette);
  layout = tiledlayout(fig, 2, 2, 'TileSpacing', 'compact');
  title(layout, mode, 'Interpreter', 'none', 'Color', 'k');
  nexttile;
  hold on;
  labels = {};
  for k = logIndices
    plot(results(k).rangeM, power_db(results(k).meanPower));
    labels{end+1} = results(k).file; %#ok<AGROW>
  end
  grid on;
  xlim([0 cfg.maxRangeM]);
  ylim([-100 5]);
  xlabel('Apparent range (m)');
  ylabel('Mean spectral level (dBFS)');
  legend(labels, 'Interpreter', 'none', 'Location', 'best');
  title('Matched-gain RF tests');
  nexttile;
  hold on;
  labels = {};
  for k = logIndices
    if ~isempty(results(k).backgroundFile)
      plot(results(k).rangeM, results(k).contrastDb);
      labels{end+1} = results(k).file; %#ok<AGROW>
    end
  end
  yline(cfg.backgroundContrastDb, 'k:');
  yline(0, 'k-');
  grid on;
  xlim([0 cfg.maxRangeM]);
  xlabel('Apparent range (m)');
  ylabel('Mean power / empty-scene power (dB)');
  legend(labels, 'Interpreter', 'none', 'Location', 'best');
  title('Empty-reference spectral contrast');
  nexttile;
  hold on;
  labels = {};
  for k = logIndices
    plot(results(k).shots.Shot, results(k).shots.AcRmsCodes, '-o');
    labels{end+1} = results(k).file; %#ok<AGROW>
  end
  yline(cfg.quietRmsCodes, 'k:', 'Quiet cutoff');
  set(gca, 'YScale', 'log');
  grid on;
  xlabel('Independent shot');
  ylabel('Channel AC RMS (ADC codes)');
  legend(labels, 'Interpreter', 'none', 'Location', 'best');
  title('All acquired shots and the selection threshold');
  nexttile;
  selected = find(isfinite([results.expectedRangeM]));
  hold on;
  x = [0 cfg.maxRangeM];
  plot(x, x, 'k:');
  targets = target_table(results, cfg);
  if ~isempty(targets)
    scatter(targets.ExpectedRangeM, targets.StrongestRangeM, 50, 'x');
    scatter(targets.ExpectedRangeM, targets.ExpectedGatePeakM, 50, 'o');
    for k = 1:numel(selected)
      text(targets.ExpectedRangeM(k), targets.StrongestRangeM(k), ...
        [' ' char(targets.File(k))], 'Interpreter', 'none', 'FontSize', 8);
    end
    legend('Ideal', 'Strongest peak', 'Peak within expected gate', 'Location', 'best');
  end
  grid on;
  xlim([0 9]);
  ylim([0 cfg.maxRangeM]);
  xlabel('Known target range (m)');
  ylabel('Estimated apparent range (m)');
  title('Global peak and explicitly truth-gated candidate');
  save_figure(fig, fullfile(folder, 'comparisons.png'));

  % Window comparison in frequency units exposes harmonics and leakage.
  if ~isempty(logIndices)
    fig = figure('Name', [mode ' window comparison'], 'Visible', cfg.figureVisible, ...
      'Position', [110 110 1300 250*numel(logIndices)], 'DefaultAxesColorOrder', palette);
    layout = tiledlayout(fig, numel(logIndices), 1, 'TileSpacing', 'compact');
    title(layout, [mode ' | single-precision range FFT window comparison'], ...
      'Interpreter', 'none', 'Color', 'k');
    for k = logIndices
      r = results(k);
      nexttile;
      plot(r.frequencyHz/1000, power_db(r.rectangularPower), ':', ...
        r.frequencyHz/1000, power_db(r.meanPower), '-', ...
        r.frequencyHz/1000, power_db(r.blackmanPower), '--');
      hold on;
      if isfinite(r.expectedRangeM)
        expected = 2*r.slopeHzPerS*(r.expectedRangeM+cfg.rangeBiasM)/299792458;
        for harmonic = 1:floor(r.metadata.sample_rate_hz/2/expected)
          xline(harmonic*expected/1000, 'k:');
        end
      end
      grid on;
      xlim([0 r.metadata.sample_rate_hz/2000]);
      ylim([-100 5]);
      xlabel('Beat frequency (kHz)');
      ylabel('Amplitude (dBFS)');
      legend('Rectangular', 'Hann', 'Blackman', 'Location', 'best');
      title(r.file, 'Interpreter', 'none');
    end
    save_figure(fig, fullfile(folder, 'window_comparison.png'));
  end
end

function save_figure(fig, path)
  % Keep exported diagnostics legible across MATLAB desktop themes.
  set(fig, 'Color', 'w');
  set(findall(fig, 'Type', 'axes'), 'Color', 'w', 'XColor', 'k', 'YColor', 'k', ...
    'GridColor', [0.4 0.4 0.4], 'MinorGridColor', [0.6 0.6 0.6]);
  set(findall(fig, 'Type', 'text'), 'Color', 'k');
  set(findall(fig, 'Type', 'legend'), 'Color', 'w', 'TextColor', 'k', 'EdgeColor', [0.4 0.4 0.4]);
  set(findall(fig, 'Type', 'colorbar'), 'Color', 'k');
  exportgraphics(fig, path, 'Resolution', 140, 'BackgroundColor', 'white');
end

function write_notes(path, cfg)
  fid = fopen(path, 'w');
  assert(fid >= 0, 'Cannot write %s', path);
  cleanup = onCleanup(@() fclose(fid));
  fprintf(fid, ['FMCW capture analysis\n\n' ...
    'Channel %s is processed as a real receiver.\n' ...
    'allShots includes every captured shot. activeOnly excludes AC RMS < %.1f ADC codes.\n' ...
    'The RMS decision uses raw mean-removed data before optional detrending.\n' ...
    'Every acquired waveform remains in the MAT file and diagnostic images.\n' ...
    'Per-shot quality and selection metrics are exported to shots.csv.\n' ...
    'Exclusion describes low signal variation. Its physical cause requires bench diagnosis.\n\n' ...
    'The original firmware export already removes the ADC previous-conversion word.\n' ...
    'All 61 exported samples are retained, spanning 150 us at 400 kSPS.\n' ...
    'Programmed slope is step_hz * step_rate_hz, scaled here by %.9g.\n' ...
    'Apparent ranges use c*f/(2*slope) minus a manually specified bias of %.9g m.\n' ...
    'Measured RF slope, group delay and sweep linearity remain uncalibrated.\n' ...
    'Sampled bandwidth uses slope*(N-1)/fs. Physical resolution is c/(2*bandwidth).\n' ...
    'The Hann window broadens target responses. Zero padding interpolates spectra.\n\n' ...
    'The core uses float32 DC removal, optional detrending, window, FFT and power.\n' ...
    'The plotting FFT size is %d, supported by CMSIS-DSP arm_rfft_fast_f32.\n' ...
    'Detection uses the smallest CMSIS-supported FFT holding the retained samples.\n' ...
    'For the current 61-sample captures, detection uses 64 points.\n' ...
    'CFAR uses local maxima and actual available edge training counts.\n' ...
    'Nominal single-shot Pfa is %.4g. Window correlation and clutter affect actual Pfa.\n' ...
    'The mean threshold is the mean of the single-shot thresholds.\n' ...
    'Averaged-power curves and their thresholds provide descriptive comparisons.\n' ...
    'LocalPowerRatioDb is a local spectral ratio, affected by clutter and harmonics.\n\n' ...
    'Ground-truth log ranges are 9ft=2.7432m, 15ft=4.572m and 24ft=7.3152m.\n' ...
    'Legacy outdoor CSV ranges are left unspecified.\n' ...
    'ExpectedGatePeakM explicitly uses the known range +/- %.3g m.\n' ...
    'StrongestRangeM is the independent strongest-peak estimate.\n' ...
    'CandidateScreenPass requires a gate peak, >=50%% per-shot CFAR hits,\n' ...
    '>=%.3g dB empty-scene contrast at that peak and zero ADC rail clips.\n' ...
    'This descriptive rule exposes its component metrics in known_targets.csv.\n' ...
    'Harmonic flags indicate proximity to integer multiples of the expected beat.\n' ...
    'Separate physical targets may also appear at those ranges.\n\n' ...
    'Background ratios and signed power excess compare incoherent mean powers.\n' ...
    'Gain, timing and available frequency metadata must match. Legacy metadata\n' ...
    'omits RF start frequency, so RF-log comparisons assume the same board profile.\n' ...
    'Scene changes and signal cross terms affect background contrasts.\n' ...
    'Signed excessPower is retained in the MAT file.\n\n' ...
    'Repeated USB shots lack timing and phase-continuity metadata.\n' ...
    'Doppler/MTI/coherent integration require a phase-coherent timed train.\n' ...
    'AoA and angular separation require populated calibrated receive antennas.\n' ...
    'The same-range dual target test superposes into a shared range response.\n' ...
    'IMU fusion and mapping require pose-synchronized data.\n' ...
    'The current records span only the tested distances and eight shots per RF log.\n' ...
    '20m detection, 1m two-target separation and measured Pd/Pfa require more data.\n' ...
    'MATLAB timings do not establish MCU processing latency.\n\n' ...
    'References\n' ...
    'https://arm-software.github.io/CMSIS-DSP/main/group__RealFFTF32.html\n' ...
    'https://www.mathworks.com/help/phased/ug/constant-false-alarm-rate-cfar-detection.html\n' ...
    'https://www.ti.com/content/dam/videos/external-videos/en-us/2/3816841626001/5415203482001.mp4/subassets/mmwaveSensing-FMCW-offlineviewing_0.pdf\n'], ...
    upper(cfg.channel), cfg.quietRmsCodes, cfg.slopeScale, cfg.rangeBiasM, ...
    cfg.fftSize, cfg.cfarNominalPfa, cfg.expectedGateM, cfg.backgroundContrastDb);
end
