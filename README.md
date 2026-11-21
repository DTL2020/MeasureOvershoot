# MeasureOvershoot
AVS filter to measure overshoot amplitude. Debug and filter/script setup/tune supplementary filter.

Params:
N (integer), default 4, valid range 0 to 20 - internal upscale (interpolation) ratio with SincLin2Resize(width_x_N, height_x_N, taps=16) to better restore true peaks with sinc interpolation. If N=0 - input sized frame is analysed.
tr (float), default 0.25, valid range 0.0 to 1.0  - minimum transient amplitude of Y to be measured as transient. Too low values may be more damaged by noise. Too high may skip some good transients.
Usage:
MeasureOvershoot(N=8, tr=0.3)

Output is text overlay with Subtitle over input frame.

Designed mostly by Google AI.
