#include "FilenameConfig.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

// Characters that are illegal in filenames on Windows (covers most UNIX constraints too).
static const std::string kIllegalChars = "<>:\"/\\|?*";

static bool hasIllegalChar(const std::string& s)
{
    for (unsigned char c : s)
        if (c < 0x20 || kIllegalChars.find(c) != std::string::npos)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// FilenameToken  –  TOML round-trip
// ---------------------------------------------------------------------------

toml::table FilenameToken::toToml() const
{
    toml::table t;
    switch (type)
    {
        case Type::OriginalName: t.insert_or_assign("type", "OriginalName"); break;
        case Type::Counter:      t.insert_or_assign("type", "Counter");      break;
        case Type::LiteralText:  t.insert_or_assign("type", "LiteralText");  break;
        case Type::SampleRate:   t.insert_or_assign("type", "SampleRate");   break;
    }
    if (type == Type::LiteralText)
        t.insert_or_assign("text", literalText);
    if (type == Type::Counter)
    {
        t.insert_or_assign("counter_start", static_cast<int64_t>(counterStart));
        t.insert_or_assign("counter_step",  static_cast<int64_t>(counterStep));
        t.insert_or_assign("counter_pad",   static_cast<int64_t>(counterPad));
    }
    return t;
}

FilenameToken FilenameToken::fromToml(const toml::table& t)
{
    FilenameToken tok;
    const auto typeStr = t["type"].value<std::string>().value_or("OriginalName");
    if      (typeStr == "Counter")     tok.type = Type::Counter;
    else if (typeStr == "LiteralText") tok.type = Type::LiteralText;
    else if (typeStr == "SampleRate")  tok.type = Type::SampleRate;
    else                               tok.type = Type::OriginalName;

    if (tok.type == Type::LiteralText)
        tok.literalText = t["text"].value<std::string>().value_or("");

    if (tok.type == Type::Counter)
    {
        if (auto v = t["counter_start"].value<int64_t>()) tok.counterStart = static_cast<int>(*v);
        if (auto v = t["counter_step"].value<int64_t>())  tok.counterStep  = static_cast<int>(*v);
        if (auto v = t["counter_pad"].value<int64_t>())   tok.counterPad   = static_cast<int>(*v);
    }
    return tok;
}

// ---------------------------------------------------------------------------
// FilenameTemplate  –  validate / resolve / TOML round-trip
// ---------------------------------------------------------------------------

std::optional<std::string> FilenameTemplate::validate() const
{
    if (segments.empty())
        return "Filename template contains no tokens.";

    for (const auto& seg : segments)
    {
        if (seg.token.type == FilenameToken::Type::LiteralText)
        {
            if (hasIllegalChar(seg.token.literalText))
                return "Custom text token contains illegal filename characters.";
        }
        if (hasIllegalChar(seg.separator))
            return "A separator contains illegal filename characters.";
        if (seg.token.type == FilenameToken::Type::Counter)
        {
            if (seg.token.counterPad < 1 || seg.token.counterPad > 8)
                return "Counter padding width must be between 1 and 8.";
            if (seg.token.counterStep < 1)
                return "Counter step must be >= 1.";
            if (seg.token.counterStart < 0)
                return "Counter start value must be >= 0.";
        }
    }
    return std::nullopt;
}

std::string FilenameTemplate::resolve(const std::string& sourceStem, int counter,
                                      int sampleRate) const
{
    std::string result;
    result.reserve(64);

    for (const auto& seg : segments)
    {
        switch (seg.token.type)
        {
            case FilenameToken::Type::OriginalName:
                result += sourceStem;
                break;

            case FilenameToken::Type::LiteralText:
                result += seg.token.literalText;
                break;

            case FilenameToken::Type::SampleRate:
                if (sampleRate > 0)
                    result += std::to_string(sampleRate);
                else
                    result += "0";
                break;

            case FilenameToken::Type::Counter:
            {
                const int value = seg.token.counterStart +
                                  (counter - 1) * seg.token.counterStep;
                std::ostringstream oss;
                oss << std::setw(seg.token.counterPad) << std::setfill('0') << value;
                result += oss.str();
                break;
            }
        }
        result += seg.separator;
    }

    if (sanitizeSpaces)
        std::replace(result.begin(), result.end(), ' ', '_');

    if (maxLength > 0 && static_cast<int>(result.size()) > maxLength)
        result.resize(static_cast<size_t>(maxLength));

    return result;
}

// ---- TOML serialization ----------------------------------------------------

toml::table FilenameTemplate::toToml() const
{
    toml::table t;

    toml::array segsArray;
    for (const auto& seg : segments)
    {
        toml::table segTable = seg.token.toToml();
        segTable.insert_or_assign("separator", seg.separator);
        segsArray.push_back(segTable);
    }
    t.insert_or_assign("segments", std::move(segsArray));

    switch (conflictPolicy)
    {
        case ConflictPolicy::Skip:       t.insert_or_assign("conflict_policy", "Skip");       break;
        case ConflictPolicy::Overwrite:  t.insert_or_assign("conflict_policy", "Overwrite");  break;
        case ConflictPolicy::AutoRename: t.insert_or_assign("conflict_policy", "AutoRename"); break;
    }

    t.insert_or_assign("sanitize_spaces", sanitizeSpaces);
    t.insert_or_assign("max_length",      static_cast<int64_t>(maxLength));

    return t;
}

FilenameTemplate FilenameTemplate::fromToml(const toml::table& t)
{
    FilenameTemplate tmpl;

    if (const auto* arr = t["segments"].as_array())
    {
        for (const auto& item : *arr)
        {
            if (const auto* segTable = item.as_table())
            {
                Segment seg;
                seg.token     = FilenameToken::fromToml(*segTable);
                seg.separator = segTable->get("separator")
                                ? segTable->get("separator")->value<std::string>().value_or("")
                                : "";
                tmpl.segments.push_back(std::move(seg));
            }
        }
    }

    if (tmpl.segments.empty())
        tmpl = FilenameTemplate::makeDefault();

    const auto policy = t["conflict_policy"].value<std::string>().value_or("AutoRename");
    if      (policy == "Skip")      tmpl.conflictPolicy = ConflictPolicy::Skip;
    else if (policy == "Overwrite") tmpl.conflictPolicy = ConflictPolicy::Overwrite;
    else                            tmpl.conflictPolicy = ConflictPolicy::AutoRename;

    if (auto v = t["sanitize_spaces"].value<bool>())    tmpl.sanitizeSpaces = *v;
    if (auto v = t["max_length"].value<int64_t>())      tmpl.maxLength = static_cast<int>(*v);

    return tmpl;
}
