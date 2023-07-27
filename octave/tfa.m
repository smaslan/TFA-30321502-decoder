%==================================================================================================== 
% TFA Dostmann 30.3215.02 sensor decoder from oscilloscope record.
% Tested on OWON 7102V scope set to 500kSa/s, 2s record of 433MHz RX module:
% Aurel AC-RX2/CS  
%
% Every transmission of sensor consist of 7 repetitions of the same packet.
% Data coding is PPM (pulse position modulation) driven by gap (low) length. 
% Start bit is long gap (~8ms), stop bit is short gap (~0.5ms).
% High bit is long gap (~3.6ms), low bit is short gap (~1.8ms).
% Pulse width is approx 0.5ms. It may vary on receiver and signal strength!
% There is no CRC. It can be replaced by comparing the 7 repetitions 
% and selecting most common packet data.
%
% Reverse engineered sensor packet (36 bits, bits(36:1)):
%   bits(8:1)   = sensor type ID (0x90)?   
%   bits(12:9)  = random ID (generated when battery replaced)?
%   bits(13)    = battery low flag (1=low, 0=good)
%   bits(14)    = sync button pressed (1=sync report, 0=self reporting)
%   bits(16:15) = channel ID as set on switch (0=chn1, 1=chn2, ...)
%   bits(28:17) = 2's complement temperature [10*deg C] (237=23.7degC)
%   bits(36:29) = relative humidity [%]
%
% (c) 2023 Stanislav Maslan, s.maslan@seznam.cz.
% The script is distributed under MIT license, https://opensource.org/licenses/MIT. 
%====================================================================================================
clear all;
close all;
clc;

warning('off');

% for bit manipulations
pkg load communications;

% this folder
mfld = fileparts(mfilename('fullpath'));
cd(mfld);

% show debug plots?
show_plots = 1;

% --- sample data:
% ch3 22.3C 49%
%bin.bin_path = 'TFA_ch3t223rh49.bin';
%bin.t = 22.3;
%bin.rh = 49;
%bin.ch = 3;

% ch3 23.7C 66%
%bin.bin_path = 'TFA_ch3t237rh66.bin';
%bin.t = 23.7;
%bin.rh = 66;
%bin.ch = 3;

% ch2 22.5C 72%
%bin.bin_path = 'TFA_ch2t225rh72.bin';
%bin.t = 22.5;
%bin.rh = 72;
%bin.ch = 2;

% ch1 22.5C 60%
%bin.bin_path = 'TFA_ch1t225rh60.bin';
%bin.t = 22.5;
%bin.rh = 60;
%bin.ch = 1;

% ch3 -13.8C 48%
%bin.bin_path = 'TFA_ch3t-138rh48.bin';
%bin.t = -13.8;
%bin.rh = 48;
%bin.ch = 3;

% ch3 -10.8C 49%
bin.bin_path = fullfile('..','data','TFA_ch3t-108rh49.bin');
bin.t = -10.8;
bin.rh = 49;
bin.ch = 3;

% ch1 -5.7C 57%
%bin.bin_path = 'TFA_ch1t-057rh57.bin';
%bin.t = -5.7;
%bin.rh = 57;
%bin.ch = 1;

% ch3 23.9C 62% batt ok, reset
%bin.bin_path = 'TFA_ch3t239rh62bok.bin';
%bin.t = 23.9;
%bin.rh = 62;
%bin.ch = 3;

% ch3 24.4C 55% batt ok, reset
%bin.bin_path = 'TFA_ch3t244rh55bok.bin';
%bin.t = 24.4;
%bin.rh = 55;
%bin.ch = 3;

% ch3 24.2C 53% batt fail, reset
%bin.bin_path = 'TFA_ch3t242rh53bfail.bin';
%bin.t = 24.2;
%bin.rh = 53;
%bin.ch = 3;

% ch3 24.3C 50% batt fail
%bin.bin_path = 'TFA_ch3t243rh50bfail.bin';
%bin.t = 24.3;
%bin.rh = 50;
%bin.ch = 3;

% ch3 24.1C 51% batt fail, reset
%bin.bin_path = 'TFA_ch3t241rh51bfail.bin';
%bin.t = 24.1;
%bin.rh = 51;
%bin.ch = 3;

% ch3 24.2C 50% batt fail
%bin.bin_path = 'TFA_ch3t242rh50bfail.bin';
%bin.t = 24.2;
%bin.rh = 50;
%bin.ch = 3;

% ch3 24.2C 50% batt ok, reset
%bin.bin_path = 'TFA_ch3t242rh50bok.bin';
%bin.t = 24.2;
%bin.rh = 50;
%bin.ch = 3;

% ch3 25.0C 50% batt ok, reset
%bin.bin_path = fullfile('..','data','TFA_ch3t250rh50bok.bin');
%bin.t = 25.0;
%bin.rh = 50;
%bin.ch = 3;


% scope signal convolution filter [s]
T_filt = 100e-6;

% load scope wave data
data = owon_read(bin.bin_path);

% filter glitches (convolution filter)
N_filt = round(T_filt/data.Ts/2)+1;
data.uf = conv(data.u,repmat(1/N_filt,[N_filt 1]),'same');

% show wave
if show_plots
    plot(data.t,data.u)
    hold on;
    plot(data.t,data.uf,'r')
    hold off;
    grid on;
    title('Captured waveform');
    xlabel('t [s]');
    ylabel('u [V]');
    legend('Raw waveform','Filtered waveform');
endif

% detect edges
u_trh = 0.5*(max(data.uf) + min(data.uf));
sid_rise = find(data.uf(2:end) >= u_trh & data.uf(1:end-1) < u_trh);
sid_fall = find(data.uf(2:end) <= u_trh & data.uf(1:end-1) > u_trh);
t_rise = data.t(sid_rise);
t_fall = data.t(sid_fall);

% measure pulse widths (highs)
len_list = t_fall - t_rise';
for k = 1:size(len_list,1)
    sid = find(len_list(k,:) > 0);
    if ~isempty(sid)
        len_list(k,1) = min(len_list(k,sid));
    else
        len_list(k,1) = NaN;
    endif
endfor
len_list = len_list(:,1);
high_len = median(len_list)

% measure pulse widths (lows)
len_list = t_rise - t_fall';
for k = 1:size(len_list,1)
    sid = find(len_list(k,:) > 0);
    if ~isempty(sid)
        len_list(k,1) = min(len_list(k,sid));        
    else
        len_list(k,1) = NaN;
    endif
endfor
len_list = len_list(:,1);
len_times = data.t(sid_rise);

if show_plots
    figure;
    hist(1000*len_list,100,100);
    title('Low-level widths');
    xlabel('t_{LOW} [ms]');
    ylabel('r [%]');
endif

% rough pulse len decision rule [s]
t_mid = 3e-3;

% split short/long gaps [s]
t_short = mode(len_list(len_list < t_mid));
t_long = mode(len_list(len_list >= t_mid));

% end of packet decision rule [s]
t_stop = 0.75*t_short;

% glitch rejection decision rule [s]
t_fail = 0.75*high_len;

% packet start sync pulse decision rule [s]
t_start = 1.5*t_long;

% get mean gap sizes [s]
t_mid = 0.5*(t_short + t_long);
t_short = mean(len_list(len_list < t_mid & len_list > t_stop))
t_long = mean(len_list(len_list >= t_mid & len_list < t_start))
t_mid = 0.5*(t_short + t_long);
% long/short ratio
t_long2short = t_long/t_short 

% useful bits count
bit_count = 36;

% decode
t_first = NaN;
t_last = NaN;
t_pack_id = 3; % pick n-th packet for detail view
t_pack_start = NaN;
t_pack_end = NaN;
bit_buf = [];
bit_lists = []; 
pulse_count = numel(len_list);
for k = 1:pulse_count

    per = len_list(k);

    if isnan(per)
        % invalids
        continue;
        
    elseif per < t_fail
        % fail, clear buf
        bit_buf = [];
        continue;
        
    elseif per < t_stop
        % stop bit        
        if numel(bit_buf) == bit_count
            % packet complete
            bit_lists(:,end+1) = bit_buf;            
        endif

        % for debug plot
        if ~isnan(t_pack_start) & isnan(t_pack_end)
            t_pack_end = len_times(k) + high_len;
        endif        
        t_last = len_times(k);
        
    elseif per > t_start
        % start bit        
        bit_buf = [];
        
        % for debug plot
        if t_pack_id
            t_pack_id--;
        elseif isnan(t_pack_start)
            t_pack_start = len_times(k) - per - high_len;
        endif        
        if isnan(t_first)
            t_first = len_times(k);
        endif
    
    elseif per > t_mid
        % high                
        bit_buf(end+1,1) = 1;
    else
        % low                        
        bit_buf(end+1,1) = 0;
    endif
    
endfor

% show disected wave
if show_plots
    tid = find(data.t > (t_first - 0.05) & data.t < (t_last + 0.05));
    figure
    plot(data.t(tid),data.u(tid),'r')
    grid on;
    title('Packets waveform');
    xlabel('t [s]');
    ylabel('u [V]');
        
    figure
    tid = find(data.t > (t_pack_start - 0.03) & data.t < (t_pack_start - 0.0001));
    plot(data.t(tid),data.u(tid),'b')
    hold on;        
    tid = find(data.t > (t_pack_start - 0.0001) & data.t < (t_pack_end + 0.0001));
    plot(data.t(tid),data.u(tid),'r')
    tid = find(data.t > (t_pack_end + 0.0001) & data.t < (t_pack_end + 0.03));
    plot(data.t(tid),data.u(tid),'b')
    hold off;
    grid on;
    title('Single packet waveform');
    xlabel('t [s]');
    ylabel('u [V]');
    legend('Previous packet','This packet','Next packet');
endif

% show repeated packets
fprintf('\nCaputed packets (%d):\n',size(bit_lists,2));
fprintf('   bit 1%sbit %d\n',repmat(' ',[1 bit_count-3-3]),bit_count);
fprintf('     v  %s  v\n',repmat(' ',[1 bit_count-3-3]));
for k = 1:size(bit_lists,2)
    fprintf('P%02d: ',k);
    fprintf('%d',bit_lists(:,k));
    fprintf('\n')
endfor
fprintf('     %s\n',repmat('-',[1 bit_count]));

% eliminate glitches (pick most common packet data)
packet = median(bit_lists,2)';
fprintf('res: ');
fprintf('%d',packet)
fprintf('\n\n');



% --- decode packet for TFA 30.3215.02:

% sensor type?
sensor_type_id = bi2de(packet(1:8),'left-msb');
fprintf('Sensor type ID: 0x%02X\n',sensor_type_id);

% battery ok flag
batt = packet(9);
fprintf('Battery: %s\n',{'low','good'}{batt+1});

% random ID?
rng_id = bi2de(packet(10:14),'left-msb');
fprintf('Random ID: %d\n', rng_id);

% sensor channel (as set on sensor switch)
channel_id = bi2de(packet(15:16),'left-msb') + 1;
fprintf('Channel ID: %d\n', channel_id);

% temperature [deg C] (convert from 2s complement)
temp = bi2de(packet(17:28),'left-msb');
if temp >= 2048
   temp = temp - 4096;
endif
temp = temp/10.0;
fprintf('Temperature: %+0.1f degC\n',temp);

% RH value [%]
RH = bi2de(packet(29:36),'left-msb');
fprintf('Relative humidity: %.0f%%\n',RH);





% old debug stuff

% for mode = [1:4]
%     if mode == 1
%         pp =  packet;
%     elseif mode == 2
%         pp = ~packet;
%     elseif mode == 3
%         pp =  packet(end:-1:1);
%     else
%         pp = ~packet(end:-1:1);
%     endif
%     
%     for pos = [0:28] 
%         rh = (bi2de(pp'([1:8]+pos)))
%         if abs(rh - bin.rh) < 0.5
%             fprintf('RH found at %d\n',pos);
%         endif
%     endfor
% endfor


% for mode = [1:4]
%     if mode == 1
%         pp =  packet;
%     elseif mode == 2
%         pp = ~packet;
%     elseif mode == 3
%         pp =  packet(end:-1:1);
%     else
%         pp = ~packet(end:-1:1);
%     endif
%     
%     for pos = [0:24] 
%         temp = bi2de(pp'([1:12]+pos));
%         if temp >= 2048
%             temp = temp - 4096;
%         endif
%         temp = temp/10.0
%         if abs(temp - bin.t) < 0.05
%             fprintf('temp found at %d\n',pos);
%         endif
%     endfor
% endfor

 



