fprintf('====================================================\n');
fprintf('  Compiling MEX Chess Engine v11...\n');
fprintf('====================================================\n');

try
    if exist('OCTAVE_VERSION', 'builtin')

        mkoctfile --mex -O3 -o engine_search_mex.mex engine_search.c
        mkoctfile --mex -O3 -o engine_search.mex engine_search.c
    else

        mex -O -COPTIMFLAGS="-O3" -output engine_search_mex engine_search.c
        mex -O -COPTIMFLAGS="-O3" -output engine_search engine_search.c
    end
    fprintf('✓ Biên dịch thành công! Binary đã sẵn sàng.\n');
catch err
    fprintf('✗ Lỗi khi biên dịch: %s\n', err.message);
end
