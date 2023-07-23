function [owon] = owon_read(bin_path)
% Very basic OWON 7102V scope BIN file reader (version SPBS02).
% Part reverse engineering, part from here:
%   http://bikealive.nl/owon-bin-file-format.html
%
% returns:
%   owon.fs - sampling rate [Hz]
%   owon.Ts - time step [s]
%   owon.t - time vector [s]
%   owon.u - wave data [V] 
%
% (c) 2023 Stanislav Maslan, s.maslan@seznam.cz.
% The script is distributed under MIT license, https://opensource.org/licenses/MIT. 

    fr = fopen(bin_path,'r');

    % check header identifier
    fmt_idn = fread(fr,[1,6],'uint8=>char');
    if ~strcmpi(fmt_idn,'SPBS02')
        error('Unknown format identifier ''%s''!',fmt_idn);
    endif

    something_1 = fread(fr,[1,1],'uint32');

    % get channel ID
    chn_idn = fread(fr,[1,3],'uint8=>char');

    % possibly size of data after channel ID string?
    channel_payload_size = -fread(fr,[1,1],'int32');

    something_2 = fread(fr,[1,1],'uint32');

    % display start/len?
    disp_start = fread(fr,[1,1],'uint32');
    disp_len = fread(fr,[1,1],'uint32');

    % sample count
    sample_count = fread(fr,[1,1],'uint32');

    something_3 = fread(fr,[1,1],'uint32');

    % read time base [s/div]
    timebase_list = ([1;2;5].*logspace(-9,1,11))(:);
    timebase_id = fread(fr,[1,1],'uint32');
    timebase = timebase_list(timebase_id+2);

    % vertical offset? units?
    vert_ofs_bits = fread(fr,[1,1],'int32');

    % vertical range [V/div]
    vert_list = ([1;2;5].*logspace(-3,3,7))(:);
    vert_id = fread(fr,[1,1],'uint32');
    vert = vert_list(vert_id+2);

    % attenuation
    atten_id = fread(fr,[1,1],'uint32');
    vert = vert*10^atten_id;

    % vertical scale [V/bit] (not sure here)
    %vert_scale = vert*5/128;
    vert_scale = vert*5/125;

    % sampling rate [Hz]
    owon.fs = disp_len/timebase/15.2;
    % time step [s]
    owon.Ts = 1/owon.fs;

    something_4 = fread(fr,[1,1],'uint32');
    something_5 = fread(fr,[1,1],'uint32');
    something_6 = fread(fr,[1,1],'uint32');
    something_7 = fread(fr,[1,1],'uint32');

    % read and scale wave data
    owon.u = (double(fread(fr,[sample_count,1],'int8')) - double(vert_ofs_bits))*vert_scale;
    
    % generate time vector [s] (fake, no horizontal offset because it is unclear)
    owon.t(:,1) = [0:sample_count-1]*owon.Ts;

    fclose(fr);

endfunction



