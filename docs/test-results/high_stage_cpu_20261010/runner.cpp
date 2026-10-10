// One-off experiment: reuse production High-only inference and LK tracking.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/resource.h>
#include <json/json.h>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <openvino/core/version.hpp>
#include "config/app_config.h"
#include "image/image_processing.h"
#include "model/yolo_engine.h"
#include "profiled_video.h"
#include <sched.h>

namespace {
double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
}
double rssMiB() {
    std::ifstream stream("/proc/self/statm");
    long pages = 0, resident = 0;
    if (!(stream >> pages >> resident)) throw std::runtime_error("Cannot sample RSS");
    return resident * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0*1024.0);
}
class MemorySamples {
public:
    std::vector<double> values;
    MemorySamples() : worker_([this] {
        while (!done_) {
            values.push_back(rssMiB());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }) {}
    void stop() { done_ = true; if (worker_.joinable()) worker_.join(); }
    ~MemorySamples() { stop(); }
private:
    std::atomic<bool> done_{false};
    std::thread worker_;
};
Json::Value array(const std::vector<double>& values) {
    Json::Value out(Json::arrayValue);
    for (double value : values) out.append(value);
    return out;
}
Json::Value boxes(const std::vector<yolo::TrackedDetection>& tracks) {
    Json::Value out(Json::arrayValue);
    for (const auto& track : tracks) {
        const auto& d = track.detection;
        Json::Value value;
        value["id"] = track.track_id;
        value["class_id"] = d.class_id;
        value["score"] = d.score;
        value["box"] = Json::Value(Json::arrayValue);
        for (float coordinate : {d.x1, d.y1, d.x2, d.y2}) value["box"].append(coordinate);
        out.append(value);
    }
    return out;
}
Json::Value serialize(const yolo::VideoInferResult& result) {
    Json::Value out;
    out["elapsed_ms"] = result.timing.total_elapsed_ms;
    out["frame_count"] = Json::Int64(result.frame_counts.frame_count);
    out["model_calls"] = Json::UInt64(result.timing_samples.infer_ms.size());
    out["lk_frames"] = Json::Int64(result.frame_counts.weak_tracked_frame_count);
    out["stride"] = result.detection_policy.base_frame_stride;
    out["stride_mode"] = result.detection_policy.stride_mode;
    out["source_fps"] = result.video_info.source_fps;
    out["lk_total_ms"] = result.timing.optical_flow_ms;
    out["tracker_total_ms"] = result.timing.tracker_ms;
    out["preprocess_ms"] = array(result.timing_samples.preprocess_ms);
    out["infer_ms"] = array(result.timing_samples.infer_ms);
    out["output_decode_ms"] = array(result.timing_samples.decode_ms);
    out["external_postprocess_ms"] = array(result.timing_samples.postprocess_ms);
    std::vector<double> combined;
    if (result.timing_samples.decode_ms.size() != result.timing_samples.postprocess_ms.size()) {
        throw std::runtime_error("Postprocess sample mismatch");
    }
    for (size_t i = 0; i < result.timing_samples.decode_ms.size(); ++i) {
        combined.push_back(result.timing_samples.decode_ms[i]+result.timing_samples.postprocess_ms[i]);
    }
    out["postprocess_ms"] = array(combined);
    out["frames"] = Json::Value(Json::arrayValue);
    for (const auto& frame : result.frames) {
        Json::Value f;
        f["index"] = Json::Int64(frame.frame_index);
        f["detection_frame"] = frame.is_detection_frame;
        f["source"] = frame.tracks_source;
        f["boxes"] = boxes(frame.tracks);
        out["frames"].append(f);
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 6) throw std::runtime_error("Usage: runner MODEL OUTPUT high_lk WARMUP VIDEO...");
        const std::filesystem::path output(argv[2]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Refusing to overwrite output");
        const std::string mode(argv[3]);
        if (mode != "high_lk") throw std::runtime_error("Invalid mode");
        const int warmup = std::stoi(argv[4]);
        if (warmup < 0) throw std::runtime_error("Invalid warmup");
        cv::setNumThreads(4);
        yolo::AppConfig config;
        config.model_path = argv[1];
        config.model_backend = "openvino";
        config.thread_num = 8;
        config.infer_request_count = 1;
        config.openvino_cpu_pinning = false;
        config.video_model_async = false;
        config.video_onnx_async = false;
        config.video_stride_mode = "fixed";
        config.video_detect_fps = 4.0F;
        config.conf_threshold = 0.25F;
        Json::Value report;
        report["mode"] = mode;
        report["logical_cpus"] = Json::Int64(sysconf(_SC_NPROCESSORS_ONLN));
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0) throw std::runtime_error("Cannot read CPU affinity");
        report["affinity_cpus"] = CPU_COUNT(&affinity);
        report["model"] = config.model_path;
        report["input_width"] = config.input_width;
        report["input_height"] = config.input_height;
        report["model_threads"] = config.thread_num;
        report["opencv_threads"] = cv::getNumThreads();
        report["opencv_version"] = CV_VERSION;
        report["openvino_version"] = ov::get_openvino_version().buildNumber;
        report["confidence_threshold"] = config.conf_threshold;
        report["rss_before_model_mib"] = rssMiB();
        const auto load_start = std::chrono::steady_clock::now();
        auto engine = std::make_shared<yolo::YoloEngine>(config);
        report["model_load_ms"] = elapsed(load_start);
        report["rss_after_model_mib"] = rssMiB();
        cv::Mat first;
        {
            cv::VideoCapture capture;
            if (!yolo::openVideoCapture(capture, argv[5]) || !capture.read(first)) {
                throw std::runtime_error("Cannot read warmup frame");
            }
        }
        auto input = yolo::preprocessImageMat(first, config);
        if (!input) throw std::runtime_error("Cannot preprocess warmup frame");
        for (int i = 0; i < warmup; ++i) engine->infer(*input);
        input.reset();
        first.release();
        report["warmup_calls"] = warmup;
        report["rss_after_warmup_mib"] = rssMiB();
        report["videos"] = Json::Value(Json::objectValue);
        for (int i = 5; i < argc; ++i) {
            MemorySamples memory;
            Json::Value video;
            if (mode == "high_lk") {
                const auto measured = yolo::profiledInferVideoFile(engine, config, argv[i]);
                memory.stop();
                video = serialize(measured.result);
                video["video_decode_ms"] = array(measured.profile.video_decode_ms);
                video["lk_flow_ms"] = array(measured.profile.lk_flow_ms);
                video["byte_track_ms"] = array(measured.profile.byte_track_ms);
                video["cpu_seconds"] = measured.profile.cpu_seconds;
                video["cpu_wall_ms"] = measured.profile.cpu_wall_ms;
                video["cpu_process_percent"] = 100000.0 * measured.profile.cpu_seconds / measured.profile.cpu_wall_ms;
            }
            memory.stop();
            video["rss_samples_mib"] = array(memory.values);
            const std::string name = std::filesystem::path(argv[i]).stem().string();
            report["videos"][name] = std::move(video);
            std::cout << "completed " << name << " " << mode << '\n' << std::flush;
        }
        struct rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        report["process_peak_rss_mib"] = usage.ru_maxrss / 1024.0;
        report["rss_end_mib"] = rssMiB();
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::ofstream stream(output);
        if (!stream) throw std::runtime_error("Cannot create report");
        stream << Json::writeString(writer, report) << '\n';
        if (!stream) throw std::runtime_error("Cannot write report");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
