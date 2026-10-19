#pragma once

#include <nekomote/asset_io/asset_file.hpp>

#include <istream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "asset_json.hpp"

namespace nekomote::asset_io
{
using Json = nlohmann::json;
// Drop frame/layer/part DOM records immediately after checked conversion, so
// both binary decoding and source migration retain only bounded record trees.
class ModelStream final : public nlohmann::json_sax<Json>
{
    enum class Kind
    {
        Other,
        Envelope,
        Model,
        Parts,
        Part,
        Layers,
        Layer,
        Frames,
        Frame
    };
    struct Context
    {
        Kind kind{Kind::Other};
        std::string key;
        std::unordered_set<std::string> keys;
        std::vector<model::Layer> layers;
        std::vector<model::LayerFrame> frames;
    };
    std::vector<Context> stack_;
    std::vector<model::Part> parts_;

    static bool records(Kind kind)
    {
        return kind == Kind::Parts || kind == Kind::Layers || kind == Kind::Frames;
    }
    Context& owner(Kind first, Kind second = Kind::Other)
    {
        for (auto i = stack_.rbegin(); i != stack_.rend(); ++i)
            if (i->kind == first || (second != Kind::Other && i->kind == second))
                return *i;
        throw std::runtime_error("invalid streamed model structure");
    }
    bool event(Json::parse_event_t event, Json& value)
    {
        using Event = Json::parse_event_t;
        if (event == Event::key)
        {
            auto& context = stack_.back();
            context.key = value.get<std::string>();
            // A streamed record cannot be replaced after it has been consumed.
            // Reject ambiguous duplicate keys instead of silently changing data.
            if (!context.keys.insert(context.key).second)
                throw std::runtime_error("duplicate asset key: " + context.key);
            return true;
        }
        if (event == Event::object_start || event == Event::array_start)
        {
            const bool object = event == Event::object_start;
            Kind kind = Kind::Other;
            if (stack_.empty())
                kind = object ? Kind::Envelope : Kind::Other;
            else
            {
                const auto& parent = stack_.back();
                if (records(parent.kind) && !object)
                    throw std::runtime_error("asset record has wrong type");
                if (object)
                {
                    if (parent.kind == Kind::Envelope && parent.key == "model")
                        kind = Kind::Model;
                    else if (parent.kind == Kind::Parts)
                        kind = Kind::Part;
                    else if (parent.kind == Kind::Layers)
                        kind = Kind::Layer;
                    else if (parent.kind == Kind::Frames)
                        kind = Kind::Frame;
                }
                else if (parent.kind == Kind::Model && parent.key == "parts")
                    kind = Kind::Parts;
                else if ((parent.kind == Kind::Part && parent.key == "layers") ||
                         (parent.kind == Kind::Layer && parent.key == "children"))
                    kind = Kind::Layers;
                else if (parent.kind == Kind::Layer && parent.key == "frame_list")
                    kind = Kind::Frames;
            }
            stack_.emplace_back();
            stack_.back().kind = kind;
            return true;
        }
        if (event == Event::value)
        {
            if (!stack_.empty() && records(stack_.back().kind))
                throw std::runtime_error("asset record has wrong type");
            return true;
        }
        auto context = std::move(stack_.back());
        stack_.pop_back();
        if (context.kind == Kind::Frame)
        {
            owner(Kind::Layer).frames.push_back(model::checked_value<model::LayerFrame>(value));
            return false;
        }
        if (context.kind == Kind::Layer)
        {
            auto layer = model::checked_value<model::Layer>(value);
            context.frames.shrink_to_fit();
            context.layers.shrink_to_fit();
            layer.frame_list = std::move(context.frames);
            layer.children = std::move(context.layers);
            owner(Kind::Layer, Kind::Part).layers.push_back(std::move(layer));
            return false;
        }
        if (context.kind == Kind::Part)
        {
            auto part = model::checked_value<model::Part>(value);
            context.layers.shrink_to_fit();
            part.layers = std::move(context.layers);
            parts_.push_back(std::move(part));
            return false;
        }
        return true;
    }

    std::vector<Json> values_;
    std::vector<std::string> pending_keys_;
    Json document_;

    void append(Json value)
    {
        if (values_.empty())
            document_ = std::move(value);
        else if (values_.back().is_object())
            values_.back()[pending_keys_.back()] = std::move(value);
        else
            values_.back().push_back(std::move(value));
    }
    bool scalar(Json value)
    {
        if (event(Json::parse_event_t::value, value))
            append(std::move(value));
        return true;
    }
    bool begin(bool object)
    {
        if (values_.size() >= 256)
            throw std::runtime_error("model nesting exceeds limit");
        Json value = object ? Json::object() : Json::array();
        event(object ? Json::parse_event_t::object_start : Json::parse_event_t::array_start, value);
        values_.push_back(std::move(value));
        pending_keys_.emplace_back();
        return true;
    }
    bool end(bool object)
    {
        auto value = std::move(values_.back());
        values_.pop_back();
        pending_keys_.pop_back();
        if (event(object ? Json::parse_event_t::object_end : Json::parse_event_t::array_end, value))
            append(std::move(value));
        return true;
    }

  public:
    bool null() override
    {
        return scalar(nullptr);
    }
    bool boolean(bool value) override
    {
        return scalar(value);
    }
    bool number_integer(Json::number_integer_t value) override
    {
        return scalar(value);
    }
    bool number_unsigned(Json::number_unsigned_t value) override
    {
        return scalar(value);
    }
    bool number_float(Json::number_float_t value, const std::string&) override
    {
        return scalar(value);
    }
    bool string(std::string& value) override
    {
        return scalar(std::move(value));
    }
    bool binary(Json::binary_t&) override
    {
        throw std::runtime_error("binary values are not supported in model records");
    }
    bool start_object(std::size_t) override
    {
        return begin(true);
    }
    bool start_array(std::size_t) override
    {
        return begin(false);
    }
    bool end_object() override
    {
        return end(true);
    }
    bool end_array() override
    {
        return end(false);
    }
    bool key(std::string& value) override
    {
        Json token = value;
        event(Json::parse_event_t::key, token);
        pending_keys_.back() = std::move(value);
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const Json::exception& error) override
    {
        throw std::runtime_error(error.what());
    }
    model::ModelAsset read(std::istream& input, Json::input_format_t format)
    {
        // The same checked record builder serves the binary runtime loader and
        // the explicit offline JSON migrator. No runtime format autodetection.
        if (!Json::sax_parse(input, this, format, true))
            throw std::runtime_error("model record parsing failed");
        if (model::checked_value<int>(document_.at("schema")) != kAssetSchema)
            throw std::runtime_error("unsupported asset schema; rebuild with nekomote_import");
        auto asset = document_.at("model").get<model::ModelAsset>();
        parts_.shrink_to_fit();
        asset.parts = std::move(parts_);
        return asset;
    }
};
} // namespace nekomote::asset_io
