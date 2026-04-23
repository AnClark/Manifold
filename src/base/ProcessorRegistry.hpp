#pragma once

#include "IAudioProcessor.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

/**
 * @brief Singleton registry for all audio processors.
 *
 * Manages the factory functions of every IAudioProcessor subclass.
 * Processors self-register at program startup via AutoRegister / REGISTER_PROCESSOR,
 * and ProcessingChain requests instances from this registry by string ID at runtime.
 *
 * @note Registration happens exclusively before main() runs; lookups happen exclusively
 *       during main(). No locking is therefore required. If runtime plug-in loading is
 *       ever needed, add a mutex around reg().
 * @note The registry does not manage processor lifetimes; it only stores factory functions.
 * @note The registry is for built-in processors, and all those processors must be registered
 *       at compile time.
 * @note Dynamic loading of processors is not encouraged; prefer audio plugin support
 *       (VST2/VST3/CLAP) instead.
 */
class ProcessorRegistry {
public:
    /**
     * @brief Factory function type.
     *
     * A zero-argument callable that constructs and returns a new processor instance.
     * Factory functions are generated automatically by AutoRegister / REGISTER_PROCESSOR.
     *
     * What does "Factory" mean?
     * - A Factory is a callable entity (function, lambda, or functor) that creates and returns a new instance of an object.
     *
     * What does this signature mean?
     * - std::unique_ptr<IAudioProcessor>: The factory returns a unique_ptr owning the new processor instance.
     * - (): The factory takes no arguments; it must construct the processor without any input.
     * - Factory: A function with no arguments that returns a unique_ptr to an IAudioProcessor. This is the type stored in the registry.
     */
    using Factory = std::function<std::unique_ptr<IAudioProcessor>()>;

    /**
     * @brief Returns the global singleton instance.
     *
     * Implemented with the Meyers Singleton pattern: constructed on first call,
     * destroyed when the program exits.
     * @return Reference to the single ProcessorRegistry instance.
     */
    static ProcessorRegistry& getInstance();

    /**
     * @brief Registers a processor factory under the given ID.
     *
     * Prefer the REGISTER_PROCESSOR macro over calling this directly.
     * Registering the same @p id twice overwrites the previous factory.
     *
     * @param id Unique string identifier for the processor, e.g. "normalize" or "dc_offset_remove".
     * @param f  Factory function that creates a new instance of the processor.
     */
    void reg(std::string_view id, Factory f);

    /**
     * @brief Creates a new instance of the processor identified by @p id.
     *
     * Each call returns an independent, freshly constructed instance.
     * Ownership is transferred to the caller.
     *
     * @param id Unique string identifier, must match the ID used during registration.
     * @return A unique_ptr owning the newly created processor.
     * @throws std::runtime_error if @p id is not found in the registry.
     */
    std::unique_ptr<IAudioProcessor> create(std::string_view id) const;

    /**
     * @brief Returns the IDs of all currently registered processors.
     *
     * Useful for enumerating available processors in the UI or verifying
     * registrations during debugging. Order is not guaranteed.
     *
     * @return A vector of registered processor ID strings.
     */
    std::vector<std::string> listAll() const;

private:
    /** @brief Maps processor IDs to their corresponding factory functions. */
    std::unordered_map<std::string, Factory> _factories;
};

/**
 * @brief Compile-time registration helper that self-registers a processor before main() runs.
 *
 * @tparam T The concrete IAudioProcessor subclass to register.
 *
 * ### How it works
 * C++ guarantees that static-duration objects are constructed before main() is entered.
 * By placing a file-scope static instance of AutoRegister<T> in the processor's own .cpp file,
 * the constructor runs at program startup and inserts the factory into ProcessorRegistry —
 * with zero code required in main() or anywhere else in the codebase.
 *
 * ### Intended usage
 * Prefer the REGISTER_PROCESSOR macro, which expands to exactly this pattern:
 * @code
 * // Inside NormalizeProcessor.cpp — this single line is all that is needed:
 * REGISTER_PROCESSOR(NormalizeProcessor, "normalize")
 *
 * // Which expands to:
 * static AutoRegister<NormalizeProcessor> autoRegisterNormalizeProcessor("normalize");
 * @endcode
 *
 * ### Why a struct instead of a free function
 * A free function cannot execute arbitrary code at file scope. A static object's constructor
 * can, which is the only way to hook into the registry before main() without modifying any
 * central registration list.
 *
 * @note Each translation unit that defines a static AutoRegister object must be linked into
 *       the final binary. If a .cpp file is silently dropped by the linker (e.g. due to
 *       whole-archive optimisations), its registrations will not occur.
 */
template<typename T>
struct AutoRegister {
    explicit AutoRegister(std::string_view id) {
        ProcessorRegistry::getInstance().reg(id, [] {
            return std::make_unique<T>();
        });
    }
};

#define REGISTER_PROCESSOR(ClassType, Id) \
    static AutoRegister<ClassType> autoRegister##ClassType(Id);
