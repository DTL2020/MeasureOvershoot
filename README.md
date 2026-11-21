# MeasureOvershoot
AVS filter to measure overshoot amplitude. Debug and filter/script setup/tune supplementary filter. Only YUV (or Y only) input supported (of 8-32 bits). Y plane only used. Internal processing in foat32.

Params:

N (integer), default 4, valid range 0 to 20 - internal upscale (interpolation) ratio with SincLin2Resize(width\*N, height\*N, taps=16) to better restore true peaks with sinc interpolation. If N=0 - input sized frame is analysed.

tr (float), default 0.25, valid range 0.0 to 1.0  - minimum transient amplitude of Y to be measured as transient. Too low values may be more damaged by noise. Too high may skip some good transients.

Usage:
MeasureOvershoot(N=8, tr=0.3)

Output is text overlay with Subtitle over input frame. Also sets frame properties:"ovsh_P95", "ovsh_P99", "ovsh_Max","ovsh_Edges".

Example script:
LoadPlugin("MeasureOvershoot.dll")
BlankClip(length=240, width=200, height=50, pixel_type="YV12")
Subtitle("TEXT", size=48, text_color=$AFAFAF, noaa=true).PointResize(width\*100, height\*100)
UserDefined2Resize(width/20, height/20, b=80, c=-20, s=5)
MeasureOvershoot(N=8, th=0.3)

Designed mostly by Google AI.
