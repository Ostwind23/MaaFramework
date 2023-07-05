#include "PipelineConfig.h"

#include "MaaUtils/Logger.hpp"
#include "Vision/VisionTypes.h"

#include <tuple>

MAA_RES_NS_BEGIN

using namespace MAA_PIPELINE_RES_NS;

bool PipelineConfig::load(const std::filesystem::path& path, bool is_base)
{
    LogFunc << VAR(path) << VAR(is_base);

    if (is_base) {
        clear();
    }

    if (!std::filesystem::exists(path)) {
        if (is_base) {
            LogError << "path not exists";
            return false;
        }
        else {
            LogWarn << "path not exists, not base, ignore";
            return true;
        }
    }

    if (std::filesystem::is_directory(path)) {
        for (auto& entry : std::filesystem::directory_iterator(path)) {
            bool ret = open_and_parse_file(entry.path());
            if (!ret) {
                LogError << "open_and_parse_file failed" << VAR(entry.path());
                return false;
            }
        }
    }
    else if (std::filesystem::is_regular_file(path)) {
        bool ret = open_and_parse_file(path);
        if (!ret) {
            LogError << "open_and_parse_file failed" << VAR(path);
            return false;
        }
    }
    else {
        LogError << "path is not directory or regular file";
        return false;
    }

    return true;
}

void PipelineConfig::clear()
{
    LogFunc;

    raw_data_.clear();
}

bool PipelineConfig::open_and_parse_file(const std::filesystem::path& path)
{
    LogFunc << VAR(path);

    auto json_opt = json::open(path);
    if (!json_opt) {
        LogError << "json::open failed" << VAR(path);
        return false;
    }

    return parse_json(*json_opt);
}

bool PipelineConfig::parse_json(const json::value& input)
{
    if (!input.is_object()) {
        LogError << "json is not object";
        return false;
    }

    for (const auto& [key, value] : input.as_object()) {
        bool ret = parse_task(key, value);
        if (!ret) {
            LogError << "parse_task failed" << VAR(key) << VAR(value);
            return false;
        }
    }
    return true;
}

template <typename OutT>
bool get_and_check_value(const json::value& input, const std::string& key, OutT& output, const OutT& default_val)
{
    auto opt = input.find<OutT>(key);
    if (!opt) {
        if (input.exists(key)) {
            LogError << "type error" << VAR(key) << VAR(input);
            return false;
        }
        output = default_val;
    }
    else {
        output = *opt;
    }
    return true;
}

template <typename OutT>
bool get_and_check_value_or_array(const json::value& input, const std::string& key, std::vector<OutT>& output)
{
    auto opt = input.find(key);
    if (!opt) {
        // 因为是get，没找到就拉倒
        return true;
    }

    if (opt->is_array()) {
        output.clear();
        for (const auto& item : opt->as_array()) {
            if (!item.is<OutT>()) {
                LogError << "type error" << VAR(key) << VAR(input);
                return false;
            }
            output.emplace_back(item.as<OutT>());
        }
    }
    else if (opt->is<OutT>()) {
        output = { opt->as<OutT>() };
    }
    else {
        LogError << "type error" << VAR(key) << VAR(input);
        return false;
    }

    return !output.empty();
}

bool PipelineConfig::parse_task(const std::string& name, const json::value& input)
{
    LogFunc << VAR(name);

    TaskData data;
    data.name = name;

    if (!parse_recognition(input, data.rec_type, data.rec_params)) {
        LogError << "failed to parse_recognition" << VAR(input);
        return false;
    }

    if (!get_and_check_value(input, "cache", data.cache, false)) {
        LogError << "failed to get_and_check_value cache" << VAR(input);
        return false;
    }

    if (!parse_action(input, data.action_type, data.action_params)) {
        LogError << "failed to parse_action" << VAR(input);
        return false;
    }

    if (!get_and_check_value_or_array(input, "next", data.next)) {
        LogError << "failed to parse_next next" << VAR(input);
        return false;
    }

    uint timeout = 0;
    if (!get_and_check_value(input, "timeout", timeout, 10 * 1000U)) {
        LogError << "failed to get_and_check_value timeout" << VAR(input);
        return false;
    }
    data.timeout = std::chrono::milliseconds(timeout);

    if (!get_and_check_value_or_array(input, "timeout_next", data.timeout_next)) {
        LogError << "failed to parse_next timeout_next" << VAR(input);
        return false;
    }

    if (!get_and_check_value(input, "times_limit", data.times_limit, uint(UINT_MAX))) {
        LogError << "failed to get_and_check_value times_limit" << VAR(input);
        return false;
    }

    if (!get_and_check_value_or_array(input, "runout_next", data.runout_next)) {
        LogError << "failed to parse_next runout_next" << VAR(input);
        return false;
    }

    uint pre_delay = 0U;
    if (!get_and_check_value(input, "pre_delay", pre_delay, 0U)) {
        LogError << "failed to get_and_check_value pre_delay" << VAR(input);
        return false;
    }
    data.pre_delay = std::chrono::milliseconds(pre_delay);

    uint post_delay = 0U;
    if (!get_and_check_value(input, "post_delay", post_delay, 0U)) {
        LogError << "failed to get_and_check_value post_delay" << VAR(input);
        return false;
    }
    data.post_delay = std::chrono::milliseconds(post_delay);

    if (!get_and_check_value(input, "notify", data.notify, false)) {
        LogError << "failed to get_and_check_value notify" << VAR(input);
        return false;
    }

    if (!get_and_check_value(input, "checkpoint", data.checkpoint, false)) {
        LogError << "failed to get_and_check_value checkpoint" << VAR(input);
        return false;
    }

    raw_data_.insert_or_assign(name, std::move(data));

    return true;
}

bool PipelineConfig::parse_recognition(const json::value& input, MAA_PIPELINE_RES_NS::Recognition::Type& out_type,
                                       MAA_PIPELINE_RES_NS::Recognition::Params& out_param)
{
    using namespace MAA_PIPELINE_RES_NS::Recognition;
    using namespace MAA_VISION_NS;

    std::string rec_type_name;
    if (!get_and_check_value(input, "recognition", rec_type_name, std::string("DirectHit"))) {
        LogError << "failed to get_and_check_value recognition" << VAR(input);
        return false;
    }

    static const std::unordered_map<std::string, Type> kRecTypeMap = {
        { "DirectHit", Type::DirectHit },       { "TemplateMatch", Type::TemplateMatch },
        { "OcrDetAndRec", Type::OcrDetAndRec }, { "OcrOnlyRec", Type::OcrOnlyRec },
        { "FreezesWait", Type::FreezesWait },
    };
    auto rec_type_iter = kRecTypeMap.find(rec_type_name);
    if (rec_type_iter == kRecTypeMap.end()) {
        LogError << "rec type not found" << VAR(rec_type_name);
        return false;
    }
    out_type = rec_type_iter->second;

    switch (out_type) {
    case Type::DirectHit:
        return parse_direct_hit_params(input, std::get<DirectHitParams>(out_param));
    case Type::TemplateMatch:
        return parse_templ_matching_params(input, std::get<TemplMatchingParams>(out_param));
    case Type::OcrDetAndRec:
    case Type::OcrOnlyRec:
        return parse_ocr_params(input, std::get<OcrParams>(out_param));
    case Type::FreezesWait:
        return parse_freezes_waiting_params(input, std::get<FreezesWaitingParams>(out_param));
    default:
        return false;
    }

    return false;
}

bool PipelineConfig::parse_direct_hit_params(const json::value& input, MAA_VISION_NS::DirectHitParams& output)
{
    if (!parse_roi(input, output.roi)) {
        LogError << "failed to parse_roi" << VAR(input);
        return false;
    }

    return true;
}

bool PipelineConfig::parse_templ_matching_params(const json::value& input, MAA_VISION_NS::TemplMatchingParams& output)
{
    if (!parse_roi(input, output.roi)) {
        LogError << "failed to parse_roi" << VAR(input);
        return false;
    }

    if (!get_and_check_value_or_array(input, "templates", output.templates)) {
        LogError << "failed to get_and_check_value_or_array templates" << VAR(input);
        return false;
    }
    if (output.templates.empty()) {
        LogError << "templates is empty" << VAR(input);
        return false;
    }

    if (!get_and_check_value_or_array(input, "threshold", output.thresholds)) {
        LogError << "failed to get_and_check_value_or_array threshold" << VAR(input);
        return false;
    }

    if (output.thresholds.empty()) {
        constexpr double kDefaultThreshold = 0.8;
        output.thresholds = std::vector(output.templates.size(), kDefaultThreshold);
    }
    else if (output.templates.size() != output.thresholds.size()) {
        LogError << "templates.size() != thresholds.size()" << VAR(output.templates.size())
                 << VAR(output.thresholds.size());
        return false;
    }

    constexpr int kDefaultMethod = 3; // cv::TM_CCOEFF_NORMED
    if (!get_and_check_value(input, "method", output.method, kDefaultMethod)) {
        LogError << "failed to get_and_check_value method" << VAR(input);
        return false;
    }

    if (!get_and_check_value(input, "green_mask", output.green_mask, false)) {
        LogError << "failed to get_and_check_value green_mask" << VAR(input);
        return false;
    }

    return true;
}

bool PipelineConfig::parse_ocr_params(const json::value& input, MAA_VISION_NS::OcrParams& output)
{
    if (!parse_roi(input, output.roi)) {
        LogError << "failed to parse_roi" << VAR(input);
        return false;
    }

    if (!get_and_check_value_or_array(input, "text", output.text)) {
        LogError << "failed to get_and_check_value_or_array text" << VAR(input);
        return false;
    }

    if (auto replace_opt = input.find("replace")) {
        if (!replace_opt->is_array()) {
            LogError << "replace is not array" << VAR(input);
            return false;
        }
        auto& replace_array = replace_opt->as_array();
        for (const auto& pair : replace_array) {
            if (!pair.is_array()) {
                LogError << "replace pair is not array" << VAR(input);
                return false;
            }
            auto& pair_array = pair.as_array();
            if (pair_array.size() != 2) {
                LogError << "replace pair size != 2" << VAR(input);
                return false;
            }
            auto& first = pair_array[0];
            auto& second = pair_array[1];
            if (!first.is_string() || !second.is_string()) {
                LogError << "replace pair is not string" << VAR(input);
                return false;
            }
            output.replace.emplace_back(std::make_pair(first.as_string(), second.as_string()));
        }
    }

    return true;
}

bool PipelineConfig::parse_freezes_waiting_params(const json::value& input, MAA_VISION_NS::FreezesWaitingParams& output)
{
    if (!parse_roi(input, output.roi)) {
        LogError << "failed to parse_roi" << VAR(input);
        return false;
    }

    constexpr double kDefaultThreshold = 0.8;
    if (!get_and_check_value(input, "threshold", output.threshold, kDefaultThreshold)) {
        LogError << "failed to get_and_check_value threshold" << VAR(input);
        return false;
    }

    constexpr int kDefaultMethod = 3; // cv::TM_CCOEFF_NORMED
    if (!get_and_check_value(input, "method", output.method, kDefaultMethod)) {
        LogError << "failed to get_and_check_value method" << VAR(input);
        return false;
    }

    if (!get_and_check_value(input, "wait_time", output.wait_time, uint(UINT_MAX))) {
        LogError << "failed to get_and_check_value wait_time" << VAR(input);
        return false;
    }

    return true;
}

bool PipelineConfig::parse_roi(const json::value& input, std::vector<cv::Rect>& output)
{
    auto roi_opt = input.find("roi");
    if (!roi_opt) {
        // 这个不是必选字段，没有就没有了
        output.clear();
        return true;
    }

    cv::Rect single_roi;
    if (parse_rect(*roi_opt, single_roi)) {
        output = { single_roi };
        return true;
    }

    if (!roi_opt->is_array()) {
        LogError << "roi is not array" << VAR(input);
        return false;
    }

    auto& roi_array = roi_opt->as_array();
    output.clear();
    for (const auto& roi_item : roi_array) {
        cv::Rect roi;
        if (!parse_rect(roi_item, roi)) {
            LogError << "failed to parse_rect" << VAR(roi_item);
            return false;
        }
        output.emplace_back(roi);
    }

    return !output.empty();
}

bool PipelineConfig::parse_action(const json::value& input, MAA_PIPELINE_RES_NS::Action::Type& out_type,
                                  MAA_PIPELINE_RES_NS::Action::Params& out_param)
{
    using namespace Action;

    std::string act_type_name;
    if (!get_and_check_value(input, "action", act_type_name, std::string("DoNothing"))) {
        LogError << "failed to get_and_check_value action" << VAR(input);
        return false;
    }

    static const std::unordered_map<std::string, Type> kActTypeMap = {
        { "DoNothing", Type::DoNothing },
        { "Click", Type::Click },
        { "Swipe", Type::Swipe },
    };
    auto act_type_iter = kActTypeMap.find(act_type_name);
    if (act_type_iter == kActTypeMap.cend()) {
        LogError << "act type not found" << VAR(act_type_name);
        return false;
    }
    out_type = act_type_iter->second;

    switch (out_type) {
    case Type::DoNothing:
        return true;
    case Type::Click:
        return parse_click(input, std::get<ClickParams>(out_param));
    case Type::Swipe:
        return parse_swipe(input, std::get<SwipeParams>(out_param));
    default:
        return false;
    }

    return false;
}

bool PipelineConfig::parse_click(const json::value& input, MAA_PIPELINE_RES_NS::Action::ClickParams& output)
{
    if (!parse_action_target(input, "target", output.target, output.target_param)) {
        LogError << "failed to parse_action_target" << VAR(input);
        return false;
    }

    return true;
}

bool PipelineConfig::parse_swipe(const json::value& input, MAA_PIPELINE_RES_NS::Action::SwipeParams& output)
{
    if (!parse_action_target(input, "begin", output.begin, output.begin_param)) {
        LogError << "failed to parse_action_target begin" << VAR(input);
        return false;
    }

    if (!parse_action_target(input, "end", output.end, output.end_param)) {
        LogError << "failed to parse_action_target end" << VAR(input);
        return false;
    }

    constexpr uint kDefaultDuration = 200;
    if (!get_and_check_value(input, "duration", output.duration, kDefaultDuration)) {
        LogError << "failed to get_and_check_value duration" << VAR(input);
        return false;
    }

    return true;
}

bool PipelineConfig::parse_rect(const json::value& input_rect, cv::Rect& output)
{
    if (!input_rect.is_array()) {
        LogError << "rect is not array" << VAR(input_rect);
        return false;
    }

    auto& rect_array = input_rect.as_array();
    if (rect_array.size() != 4) {
        LogError << "rect size != 4" << VAR(rect_array.size());
        return false;
    }

    std::vector<int> rect_move_vec;
    for (const auto& r : rect_array) {
        if (!r.is_number()) {
            LogError << "type error" << VAR(r) << "is not integer";
            return false;
        }
        rect_move_vec.emplace_back(r.as_integer());
    }
    output = cv::Rect(rect_move_vec[0], rect_move_vec[1], rect_move_vec[2], rect_move_vec[3]);
    return true;
}

bool PipelineConfig::parse_action_target(const json::value& input, const std::string& key,
                                         MAA_PIPELINE_RES_NS::Action::Target& output_type,
                                         MAA_PIPELINE_RES_NS::Action::TargetParam& output_param)
{
    using namespace MAA_PIPELINE_RES_NS::Action;

    auto param_opt = input.find(key);
    if (!param_opt) {
        output_type = Target::Self;
    }
    else {
        if (param_opt->is_boolean() && param_opt->as_boolean()) {
            output_type = Target::Self;
        }
        else if (param_opt->is_string()) {
            output_type = Target::PreTask;
            output_param = param_opt->as_string();
        }
        else if (param_opt->is_array()) {
            output_type = Target::Region;
            parse_rect(*param_opt, std::get<cv::Rect>(output_param));
        }
        else {
            LogError << "param type error" << VAR(*param_opt);
            return false;
        }
    }

    return true;
}

MAA_RES_NS_END
