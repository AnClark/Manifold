#pragma once

/**
 * @file FilenameConfig.hpp
 * @brief Output filename template system.
 *
 * A FilenameTemplate is an ordered list of (FilenameToken, separator) segments.
 * At processing time each token is resolved against the source file and a
 * global counter to produce a concrete filename stem (no extension, no directory).
 *
 * ### TOML schema (stored under [output.filename])
 * @code{.toml}
 * conflict_policy  = "AutoRename"   # "Skip" | "Overwrite" | "AutoRename"
 * sanitize_spaces  = false
 * max_length       = 128
 *
 * [[output.filename.segments]]
 * type      = "OriginalName"
 * separator = "_"
 *
 * [[output.filename.segments]]
 * type          = "Counter"
 * counter_start = 1
 * counter_step  = 1
 * counter_pad   = 2
 * separator     = ""
 * @endcode
 */

#include <optional>
#include <string>
#include <vector>

#include <toml.hpp>

// ---------------------------------------------------------------------------
// FilenameToken  –  one element in a filename template
// ---------------------------------------------------------------------------

struct FilenameToken
{
    enum class Type
    {
        OriginalName,   ///< stem of the source file (no extension)
        Counter,        ///< auto-incrementing integer
        LiteralText,    ///< fixed user-supplied string
        SampleRate,     ///< source file sample rate in Hz (e.g. "44100")
    };

    Type        type         = Type::OriginalName;
    std::string literalText;        ///< used when type == LiteralText
    int         counterStart = 1;   ///< used when type == Counter; starting value
    int         counterStep  = 1;   ///< used when type == Counter; increment per file
    int         counterPad   = 2;   ///< used when type == Counter; zero-padding width (2 → "01")

    toml::table toToml() const;
    static FilenameToken fromToml(const toml::table& t);
};

// ---------------------------------------------------------------------------
// FilenameTemplate  –  ordered sequence of (token, separator) pairs
// ---------------------------------------------------------------------------

struct FilenameTemplate
{
    struct Segment
    {
        FilenameToken token;
        std::string   separator;   ///< literal text appended after this token's value
    };

    std::vector<Segment> segments;

    enum class ConflictPolicy { Skip, Overwrite, AutoRename };
    ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;

    bool sanitizeSpaces = false;  ///< replace ' ' with '_' in the resolved stem
    int  maxLength      = 128;    ///< truncate result to this many characters (0 = no limit)

    // -----------------------------------------------------------------------

    /**
     * @brief Validate the template.
     * @return nullopt if valid; an error description if invalid.
     */
    std::optional<std::string> validate() const;

    /**
     * @brief Resolve the template to a concrete filename stem (no extension).
     * @param sourceStem   Stem of the source file.
     * @param counter      1-based file counter for the current batch.
     * @param sampleRate   Sample rate of the source file in Hz (0 = unknown).
     */
    std::string resolve(const std::string& sourceStem, int counter,
                        int sampleRate = 0) const;

    // -----------------------------------------------------------------------

    toml::table toToml() const;
    static FilenameTemplate fromToml(const toml::table& t);

    // -----------------------------------------------------------------------

    /**
     * @brief Build the default template: original name, no suffix.
     */
    static FilenameTemplate makeDefault()
    {
        FilenameTemplate t;
        t.segments.push_back({ FilenameToken{ FilenameToken::Type::OriginalName }, "" });
        return t;
    }
};
