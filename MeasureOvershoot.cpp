#define NOMINMAX // to save from std::min (max) errors
#include <windows.h>
#include "include\avisynth.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <cmath>

class MeasureOvershoot : public GenericVideoFilter {
private:
    int N;
    int scale;
    double edge_threshold; // Minimum Y transient relative amplitude to be treated as transient 
    PClip upscaled_clip;

    // Overshoots measurement function for 1D profile 
    void AnalyzeLine(const float* line, int length, int step, std::vector<double>& overshoots) {
        // helper lambda 
        auto get_shelf = [&](int idx_from, int idx_to, double& mean_val) -> bool {
            if (idx_from < 0 || idx_to >= length || idx_from > idx_to) return false;
            float min_v = line[idx_from], max_v = line[idx_from];
            double sum = 0.0;
            int count = idx_to - idx_from + 1;
            for (int j = idx_from; j <= idx_to; ++j) {
                float v = line[j];
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
                sum += v;
            }
            mean_val = sum / count;
            return (max_v - min_v) < (edge_threshold * 0.15);
        };

        // Search for contigous parts of transients (rise or fall) 
        int i = step;
        while (i < length - step) {
            float diff = line[i] - line[i - step];
            if (std::abs(diff) < 1e-4f) {
                i += step;
                continue;
            }

            int dir = (diff > 0) ? 1 : -1;
            int start = i - step;
            int end = i;

            // Step via contigous part
            while (end + step < length) {
                float next_diff = line[end + step] - line[end];
                if ((dir > 0 && next_diff > 1e-4f) || (dir < 0 && next_diff < -1e-4f)) {
                    end += step;
                }
                else {
                    break;
                }
            }

            float v_start = line[start];
            float v_end = line[end];
            double raw_amp = std::abs(line[end] - line[start]);
            if (raw_amp >= edge_threshold) {
                int shelf_offset = 3 * scale;
                int shelf_len = 2 * scale;

                double v_shelf_start = 0.0, v_shelf_end = 0.0;
                bool left_flat = get_shelf(start - shelf_offset - shelf_len, start - shelf_offset, v_shelf_start);
                bool right_flat = get_shelf(end + shelf_offset, end + shelf_offset + shelf_len, v_shelf_end);

                if (left_flat && right_flat) {
                    double edge_amp = std::abs(v_shelf_end - v_shelf_start);
                    if (edge_amp >= edge_threshold) {
                        int search_radius = 3 * scale;

                    double max_post = v_shelf_end;
                    int post_limit = std::min(length - 1, end + search_radius);
                    for (int k = end; k <= post_limit; ++k) {
                        if ((dir > 0 && line[k] < v_shelf_end - edge_amp * 0.25) ||
                            (dir <= 0 && line[k] > v_shelf_end + edge_amp * 0.25)) break;
                        if ((dir > 0 && line[k] > max_post) || (dir <= 0 && line[k] < max_post)) max_post = line[k];
                    }
                    double post_over = std::abs(max_post - v_shelf_end);

                    double max_pre = v_shelf_start;
                    int pre_limit = std::max(0, start - search_radius);
                    for (int k = start; k >= pre_limit; --k) {
                        if ((dir > 0 && line[k] > v_shelf_start + edge_amp * 0.25) ||
                            (dir <= 0 && line[k] < v_shelf_start - edge_amp * 0.25)) break;
                        if ((dir > 0 && line[k] < max_pre) || (dir <= 0 && line[k] > max_pre)) max_pre = line[k];
                    }
                    double pre_over = std::abs(max_pre - v_shelf_start);
                    double rel_pct = (std::max(post_over, pre_over) / edge_amp) * 100.0;
                    if (rel_pct > 0.0 && rel_pct < 100.0) overshoots.push_back(rel_pct);
                    }
                }
            }
            i = end + step;
        }
    }

public:
    MeasureOvershoot(PClip _child, int _N, double _threshold, IScriptEnvironment* env)
        : GenericVideoFilter(_child), N(_N), scale((_N == 0) ? 1 : _N), edge_threshold(_threshold) {

        if (N != 0 && (N < 2 || N > 20)) {
            env->ThrowError("MeasureOvershoot: N must be 0 (no resize) or between 2 and 20.");
        }

        if (!vi.IsPlanar() && !vi.IsY()) {
            env->ThrowError("MeasureOvershoot: Only Planar YUV or Y formats are supported.");
        }

        // Convert to float32 Y-only to make simple solution
        AVSValue conv_args[1] = { child };
        PClip y_float = env->Invoke("ConvertToY", AVSValue(conv_args, 1)).AsClip();

        AVSValue bit_args[2] = { y_float, 32 };
        const char* bit_names[2] = { nullptr, nullptr };
        y_float = env->Invoke("ConvertBits", AVSValue(bit_args, 2), bit_names).AsClip();

        if (N != 0)
        {
            // Call SincResize(width * N, height * N, taps=10)
            int up_w = vi.width * N;
            int up_h = vi.height * N;

            AVSValue resize_args[4] = { y_float, up_w, up_h, 16 };
            const char* resize_names[4] = { nullptr, nullptr, nullptr, "taps" };
            upscaled_clip = env->Invoke("SincLin2Resize", AVSValue(resize_args, 4), resize_names).AsClip();
        }
        else
            upscaled_clip = y_float;
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
        // Get upscaled frame for analisys
        PVideoFrame up_frame = upscaled_clip->GetFrame(n, env);
        const float* srcp = reinterpret_cast<const float*>(up_frame->GetReadPtr(PLANAR_Y));
        int pitch = up_frame->GetPitch(PLANAR_Y) / sizeof(float);
        int width = up_frame->GetRowSize(PLANAR_Y) / sizeof(float);
        int height = up_frame->GetHeight(PLANAR_Y);

        std::vector<double> overshoots;
        overshoots.reserve((width + height) * 2);

        // 1. Horizontal analisys with step of N
        for (int y = scale; y < height - scale; y += scale) {
            AnalyzeLine(srcp + y * pitch, width, 1, overshoots);
        }

        // 2. Vertical analisys 
        std::vector<float> col_buf(height);
        for (int x = scale; x < width - scale; x += scale) {
            for (int y = 0; y < height; ++y) {
                col_buf[y] = srcp[y * pitch + x];
            }
            AnalyzeLine(col_buf.data(), height, 1, overshoots);
        }

        char text_buf[256];
        if (overshoots.empty()) {
            std::snprintf(text_buf, sizeof(text_buf),
                "Overshoot (N=%d): No sharp edges > %.0f%% found", N, edge_threshold * 100.0);
        }
        else {
            std::sort(overshoots.begin(), overshoots.end());
            double max_val = overshoots.back();
            double p95_val = overshoots[static_cast<size_t>(overshoots.size() * 0.95)];
            double p99_val = overshoots[static_cast<size_t>(overshoots.size() * 0.99)];

            std::snprintf(text_buf, sizeof(text_buf),
                "Overshoot (N=%d, edges=%zu): P95 = %.2f%% | P99 = %.2f%% | Max = %.2f%%",
                N, overshoots.size(), p95_val, p99_val, max_val);
        }

        // Get original frame and overlay text via Subtitle
        PVideoFrame orig_frame = child->GetFrame(n, env);

        // Warp 1 frame to temporal clip or call Subtitle directly to child        
        AVSValue sub_args[5] = { child, text_buf, 16, 16, n };
        const char* sub_names[5] = { nullptr, nullptr, "x", "y", "first_frame" };
        // To save from drawing same text to all frames at caching:
        AVSValue sub_args_full[6] = { child, text_buf, 16, 16, n, n };
        const char* sub_names_full[6] = { nullptr, nullptr, "x", "y", "first_frame", "last_frame" };

        PClip annotated = env->Invoke("Subtitle", AVSValue(sub_args_full, 6), sub_names_full).AsClip();
        return annotated->GetFrame(n, env);
    }

    int __stdcall SetCacheHints(int cachehints, int frame_range) override {
        return cachehints == CACHE_GET_MTMODE ? MT_MULTI_INSTANCE : 0;
    }
};

AVSValue __cdecl Create_MeasureOvershoot(AVSValue args, void* user_data, IScriptEnvironment* env) {
    return new MeasureOvershoot(
        args[0].AsClip(),
        args[1].AsInt(4),          // N default = 4
        args[2].AsFloat(0.25f),    // th (threshold) default = 0.25 (transient amplitude of 25% Y)
        env
    );
}

const AVS_Linkage* AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(
    IScriptEnvironment * env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;
    env->AddFunction("MeasureOvershoot", "c[N]i[th]f", Create_MeasureOvershoot, nullptr);
    return "MeasureOvershoot plugin for SincDownsizeCoeffs";
}

