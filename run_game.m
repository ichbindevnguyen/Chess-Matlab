clc;
this_dir = fileparts(mfilename('fullpath'));
if isempty(this_dir), this_dir = pwd; end

addpath(fullfile(this_dir, 'game_source'));
addpath(fullfile(this_dir, 'engine_source'));

fprintf('====================================================\n');
fprintf('  Đang khởi động Chess vs Engine (MATLAB/Octave)...\n');
fprintf('====================================================\n');

chess_vs_engine();
